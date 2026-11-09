#include "directorysizeindex.h"
#include "directorysizestore_p.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QPointer>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <QThreadPool>
#include <QTimer>
#include <QUuid>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>
#include <sys/inotify.h>
#include <unistd.h>
#include <sys/stat.h>
#include <array>
#include <algorithm>
#include <cstring>

namespace {
QPointer<DirectorySizeIndex> sharedIndex;
QString localPath(const QString& value)
{
    const QUrl url(value);
    if (url.isLocalFile())
        return QDir::cleanPath(url.toLocalFile());
    if (url.scheme().isEmpty() && QDir::isAbsolutePath(value))
        return QDir::cleanPath(value);
    return { };
}
bool within(const QString& path, const QString& root)
{
    return path == root || path.startsWith(root == "/" ? root : root + '/');
}
}

struct DirectorySizeIndex::Private {
    QHash<QString, DirectorySizeSnapshot> snapshots;
    QHash<QString, quint64> used;
    quint64 serial = 0;
    QHash<QObject*, QString> consumers;
    QHash<QObject*, QMetaObject::Connection> consumerConnections;
    QHash<QObject*, QStringList> watchOwners;
    QHash<QObject*, QMetaObject::Connection> watchConnections;
    QFutureWatcher<void>* watchInstaller = nullptr;
    bool watchRetry = false;
    QStringList activeRequested;
    QHash<QString, quint64> activeRequestIds;
    QSet<quint64> activeSelections;
    std::shared_ptr<std::atomic_bool> jobCancelled;
    QHash<QObject*, QMetaObject::Connection> transfers;
    std::shared_ptr<std::atomic_bool> ioBusy = std::make_shared<std::atomic_bool>(false);
    QSet<QString> paths, directories, verify, unloaded;
    QSet<QString> invalidatedPaths;
    QSet<QString> uncertainRoots;
    QList<QPair<QString, QString>> moves;
    QHash<uint, QString> renameSources;
    QHash<quint64, QStringList> selections, selectionScopes;
    quint64 nextSelection = 0;
    QTimer debounce, progressTimer;
    QThreadPool* pool = new QThreadPool;
    QThreadPool* peekPool = new QThreadPool;
    QFutureWatcher<DirectorySizeWorkResult>* watcher = nullptr;
    QFutureWatcher<DirectorySizeWorkResult>* peekWatcher = nullptr;
    QSet<QString> peekPaths;
    bool warmupStarted = false, warmupPending = false, activeWarmup = false;
    QString warmupPath;
    bool invalidateAll = false;
    QString databasePath, session = QUuid::createUuid().toString(QUuid::Id128);
    std::shared_ptr<std::atomic_bool> cancelled = std::make_shared<std::atomic_bool>(false);
    std::shared_ptr<std::atomic_int> demand = std::make_shared<std::atomic_int>(0);
    std::shared_ptr<DirectorySizeProgress> progress;
    std::shared_ptr<DirectorySizeWatchState> watches = std::make_shared<DirectorySizeWatchState>();
    bool pending = false;
    QString progressPath;

    bool relevant(const QString& path) const
    {
        if (within(path, QFileInfo(databasePath).absolutePath()))
            return false;
        for (auto it = snapshots.cbegin(); it != snapshots.cend(); ++it)
            if (within(path, it.key()) || (!it->canonicalPath.isEmpty() && within(path, it->canonicalPath)))
                return true;
        for (const auto& scope : selectionScopes)
            for (const auto& root : scope)
                if (within(path, root))
                    return true;
        QMutexLocker lock(&watches->mutex);
        if (watches->names.contains(path) || watches->names.contains(QFileInfo(path).absolutePath()))
            return true;
        return false;
    }
    // Persistent hint for a path no visible reader covers. Floods (a content
    // index rebuild, a large copy) collapse into their directories instead of
    // invalidating every cached root, which forced a full verification walk.
    void invalidate(const QString& path)
    {
        if (invalidatedPaths.size() < 2048) {
            invalidatedPaths.insert(path);
            return;
        }
        const auto directory = QFileInfo(path).absolutePath();
        if (invalidatedPaths.size() < 4096 || invalidatedPaths.contains(directory))
            invalidatedPaths.insert(directory);
        else
            invalidateAll = true;
    }
    bool touches(const QString& path, const DirectorySizeSnapshot& s) const
    {
        const auto root = s.canonicalPath.isEmpty() ? s.path : s.canonicalPath;
        return within(path, root) || within(root, path) || within(path, s.path) || within(s.path, path);
    }
    bool pendingChanges(const DirectorySizeSnapshot& s) const
    {
        if (verify.contains(s.path) || unloaded.contains(s.path))
            return true;
        for (const auto& path : paths)
            if (touches(path, s))
                return true;
        for (const auto& path : directories)
            if (touches(path, s))
                return true;
        for (const auto& pair : moves)
            if (touches(pair.first, s) || touches(pair.second, s))
                return true;
        return false;
    }
};

DirectorySizeIndex* DirectorySizeIndex::instance()
{
    if (!sharedIndex)
        sharedIndex = new DirectorySizeIndex({ }, QCoreApplication::instance());
    return sharedIndex;
}
DirectorySizeIndex* DirectorySizeIndex::existing() { return sharedIndex; }

DirectorySizeIndex::DirectorySizeIndex(const QString& path, QObject* parent, int directoryWatchLimit)
    : QObject(parent)
    , d(new Private)
{
    d->watches->limit = qBound(0, directoryWatchLimit, 256);
    d->databasePath = path.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
            + QStringLiteral("/directory-size-index/v1.sqlite")
                                     : QFileInfo(path).absoluteFilePath();
    d->pool->setMaxThreadCount(1);
    d->pool->setThreadPriority(QThread::LowPriority);
    d->pool->setExpiryTimeout(1500);
    d->peekPool->setMaxThreadCount(1);
    d->peekPool->setExpiryTimeout(1500);
    d->debounce.setSingleShot(true);
    d->debounce.setInterval(200);
    connect(&d->debounce, &QTimer::timeout, this, &DirectorySizeIndex::startPending);
    d->progressTimer.setInterval(500);
    connect(&d->progressTimer, &QTimer::timeout, this, [this] {
        if (!d->progress || d->progressPath.isEmpty())
            return;
        auto& s = d->snapshots[d->progressPath];
        // Retain a previous complete snapshot during validation. Fresh scans
        // publish a lower bound, never a partially scanned replacement total.
        if (s.available)
            return;
        s.totals.bytes = d->progress->bytes.load(std::memory_order_relaxed);
        s.totals.files = d->progress->files.load(std::memory_order_relaxed);
        s.totals.folders = d->progress->folders.load(std::memory_order_relaxed);
        emit snapshotChanged(s.path);
    });
    d->watches->descriptor = ::inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
    if (d->watches->descriptor >= 0) {
        auto* notifier = new QSocketNotifier(d->watches->descriptor, QSocketNotifier::Read, this);
        connect(notifier, &QSocketNotifier::activated, this, [this] { readEvents(); });
    }
}
DirectorySizeIndex::~DirectorySizeIndex()
{
    for (const auto& c : d->consumerConnections) disconnect(c);
    for (const auto& c : d->watchConnections) disconnect(c);
    d->cancelled->store(true);
    if (d->jobCancelled) d->jobCancelled->store(true);
    if (d->watcher)
        disconnect(d->watcher, nullptr, this, nullptr);
    else
        delete d->pool;
    if (d->peekWatcher)
        disconnect(d->peekWatcher, nullptr, this, nullptr);
    if (d->watchInstaller)
        disconnect(d->watchInstaller, nullptr, this, nullptr);
    if (!d->peekWatcher && !d->watchInstaller)
        delete d->peekPool;
    // The worker may still hold this state while cancelling; close the fd only
    // after its final reference goes away. No filesystem wait on the UI thread.
}

DirectorySizeSnapshot DirectorySizeIndex::snapshot(const QString& path) const
{
    return d->snapshots.value(localPath(path));
}
bool DirectorySizeIndex::busy() const { return d->watcher || d->pending || d->peekWatcher || !d->peekPaths.isEmpty(); }
std::shared_ptr<std::atomic_int> DirectorySizeIndex::foregroundDemand() const { return d->demand; }

void DirectorySizeIndex::warmup(const QString& value)
{
    const auto path = localPath(value);
    if (path.isEmpty() || d->warmupStarted) return;
    d->warmupStarted = true;
    d->warmupPending = true;
    d->warmupPath = path;
    d->pending = true;
    if (!d->watcher) d->debounce.start(200);
}

void DirectorySizeIndex::retain(QObject* consumer, const QString& value)
{
    const auto path = localPath(value);
    if (d->consumers.value(consumer) == path) return;
    const bool alreadyNeeded = d->consumers.values().contains(path);
    release(consumer);
    d->consumers[consumer] = path;
    d->consumerConnections[consumer]
        = connect(consumer, &QObject::destroyed, this, [this, consumer] { release(consumer); });
    if (!alreadyNeeded && d->snapshots.value(path).available) {
        d->paths.insert(path); d->pending = true;
        if (!d->watcher) d->debounce.start(200);
    }
}
void DirectorySizeIndex::release(QObject* consumer)
{
    const auto path = d->consumers.take(consumer);
    disconnect(d->consumerConnections.take(consumer));
    if (!path.isEmpty() && !d->consumers.values().contains(path)) {
        d->verify.remove(path); d->unloaded.remove(path); d->peekPaths.remove(path);
        auto it = d->snapshots.find(path);
        if (it != d->snapshots.end()) {
            d->uncertainRoots.insert(path);
            it->calculating = false; it->state = it->available ? "cached" : "missing";
            emit snapshotChanged(path);
        }
        bool needed = !d->selectionScopes.isEmpty();
        for (const auto& active : d->activeRequested)
            needed = needed || d->consumers.values().contains(active);
        const auto active = d->progress ? d->progress->activeRequest.load(std::memory_order_relaxed) : 0;
        if (!d->activeWarmup && d->jobCancelled
            && ((!needed) || (active && active == d->activeRequestIds.value(path)))) {
            d->jobCancelled->store(true);
            if (needed) d->pending = true;
        }
    }
}
void DirectorySizeIndex::setWatchedDirectories(QObject* owner, const QStringList& paths)
{
    if (!owner) return;
    if (!d->watchConnections.contains(owner) && !paths.isEmpty())
        d->watchConnections[owner] = connect(owner, &QObject::destroyed, this,
            [this, owner] { setWatchedDirectories(owner, {}); });
    if (paths.isEmpty()) {
        d->watchOwners.remove(owner);
        disconnect(d->watchConnections.take(owner));
    } else {
        QStringList clean;
        for (const auto& value : paths) {
            const auto path = localPath(value);
            if (!path.isEmpty() && !clean.contains(path) && clean.size() < 64) clean.append(path);
        }
        d->watchOwners[owner] = clean;
    }
    QSet<QString> wanted;
    for (const auto& values : d->watchOwners)
        for (const auto& p : values) if (wanted.size() < qMin(256, d->watches->limit)) wanted.insert(p);
    {
        QMutexLocker lock(&d->watches->mutex);
        d->watches->allowed = wanted;
        const auto names = d->watches->names;
        for (const auto& path : names) if (!wanted.contains(path)) {
            const int wd = d->watches->descriptors.take(path);
            d->watches->names.remove(path); d->watches->identities.remove(path);
            auto aliases = d->watches->paths.find(wd);
            if (aliases != d->watches->paths.end()) {
                aliases->remove(path);
                if (aliases->isEmpty()) {
                    d->watches->paths.erase(aliases);
                    ::inotify_rm_watch(d->watches->descriptor, wd);
                }
            }
        }
    }
    updateWatches();
}
void DirectorySizeIndex::updateWatches()
{
    if (d->watchInstaller) { d->watchRetry = true; return; }
    const auto state = d->watches;
    if (state->descriptor < 0) return;
    auto* future = d->watchInstaller = new QFutureWatcher<void>(this);
    connect(future, &QFutureWatcher<void>::finished, this, [this, future] {
        d->watchInstaller = nullptr; future->deleteLater();
        if (d->watchRetry) { d->watchRetry = false; updateWatches(); }
    });
    future->setFuture(QtConcurrent::run(d->peekPool, [state] {
        QSet<QString> wanted;
        { QMutexLocker lock(&state->mutex); wanted = state->allowed; }
        for (const auto& path : wanted) {
            { QMutexLocker lock(&state->mutex); if (state->names.contains(path) || !state->allowed.contains(path)) continue; }
            struct stat st {};
            if (::stat(QFile::encodeName(path).constData(), &st) || !S_ISDIR(st.st_mode)) continue;
            const int wd = ::inotify_add_watch(state->descriptor, QFile::encodeName(path).constData(),
                IN_CREATE | IN_DELETE | IN_MODIFY | IN_CLOSE_WRITE | IN_ATTRIB | IN_MOVED_FROM | IN_MOVED_TO
                | IN_DELETE_SELF | IN_MOVE_SELF | IN_ONLYDIR);
            if (wd < 0) continue;
            QMutexLocker lock(&state->mutex);
            if (!state->allowed.contains(path)) {
                if (!state->paths.contains(wd)) ::inotify_rm_watch(state->descriptor, wd);
                continue;
            }
            state->names.insert(path); state->paths[wd].insert(path); state->descriptors[path] = wd;
            state->identities[path] = {quint64(st.st_dev),quint64(st.st_ino)};
        }
    }));
}
void DirectorySizeIndex::setIoBusy(QObject* producer, bool busy)
{
    if (busy && !d->transfers.contains(producer))
        d->transfers[producer]
            = connect(producer, &QObject::destroyed, this, [this, producer] { setIoBusy(producer, false); });
    if (!busy)
        disconnect(d->transfers.take(producer));
    d->ioBusy->store(!d->transfers.isEmpty(), std::memory_order_relaxed);
}
bool DirectorySizeIndex::request(const QString& value, bool force)
{
    const QString path = localPath(value);
    if (path.isEmpty())
        return false;
    // The old source-object cache is bounded separately. Keep this cache finite
    // too, even if callers request many different directories in one session.
    if (!d->snapshots.contains(path)) {
        if (d->snapshots.size() >= 64) {
            auto oldest = d->snapshots.end();
            for (auto it = d->snapshots.begin(); it != d->snapshots.end(); ++it)
                if (!it->calculating && !d->consumers.values().contains(it.key())
                    && (oldest == d->snapshots.end()
                        || d->used.value(it.key()) < d->used.value(oldest.key())))
                    oldest = it;
            if (oldest == d->snapshots.end())
                return false;
            d->used.remove(oldest.key());
            d->uncertainRoots.remove(oldest.key());
            d->snapshots.erase(oldest);
        }
        DirectorySizeSnapshot s;
        s.path = path;
        d->snapshots.insert(path, s);
        d->unloaded.insert(path);
    }
    auto& s = d->snapshots[path];
    d->used[path] = ++d->serial;
    if (s.calculating && !force)
        return true;
    if (s.complete && !force) {
        if (s.checkedAt.msecsTo(QDateTime::currentDateTimeUtc()) < 5000) return true;
        d->paths.insert(path); // on-demand metadata check; never a TTL timer
    }
    if (force)
        d->verify.insert(path);
    // A visible size request takes the serial writer before startup work.
    // The warmup commits its partial cache before yielding, so this does not
    // discard every completed subtree or restart the whole tree from scratch.
    if (d->activeWarmup && (path != d->warmupPath || force) && d->jobCancelled)
        d->jobCancelled->store(true);
    if (d->activeWarmup && path == d->warmupPath && !s.available)
        d->progressPath = path;
    s.calculating = true;
    if (s.available)
        s.state = "cached";
    else
        s.state = "scanning";
    d->pending = true;
    emit snapshotChanged(path);
    if (d->watcher && !s.available) {
        d->peekPaths.insert(path);
        QTimer::singleShot(0, this, &DirectorySizeIndex::startPeek);
    }
    if (!d->watcher)
        d->debounce.start(0);
    return true;
}
void DirectorySizeIndex::startPeek()
{
    if (d->peekWatcher || d->peekPaths.isEmpty())
        return;
    DirectorySizeWork work;
    work.databasePath = d->databasePath; work.session = d->session;
    work.requested = d->peekPaths.values(); d->peekPaths.clear();
    work.watches = d->watches;
    auto* watcher = d->peekWatcher = new QFutureWatcher<DirectorySizeWorkResult>(this);
    connect(watcher, &QFutureWatcher<DirectorySizeWorkResult>::finished, this, [this, watcher] {
        const auto result = watcher->result();
        d->peekWatcher = nullptr; watcher->deleteLater();
        for (auto it = result.snapshots.cbegin(); it != result.snapshots.cend(); ++it) {
            auto current = d->snapshots.find(it.key());
            if (current == d->snapshots.end() || current->available || !current->calculating || !it->available)
                continue;
            current.value() = it.value();
            current->state = "cached";
            current->calculating = true;
            emit snapshotChanged(it.key());
        }
        startPeek();
    });
    watcher->setFuture(QtConcurrent::run(d->peekPool, [work] { return readDirectorySizeSnapshots(work); }));
}
void DirectorySizeIndex::observePaths(const QStringList& values)
{
    for (const auto& value : values) {
        const QString path = localPath(value);
        if (path.isEmpty())
            continue;
        if (within(path, QFileInfo(d->databasePath).absolutePath()))
            continue; // observing the index itself must never feed another write
        if (!d->relevant(path)) {
            d->invalidate(path);
            d->pending = true; continue;
        }
        bool needed = false;
        for (auto it = d->snapshots.begin(); it != d->snapshots.end(); ++it) {
            if (!d->touches(path, it.value())) continue;
            needed = needed || d->consumers.values().contains(it.key());
            if (!d->consumers.values().contains(it.key())) {
                it->complete = false; it->state = "cached";
            }
        }
        if (!needed) {
            d->invalidate(path);
            d->pending = true;
            continue;
        }
        if (d->paths.size() < 8192)
            d->paths.insert(path);
        else
            observeDirectory(QFileInfo(path).absolutePath(), true);
        d->pending = true;
    }
    if (d->pending && !d->watcher && !d->debounce.isActive())
        d->debounce.start(200);
}
void DirectorySizeIndex::observeOperation(const QStringList& paths, const QStringList& directories)
{
    observePaths(paths);
    QSet<QString> changed;
    for (const auto& value : paths) {
        const auto path = localPath(value);
        if (!path.isEmpty()) changed.insert(QFileInfo(path).absolutePath());
    }
    for (const auto& value : directories) {
        const auto path = localPath(value);
        if (path.isEmpty()) continue;
        observeDirectory(path);
        changed.insert(path);
    }
    // Own operations update the category index and views even when no native
    // watch is installed. Index-produced hints use observePaths, so no feedback.
    for (const auto& path : changed) emit directoryChanged(path);
}
void DirectorySizeIndex::observeDirectory(const QString& value, bool forceReconciliation)
{
    const QString path = localPath(value);
    if (path.isEmpty() || !d->relevant(path))
        return;
    bool needed = false;
    for (const auto& root : d->consumers.values())
        needed = needed || within(path, root) || within(root, path);
    if (!needed) { observePaths({path}); return; }
    if (!forceReconciliation) {
        // DirectoryModel/category notifications describe a whole directory.
        // Our watch already supplies the actual child names, so do not relist
        // a large flat directory just because both producers noticed it.
        QMutexLocker lock(&d->watches->mutex);
        if (d->watches->names.contains(path))
            return;
    }
    if (d->directories.size() < 4096)
        d->directories.insert(path);
    else
        for (auto it = d->snapshots.cbegin(); it != d->snapshots.cend(); ++it)
            if (d->touches(path, it.value()))
                d->verify.insert(it.key());
    d->pending = true;
    if (!d->watcher && !d->debounce.isActive())
        d->debounce.start(200);
}
void DirectorySizeIndex::observeMove(const QString& from, const QString& to)
{
    const QString source = localPath(from), destination = localPath(to);
    if (source.isEmpty() || destination.isEmpty() || (!d->relevant(source) && !d->relevant(destination)))
        return;
    bool needed = false;
    for (const auto& root : d->consumers.values())
        needed = needed || within(source, root) || within(destination, root) || within(root, source) || within(root, destination);
    if (!needed) { observePaths({from, to}); return; }
    if (d->moves.size() >= 4096) {
        observeDirectory(QFileInfo(source).absolutePath(), true);
        observeDirectory(QFileInfo(destination).absolutePath(), true);
        return;
    }
    d->moves.append({ source, destination });
    d->pending = true;
    if (!d->watcher && !d->debounce.isActive())
        d->debounce.start(200);
}
void DirectorySizeIndex::verifyAll()
{
    // An overflow can also lose changes for roots evicted from the hot cache.
    // A new verification epoch makes those disk records recheck on next use.
    d->session = QUuid::createUuid().toString(QUuid::Id128);
    d->invalidateAll = true;
    for (auto it = d->snapshots.begin(); it != d->snapshots.end(); ++it) {
        if (d->consumers.values().contains(it.key())) d->verify.insert(it.key());
        else {
            d->uncertainRoots.insert(it.key());
            it->complete = false;
            it->state = it->available ? "cached" : "missing";
            emit snapshotChanged(it.key());
        }
    }
    // Persist invalidation even with no visible reader, without scanning any
    // hidden cached tree merely because the event queue overflowed.
    d->pending = true;
    if (d->pending && !d->watcher)
        d->debounce.start(200);
}
void DirectorySizeIndex::recoverLostEvents()
{
    // Native watches cover only the visible directories (non-recursively), so
    // a kernel queue overflow can only have lost changes inside them. Reconcile
    // exactly those directories; verifyAll() would re-stat every cached tree.
    QSet<QString> watched;
    {
        QMutexLocker lock(&d->watches->mutex);
        watched = d->watches->allowed;
    }
    for (const auto& path : watched) {
        if (d->relevant(path)) {
            observeDirectory(path, true);
        } else {
            d->invalidate(path);
            d->pending = true;
        }
    }
    if (d->pending && !d->watcher && !d->debounce.isActive())
        d->debounce.start(200);
}
int DirectorySizeIndex::activeWatchCount() const
{
    QMutexLocker lock(&d->watches->mutex);
    return d->watches->paths.size();
}
void DirectorySizeIndex::invalidateAllCached()
{
    for (auto it = d->snapshots.begin(); it != d->snapshots.end(); ++it) {
        it->complete = false; it->state = "cached";
        emit snapshotChanged(it.key());
    }
    d->invalidateAll = true; d->pending = true;
    if (!d->watcher) d->debounce.start(200);
}
quint64 DirectorySizeIndex::inspectSelection(const QStringList& values)
{
    QStringList paths;
    for (const auto& value : values) {
        const auto path = localPath(value);
        if (!path.isEmpty())
            paths.push_back(path);
    }
    paths.removeDuplicates();
    const auto id = ++d->nextSelection;
    d->selections[id] = paths;
    d->selectionScopes[id] = paths;
    d->pending = true;
    if (d->activeWarmup && d->jobCancelled) d->jobCancelled->store(true);
    if (!d->watcher)
        d->debounce.start(0);
    return id;
}
void DirectorySizeIndex::cancelSelection(quint64 id)
{
    d->selections.remove(id);
    d->selectionScopes.remove(id);
    if (d->jobCancelled && d->activeSelections.contains(id)) {
        d->jobCancelled->store(true);
        d->pending = true; // remaining visible requests rejoin the next batch
    }
}
void DirectorySizeIndex::readEvents()
{
    if (d->watches->descriptor < 0)
        return;
    alignas(inotify_event) std::array<char, 64 * 1024> buffer { };
    ssize_t count;
    int drained = 0;
    QSet<QString> changedDirectories, changedFiles;
    // Yield to input/painting during sustained event storms; the readable fd
    // schedules the remaining batch without polling or discarding events.
    while (drained < 256 * 1024 && (count = ::read(d->watches->descriptor, buffer.data(), buffer.size())) > 0) {
        drained += int(count);
        for (ssize_t offset = 0; offset + ssize_t(sizeof(inotify_event)) <= count;) {
            const auto* event = reinterpret_cast<const inotify_event*>(buffer.data() + offset);
            if (offset + ssize_t(sizeof(inotify_event)) + event->len > count) {
                recoverLostEvents();
                break;
            }
            offset += sizeof(inotify_event) + event->len;
            if (event->mask & IN_Q_OVERFLOW) {
                // Size-index verification alone cannot repaint the file list.
                // An overflow may drop the only CREATE/MOVED_TO notification
                // for an externally produced file (e.g. an MKV muxer output).
                recoverLostEvents();
                QMutexLocker lock(&d->watches->mutex);
                changedDirectories.unite(d->watches->allowed);
                continue;
            }
            QSet<QString> parents;
            {
                QMutexLocker lock(&d->watches->mutex);
                parents = d->watches->paths.value(event->wd);
                if (event->mask & (IN_IGNORED | IN_DELETE_SELF | IN_MOVE_SELF)) {
                    for (const auto& parent : parents) {
                        if (d->watches->descriptors.value(parent, -1) != event->wd) continue;
                        d->watches->names.remove(parent);
                        d->watches->descriptors.remove(parent);
                        d->watches->identities.remove(parent);
                    }
                    d->watches->paths.remove(event->wd);
                    if (!(event->mask & IN_IGNORED)) ::inotify_rm_watch(d->watches->descriptor, event->wd);
                }
            }
            if (event->mask & IN_IGNORED)
                changedDirectories.unite(parents); // relist and rearm a lost watch
            if (parents.isEmpty() || (event->mask & IN_IGNORED)) continue;
            // A visible symlink alias and its target can share a native wd.
            // Notify each subscribed spelling without removing another owner.
            for (const auto& parent : parents) {
                const QString name = event->len
                    ? QFile::decodeName(QByteArray(event->name, int(strnlen(event->name, event->len))))
                    : QString();
                const QString path = name.isEmpty() ? parent : QDir(parent).filePath(name);
                if (name.isEmpty() || (event->mask & (IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO)))
                    changedDirectories.insert(parent);
                if (!name.isEmpty() && changedFiles.size() < 8192) changedFiles.insert(path);
                if (event->mask & IN_MOVED_FROM) {
                    d->renameSources[event->cookie] = path;
                    observePaths({ path });
                    continue;
                }
                if (event->mask & IN_MOVED_TO) {
                    const auto from = d->renameSources.take(event->cookie);
                    if (!from.isEmpty())
                        observeMove(from, path);
                    else
                        observePaths({ path });
                    continue;
                }
                if (name.isEmpty())
                    observeDirectory(path, true);
                else
                    observePaths({ path });
            }
        }
    }
    // Missing MOVED_TO is a move out of the monitored tree. The queued path
    // recheck handles it; unmatched cookies need not survive across bursts.
    if (d->renameSources.size() > 8192)
        d->renameSources.clear();
    for (const auto& path : changedDirectories) emit directoryChanged(path);
    for (const auto& path : changedFiles) emit fileChanged(QFileInfo(path).absolutePath(), path);
    if (!changedDirectories.isEmpty()) updateWatches();
}
void DirectorySizeIndex::startPending()
{
    if (d->watcher || !d->pending)
        return;
    d->pending = false;
    DirectorySizeWork work;
    work.databasePath = d->databasePath;
    work.session = d->session;
    work.cachedRoots = d->uncertainRoots.values();
    work.invalidateAll = d->invalidateAll;
    d->invalidateAll = false;
    const auto ownDirectory = QFileInfo(d->databasePath).absolutePath();
    for (auto it = d->snapshots.cbegin(); it != d->snapshots.cend(); ++it) {
        bool affected = it->calculating || d->verify.contains(it.key()) || d->unloaded.contains(it.key());
        for (const auto& path : d->paths)
            affected = affected || d->touches(path, it.value());
        for (const auto& path : d->directories)
            affected = affected || d->touches(path, it.value());
        for (const auto& pair : d->moves)
            affected = affected || d->touches(pair.first, it.value()) || d->touches(pair.second, it.value());
        for (const auto& selection : d->selections)
            for (const auto& path : selection)
                affected = affected || d->touches(path, it.value());
        if (affected && (it->calculating || d->consumers.values().contains(it.key())
            || d->verify.contains(it.key()) || d->unloaded.contains(it.key())))
            work.requested.push_back(it.key());
    }
    // Serve the latest visible demand first; previously scanned children still reuse records.
    std::sort(work.requested.begin(), work.requested.end(),
        [this](const auto& a, const auto& b) { return d->used.value(a) > d->used.value(b); });
    work.verify = d->verify.values();
    d->verify.clear();
    work.peek = d->unloaded.values();
    d->unloaded.clear();
    work.paths = d->paths.values();
    work.invalidatedPaths = d->invalidatedPaths.values();
    d->invalidatedPaths.clear();
    d->paths.clear();
    work.directories = d->directories.values();
    d->directories.clear();
    work.moves = std::move(d->moves);
    d->moves.clear();
    work.selections = std::move(d->selections);
    d->selections.clear();
    d->activeSelections.clear();
    for (auto it = work.selections.cbegin(); it != work.selections.cend(); ++it) d->activeSelections.insert(it.key());
    if (d->warmupPending && work.requested.isEmpty() && work.paths.isEmpty()
        && work.directories.isEmpty() && work.moves.isEmpty() && work.selections.isEmpty()
        && work.invalidatedPaths.isEmpty() && !work.invalidateAll) {
        work.requested = {d->warmupPath};
        work.backgroundWarmup = true;
    }
    for (qsizetype i = 0; i < work.requested.size(); ++i) work.requestIds[work.requested.at(i)] = i + 1;
    d->activeRequestIds = work.requestIds;
    if (work.requested.isEmpty() && work.paths.isEmpty() && work.directories.isEmpty()
        && work.moves.isEmpty() && work.selections.isEmpty() && work.invalidatedPaths.isEmpty()
        && !work.invalidateAll) return;
    work.cancelled = d->jobCancelled = std::make_shared<std::atomic_bool>(false);
    d->activeWarmup = work.backgroundWarmup;
    d->activeRequested = work.requested;
    work.demand = d->demand;
    work.ioBusy = d->ioBusy;
    work.watches = d->watches;
    work.progress = d->progress = std::make_shared<DirectorySizeProgress>();
    d->progressPath.clear();
    const auto keys = d->snapshots.keys();
    for (const auto& key : keys) {
        auto& s = d->snapshots[key];
        if (s.calculating && !s.available && d->progressPath.isEmpty())
            d->progressPath = s.path;
        if (work.verify.contains(key))
            s.calculating = true;
        bool dirty = work.verify.contains(key);
        for (const auto& path : work.paths)
            dirty = dirty || d->touches(path, s);
        for (const auto& path : work.directories)
            dirty = dirty || d->touches(path, s);
        for (const auto& pair : work.moves)
            dirty = dirty || d->touches(pair.first, s) || d->touches(pair.second, s);
        if (dirty) {
            s.calculating = true;
            if (s.available)
                s.state = "cached";
        }
        if (s.calculating)
            emit snapshotChanged(s.path);
    }
    d->progressTimer.start();
    auto* watcher = d->watcher = new QFutureWatcher<DirectorySizeWorkResult>(this);
    connect(watcher, &QFutureWatcher<DirectorySizeWorkResult>::finished, this, [this, watcher, work] {
        const auto result = watcher->result();
        d->activeWarmup = false;
        d->activeSelections.clear();
        if (result.error == "Cancelled") {
            // Ordinary transactions roll back; startup work retains partial
            // records. Preserve mutation hints in either case (idempotently).
            auto invalidations = work.invalidatedPaths + work.paths + work.directories + work.verify;
            for (const auto& move : work.moves) invalidations << move.first << move.second;
            d->invalidateAll = d->invalidateAll || work.invalidateAll;
            for (const auto& path : invalidations)
                d->invalidate(path);
            for (auto it = work.selections.cbegin(); it != work.selections.cend(); ++it)
                if (d->selectionScopes.contains(it.key())) d->selections.insert(it.key(), it.value());
            // A cancelled transaction has no completed snapshot result to
            // clear its calculating bit. Leaving it set strands subsequent
            // Properties requests forever even after the worker is gone.
            // Visible retained readers rejoin the next batch; detached cache
            // owners keep their last usable value until requested again.
            for (const auto &path : work.requested) {
                auto it = d->snapshots.find(path);
                if (it == d->snapshots.end()) continue;
                it->calculating = false;
                it->state = it->available ? QStringLiteral("cached") : QStringLiteral("missing");
                emit snapshotChanged(path);
                if (d->consumers.values().contains(path)) {
                    d->paths.insert(path);
                    d->pending = true;
                }
            }
            d->pending = d->pending || d->warmupPending || d->invalidateAll
                || !d->invalidatedPaths.isEmpty() || !d->selections.isEmpty();
            d->watcher = nullptr; watcher->deleteLater(); d->progressTimer.stop();
            d->activeRequested.clear(); d->jobCancelled.reset();
            if (d->pending) d->debounce.start(0);
            return;
        }
        d->watcher = nullptr;
        watcher->deleteLater();
        d->progressTimer.stop();
        if (work.backgroundWarmup) d->warmupPending = false;
        readEvents();
        for (const auto& path : result.changedDirectories)
            observeDirectory(path, true);
        for (const auto& path : result.verifyAgain)
            d->verify.insert(path);
        if (!result.recheck.isEmpty())
            d->pending = true;
        const auto keys = d->snapshots.keys();
        for (const auto& key : keys) {
            auto it = d->snapshots.find(key);
            if (it == d->snapshots.end())
                continue;
            if (result.snapshots.contains(it.key())) {
                const auto previous = it.value();
                it.value() = result.snapshots.value(it.key());
                if (!it->available && it->state == "unavailable" && previous.available) {
                    it->totals = previous.totals;
                    it->available = true;
                }
                it->calculating = result.recheck.contains(it.key()) || d->pendingChanges(it.value());
                if (it->calculating && it->available && it->state == "ready")
                    it->state = "cached";
            }
            if (!result.error.isEmpty()) {
                it->error = result.error;
                it->state = it->available ? "cached" : "error";
                it->complete = false;
                it->calculating = false;
            }
        }
        for (const auto& key : keys)
            if (result.snapshots.contains(key) || !result.error.isEmpty())
                emit snapshotChanged(key);
        for (auto it = result.selections.cbegin(); it != result.selections.cend(); ++it)
            if (d->selectionScopes.remove(it.key()))
                emit selectionReady(it.key(), it.value());
        if (!result.error.isEmpty()) {
            const auto requests = d->selectionScopes.keys();
            for (auto id : requests) {
                DirectorySizeSnapshot failed;
                failed.state = "error";
                failed.error = result.error;
                d->selectionScopes.remove(id);
                emit selectionReady(id, failed);
            }
        }
        emit workCompleted(result.fileStats, result.directoryListings);
        if (d->warmupPending) d->pending = true;
        if (d->pending)
            d->debounce.start(200);
    });
    watcher->setFuture(QtConcurrent::run(d->pool, [work] { return runDirectorySizeWork(work); }));
}
