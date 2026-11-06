#include "filepropertiesmanager.h"
#include "directorysizeworker.h"
#include "directorysizeindex.h"

#include <QDir>
#include <QCoreApplication>
#include <QDirIterator>
#include <QFileInfo>
#include <QFileDevice>
#include <QFutureWatcher>
#include <QFile>
#include <QLocale>
#include <QSettings>
#include <QUrl>
#include <QSet>
#include <QtConcurrent/QtConcurrentRun>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>
#include <QHash>
#include <QThreadPool>

#include <algorithm>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
QString securityMessage(const char *english, const char *turkish)
{
    const QString fallback = QLocale::system().language() == QLocale::Turkish ? QStringLiteral("tr") : QStringLiteral("en");
    return QString::fromUtf8(QSettings().value(QStringLiteral("ui/language"), fallback).toString() == QStringLiteral("tr") ? turkish : english);
}

using FolderStats = DirectorySizeResult;

struct SizeCache : QObject {
    struct Entry {
        QPointer<FilePropertiesManager> source;
        quint64 used = 0;
    };
    QHash<QString, Entry> entries;
    quint64 serial = 0;
    explicit SizeCache(QObject *parent) : QObject(parent) {}
};

SizeCache *sizeCache()
{
    static QPointer<SizeCache> cache;
    if (!cache) cache = new SizeCache(QCoreApplication::instance());
    return cache;
}

QString localPathFromInput(const QString &value, const QString &baseDirectory)
{
    const QString trimmed = value.trimmed();
    const QUrl candidate(trimmed);
    if (candidate.isValid() && candidate.isLocalFile())
        return candidate.toLocalFile();
    if (QDir::isAbsolutePath(trimmed))
        return QDir::cleanPath(trimmed);
    return QDir(baseDirectory).absoluteFilePath(trimmed);
}

bool atomicReplaceLink(const QString &path, const QString &target, bool symbolic, QString *errorText)
{
    const QFileInfo linkInfo(path);
    const QString tempPath = linkInfo.absolutePath() + QStringLiteral("/.gfile-link-%1-%2.tmp")
        .arg(QCoreApplication::applicationPid())
        .arg(QDateTime::currentMSecsSinceEpoch());
    const QByteArray nativeTemp = QFile::encodeName(tempPath);
    const QByteArray nativeTarget = QFile::encodeName(target);
    int rc = symbolic ? ::symlink(nativeTarget.constData(), nativeTemp.constData())
                      : ::link(nativeTarget.constData(), nativeTemp.constData());
    if (rc != 0) {
        if (errorText)
            *errorText = QString::fromLocal8Bit(std::strerror(errno));
        return false;
    }

    const QByteArray nativePath = QFile::encodeName(path);
    if (::rename(nativeTemp.constData(), nativePath.constData()) != 0) {
        const int savedErrno = errno;
        QFile::remove(tempPath);
        if (errorText)
            *errorText = QString::fromLocal8Bit(std::strerror(savedErrno));
        return false;
    }
    return true;
}
}

FilePropertiesManager::FilePropertiesManager(QObject *parent)
    : QObject(parent)
{
    initializeAccess();
}

void FilePropertiesManager::setCalculating(bool value)
{
    if (m_calculating == value)
        return;
    m_calculating = value;
    emit calculatingChanged();
}

QString FilePropertiesManager::readFolderIcon(const QString &path)
{
    const QString metadataPath = QDir(path).filePath(QStringLiteral(".directory"));
    if (!QFileInfo::exists(metadataPath))
        return QStringLiteral("folder");
    QSettings metadata(metadataPath, QSettings::IniFormat);
    metadata.beginGroup(QStringLiteral("Desktop Entry"));
    const QString icon = metadata.value(QStringLiteral("Icon"), QStringLiteral("folder")).toString().trimmed();
    metadata.endGroup();
    return icon.isEmpty() ? QStringLiteral("folder") : icon;
}

void FilePropertiesManager::cancelInspection()
{
    ++m_generation;
    disconnect(m_cachedPropertiesConnection);
    disconnect(m_cachedCalculatingConnection);
    if (m_cachedSource) {
        --m_cachedSource->m_cachedReaders;
        m_cachedSource.clear();
    }
    if (m_statsProgressTimer) m_statsProgressTimer->stop();
    disconnect(m_statsConnection);
    disconnect(m_indexConnection);
    if (auto *index = DirectorySizeIndex::existing()) index->release(this);
    if (m_indexSelection) {
        if (auto *index = DirectorySizeIndex::existing()) index->cancelSelection(m_indexSelection);
        m_indexSelection = 0;
    }
    if (m_waitingStatsDemand) {
        m_waitingStatsDemand->fetch_sub(1, std::memory_order_relaxed);
        m_waitingStatsDemand.reset();
    }
    m_statsReady = false;
    m_statsAvailable = false;
    m_statsState.clear(); m_statsError.clear();
    m_childStats.clear();
    if (m_statsCancelled) m_statsCancelled->store(true);
    setCalculating(false);
}

// Share the visible directory's recursive snapshot with Properties. Each
// immediate child's contribution includes hidden descendants, so subsets and
// selections that hide dotfiles never accidentally inherit the entire total.
bool FilePropertiesManager::reuseStatsOrWait(const QStringList &paths, bool selection, int generation)
{
    auto *source = m_statsSource.data();
    if (!source || source->m_path.isEmpty() || paths.isEmpty()) return false;
    for (const auto &path : paths)
        if (path != source->m_path && QFileInfo(path).absolutePath() != source->m_path) return false;
    // Prefer an already known child contribution over the parent's busy flag.
    // Home can be validating in the background (or recovering from a cancelled
    // scan) while its previously completed child totals are still usable.
    // The caller may open Properties immediately without joining that scan.
    if (source->m_statsAvailable || source->m_statsReady) {
        qint64 bytes = 0;
        int files = 0, folders = 0;
        bool allKnown = true;
        const bool entireRoot = paths.contains(source->m_path);
        for (const auto &path : paths) {
            if (entireRoot && path != source->m_path) continue;
            if (path == source->m_path) {
                bytes += source->m_totalSize;
                files += source->m_fileCount;
                folders += source->m_folderCount + (selection ? 1 : 0);
            } else {
                const auto child = source->m_childStats.value(path).toMap();
                if (!source->m_childStats.contains(path)) {
                    allKnown = false;
                    break;
                }
                bytes += child.value("bytes").toLongLong();
                files += child.value("files").toInt();
                folders += child.value("folders").toInt();
                if (!selection && child.value("directory").toBool()) --folders;
            }
        }
        if (allKnown) {
            m_totalSize = bytes;
            m_fileCount = files;
            m_folderCount = folders;
            m_statsReady = m_statsAvailable = true;
            // The previous total is immediately displayable during validation;
            // show it as cached rather than falsely claiming a fresh scan.
            m_statsState = source->m_statsReady ? source->m_statsState : QStringLiteral("cached");
            m_statsError = source->m_statsError;
            setCalculating(false);
            emit propertiesChanged();
            return true;
        }
    }
    // Do not wait on the displayed directory's calculating flag. A navigation
    // back to Home can leave that *parent* checking metadata even though the
    // requested child's size is already persisted. Fall through to the child's
    // own indexed request (or the selection request) instead. Waiting for the
    // parent calculatingChanged signal can strand every Properties dialog in
    // Home while Properties inside an opened child still works normally.
    return false;
}

void FilePropertiesManager::inspect(const QString &urlOrPath)
{
    inspectInternal(urlOrPath, false);
}

void FilePropertiesManager::inspectCached(const QString &urlOrPath)
{
    inspectInternal(urlOrPath, true);
}

bool FilePropertiesManager::restoreCachedStats(const QString &urlOrPath)
{
    const QUrl url = QUrl::fromUserInput(urlOrPath);
    if (!url.isLocalFile() || !cachedStats(QDir::cleanPath(url.toLocalFile()), true)) return false;
    inspectCached(urlOrPath);
    return true;
}

void FilePropertiesManager::invalidateCachedStats(const QString &urlOrPath)
{
    const QUrl url = QUrl::fromUserInput(urlOrPath);
    if (!url.isLocalFile()) return;
    auto *index = DirectorySizeIndex::instance();
    if (!index->snapshot(url.toLocalFile()).path.isEmpty())
        index->observeDirectory(url.toLocalFile());
    else
        index->observePaths({url.toLocalFile()});
}

void FilePropertiesManager::verifyCachedStats(const QString &urlOrPath)
{
    DirectorySizeIndex::instance()->request(urlOrPath, true);
}

FilePropertiesManager *FilePropertiesManager::cachedStats(const QString &path, bool lookupOnly)
{
    auto *cache = sizeCache();
    if (lookupOnly && !cache->entries.contains(path)) return nullptr;
    if (!cache->entries.contains(path) && cache->entries.size() >= 32) {
        auto oldest = cache->entries.end();
        for (auto it = cache->entries.begin(); it != cache->entries.end(); ++it) {
            if (it->source && it->source->m_cachedReaders > 0) continue;
            const bool running = it->source && it->source->calculating();
            const bool oldestRunning = oldest != cache->entries.end() && oldest->source && oldest->source->calculating();
            if (oldest == cache->entries.end() || (oldestRunning && !running)
                    || (oldestRunning == running && it->used < oldest->used)) oldest = it;
        }
        if (oldest == cache->entries.end()) return nullptr;
        delete oldest->source.data();
        cache->entries.erase(oldest);
    }
    auto &entry = cache->entries[path];
    entry.used = ++cache->serial;
    if (!entry.source) {
        entry.source = new FilePropertiesManager(cache);
        entry.source->setObjectName(QStringLiteral("cachedDirectorySize"));
        entry.source->m_backgroundStats = true;
        entry.source->m_indexOwner = true;
        entry.source->inspect(path);
    }
    // The cached source owns the initial request and listens for index change
    // notifications. Looking it up must not start a second validation each
    // time a Properties dialog opens or Home becomes visible again.
    const auto snapshot = DirectorySizeIndex::instance()->snapshot(path);
    if (snapshot.path.isEmpty() || (!entry.source->m_statsAvailable && !entry.source->calculating()))
        DirectorySizeIndex::instance()->request(path);
    return entry.source;
}

void FilePropertiesManager::attachSizeIndex()
{
    auto *index = DirectorySizeIndex::instance();
    if (!m_indexOwner) index->retain(this, m_path);
    m_statsDemand = index->foregroundDemand();
    if (!m_backgroundStats) {
        m_waitingStatsDemand = m_statsDemand;
        m_waitingStatsDemand->fetch_add(1, std::memory_order_relaxed);
    }
    const auto generation = m_generation;
    const auto update = [this, index, generation] {
        if (generation != m_generation) return;
        const auto snapshot = index->snapshot(m_path);
        m_totalSize = snapshot.totals.bytes; m_fileCount = snapshot.totals.files;
        m_folderCount = snapshot.totals.folders; m_childStats = snapshot.totals.children;
        m_statsReady = snapshot.complete && !snapshot.calculating && snapshot.state != "unavailable";
        m_statsAvailable = snapshot.available;
        m_statsState = snapshot.state; m_statsError = snapshot.error;
        setCalculating(snapshot.calculating);
        emit propertiesChanged();
    };
    m_indexConnection = connect(index, &DirectorySizeIndex::snapshotChanged, this,
        [this, update](const QString &path) { if (path == m_path) update(); });
    if (!index->request(m_path)) {
        m_statsState = "error";
        m_statsError = "Too many active directory-size requests.";
        setCalculating(false); emit propertiesChanged(); return;
    }
    update();
}

void FilePropertiesManager::inspectInternal(const QString &urlOrPath, bool cached)
{
    if (!m_accessBusy) {
        m_accessError.clear();
        emit accessProgressChanged();
    }
    cancelInspection();
    m_statsCancelled = std::make_shared<std::atomic_bool>(false);
    m_statsDemand = std::make_shared<std::atomic_int>(0);
    const auto cancelled = m_statsCancelled;
    const int generation = m_generation;
    const QUrl url = QUrl::fromUserInput(urlOrPath);
    if (m_indexOwner && url.isLocalFile()) {
        m_path = QDir::cleanPath(url.toLocalFile()); m_selection = {m_path};
        m_directory = m_localFile = true;
        attachSizeIndex();
        return;
    }
    m_localFile = url.isLocalFile();
    const QString local = url.isLocalFile() ? url.toLocalFile() : urlOrPath;
    const QFileInfo info(local);

    m_linkType.clear();
    m_linkTarget.clear();
    m_linkTargetExists = false;
    m_inode = 0;
    m_linkCount = 1;

    m_path = QDir::cleanPath(info.absoluteFilePath());
    m_selection = {m_path};
    m_commonLocation = info.absolutePath();
    m_selectedFolders = info.isDir() && !info.isSymLink() ? 1 : 0;
    m_selectedLinks = info.isSymLink() ? 1 : 0;
    m_selectedFiles = !m_selectedFolders && !m_selectedLinks ? 1 : 0;
    m_name = info.fileName();
    m_directory = info.isDir();
    m_totalSize = m_directory ? 0 : info.size();
    m_fileCount = m_directory ? 0 : 1;
    m_folderCount = 0;
    m_created = info.birthTime();
    m_modified = info.lastModified();
    m_accessed = info.lastRead();
    m_iconName = m_directory ? readFolderIcon(m_path) : QString();

    struct stat st {};
    const QByteArray nativePath = QFile::encodeName(m_path);
    if (::lstat(nativePath.constData(), &st) == 0) {
        m_inode = static_cast<qulonglong>(st.st_ino);
        m_linkCount = static_cast<int>(st.st_nlink);
    }

    if (info.isSymLink()) {
        m_linkType = QStringLiteral("symlink");
        m_linkTarget = info.symLinkTarget();
        m_linkTargetExists = QFileInfo(m_linkTarget).exists();
    } else if (info.suffix().compare(QStringLiteral("desktop"), Qt::CaseInsensitive) == 0) {
        QSettings desktop(m_path, QSettings::IniFormat);
        desktop.beginGroup(QStringLiteral("Desktop Entry"));
        const QString desktopType = desktop.value(QStringLiteral("Type")).toString().trimmed();
        if (desktopType.compare(QStringLiteral("Link"), Qt::CaseInsensitive) == 0) {
            m_linkType = QStringLiteral("shortcut");
            m_linkTarget = desktop.value(QStringLiteral("URL")).toString().trimmed();
            const QUrl targetUrl(m_linkTarget);
            m_linkTargetExists = !targetUrl.isLocalFile() || QFileInfo(targetUrl.toLocalFile()).exists();
        }
        desktop.endGroup();
    }
    if (m_linkType.isEmpty() && S_ISREG(st.st_mode) && st.st_nlink > 1)
        m_linkType = QStringLiteral("hardlink");

    refreshPermissions();
    emit propertiesChanged();

    if (cached && m_localFile && (m_directory || !info.exists())) {
        if (auto *source = cachedStats(m_path)) {
            DirectorySizeIndex::instance()->retain(this, m_path);
            m_cachedSource = source;
            ++source->m_cachedReaders;
            const auto update = [this, source, generation] {
                if (generation != m_generation) return;
                m_totalSize = source->m_totalSize;
                m_fileCount = source->m_fileCount;
                m_folderCount = source->m_folderCount;
                m_childStats = source->m_childStats;
                m_statsReady = source->m_statsReady;
                m_statsAvailable = source->m_statsAvailable;
                m_statsState = source->m_statsState; m_statsError = source->m_statsError;
                m_statsDemand = source->m_statsDemand;
                setCalculating(source->calculating());
                emit propertiesChanged();
            };
            m_cachedPropertiesConnection = connect(source, &FilePropertiesManager::propertiesChanged, this, update);
            m_cachedCalculatingConnection = connect(source, &FilePropertiesManager::calculatingChanged, this, update);
            update();
            return;
        }
    }
    if (!m_directory || (info.isSymLink() && !m_backgroundStats) || !info.exists()) {
        setCalculating(false);
        return;
    }
    if (reuseStatsOrWait({m_path}, false, generation)) return;
    if (cached && m_localFile) {
        attachSizeIndex();
        return;
    }
    if (m_localFile) {
        attachSizeIndex();
        return;
    }
    setCalculating(true);
    auto *watcher = new QFutureWatcher<FolderStats>(this);
    // Large trees (notably Home) can take time within the background CPU
    // budget. Publish a lower bound at most twice a second without queuing
    // per-file events or allowing Properties to reuse an unfinished snapshot.
    const auto progress = std::make_shared<DirectorySizeProgress>();
    auto *progressTimer = new QTimer(watcher);
    m_statsProgressTimer = progressTimer;
    progressTimer->setInterval(500);
    connect(progressTimer, &QTimer::timeout, this, [this, progress, generation] {
        if (generation != m_generation) return;
        const auto bytes = progress->bytes.load(std::memory_order_relaxed);
        const auto files = progress->files.load(std::memory_order_relaxed);
        const auto folders = progress->folders.load(std::memory_order_relaxed);
        if (bytes == m_totalSize && files == m_fileCount && folders == m_folderCount) return;
        m_totalSize = bytes; m_fileCount = files; m_folderCount = folders;
        emit propertiesChanged();
    });
    if (m_backgroundStats) progressTimer->start();
    connect(watcher, &QFutureWatcher<FolderStats>::finished, this, [this, watcher, progressTimer, generation]() {
        progressTimer->stop();
        const FolderStats stats = watcher->result();
        watcher->deleteLater();
        if (generation != m_generation)
            return;
        m_totalSize = stats.bytes;
        m_fileCount = stats.files;
        m_folderCount = stats.folders;
        m_childStats = stats.children;
        m_statsReady = true;
        setCalculating(false);
        emit propertiesChanged();
    });
    const QString root = m_path;
    const bool background = m_backgroundStats;
    const auto demand = m_statsDemand;
    watcher->setFuture(QtConcurrent::run(QThreadPool::globalInstance(),
        [root, cancelled, background, demand, progress] {
            return scanDirectorySize(root, cancelled, demand, progress, background);
        }));
}

void FilePropertiesManager::inspectSelection(const QStringList &urls)
{
    QStringList selection;
    bool local = true;
    for (const auto &input : urls) {
        const QUrl url = QUrl::fromUserInput(input);
        const QString item = url.isLocalFile() ? QDir::cleanPath(url.toLocalFile()) : url.toString();
        if (item.isEmpty() || selection.contains(item)) continue;
        selection.append(item);
        local &= url.isLocalFile();
    }
    if (selection.size() == 1) { inspect(urls.first()); return; }
    cancelInspection();
    m_statsCancelled = std::make_shared<std::atomic_bool>(false);
    m_statsDemand = std::make_shared<std::atomic_int>(0);
    const auto cancelled = m_statsCancelled;
    const int generation = m_generation;
    m_selection = selection;
    m_localFile = local && !selection.isEmpty();
    m_path = m_localFile ? selection.first() : QString();
    m_name = securityMessage("%1 selected items", "%1 seçili öğe").arg(selection.size());
    m_commonLocation.clear();
    m_selectedFiles = m_selectedFolders = m_selectedLinks = 0;
    m_totalSize = m_fileCount = m_folderCount = 0;
    m_created = m_modified = m_accessed = {};
    m_iconName.clear(); m_linkType.clear(); m_linkTarget.clear();
    m_linkTargetExists = false; m_inode = 0; m_linkCount = 1;
    m_directory = false;
    if (!m_accessBusy) { m_accessError.clear(); emit accessProgressChanged(); }
    if (m_localFile) {
        m_commonLocation = QFileInfo(selection.first()).absolutePath();
        for (const auto &path : selection) {
            if (QFileInfo(path).absolutePath() != m_commonLocation) m_commonLocation.clear();
            struct stat st {};
            if (::lstat(QFile::encodeName(path).constData(), &st) != 0) continue;
            if (S_ISDIR(st.st_mode)) ++m_selectedFolders;
            else if (S_ISLNK(st.st_mode)) ++m_selectedLinks;
            else if (S_ISREG(st.st_mode)) ++m_selectedFiles;
        }
    }
    m_directory = m_selectedFolders > 0;
    refreshPermissions();
    emit propertiesChanged();
    if (!m_localFile) { setCalculating(false); return; }

    // Combining file sizes must not queue a second recursive index scan. A
    // previous single-item Properties visit can also leave complete directory
    // snapshots in the shared index. Reuse them immediately (marking cached
    // snapshots approximate); only missing/incomplete directory totals scan.
    // Sort by path depth so a selected directory covers its selected children.
    auto *index = DirectorySizeIndex::instance();
    QStringList roots = selection;
    std::sort(roots.begin(), roots.end(), [](const QString &a, const QString &b) {
        return a.size() == b.size() ? a < b : a.size() < b.size();
    });
    QSet<QString> countedDirectories;
    qint64 knownBytes = 0;
    int knownFiles = 0, knownFolders = 0;
    bool allKnown = true, hasCachedSnapshot = false;
    for (const auto &path : roots) {
        // Walking parent paths is bounded by directory depth, not by how
        // many sibling folders were selected.
        bool covered = countedDirectories.contains(path);
        for (QString parent = QFileInfo(path).absolutePath(); !covered;) {
            covered = countedDirectories.contains(parent);
            if (parent == QStringLiteral("/") || parent.isEmpty()) break;
            parent = QFileInfo(parent).absolutePath();
        }
        if (covered) continue;
        struct stat info { };
        if (::lstat(QFile::encodeName(path).constData(), &info) != 0) {
            allKnown = false;
            break;
        }
        if (S_ISREG(info.st_mode)) {
            knownBytes += info.st_size;
            ++knownFiles;
        } else if (S_ISDIR(info.st_mode)) {
            const auto snapshot = index->snapshot(path);
            // An available, completed snapshot remains usable while its
            // metadata is being revalidated. Do not couple Properties to an
            // unrelated parent's in-progress verification.
            if (!snapshot.complete || !snapshot.available
                    || (snapshot.state != QStringLiteral("ready")
                        && snapshot.state != QStringLiteral("cached"))) {
                allKnown = false;
                break;
            }
            hasCachedSnapshot |= snapshot.calculating || snapshot.state == QStringLiteral("cached");
            knownBytes += snapshot.totals.bytes;
            knownFiles += snapshot.totals.files;
            knownFolders += snapshot.totals.folders + 1;
            countedDirectories.insert(path);
        }
        // Symlinks/special files are intentionally excluded, as in the index.
    }
    if (allKnown) {
        m_totalSize = knownBytes;
        m_fileCount = knownFiles;
        m_folderCount = knownFolders;
        m_statsReady = m_statsAvailable = true;
        // A cached subtree is a usable estimate, not a new verification.
        m_statsState = hasCachedSnapshot ? QStringLiteral("cached") : QStringLiteral("ready");
        setCalculating(false);
        emit propertiesChanged();
        return;
    }
    // Only selections with unknown directory totals need to join the footer
    // scan (or fall through to the selection worker). A plain file selection
    // must never wait for a large, unrelated footer scan to finish.
    if (reuseStatsOrWait(selection, true, generation)) return;

    setCalculating(true);
    m_statsDemand = index->foregroundDemand();
    m_waitingStatsDemand = m_statsDemand;
    m_waitingStatsDemand->fetch_add(1, std::memory_order_relaxed);
    m_indexConnection = connect(index, &DirectorySizeIndex::selectionReady, this,
        [this, generation](quint64 request, const DirectorySizeSnapshot &snapshot) {
            if (generation != m_generation || request != m_indexSelection) return;
            m_indexSelection = 0;
            m_totalSize = snapshot.totals.bytes; m_fileCount = snapshot.totals.files;
            m_folderCount = snapshot.totals.folders;
            m_statsReady = snapshot.complete; m_statsAvailable = snapshot.available;
            m_statsState = snapshot.state; m_statsError = snapshot.error;
            setCalculating(false); emit propertiesChanged();
        });
    m_indexSelection = index->inspectSelection(selection);
}

void FilePropertiesManager::refreshPermissions()
{
    m_permissionMode = 0;
    m_ownerName.clear();
    m_groupName.clear();
    m_permissionsEditable = false;
    m_permissionsText.clear();
    m_mixedPermissionMask = 0;
    if (multiple()) {
        bool first = true, mixedOwner = false, mixedGroup = false;
        m_permissionsEditable = m_localFile;
        for (const auto &path : m_selection) {
            struct stat item {};
            if (!m_localFile || ::lstat(QFile::encodeName(path).constData(), &item) != 0) {
                m_permissionsEditable = false; continue;
            }
            if (!S_ISREG(item.st_mode) && !S_ISDIR(item.st_mode)) continue;
            const auto user = ::getpwuid(item.st_uid);
            const QString owner = user ? QString::fromLocal8Bit(user->pw_name) : QString::number(item.st_uid);
            const auto account = ::getgrgid(item.st_gid);
            const QString group = account ? QString::fromLocal8Bit(account->gr_name) : QString::number(item.st_gid);
            const int mode = item.st_mode & 0777;
            if (first) { m_permissionMode = mode; m_ownerName = owner; m_groupName = group; first = false; }
            else {
                m_mixedPermissionMask |= m_permissionMode ^ mode;
                m_permissionMode &= mode;
                mixedOwner |= owner != m_ownerName; mixedGroup |= group != m_groupName;
            }
            m_permissionsEditable &= ::geteuid() == 0 || ::geteuid() == item.st_uid;
        }
        if (mixedOwner) m_ownerName.clear();
        if (mixedGroup) m_groupName.clear();
        m_permissionsEditable &= !first;
        m_permissionsText = first ? QString() : m_mixedPermissionMask
            ? securityMessage("Mixed permissions", "Farklı izinler")
            : QStringLiteral("0%1").arg(m_permissionMode, 3, 8, QLatin1Char('0'));
        emit permissionsChanged(); return;
    }
    struct stat st {};
    const QByteArray nativePath = QFile::encodeName(m_path);
    if (m_localFile && ::lstat(nativePath.constData(), &st) == 0) {
        m_permissionMode = static_cast<int>(st.st_mode & 0777);
        const auto owner = ::getpwuid(st.st_uid);
        m_ownerName = owner ? QString::fromLocal8Bit(owner->pw_name) : QString::number(st.st_uid);
        const auto group = ::getgrgid(st.st_gid);
        m_groupName = group ? QString::fromLocal8Bit(group->gr_name) : QString::number(st.st_gid);
        m_permissionsEditable = (S_ISREG(st.st_mode) || S_ISDIR(st.st_mode))
                                && (::geteuid() == 0 || ::geteuid() == st.st_uid);
        m_permissionsText = QStringLiteral("---------");
        const char letters[] = "rwxrwxrwx";
        for (int index = 0; index < 9; ++index)
            if (st.st_mode & (0400 >> index))
                m_permissionsText[index] = QLatin1Char(letters[index]);
        if (st.st_mode & S_ISUID) m_permissionsText[2] = QLatin1Char(st.st_mode & S_IXUSR ? 's' : 'S');
        if (st.st_mode & S_ISGID) m_permissionsText[5] = QLatin1Char(st.st_mode & S_IXGRP ? 's' : 'S');
        if (st.st_mode & S_ISVTX) m_permissionsText[8] = QLatin1Char(st.st_mode & S_IXOTH ? 't' : 'T');
    }
    emit permissionsChanged();
}

bool FilePropertiesManager::setPermissionMode(int mode)
{
    if (m_accessBusy || multiple())
        return false;
    if (!m_localFile || m_path.isEmpty() || mode < 0 || mode > 0777)
        return false;
    struct stat st {};
    const QByteArray nativePath = QFile::encodeName(m_path);
    if (::lstat(nativePath.constData(), &st) != 0) {
        emit error(tr("Could not read file permissions: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
        return false;
    }
    // Symbolic links have no editable POSIX mode. Never change their targets
    // through this dialog, including when the path was replaced after inspect().
    if ((!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode))
        || (::geteuid() != 0 && ::geteuid() != st.st_uid)) {
        emit error(tr("You do not own this item, or its permissions cannot be changed here."));
        return false;
    }
    // Ordinary rwx editor only: deliberately preserve existing setuid/setgid/sticky.
    const mode_t requested = static_cast<mode_t>(mode) | (st.st_mode & (S_ISUID | S_ISGID | S_ISVTX));
    if (::fchmodat(AT_FDCWD, nativePath.constData(), requested, AT_SYMLINK_NOFOLLOW) != 0) {
        emit error(tr("Could not change file permissions: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
        return false;
    }
    refreshPermissions();
    emit propertiesChanged();
    emit permissionsApplied(m_path);
    return true;
}

bool FilePropertiesManager::setFolderIcon(const QString &iconName)
{
    if (multiple() || !m_directory || m_path.isEmpty() || iconName.trimmed().isEmpty())
        return false;
    const QString metadataPath = QDir(m_path).filePath(QStringLiteral(".directory"));
    QSettings metadata(metadataPath, QSettings::IniFormat);
    metadata.beginGroup(QStringLiteral("Desktop Entry"));
    metadata.setValue(QStringLiteral("Icon"), iconName.trimmed());
    metadata.endGroup();
    metadata.sync();
    if (metadata.status() != QSettings::NoError) {
        emit error(QStringLiteral("Folder icon could not be saved."));
        return false;
    }
    m_iconName = iconName.trimmed();
    emit propertiesChanged();
    emit folderIconChanged(m_path, m_iconName);
    return true;
}

bool FilePropertiesManager::setLinkTarget(const QString &target)
{
    const QString trimmed = target.trimmed();
    if (multiple() || m_path.isEmpty() || m_linkType.isEmpty() || trimmed.isEmpty())
        return false;

    if (m_linkType == QStringLiteral("shortcut")) {
        QSettings desktop(m_path, QSettings::IniFormat);
        desktop.beginGroup(QStringLiteral("Desktop Entry"));
        desktop.setValue(QStringLiteral("URL"), trimmed);
        desktop.endGroup();
        desktop.sync();
        if (desktop.status() != QSettings::NoError) {
            emit error(QStringLiteral("Shortcut target could not be saved."));
            return false;
        }
        inspect(m_path);
        return true;
    }

    QString errorText;
    if (m_linkType == QStringLiteral("symlink")) {
        QString linkValue = trimmed;
        const QUrl candidate(trimmed);
        if (candidate.isValid() && candidate.isLocalFile())
            linkValue = candidate.toLocalFile();
        if (!atomicReplaceLink(m_path, linkValue, true, &errorText)) {
            emit error(QStringLiteral("Symbolic link target could not be changed: %1").arg(errorText));
            return false;
        }
        inspect(m_path);
        return true;
    }

    if (m_linkType == QStringLiteral("hardlink")) {
        const QString targetPath = localPathFromInput(trimmed, QFileInfo(m_path).absolutePath());
        const QFileInfo targetInfo(targetPath);
        if (!targetInfo.exists() || !targetInfo.isFile() || targetInfo.isSymLink()) {
            emit error(QStringLiteral("Hard link target must be an existing regular file."));
            return false;
        }
        if (!atomicReplaceLink(m_path, targetPath, false, &errorText)) {
            emit error(QStringLiteral("Hard link could not be changed: %1").arg(errorText));
            return false;
        }
        inspect(m_path);
        return true;
    }

    return false;
}

QString FilePropertiesManager::formatBytes(qint64 bytes) const
{
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}
