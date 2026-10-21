#include "archivedropobserver.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QUrl>
#include <QtConcurrent>
#include <sys/stat.h>

ArchiveDropObserver::ArchiveDropObserver(const QString &destination, QObject *parent)
    : QObject(parent), m_destination(QDir::cleanPath(destination)),
      m_watchRoot(QFileInfo(destination).canonicalFilePath())
{
    m_scanTimer.setSingleShot(true);
    m_scanTimer.setInterval(80);
    m_installTimer.setSingleShot(true);
    m_installTimer.setInterval(1);
    m_publishTimer.setSingleShot(true);
    m_publishTimer.setInterval(250);
    m_idleTimer.setSingleShot(true);
    m_idleTimer.setInterval(30000);
    m_lifetimeTimer.setSingleShot(true);
    m_lifetimeTimer.setInterval(180000);
    connect(&m_scanTimer, &QTimer::timeout, this, &ArchiveDropObserver::scan);
    connect(&m_installTimer, &QTimer::timeout, this, &ArchiveDropObserver::installWatchBatch);
    connect(&m_publishTimer, &QTimer::timeout, this, &ArchiveDropObserver::publish);
    connect(&m_idleTimer, &QTimer::timeout, this, &QObject::deleteLater);
    connect(&m_lifetimeTimer, &QTimer::timeout, this, &QObject::deleteLater);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &ArchiveDropObserver::changed);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &ArchiveDropObserver::changed);
}

ArchiveDropObserver::Snapshot ArchiveDropObserver::snapshot(const QString &path)
{
    Snapshot result;
    result.valid = QFileInfo(path).isDir();
    if (!result.valid) return result;
    // The complete shallow snapshot is taken off the GUI thread. Bound native
    // watches; never descend into unrelated repositories/media subtrees.
    constexpr int maxWatches = 4096;
    QStringList directories, files;
    QDirIterator entries(path, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    while (entries.hasNext()) {
        const QString entryPath = entries.next();
        struct stat st {};
        if (::lstat(QFile::encodeName(entryPath).constData(), &st) != 0) continue;
        QByteArray stamp = QByteArray::number(st.st_ino) + ':' + QByteArray::number(st.st_size)
            + ':' + QByteArray::number(st.st_mtim.tv_sec) + ':' + QByteArray::number(st.st_mtim.tv_nsec)
            + ':' + QByteArray::number(st.st_ctim.tv_sec) + ':' + QByteArray::number(st.st_ctim.tv_nsec);
        result.stamps.insert(entries.fileName(), stamp);
        if (S_ISDIR(st.st_mode) && directories.size() < maxWatches)
            directories.append(entryPath);
        else if (S_ISREG(st.st_mode) && files.size() < maxWatches)
            files.append(entryPath);
    }
    result.watchPaths = directories + files.mid(0, maxWatches - directories.size());
    return result;
}

void ArchiveDropObserver::start()
{
    if (m_watchRoot.isEmpty() || !m_watcher.addPath(m_watchRoot)) {
        emit unavailable();
        deleteLater();
        return;
    }
    m_lifetimeTimer.start();
    scan();
}

void ArchiveDropObserver::scan()
{
    if (m_scanning) { m_rescan = true; return; }
    m_scanning = true;
    auto *future = new QFutureWatcher<Snapshot>(this);
    connect(future, &QFutureWatcher<Snapshot>::finished, this, [this, future] {
        const Snapshot result = future->result();
        future->deleteLater();
        m_scanning = false;
        if (!result.valid) {
            if (m_initial) emit unavailable();
            deleteLater();
            return;
        }
        const bool initial = m_initial;
        if (!initial) {
            for (auto it = result.stamps.cbegin(); it != result.stamps.cend(); ++it)
                if (!m_stamps.contains(it.key()) || m_stamps.value(it.key()) != it.value())
                    m_changedNames.insert(it.key());
        }
        m_stamps = result.stamps;
        const QStringList watched = m_watcher.directories() + m_watcher.files();
        QSet<QString> existing(watched.cbegin(), watched.cend());
        for (const QString &path : std::as_const(m_pendingWatches)) existing.insert(path);
        QStringList missing;
        for (const QString &path : result.watchPaths)
            if (!existing.contains(path)) missing.append(path);
        m_pendingWatches.append(missing);
        if (!m_pendingWatches.isEmpty()) m_installTimer.start();
        else if (initial) { m_initial = false; emit ready(); }
        if (!m_changedNames.isEmpty()) m_publishTimer.start();
        if (m_rescan) { m_rescan = false; m_scanTimer.start(); }
    });
    const QString path = m_watchRoot;
    future->setFuture(QtConcurrent::run([path] { return snapshot(path); }));
}

void ArchiveDropObserver::installWatchBatch()
{
    // Adding thousands of native watches in one call would stall input.
    const int count = qMin(32, m_pendingWatches.size());
    if (count > 0) {
        const QStringList batch = m_pendingWatches.mid(0, count);
        m_pendingWatches.remove(0, count);
        m_watcher.addPaths(batch);
    }
    if (!m_pendingWatches.isEmpty()) m_installTimer.start();
    else if (m_initial) { m_initial = false; emit ready(); }
}

void ArchiveDropObserver::changed(const QString &path)
{
    if (m_initial) return;
    if (path == m_watchRoot) {
        if (!m_scanTimer.isActive()) m_scanTimer.start();
    } else {
        // A child directory can gain files while its parent is unchanged.
        const QString name = QDir(m_watchRoot).relativeFilePath(path);
        if (!name.isEmpty() && !name.contains('/') && name != "..") {
            m_changedNames.insert(name);
            m_publishTimer.start();
        }
    }
    m_idleTimer.start();
}

void ArchiveDropObserver::publish()
{
    QStringList names(m_changedNames.cbegin(), m_changedNames.cend());
    m_changedNames.clear();
    names.sort();
    QStringList urls;
    for (const QString &name : names) {
        const QString path = QDir(m_destination).filePath(name);
        if (QFileInfo::exists(path)) urls.append(QUrl::fromLocalFile(path).toString());
    }
    if (urls.isEmpty()) return;
    const bool first = !m_reported;
    m_reported = true;
    m_idleTimer.start();
    emit itemsChanged(urls, first);
}
