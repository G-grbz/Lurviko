#include "directorymodel.h"
#include "contentindexmodel.h"
#include "svgpreviewidentity.h"

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QLocale>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QProcess>
#include <QSaveFile>
#include <QStorageInfo>
#include <QThreadPool>
#include <QUrlQuery>
#include <QMimeDatabase>
#include <QSettings>
#include <QSet>
#include <QStandardPaths>
#include <QtConcurrent/QtConcurrentRun>

#include <KIO/ListJob>
#include <KIO/UDSEntry>
#include <KJob>

#include <algorithm>
#include <utility>

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace {

struct LocalLinkMetadata {
    QString type;
    QString target;
    QString previewRevision;
};

LocalLinkMetadata localLinkMetadata(const QString &path, const QString &suffix)
{
    LocalLinkMetadata result;
    const QFileInfo info(path);
    if (info.isSymLink()) {
        result.type = QStringLiteral("symlink");
        result.target = info.symLinkTarget();
        if (suffix == QStringLiteral("svg")) result.previewRevision = svgPreviewIdentity(path);
        return result;
    }

    if (suffix.compare(QStringLiteral("desktop"), Qt::CaseInsensitive) == 0) {
        QSettings desktop(path, QSettings::IniFormat);
        desktop.beginGroup(QStringLiteral("Desktop Entry"));
        const QString type = desktop.value(QStringLiteral("Type")).toString().trimmed();
        if (type.compare(QStringLiteral("Link"), Qt::CaseInsensitive) == 0) {
            result.type = QStringLiteral("shortcut");
            result.target = desktop.value(QStringLiteral("URL")).toString().trimmed();
            desktop.endGroup();
            return result;
        }
        desktop.endGroup();
    }

    struct stat st {};
    const QByteArray nativePath = QFile::encodeName(path);
    if (::lstat(nativePath.constData(), &st) == 0) {
        if (S_ISREG(st.st_mode) && st.st_nlink > 1) result.type = QStringLiteral("hardlink");
        if (suffix == QStringLiteral("svg")) result.previewRevision = svgPreviewIdentity(st);
    }
    return result;
}

struct VideoMetadataCache {
    bool loaded = false;
    bool dirty = false;
    QHash<QString, QString> values;
};

VideoMetadataCache &videoMetadataCache()
{
    static VideoMetadataCache cache;
    return cache;
}

QString videoMetadataCachePath()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(base);
    return QDir(base).filePath(QStringLiteral("video-metadata-v1.json"));
}

QString videoMetadataKey(const QString &path, qint64 size, qint64 modifiedMs)
{
    const QByteArray input = path.toUtf8() + '\0' + QByteArray::number(size) + '\0'
        + QByteArray::number(modifiedMs);
    return QString::fromLatin1(QCryptographicHash::hash(input, QCryptographicHash::Sha256).toHex());
}

void ensureVideoMetadataCacheLoaded()
{
    VideoMetadataCache &cache = videoMetadataCache();
    if (cache.loaded)
        return;
    cache.loaded = true;
    QFile file(videoMetadataCachePath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject values = QJsonDocument::fromJson(file.readAll()).object()
                                   .value(QStringLiteral("entries")).toObject();
    for (auto it = values.constBegin(); it != values.constEnd(); ++it)
        cache.values.insert(it.key(), it.value().toString());
}

bool cachedVideoMetadata(const QString &path, qint64 size, qint64 modifiedMs, QString *value)
{
    ensureVideoMetadataCacheLoaded();
    const auto &values = videoMetadataCache().values;
    const auto it = values.constFind(videoMetadataKey(path, size, modifiedMs));
    if (it == values.constEnd())
        return false;
    if (value)
        *value = it.value();
    return true;
}

void storeVideoMetadata(const QString &path, qint64 size, qint64 modifiedMs, const QString &value)
{
    ensureVideoMetadataCacheLoaded();
    VideoMetadataCache &cache = videoMetadataCache();
    cache.values.insert(videoMetadataKey(path, size, modifiedMs), value);
    cache.dirty = true;
}

void persistVideoMetadataCache()
{
    VideoMetadataCache &cache = videoMetadataCache();
    if (!cache.loaded || !cache.dirty)
        return;
    QJsonObject values;
    for (auto it = cache.values.constBegin(); it != cache.values.constEnd(); ++it)
        values.insert(it.key(), it.value());
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("entries"), values);
    QSaveFile file(videoMetadataCachePath());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        if (file.commit())
            cache.dirty = false;
    }
}

bool isVideoEntry(const QString &mimeType, const QString &suffix)
{
    static const QSet<QString> suffixes = {
        QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("avi"),
        QStringLiteral("mov"), QStringLiteral("webm"), QStringLiteral("m4v"),
        QStringLiteral("ts"), QStringLiteral("mpeg"), QStringLiteral("mpg"),
        QStringLiteral("m2ts"), QStringLiteral("mts"), QStringLiteral("flv"),
        QStringLiteral("wmv"), QStringLiteral("ogv"), QStringLiteral("3gp")
    };
    return mimeType.startsWith(QStringLiteral("video/")) || suffixes.contains(suffix);
}

int folderPreviewPriority(const QString &suffix)
{
    static const QSet<QString> imageTypes = {
        QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
        QStringLiteral("webp"), QStringLiteral("bmp"), QStringLiteral("gif"),
        QStringLiteral("svg")
    };
    static const QSet<QString> videoTypes = {
        QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("avi"),
        QStringLiteral("mov"), QStringLiteral("webm"), QStringLiteral("m4v"),
        QStringLiteral("ts"), QStringLiteral("mpeg"), QStringLiteral("mpg")
    };
    static const QSet<QString> documentTypes = {
        QStringLiteral("pdf"), QStringLiteral("mp3")
    };
    static const QSet<QString> textTypes = {
        QStringLiteral("srt"), QStringLiteral("lrc"), QStringLiteral("lrclib"),
        QStringLiteral("vtt"), QStringLiteral("ass"), QStringLiteral("ssa"),
        QStringLiteral("txt"), QStringLiteral("md"), QStringLiteral("log"),
        QStringLiteral("nfo"), QStringLiteral("json"), QStringLiteral("xml"),
        QStringLiteral("yaml"), QStringLiteral("yml"), QStringLiteral("toml"),
        QStringLiteral("ini"), QStringLiteral("conf"), QStringLiteral("cfg"),
        QStringLiteral("desktop"), QStringLiteral("service"), QStringLiteral("csv"),
        QStringLiteral("tsv"), QStringLiteral("m3u"), QStringLiteral("m3u8"),
        QStringLiteral("pls")
    };

    if (imageTypes.contains(suffix))
        return 0;
    if (videoTypes.contains(suffix))
        return 1;
    if (documentTypes.contains(suffix))
        return 2;
    if (textTypes.contains(suffix))
        return 3;
    return -1;
}

} // namespace

DirectoryModel::DirectoryModel(QObject *parent)
    : QAbstractListModel(parent), m_location(QUrl::fromLocalFile(QDir::homePath()))
{
    QSettings settings(QStringLiteral("Lurviko"), QStringLiteral("Lurviko"));
    m_showHidden = settings.value(QStringLiteral("browser/showHidden"), false).toBool();
    m_hiddenPlacement = settings.value(QStringLiteral("browser/hiddenPlacement"), QStringLiteral("normal")).toString();
    if (m_hiddenPlacement != QStringLiteral("top") && m_hiddenPlacement != QStringLiteral("bottom"))
        m_hiddenPlacement = QStringLiteral("normal");
    m_normalSortMode = settings.value(QStringLiteral("browser/sortMode"), QStringLiteral("name")).toString();
    m_normalSortAscending = settings.value(QStringLiteral("browser/sortAscending"), true).toBool();
    m_categorySortMode = settings.value(QStringLiteral("category/sortMode"), QStringLiteral("date")).toString();
    m_categorySortAscending = settings.value(QStringLiteral("category/sortAscending"), false).toBool();
    m_folderPreviewsEnabled = settings.value(QStringLiteral("BrowserView/folderPreviewsEnabled"), true).toBool();
    if (!settings.contains(QStringLiteral("category/sortMode")))
        settings.setValue(QStringLiteral("category/sortMode"), m_categorySortMode);
    if (!settings.contains(QStringLiteral("category/sortAscending")))
        settings.setValue(QStringLiteral("category/sortAscending"), m_categorySortAscending);
    m_sortMode = m_normalSortMode;
    m_sortAscending = m_normalSortAscending;
    // Search results request video metadata only for visible delegates. Keep
    // availability ready without scanning every search result at completion.
    m_videoProbeAvailable = !QStandardPaths::findExecutable(QStringLiteral("ffprobe")).isEmpty();

    // One bounded update per event-loop turn gives input and painting a chance
    // to run even when a fast scan finds tens of thousands of matches.
    m_searchResultTimer.setInterval(16);
    m_searchResultTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_searchResultTimer, &QTimer::timeout, this, &DirectoryModel::drainSearchResults);

    m_localRefreshTimer.setSingleShot(true);
    // Coalesce the burst of directoryChanged notifications produced by KIO
    // copy/move/delete/overwrite jobs.  The operation-finished path uses this
    // very same timer, so one filesystem change results in one model refresh.
    m_localRefreshTimer.setInterval(320);
    connect(&m_localRefreshTimer, &QTimer::timeout, this, [this]() {
        // Recursive search owns a stable snapshot/session. Filesystem watcher
        // refreshes must never restart that recursive scan underneath it.
        if (!m_searchQuery.isEmpty() || !m_activeSearchSessionId.isEmpty())
            return;
        if (m_location.isLocalFile()
                || (m_location.scheme() == QStringLiteral("trash")
                    && !resolveTrashLocalPath(m_location).isEmpty()))
            refresh();
    });
    connect(&m_localWatcher, &QFileSystemWatcher::directoryChanged,
            this, [this](const QString &) {
        if (!m_searchQuery.isEmpty() || !m_activeSearchSessionId.isEmpty())
            return;
        if (m_heavyIoBusy) {
            m_refreshDeferredByHeavyIo = true;
            return;
        }
        if (!m_localRefreshTimer.isActive())
            m_localRefreshTimer.start();
    });

    // QFileSystemWatcher does not emit directoryChanged when an existing child
    // file merely grows.  Watch the child files as well and update only the
    // affected model rows.  Not restarting this timer while it is active is
    // important: a continuously-written MKV must still repaint every few
    // hundred milliseconds rather than postponing the update until writing ends.
    m_localWatchInstallTimer.setSingleShot(true);
    m_localWatchInstallTimer.setInterval(4);
    connect(&m_localWatchInstallTimer, &QTimer::timeout,
            this, &DirectoryModel::installLocalFileWatchBatch);

    m_localMetadataTimer.setSingleShot(true);
    m_previewSettleTimer.setSingleShot(true);
    m_previewSettleTimer.setInterval(3000);
    connect(&m_previewSettleTimer, &QTimer::timeout, this, &DirectoryModel::settleVideoPreviews);
    m_localMetadataTimer.setInterval(220);
    connect(&m_localMetadataTimer, &QTimer::timeout, this, &DirectoryModel::refreshLocalFileMetadata);
    connect(&m_localWatcher, &QFileSystemWatcher::fileChanged, this,
            [this](const QString &path) {
        if (!m_location.isLocalFile())
            return;
        if (!m_searchQuery.isEmpty() || !m_activeSearchSessionId.isEmpty())
            return;
        if (m_heavyIoBusy) {
            m_refreshDeferredByHeavyIo = true;
            return;
        }
        m_pendingLocalMetadataPaths.insert(QDir::cleanPath(path));
        if (!m_localMetadataTimer.isActive())
            m_localMetadataTimer.start();
    });
    // Let QML apply an initialLocation binding before starting the first list.
    // Starting Home synchronously here caused every externally opened folder
    // to launch and immediately discard an unnecessary Home directory job.
    QTimer::singleShot(0, this, [this] {
        if (m_localGeneration == 0 && !m_loading)
            refresh();
    });
}

DirectoryModel::~DirectoryModel()
{
    cancelKioListing();
    const auto sessions = m_searchSessions.values();
    for (const auto &session : sessions) {
        if (session && session->canceled)
            session->canceled->store(true, std::memory_order_relaxed);
        if (session && session->stream) {
            QMutexLocker lock(&session->stream->mutex);
            session->stream->spaceAvailable.wakeAll();
        }
        if (session && session->job)
            session->job->kill();
    }
    persistVideoMetadataCache();
}

int DirectoryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant DirectoryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return {};

    const Entry &e = m_items.at(index.row());
    switch (role) {
    case NameRole: return e.name;
    case UrlRole: return e.url.toString();
    case LocalPathRole: return e.localPath;
    case IsDirRole: return e.isDir;
    case SizeRole: return e.size;
    case ModifiedRole: return e.modified;
    case CreatedRole: return e.created;
    case SuffixRole: return e.suffix;
    case HiddenRole: return e.hidden;
    case MimeTypeRole: return e.mimeType;
    case SystemIconNameRole: return e.systemIconName;
    case VideoInfoRole: return e.videoInfo;
    case ChildCountRole: return e.childCount;
    case FolderPreviewPathsRole: return e.folderPreviewPaths;
    case LinkTypeRole: return e.linkType;
    case LinkTargetRole: return e.linkTarget;
    case PreviewRevisionRole: return e.previewRevision.isEmpty()
        ? QString::number(e.modified.toMSecsSinceEpoch()) + '-' + QString::number(e.size) : e.previewRevision;
    default: return {};
    }
}


QVariantMap DirectoryModel::itemAt(int row) const
{
    QVariantMap result;
    if (row < 0 || row >= m_items.size())
        return result;

    const Entry &e = m_items.at(row);
    result.insert(QStringLiteral("name"), e.name);
    result.insert(QStringLiteral("itemUrl"), e.url.toString());
    result.insert(QStringLiteral("localPath"), e.localPath);
    result.insert(QStringLiteral("isDir"), e.isDir);
    result.insert(QStringLiteral("size"), e.size);
    result.insert(QStringLiteral("modified"), e.modified);
    result.insert(QStringLiteral("previewRevision"), data(index(row, 0), PreviewRevisionRole));
    result.insert(QStringLiteral("created"), e.created);
    result.insert(QStringLiteral("suffix"), e.suffix);
    result.insert(QStringLiteral("hidden"), e.hidden);
    result.insert(QStringLiteral("mimeType"), e.mimeType);
    result.insert(QStringLiteral("systemIconName"), e.systemIconName);
    result.insert(QStringLiteral("videoInfo"), e.videoInfo);
    result.insert(QStringLiteral("childCount"), e.childCount);
    result.insert(QStringLiteral("folderPreviewPaths"), e.folderPreviewPaths);
    result.insert(QStringLiteral("linkType"), e.linkType);
    result.insert(QStringLiteral("linkTarget"), e.linkTarget);
    return result;
}

QVariantList DirectoryModel::itemsForUrls(const QStringList &urls) const
{
    QVariantList items;
    if (urls.isEmpty())
        return items;

    QSet<QUrl> requested;
    for (const QString &url : urls)
        requested.insert(QUrl(url));
    items.reserve(qMin(requested.size(), m_items.size()));
    for (int row = 0; row < m_items.size(); ++row) {
        if (!requested.remove(m_items.at(row).url))
            continue;
        items.append(itemAt(row));
        if (requested.isEmpty())
            break;
    }
    return items;
}

QVariantList DirectoryModel::allItemUrls() const
{
    QVariantList urls;
    urls.reserve(m_items.size());
    for (const Entry &entry : m_items)
        urls.append(entry.url.toString());
    return urls;
}

int DirectoryModel::indexOfUrl(const QString &itemUrl) const
{
    const QString trimmed = itemUrl.trimmed();
    if (trimmed.isEmpty())
        return -1;

    QUrl requested(trimmed);
    if (!requested.isValid() || requested.scheme().isEmpty())
        requested = QUrl::fromLocalFile(QDir::cleanPath(trimmed));

    const bool requestedLocal = requested.isLocalFile();
    const QString requestedPath = requestedLocal
        ? QDir::cleanPath(requested.toLocalFile()) : QString();
    const QString requestedEncoded = requested.toString(QUrl::FullyEncoded);

    for (int i = 0; i < m_items.size(); ++i) {
        const QUrl &entryUrl = m_items.at(i).url;
        if (requestedLocal && entryUrl.isLocalFile()) {
            if (QDir::cleanPath(entryUrl.toLocalFile()) == requestedPath)
                return i;
            continue;
        }
        if (entryUrl.toString(QUrl::FullyEncoded) == requestedEncoded)
            return i;
    }
    return -1;
}

QVariantList DirectoryModel::pathCompletions(const QString &input) const
{
    QVariantList result;
    QString typed = input.trimmed();
    if (typed.isEmpty())
        return result;

    // The editable location bar normally contains a native local path.  Keep
    // URL schemes untouched; local file:// URLs are converted back to paths.
    QString localInput;
    const QUrl candidate(typed);
    if (candidate.isValid() && !candidate.scheme().isEmpty()) {
        if (!candidate.isLocalFile())
            return result;
        localInput = candidate.toLocalFile();
    } else {
        localInput = typed;
    }

    if (localInput == QStringLiteral("~"))
        localInput = QDir::homePath();
    else if (localInput.startsWith(QStringLiteral("~/")))
        localInput = QDir::homePath() + localInput.mid(1);

    if (!QDir::isAbsolutePath(localInput)) {
        if (!m_location.isLocalFile())
            return result;
        localInput = QDir(m_location.toLocalFile()).absoluteFilePath(localInput);
    }

    const bool endsWithSlash = localInput.endsWith(QLatin1Char('/'));
    QString parentPath;
    QString prefix;
    if (endsWithSlash) {
        parentPath = QDir::cleanPath(localInput);
        prefix.clear();
    } else {
        QFileInfo partial(localInput);
        parentPath = partial.path();
        prefix = partial.fileName();
    }

    QDir parentDir(parentPath);
    if (!parentDir.exists())
        return result;

    QDir::Filters filters = QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System;
    const QFileInfoList entries = parentDir.entryInfoList(filters, QDir::Name | QDir::IgnoreCase);
    const bool explicitHiddenPrefix = prefix.startsWith(QLatin1Char('.'));

    for (const QFileInfo &entry : entries) {
        const QString name = entry.fileName();
        if (!prefix.isEmpty() && !name.startsWith(prefix, Qt::CaseInsensitive))
            continue;
        if (name.startsWith(QLatin1Char('.')) && !explicitHiddenPrefix)
            continue;

        const QString fullPath = QDir::cleanPath(entry.absoluteFilePath()) + QLatin1Char('/');
        QVariantMap item;
        item.insert(QStringLiteral("label"), name + QLatin1Char('/'));
        item.insert(QStringLiteral("value"), fullPath);
        result.push_back(item);
        if (result.size() >= 40)
            break;
    }
    return result;
}

QHash<int, QByteArray> DirectoryModel::roleNames() const
{
    return {
        {NameRole, "name"},
        {UrlRole, "itemUrl"},
        {LocalPathRole, "localPath"},
        {IsDirRole, "isDir"},
        {SizeRole, "size"},
        {ModifiedRole, "modified"},
        {PreviewRevisionRole, "previewRevision"},
        {CreatedRole, "created"},
        {SuffixRole, "suffix"},
        {HiddenRole, "hidden"},
        {MimeTypeRole, "mimeType"},
        {SystemIconNameRole, "systemIconName"},
        {VideoInfoRole, "videoInfo"},
        {ChildCountRole, "childCount"},
        {FolderPreviewPathsRole, "folderPreviewPaths"},
        {LinkTypeRole, "linkType"},
        {LinkTargetRole, "linkTarget"}
    };
}

QUrl DirectoryModel::normalizeLocation(const QString &location)
{
    const QString trimmed = location.trimmed();
    if (trimmed.isEmpty())
        return {};

    // KIO URLs such as trash:/ have a valid scheme without containing ://.
    // Detect schemes first so they are not accidentally converted into a
    // local path like /current/directory/trash:.
    const QUrl candidate(trimmed);
    if (candidate.isValid() && !candidate.scheme().isEmpty())
        return candidate;

    QString localPath = trimmed;
    if (localPath == QStringLiteral("~"))
        localPath = QDir::homePath();
    else if (localPath.startsWith(QStringLiteral("~/")))
        localPath = QDir::homePath() + localPath.mid(1);
    return QUrl::fromLocalFile(QDir(localPath).absolutePath());
}

void DirectoryModel::setLocation(const QString &location)
{
    const QUrl url = normalizeLocation(location);
    if (!url.isValid() || url.isEmpty() || url == m_location)
        return;

    m_localWatchInstallTimer.stop();
    m_localWatchQueue.clear();

    if (!m_searchQuery.isEmpty() || !m_activeSearchSessionId.isEmpty()) {
        const QString sessionId = m_activeSearchSessionId;
        m_activeSearchSessionId.clear();
        if (!sessionId.isEmpty())
            cancelSearchSession(sessionId, true);
        m_searchJob.clear();
        m_searchQuery.clear();
        m_searchEverywhere = false;
        ++m_searchGeneration;
        emit searchChanged();
    }

    const bool wasCategory = m_location.scheme() == QStringLiteral("category");
    const bool isCategory = url.scheme() == QStringLiteral("category");
    const QString nextSortMode = isCategory ? m_categorySortMode : m_normalSortMode;
    const bool nextSortAscending = isCategory ? m_categorySortAscending : m_normalSortAscending;
    const bool sortModeChangedForLocation = m_sortMode != nextSortMode;
    const bool sortDirectionChangedForLocation = m_sortAscending != nextSortAscending;

    m_location = url;
    if (wasCategory != isCategory) {
        m_sortMode = nextSortMode;
        m_sortAscending = nextSortAscending;
    }
    emit locationChanged();
    if (sortModeChangedForLocation)
        emit sortModeChanged();
    if (sortDirectionChangedForLocation)
        emit sortAscendingChanged();
    refreshInternal(false);
}

void DirectoryModel::setContentIndexSource(ContentIndexModel *source)
{
    if (m_contentIndexSource == source)
        return;
    if (m_contentIndexSource)
        disconnect(m_contentIndexSource, nullptr, this, nullptr);
    m_contentIndexSource = source;
    if (source) {
        connect(source, &ContentIndexModel::categoryFilesChanged, this, [this](const QStringList &categories) {
            if (m_location.scheme() == QStringLiteral("category")
                    && categories.contains(m_location.path().section(QLatin1Char('/'), 1, 1)))
                refreshInternal(true);
        });
    }
    emit contentIndexSourceChanged();
    if (m_location.scheme() == QStringLiteral("category"))
        refreshInternal(false);
}

void DirectoryModel::setShowHidden(bool value)
{
    if (m_showHidden == value)
        return;
    m_showHidden = value;
    QSettings settings(QStringLiteral("Lurviko"), QStringLiteral("Lurviko"));
    settings.setValue(QStringLiteral("browser/showHidden"), m_showHidden);
    emit showHiddenChanged();
    refresh();
}

void DirectoryModel::setHiddenPlacement(const QString &value)
{
    QString normalized = value.toLower();
    if (normalized != QStringLiteral("top") && normalized != QStringLiteral("bottom"))
        normalized = QStringLiteral("normal");
    if (m_hiddenPlacement == normalized)
        return;
    m_hiddenPlacement = normalized;
    QSettings settings(QStringLiteral("Lurviko"), QStringLiteral("Lurviko"));
    settings.setValue(QStringLiteral("browser/hiddenPlacement"), m_hiddenPlacement);
    emit hiddenPlacementChanged();
    sortEntries();
}

void DirectoryModel::setSortMode(const QString &value)
{
    const QString normalized = value.toLower();
    if (normalized != QStringLiteral("name") && normalized != QStringLiteral("date") &&
        normalized != QStringLiteral("created") && normalized != QStringLiteral("size") &&
        normalized != QStringLiteral("type"))
        return;
    if (m_sortMode == normalized)
        return;
    m_sortMode = normalized;
    QSettings settings(QStringLiteral("Lurviko"), QStringLiteral("Lurviko"));
    if (m_location.scheme() == QStringLiteral("category")) {
        m_categorySortMode = m_sortMode;
        settings.setValue(QStringLiteral("category/sortMode"), m_categorySortMode);
    } else {
        m_normalSortMode = m_sortMode;
        settings.setValue(QStringLiteral("browser/sortMode"), m_normalSortMode);
    }
    emit sortModeChanged();
    sortEntries();
}

void DirectoryModel::setSortAscending(bool value)
{
    if (m_sortAscending == value)
        return;
    m_sortAscending = value;
    QSettings settings(QStringLiteral("Lurviko"), QStringLiteral("Lurviko"));
    if (m_location.scheme() == QStringLiteral("category")) {
        m_categorySortAscending = m_sortAscending;
        settings.setValue(QStringLiteral("category/sortAscending"), m_categorySortAscending);
    } else {
        m_normalSortAscending = m_sortAscending;
        settings.setValue(QStringLiteral("browser/sortAscending"), m_normalSortAscending);
    }
    emit sortAscendingChanged();
    sortEntries();
}

void DirectoryModel::setGoogleAccessToken(const QString &value)
{
    if (m_googleAccessToken == value)
        return;
    m_googleAccessToken = value;
    emit googleAccessTokenChanged();
    if (m_location.scheme() == QStringLiteral("gdrive"))
        refresh();
}

void DirectoryModel::setOneDriveAccessToken(const QString &value)
{
    if (m_oneDriveAccessToken == value)
        return;
    m_oneDriveAccessToken = value;
    emit oneDriveAccessTokenChanged();
    if (m_location.scheme() == QStringLiteral("onedrive"))
        refresh();
}

QString DirectoryModel::parentLocation() const
{
    if (m_location.scheme() == QStringLiteral("category"))
        return QUrl::fromLocalFile(QDir::homePath()).toString();
    if (m_location.scheme() == QStringLiteral("gdrive")) {
        if (m_googleFolderId == QStringLiteral("root") || m_googleParentId.isEmpty())
            return QStringLiteral("gdrive://root");
        return QStringLiteral("gdrive://folder/%1").arg(m_googleParentId);
    }
    if (m_location.scheme() == QStringLiteral("onedrive")) {
        if (m_oneDriveFolderId == QStringLiteral("root") || m_oneDriveParentId.isEmpty())
            return QStringLiteral("onedrive://root");
        return QStringLiteral("onedrive://folder/%1").arg(QString::fromLatin1(QUrl::toPercentEncoding(m_oneDriveParentId)));
    }

    if (m_location.isLocalFile()) {
        QDir dir(m_location.toLocalFile());
        dir.cdUp();
        return QUrl::fromLocalFile(dir.absolutePath()).toString();
    }

    QUrl parent = m_location.adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash);
    if (parent.path().isEmpty())
        return m_location.toString();
    return parent.toString();
}

QString DirectoryModel::displayLocation() const
{
    if (m_location.scheme() == QStringLiteral("gdrive")) {
        if (m_googleFolderId == QStringLiteral("root"))
            return QStringLiteral("Google Drive");
        return QStringLiteral("Google Drive / %1").arg(m_googleFolderName);
    }
    if (m_location.scheme() == QStringLiteral("onedrive")) {
        if (m_oneDriveFolderId == QStringLiteral("root"))
            return QStringLiteral("OneDrive");
        return QStringLiteral("OneDrive / %1").arg(m_oneDriveFolderName);
    }
    return m_location.isLocalFile() ? m_location.toLocalFile() : m_location.toDisplayString();
}

void DirectoryModel::setHeavyIoBusy(bool value)
{
    if (m_heavyIoBusy == value)
        return;
    m_heavyIoBusy = value;

    if (m_heavyIoBusy) {
        // Merely starting a transfer must not force a model refresh. Existing
        // delegates (and their already-decoded thumbnails) stay alive. A
        // consolidated refresh is armed only when an actual filesystem change,
        // explicit refresh request, or cheap busy-mode directory load occurs.
        if (m_localRefreshTimer.isActive()) {
            m_localRefreshTimer.stop();
            m_refreshDeferredByHeavyIo = true;
        }
        m_localMetadataTimer.stop();
        m_pendingLocalMetadataPaths.clear();
        const QStringList watchedFiles = m_localWatcher.files();
        if (!watchedFiles.isEmpty())
            m_localWatcher.removePaths(watchedFiles);
        ++m_videoProbeGeneration;
        m_videoProbeQueue.clear();
        m_videoProbePending.clear();
        if (m_activeVideoProbe) {
            m_activeVideoProbe->disconnect(this);
            m_activeVideoProbe->kill();
            m_activeVideoProbe->deleteLater();
            m_activeVideoProbe.clear();
        }
    } else if (m_refreshDeferredByHeavyIo && m_location.isLocalFile()) {
        m_refreshDeferredByHeavyIo = false;
        m_localRefreshTimer.start(180);
    } else if (m_location.isLocalFile()) {
        // If the transfer did not touch this directory there is nothing to
        // relist, but restore the lightweight watchers/probes that were paused.
        watchLocalFiles(m_items);
        scheduleVideoMetadata();
    }
    emit heavyIoBusyChanged();
}

void DirectoryModel::setFolderPreviewsEnabled(bool value)
{
    if (m_folderPreviewsEnabled == value)
        return;
    m_folderPreviewsEnabled = value;
    emit folderPreviewsEnabledChanged();
    // Folder details are deliberately collected off the GUI thread. Refresh
    // only the current local view when the feature is toggled so disabling it
    // immediately removes the expensive child-directory scans.
    if (m_location.isLocalFile())
        refreshInternal(true);
}

void DirectoryModel::refresh()
{
    m_localRefreshTimer.stop();
    if (m_heavyIoBusy && m_location.isLocalFile()) {
        m_refreshDeferredByHeavyIo = true;
        return;
    }
    refreshInternal(true);
}

void DirectoryModel::requestRefresh()
{
    // Local file operations also trigger QFileSystemWatcher.  Scheduling both
    // through the same single-shot timer prevents an explicit KIO completion
    // refresh from racing the watcher refresh and repeatedly resetting views.
    if (m_location.isLocalFile()) {
        if (m_heavyIoBusy) {
            m_refreshDeferredByHeavyIo = true;
            return;
        }
        m_localRefreshTimer.start();
        return;
    }
    refreshInternal(true);
}

void DirectoryModel::refreshInternal(bool preserveView)
{
    cancelKioListing();
    const bool incrementalLocalRefresh = preserveView && m_searchQuery.isEmpty() && m_location.isLocalFile();
    const bool incrementalSearchRefresh = preserveView && !m_searchQuery.isEmpty() && m_location.isLocalFile();
    const bool incrementalCategoryRefresh = preserveView
            && m_location.scheme() == QStringLiteral("category") && !m_loading;
    // Automatic refreshes keep the existing view usable, so QML must not arm
    // its reset-oriented scroll restore path or replace it with a loading panel.
    if (!incrementalLocalRefresh && !incrementalSearchRefresh && !incrementalCategoryRefresh)
        emit refreshAboutToStart(preserveView);
    setErrorString(QString());

    // Any non-local load/search invalidates an older local worker that may
    // still be waiting on an automount.
    if (!m_searchQuery.isEmpty() || !m_location.isLocalFile())
        ++m_localGeneration;

    if (!m_location.isLocalFile()) {
        m_localMetadataTimer.stop();
        m_pendingLocalMetadataPaths.clear();
        const QStringList watchedFiles = m_localWatcher.files();
        if (!watchedFiles.isEmpty())
            m_localWatcher.removePaths(watchedFiles);
        const QStringList watchedDirectories = m_localWatcher.directories();
        if (!watchedDirectories.isEmpty())
            m_localWatcher.removePaths(watchedDirectories);
    }

    // Local paths may be systemd automount points (/mnt/media*, NAS mounts,
    // removable disks).  Touching QStorageInfo/QDir on the GUI thread can
    // block until the mount is ready, preventing even the loading UI from
    // painting.  loadLocal() performs both listing and storage probing on a
    // worker thread, so return to the event loop immediately here.
    if (m_searchQuery.isEmpty() && m_location.isLocalFile()) {
        loadLocal(preserveView);
        return;
    }

    if (m_location.scheme() == QStringLiteral("category")) {
        loadIndexedCategory(incrementalCategoryRefresh);
        return;
    }
    updateStorageInfo();
    if (!m_searchQuery.isEmpty())
        loadSearch(incrementalSearchRefresh);
    else if (m_location.scheme() == QStringLiteral("gdrive"))
        loadGoogleDrive();
    else if (m_location.scheme() == QStringLiteral("onedrive"))
        loadOneDrive();
    else
        loadKio();
}

void DirectoryModel::loadIndexedCategory(bool incremental)
{
    const int generation = ++m_localGeneration;
    const QString key = m_location.path().section(QLatin1Char('/'), 1, 1);
    const QVariantList files = m_contentIndexSource
        ? m_contentIndexSource->filesForCategory(key) : QVariantList{};
    const QString query = m_searchQuery.trimmed();
    if (!incremental)
        setLoading(true);

    auto *watcher = new QFutureWatcher<LocalLoadResult>(this);
    connect(watcher, &QFutureWatcher<LocalLoadResult>::finished, this,
            [this, watcher, generation, incremental] {
        LocalLoadResult result = watcher->result();
        watcher->deleteLater();
        if (generation != m_localGeneration || m_location.scheme() != QStringLiteral("category"))
            return;
        sortEntriesVector(result.items);
        beginResetModel();
        m_items = std::move(result.items);
        endResetModel();
        const bool storageChanged = m_storageTotalBytes != result.storageTotalBytes
                || m_storageAvailableBytes != result.storageAvailableBytes;
        m_storageTotalBytes = result.storageTotalBytes;
        m_storageAvailableBytes = result.storageAvailableBytes;
        if (storageChanged)
            emit storageInfoChanged();
        emit countChanged();
        scheduleVideoMetadata();
        if (!incremental)
            setLoading(false);
        emit refreshCompleted();
    });

    watcher->setFuture(QtConcurrent::run([files, query, key] {
        LocalLoadResult result;
        QVector<Entry> &entries = result.items;
        entries.reserve(files.size());
        QMimeDatabase mimeDb;
        QHash<QString, QMimeType> mimeBySuffix;
        const bool lightweightMedia = key == QStringLiteral("images")
                                   || key == QStringLiteral("videos");
        const bool wildcard = query.contains(QLatin1Char('*')) || query.contains(QLatin1Char('?'));
        const QRegularExpression pattern = wildcard
            ? QRegularExpression::fromWildcard(query, Qt::CaseInsensitive)
            : QRegularExpression();
        for (const QVariant &value : files) {
            const QVariantMap item = value.toMap();
            const QString path = item.value(QStringLiteral("path")).toString();
            const QString name = item.value(QStringLiteral("name")).toString();
            if (path.isEmpty() || name.isEmpty())
                continue;
            if (!query.isEmpty() && !(wildcard ? pattern.match(name).hasMatch()
                                             : name.contains(query, Qt::CaseInsensitive)))
                continue;
            Entry entry;
            entry.name = name;
            entry.url = QUrl(item.value(QStringLiteral("url")).toString());
            if (!entry.url.isValid() || entry.url.isEmpty())
                entry.url = QUrl::fromLocalFile(path);
            entry.localPath = path;
            entry.size = item.value(QStringLiteral("size")).toLongLong();
            entry.modified = QDateTime::fromMSecsSinceEpoch(
                item.value(QStringLiteral("modifiedMs")).toLongLong());
            // The content index already holds the metadata used by media
            // categories. Avoid a filesystem stat and link probe for every
            // image/video whenever the user enters the category; on large
            // libraries those thousands of calls dominated page load time.
            entry.created = entry.modified;
            entry.suffix = item.value(QStringLiteral("suffix")).toString().toLower();
            if (!lightweightMedia) {
                entry.created = QFileInfo(path).birthTime();
                if (!entry.created.isValid()) entry.created = entry.modified;
                const LocalLinkMetadata linkMetadata = localLinkMetadata(entry.localPath, entry.suffix);
                entry.linkType = linkMetadata.type;
                entry.linkTarget = linkMetadata.target;
                entry.previewRevision = linkMetadata.previewRevision;
            }
            entry.hidden = name.startsWith(QLatin1Char('.')) || path.contains(QStringLiteral("/."));
            auto mimeIt = mimeBySuffix.constFind(entry.suffix);
            if (mimeIt == mimeBySuffix.constEnd()) {
                const QMimeType detected = mimeDb.mimeTypeForFile(name, QMimeDatabase::MatchExtension);
                mimeIt = mimeBySuffix.insert(entry.suffix, detected);
            }
            const QMimeType mime = mimeIt.value();
            entry.mimeType = mime.name();
            entry.systemIconName = mime.iconName();
            if (entry.systemIconName.isEmpty())
                entry.systemIconName = mime.genericIconName();
            if (entry.systemIconName.isEmpty())
                entry.systemIconName = QStringLiteral("text-x-generic");
            entries.push_back(std::move(entry));
        }
        // A busy filesystem or automount may wait inside the storage probe. Category
        // refreshes must keep this disk probe off the input/render thread too.
        QStorageInfo storage(QDir::homePath());
        if (storage.isValid() && storage.isReady()) {
            result.storageTotalBytes = qMax<qint64>(0, storage.bytesTotal());
            result.storageAvailableBytes = qMax<qint64>(0, storage.bytesAvailable());
        }
        return result;
    }));
}


void DirectoryModel::search(const QString &query, bool everywhere)
{
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty()) {
        clearSearch();
        return;
    }

    // Repeated Enter / debounce for the same live search must not restart a
    // potentially huge recursive scan. The active session already owns both
    // its cached results and its background worker.
    if (!m_activeSearchSessionId.isEmpty() &&
        m_searchQuery == trimmed && m_searchEverywhere == everywhere &&
        m_searchSessions.contains(m_activeSearchSessionId)) {
        return;
    }

    if (!m_activeSearchSessionId.isEmpty())
        cancelSearchSession(m_activeSearchSessionId, true);

    cancelKioListing();

    // An unfinished ordinary folder load must not replace streamed search
    // rows after Ctrl+F is started during navigation.
    ++m_localGeneration;

    // A recursive search must not inherit the normal directory watcher. A
    // directoryChanged burst would otherwise call refresh() and launch the
    // same recursive search again while the first one is still streaming.
    m_localRefreshTimer.stop();
    m_localMetadataTimer.stop();
    m_localWatchInstallTimer.stop();
    m_localWatchQueue.clear();
    m_pendingLocalMetadataPaths.clear();
    const QStringList watchedFiles = m_localWatcher.files();
    if (!watchedFiles.isEmpty())
        m_localWatcher.removePaths(watchedFiles);
    const QStringList watchedDirectories = m_localWatcher.directories();
    if (!watchedDirectories.isEmpty())
        m_localWatcher.removePaths(watchedDirectories);

    const auto session = createSearchSession(trimmed, everywhere);
    m_activeSearchSessionId = session->id;
    m_searchQuery = trimmed;
    m_searchEverywhere = everywhere;
    ++m_searchGeneration;
    emit searchChanged();
    loadSearch(false);
}

QSharedPointer<DirectoryModel::SearchSession> DirectoryModel::createSearchSession(const QString &query, bool everywhere)
{
    auto session = QSharedPointer<SearchSession>::create();
    session->id = QStringLiteral("search-%1").arg(++m_searchSessionCounter);
    session->originLocation = m_location;
    session->query = query;
    session->everywhere = everywhere;
    session->complete = false;
    session->canceled = std::make_shared<std::atomic_bool>(false);
    session->stream = std::make_shared<SearchStream>();
    m_searchSessions.insert(session->id, session);
    return session;
}

void DirectoryModel::cancelSearchSession(const QString &sessionId, bool remove)
{
    if (sessionId.isEmpty())
        return;
    const auto session = m_searchSessions.value(sessionId);
    if (!session)
        return;
    if (session->canceled)
        session->canceled->store(true, std::memory_order_relaxed);
    if (session->stream) {
        QMutexLocker lock(&session->stream->mutex);
        session->stream->spaceAvailable.wakeAll();
    }
    const QPointer<KIO::ListJob> sessionJob = session->job;
    if (sessionJob) {
        sessionJob->kill();
        session->job.clear();
    }
    if (m_searchJob && m_searchJob == sessionJob)
        m_searchJob.clear();
    if (remove)
        m_searchSessions.remove(sessionId);
}

QString DirectoryModel::detachSearchSession()
{
    const QString sessionId = m_activeSearchSessionId;
    if (sessionId.isEmpty())
        return QString();

    // Do not cancel the worker. A tab switch merely detaches the visible model
    // from this session; the scan continues filling its C++ cache in the
    // background and can be reattached instantly when the tab is revisited.
    m_activeSearchSessionId.clear();
    m_searchQuery.clear();
    m_searchEverywhere = false;
    m_searchJob.clear();
    setLoading(false);
    emit searchChanged();
    return sessionId;
}

bool DirectoryModel::restoreSearchSession(const QString &sessionId)
{
    const auto session = m_searchSessions.value(sessionId);
    if (!session || (session->canceled && session->canceled->load(std::memory_order_relaxed)))
        return false;

    cancelKioListing();

    // Switching tabs may already have started a normal local-directory worker
    // for this location. Invalidate it before attaching cached search rows so
    // it cannot finish later and overwrite the search with normal contents.
    ++m_localGeneration;
    m_localRefreshTimer.stop();
    m_localMetadataTimer.stop();
    m_localWatchInstallTimer.stop();
    m_localWatchQueue.clear();
    m_pendingLocalMetadataPaths.clear();
    const QStringList watchedFiles = m_localWatcher.files();
    if (!watchedFiles.isEmpty())
        m_localWatcher.removePaths(watchedFiles);
    const QStringList watchedDirectories = m_localWatcher.directories();
    if (!watchedDirectories.isEmpty())
        m_localWatcher.removePaths(watchedDirectories);

    // Session restoration is also navigation restoration. Set the original
    // location directly without invoking setLocation(), because setLocation()
    // would start a normal directory load that competes with the cached search.
    if (m_location != session->originLocation) {
        const bool wasCategory = m_location.scheme() == QStringLiteral("category");
        const bool isCategory = session->originLocation.scheme() == QStringLiteral("category");
        m_location = session->originLocation;
        if (wasCategory != isCategory) {
            const QString nextSortMode = isCategory ? m_categorySortMode : m_normalSortMode;
            const bool nextSortAscending = isCategory ? m_categorySortAscending : m_normalSortAscending;
            const bool sortModeNeedsUpdate = m_sortMode != nextSortMode;
            const bool sortDirectionChanged = m_sortAscending != nextSortAscending;
            m_sortMode = nextSortMode;
            m_sortAscending = nextSortAscending;
            if (sortModeNeedsUpdate)
                emit sortModeChanged();
            if (sortDirectionChanged)
                emit sortAscendingChanged();
        }
        emit locationChanged();
        updateStorageInfo();
    }

    m_activeSearchSessionId = sessionId;
    m_searchQuery = session->query;
    m_searchEverywhere = session->everywhere;
    m_searchJob = session->job;
    ++m_searchGeneration;

    beginResetModel();
    m_items = session->items;
    endResetModel();
    emit countChanged();
    setErrorString(QString());
    setLoading(!session->complete);
    emit searchChanged();
    return true;
}

void DirectoryModel::releaseSearchSession(const QString &sessionId)
{
    const bool wasActive = sessionId == m_activeSearchSessionId;
    cancelSearchSession(sessionId, true);
    if (wasActive) {
        m_activeSearchSessionId.clear();
        m_searchQuery.clear();
        m_searchEverywhere = false;
        m_searchJob.clear();
        setLoading(false);
        emit searchChanged();
    }
}

void DirectoryModel::cancelSearchForInput()
{
    // The user is still editing the query. Stop the currently streaming
    // session immediately so a broad prefix (for example "a") cannot keep
    // flooding the GUI while the next characters are being typed. Unlike
    // clearSearch(), do not repopulate the underlying directory here; the
    // search field stays open and the next debounced query will own the view.
    if (m_activeSearchSessionId.isEmpty() && m_searchQuery.isEmpty())
        return;

    const QString sessionId = m_activeSearchSessionId;
    m_activeSearchSessionId.clear();
    if (!sessionId.isEmpty())
        cancelSearchSession(sessionId, true);
    m_searchJob.clear();
    m_searchQuery.clear();
    m_searchEverywhere = false;
    ++m_searchGeneration;
    setLoading(false);
    setErrorString(QString());

    if (!m_items.isEmpty()) {
        beginResetModel();
        m_items.clear();
        endResetModel();
        emit countChanged();
    }
    emit searchChanged();
}

void DirectoryModel::clearSearch()
{
    if (m_activeSearchSessionId.isEmpty() && m_searchQuery.isEmpty())
        return;

    const QString sessionId = m_activeSearchSessionId;
    m_activeSearchSessionId.clear();
    if (!sessionId.isEmpty())
        cancelSearchSession(sessionId, true);
    m_searchJob.clear();
    m_searchQuery.clear();
    m_searchEverywhere = false;
    ++m_searchGeneration;
    emit searchChanged();
    refreshInternal(false);
}

void DirectoryModel::queueSearchBatch(const QString &sessionId, QVector<Entry> batch)
{
    const auto session = m_searchSessions.value(sessionId);
    if (!session || batch.isEmpty() || session->canceled->load(std::memory_order_relaxed))
        return;
    int pending = 0;
    {
        QMutexLocker lock(&session->stream->mutex);
        session->stream->pendingCount += batch.size();
        pending = session->stream->pendingCount;
        session->stream->batches.enqueue(std::move(batch));
    }
    // Local producers wait on the mailbox; remote KIO producers can suspend
    // their job instead of accumulating an unbounded result backlog.
    if (pending >= 2048 && session->job && !session->job->isSuspended())
        session->job->suspend();
    if (!m_searchResultTimer.isActive())
        m_searchResultTimer.start();
}

void DirectoryModel::drainSearchResults()
{
    QElapsedTimer budget;
    budget.start();
    bool scanning = false;
    auto sessions = m_searchSessions.values();
    // The visible tab gets a turn before detached sessions fill their caches.
    std::stable_sort(sessions.begin(), sessions.end(), [this](const auto &a, const auto &b) {
        return a->id == m_activeSearchSessionId && b->id != m_activeSearchSessionId;
    });
    for (const auto &session : sessions) {
        if (session->complete || session->canceled->load(std::memory_order_relaxed))
            continue;
        scanning = true;
        if (budget.elapsed() >= 4)
            continue;
        const auto stream = session->stream;
        QVector<Entry> batch;
        const int maximum = session->id == m_activeSearchSessionId ? 256 : 1024;
        batch.reserve(maximum);
        bool finished = false;
        int pending = 0;
        QString error;
        {
            QMutexLocker lock(&stream->mutex);
            while (!stream->batches.isEmpty() && batch.size() < maximum) {
                auto &front = stream->batches.head();
                const int count = qMin(maximum - int(batch.size()), int(front.size()) - stream->frontOffset);
                for (int i = 0; i < count; ++i)
                    batch.push_back(std::move(front[stream->frontOffset++]));
                stream->pendingCount -= count;
                if (stream->frontOffset == front.size()) {
                    stream->batches.dequeue();
                    stream->frontOffset = 0;
                }
            }
            pending = stream->pendingCount;
            finished = stream->finished && pending == 0;
            error = stream->error;
            stream->spaceAvailable.wakeAll();
        }
        appendSearchBatch(session->id, std::move(batch));
        // Model signals may have closed or replaced this search while rows
        // were being inserted. Never resume a canceled remote job afterwards.
        if (session->canceled->load(std::memory_order_relaxed))
            continue;
        if (pending < 1024 && session->job && session->job->isSuspended())
            session->job->resume();
        if (finished)
            finishSearchSession(session->id, error);
    }
    if (!scanning)
        m_searchResultTimer.stop();
}

void DirectoryModel::appendSearchBatch(const QString &sessionId, QVector<Entry> batch)
{
    const auto session = m_searchSessions.value(sessionId);
    if (!session || batch.isEmpty() ||
        (session->canceled && session->canceled->load(std::memory_order_relaxed)))
        return;

    QVector<Entry> accepted;
    accepted.reserve(batch.size());
    for (Entry &entry : batch) {
        const QString key = entry.url.toString(QUrl::FullyEncoded);
        if (key.isEmpty() || session->seenUrls.contains(key))
            continue;
        session->seenUrls.insert(key);
        session->items.push_back(entry);
        accepted.push_back(std::move(entry));
    }

    if (accepted.isEmpty() || m_activeSearchSessionId != sessionId)
        return;

    const int first = m_items.size();
    beginInsertRows(QModelIndex(), first, first + accepted.size() - 1);
    for (Entry &entry : accepted)
        m_items.push_back(std::move(entry));
    endInsertRows();
    emit countChanged();
}

void DirectoryModel::finishSearchSession(const QString &sessionId, const QString &error)
{
    const auto session = m_searchSessions.value(sessionId);
    if (!session || (session->canceled && session->canceled->load(std::memory_order_relaxed)))
        return;

    session->complete = true;
    session->job.clear();

    if (m_activeSearchSessionId != sessionId)
        return;

    m_searchJob.clear();
    if (!error.isEmpty())
        setErrorString(error);

    // Do not reshuffle thousands of rows when the final hit arrives. Search
    // results stay in discovery order while they stream; an explicit sort
    // action can reorder them later. The session cache is already populated by
    // appendSearchBatch(), so avoid copying the full result set again here.
    setLoading(false);
    emit refreshCompleted();
}

void DirectoryModel::loadSearch(bool incremental)
{
    const QString sessionId = m_activeSearchSessionId;
    const auto session = m_searchSessions.value(sessionId);
    if (!session || m_searchQuery.isEmpty())
        return;

    setErrorString(QString());
    const QUrl base = session->everywhere ? QUrl::fromLocalFile(QDir::homePath()) : session->originLocation;

    if (!incremental) {
        session->items.clear();
        session->seenUrls.clear();
        session->complete = false;
        setLoading(true);
        beginResetModel();
        m_items.clear();
        endResetModel();
        emit countChanged();
    }

    if (base.scheme() == QStringLiteral("gdrive") || base.scheme() == QStringLiteral("onedrive")) {
        finishSearchSession(sessionId,
            QStringLiteral("Built-in recursive search is currently available for local and KIO locations. Use the provider search in a later update."));
        return;
    }

    QString wildcard = session->query.trimmed();
    const bool explicitWildcard = wildcard.contains(QLatin1Char('*')) || wildcard.contains(QLatin1Char('?'));
    if (!explicitWildcard) {
        if (wildcard.startsWith(QLatin1Char('.')))
            wildcard.prepend(QLatin1Char('*'));
        else
            wildcard = QLatin1Char('*') + wildcard + QLatin1Char('*');
    }
    const QRegularExpression matcher(
        QRegularExpression::wildcardToRegularExpression(wildcard),
        QRegularExpression::CaseInsensitiveOption);

    // Background filesystem refreshes preserve the already-visible search and
    // reconcile once. Initial searches stream small batches as they are found.
    if (incremental && base.isLocalFile()) {
        const QString basePath = QDir::cleanPath(base.toLocalFile());
        const bool includeHidden = m_showHidden;
        auto *watcher = new QFutureWatcher<QVector<Entry>>(this);
        connect(watcher, &QFutureWatcher<QVector<Entry>>::finished, this,
                [this, watcher, sessionId]() {
            QVector<Entry> results = watcher->result();
            watcher->deleteLater();
            const auto liveSession = m_searchSessions.value(sessionId);
            if (!liveSession || m_activeSearchSessionId != sessionId)
                return;
            sortEntriesVector(results);
            liveSession->items = results;
            liveSession->seenUrls.clear();
            for (const Entry &entry : std::as_const(results))
                liveSession->seenUrls.insert(entry.url.toString(QUrl::FullyEncoded));
            reconcileLocalEntries(std::move(results));
            emit refreshCompleted();
        });
        watcher->setFuture(QtConcurrent::run([basePath, includeHidden, matcher]() {
            QVector<Entry> results;
            if (!QFileInfo::exists(basePath))
                return results;
            QDir::Filters filters = QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System;
            if (includeHidden)
                filters |= QDir::Hidden;
            QMimeDatabase mimeDb;
            QDirIterator it(basePath, filters, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                it.next();
                const QFileInfo info = it.fileInfo();
                const QString name = info.fileName();
                if (name.isEmpty() || !matcher.match(name).hasMatch())
                    continue;
                Entry e;
                e.name = name;
                e.localPath = info.absoluteFilePath();
                e.url = QUrl::fromLocalFile(e.localPath);
                e.isDir = info.isDir();
                e.size = e.isDir ? 0 : info.size();
                e.modified = info.lastModified();
                e.created = info.birthTime();
                if (!e.created.isValid()) e.created = e.modified;
                e.suffix = info.suffix().toLower();
                e.hidden = info.isHidden();
                const LocalLinkMetadata linkMetadata = localLinkMetadata(e.localPath, e.suffix);
                e.linkType = linkMetadata.type;
                e.linkTarget = linkMetadata.target;
                e.previewRevision = linkMetadata.previewRevision;
                const QMimeType mime = e.isDir
                    ? mimeDb.mimeTypeForName(QStringLiteral("inode/directory"))
                    : mimeDb.mimeTypeForFile(name, QMimeDatabase::MatchExtension);
                e.mimeType = mime.name();
                e.systemIconName = e.isDir ? QStringLiteral("folder") : mime.iconName();
                if (e.systemIconName.isEmpty()) e.systemIconName = mime.genericIconName();
                if (e.systemIconName.isEmpty()) e.systemIconName = e.isDir ? QStringLiteral("folder") : QStringLiteral("text-x-generic");
                results.push_back(std::move(e));
            }
            return results;
        }));
        return;
    }

    if (base.isLocalFile()) {
        const QString basePath = QDir::cleanPath(base.toLocalFile());
        const bool includeHidden = m_showHidden;
        const auto cancelFlag = session->canceled;
        const auto stream = session->stream;
        if (!m_searchResultTimer.isActive())
            m_searchResultTimer.start();

        QThreadPool::globalInstance()->start([stream, basePath, includeHidden, matcher, cancelFlag]() mutable {
            auto finish = [&]() {
                QMutexLocker lock(&stream->mutex);
                stream->finished = true;
            };
            if (!QFileInfo::exists(basePath)) {
                finish();
                return;
            }

            QDir::Filters filters = QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System;
            if (includeHidden)
                filters |= QDir::Hidden;

            QMimeDatabase mimeDb;
            QDirIterator it(basePath, filters, QDirIterator::Subdirectories);
            QVector<Entry> batch;
            batch.reserve(128);
            QElapsedTimer batchTimer;
            batchTimer.start();

            auto flush = [&]() -> bool {
                if (batch.isEmpty())
                    return true;
                QVector<Entry> outgoing = std::move(batch);
                batch.clear();
                batch.reserve(128);
                QMutexLocker lock(&stream->mutex);
                while (stream->pendingCount + outgoing.size() > 2048
                       && !cancelFlag->load(std::memory_order_relaxed))
                    stream->spaceAvailable.wait(&stream->mutex, 50);
                if (cancelFlag->load(std::memory_order_relaxed))
                    return false;
                stream->pendingCount += outgoing.size();
                stream->batches.enqueue(std::move(outgoing));
                return true;
            };

            while (it.hasNext()) {
                if (cancelFlag && cancelFlag->load(std::memory_order_relaxed))
                    return;
                it.next();
                const QFileInfo info = it.fileInfo();
                const QString name = info.fileName();

                // Flush sparse matches on time as well as dense matches by
                // size. This gives the UI early results without flooding the
                // event loop with hundreds of tiny queued insert operations.
                if (!batch.isEmpty() && batchTimer.elapsed() >= 90) {
                    if (!flush()) return;
                    batchTimer.restart();
                }

                if (name.isEmpty() || !matcher.match(name).hasMatch())
                    continue;

                Entry e;
                e.name = name;
                e.localPath = info.absoluteFilePath();
                e.url = QUrl::fromLocalFile(e.localPath);
                e.isDir = info.isDir();
                e.size = e.isDir ? 0 : info.size();
                e.modified = info.lastModified();
                e.created = info.birthTime();
                if (!e.created.isValid()) e.created = e.modified;
                e.suffix = info.suffix().toLower();
                e.hidden = info.isHidden();
                const LocalLinkMetadata linkMetadata = localLinkMetadata(e.localPath, e.suffix);
                e.linkType = linkMetadata.type;
                e.linkTarget = linkMetadata.target;
                e.previewRevision = linkMetadata.previewRevision;
                const QMimeType mime = e.isDir
                    ? mimeDb.mimeTypeForName(QStringLiteral("inode/directory"))
                    : mimeDb.mimeTypeForFile(name, QMimeDatabase::MatchExtension);
                e.mimeType = mime.name();
                e.systemIconName = e.isDir ? QStringLiteral("folder") : mime.iconName();
                if (e.systemIconName.isEmpty()) e.systemIconName = mime.genericIconName();
                if (e.systemIconName.isEmpty()) e.systemIconName = e.isDir ? QStringLiteral("folder") : QStringLiteral("text-x-generic");
                batch.push_back(std::move(e));
                const qint64 elapsed = batchTimer.elapsed();
                if (batch.size() >= 256 || (batch.size() >= 128 && elapsed >= 35)) {
                    if (!flush()) return;
                    batchTimer.restart();
                }
            }
            if (!flush()) return;
            if (cancelFlag && cancelFlag->load(std::memory_order_relaxed))
                return;
            finish();
        }, -1);
        return;
    }

    const auto flags = m_showHidden ? KIO::ListJob::ListFlag::IncludeHidden : KIO::ListJob::ListFlags{};
    auto *job = KIO::listRecursive(base, KIO::HideProgressInfo, flags);
    session->job = job;
    m_searchJob = job;

    connect(job, &KIO::ListJob::entries, this,
            [this, sessionId, base, matcher](KIO::Job *sourceJob, const KIO::UDSEntryList &entries) {
        const auto liveSession = m_searchSessions.value(sessionId);
        if (!liveSession || (liveSession->canceled && liveSession->canceled->load(std::memory_order_relaxed)))
            return;

        QUrl entryBase = base;
        if (auto *listJob = qobject_cast<KIO::ListJob *>(sourceJob))
            entryBase = listJob->url();

        QVector<Entry> incoming;
        incoming.reserve(entries.size());
        QMimeDatabase mimeDb;
        for (const KIO::UDSEntry &uds : entries) {
            const QString name = uds.stringValue(KIO::UDSEntry::UDS_NAME);
            if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral(".."))
                continue;
            const bool hidden = uds.numberValue(KIO::UDSEntry::UDS_HIDDEN, name.startsWith('.') ? 1 : 0) != 0;
            if (!m_showHidden && hidden)
                continue;
            if (!matcher.match(name).hasMatch())
                continue;

            Entry e;
            e.name = uds.stringValue(KIO::UDSEntry::UDS_DISPLAY_NAME);
            if (e.name.isEmpty()) e.name = name;
            const QString explicitUrl = uds.stringValue(KIO::UDSEntry::UDS_URL);
            const QString localPath = uds.stringValue(KIO::UDSEntry::UDS_LOCAL_PATH);
            if (!explicitUrl.isEmpty()) e.url = QUrl(explicitUrl);
            else if (!localPath.isEmpty()) e.url = QUrl::fromLocalFile(localPath);
            else {
                QUrl itemUrl = entryBase;
                QString path = itemUrl.path();
                if (!path.endsWith(QLatin1Char('/'))) path += QLatin1Char('/');
                path += name;
                itemUrl.setPath(path);
                e.url = itemUrl;
            }
            e.localPath = localPath;
            if (e.localPath.isEmpty() && e.url.isLocalFile()) e.localPath = e.url.toLocalFile();
            e.isDir = uds.isDir();
            e.size = uds.numberValue(KIO::UDSEntry::UDS_SIZE, 0);
            e.modified = QDateTime::fromSecsSinceEpoch(uds.numberValue(KIO::UDSEntry::UDS_MODIFICATION_TIME, 0));
            if (!e.localPath.isEmpty()) e.created = QFileInfo(e.localPath).birthTime();
            if (!e.created.isValid()) e.created = e.modified;
            e.suffix = QFileInfo(name).suffix().toLower();
            if (!e.localPath.isEmpty()) {
                const LocalLinkMetadata linkMetadata = localLinkMetadata(e.localPath, e.suffix);
                e.linkType = linkMetadata.type;
                e.linkTarget = linkMetadata.target;
                e.previewRevision = linkMetadata.previewRevision;
            } else {
                const QString linkDest = uds.stringValue(KIO::UDSEntry::UDS_LINK_DEST);
                if (!linkDest.isEmpty()) {
                    e.linkType = QStringLiteral("symlink");
                    e.linkTarget = linkDest;
                }
            }
            e.hidden = hidden;
            e.mimeType = uds.stringValue(KIO::UDSEntry::UDS_MIME_TYPE);
            e.systemIconName = uds.stringValue(KIO::UDSEntry::UDS_ICON_NAME);
            if (e.mimeType.isEmpty()) {
                const QMimeType mime = e.isDir
                    ? mimeDb.mimeTypeForName(QStringLiteral("inode/directory"))
                    : mimeDb.mimeTypeForFile(name, QMimeDatabase::MatchExtension);
                e.mimeType = mime.name();
                if (e.systemIconName.isEmpty()) e.systemIconName = e.isDir ? QStringLiteral("folder") : mime.iconName();
                if (e.systemIconName.isEmpty()) e.systemIconName = mime.genericIconName();
            }
            if (e.systemIconName.isEmpty()) e.systemIconName = e.isDir ? QStringLiteral("folder") : QStringLiteral("text-x-generic");
            incoming.push_back(std::move(e));
        }
        queueSearchBatch(sessionId, std::move(incoming));
    });

    connect(job, &KJob::result, this, [this, job, sessionId]() {
        const auto session = m_searchSessions.value(sessionId);
        if (!session)
            return;
        if (session->job == job)
            session->job.clear();
        {
            QMutexLocker lock(&session->stream->mutex);
            session->stream->finished = true;
            session->stream->error = job->error() ? job->errorString() : QString();
        }
        if (!m_searchResultTimer.isActive())
            m_searchResultTimer.start();
    });
}


void DirectoryModel::loadLocal(bool incremental)
{
    const int generation = ++m_localGeneration;
    const QString requestedPath = QDir::cleanPath(m_location.toLocalFile());
    const bool showHidden = m_showHidden;
    const bool collectFolderDetails = !m_heavyIoBusy;
    const bool collectFolderPreviews = m_folderPreviewsEnabled && collectFolderDetails;
    if (!collectFolderDetails)
        m_refreshDeferredByHeavyIo = true;

    // Navigation still paints a clean loading state. Background refreshes from
    // QFileSystemWatcher deliberately keep the existing model alive so a
    // single new/removed file does not tear down every delegate in the view.
    if (!incremental) {
        setLoading(true);
        beginResetModel();
        m_items.clear();
        endResetModel();
        emit countChanged();
    }

    auto *watcher = new QFutureWatcher<LocalLoadResult>(this);
    connect(watcher, &QFutureWatcher<LocalLoadResult>::finished, this,
            [this, watcher, generation, incremental]() {
        LocalLoadResult result = watcher->result();
        watcher->deleteLater();
        if (generation != m_localGeneration)
            return;

        if (!result.error.isEmpty()) {
            setErrorString(result.error);
            if (!incremental)
                setLoading(false);
            return;
        }

        watchLocalDirectory(result.absolutePath);
        watchLocalFiles(result.items);
        sortEntriesVector(result.items);

        if (incremental)
            reconcileLocalEntries(std::move(result.items));
        else {
            beginResetModel();
            m_items = std::move(result.items);
            endResetModel();
            emit countChanged();
        }

        const bool storageChanged = m_storageTotalBytes != result.storageTotalBytes
            || m_storageAvailableBytes != result.storageAvailableBytes;
        m_storageTotalBytes = result.storageTotalBytes;
        m_storageAvailableBytes = result.storageAvailableBytes;
        if (storageChanged)
            emit storageInfoChanged();

        scheduleVideoMetadata();
        if (!incremental)
            setLoading(false);
        emit refreshCompleted();
    });

    watcher->setFuture(QtConcurrent::run([requestedPath, showHidden, collectFolderDetails, collectFolderPreviews]() -> LocalLoadResult {
        LocalLoadResult result;
        QDir dir(requestedPath);
        if (!dir.exists()) {
            result.error = QStringLiteral("Folder does not exist.");
            return result;
        }
        result.absolutePath = dir.absolutePath();

        // readdir can return names without directory search permission, but
        // stat cannot read any child metadata. QFileInfo then reports size 0,
        // which must not be presented as if the files had become empty.
        // Check effective access (including ACLs) rather than just mode bits.
        if (::faccessat(AT_FDCWD, QFile::encodeName(result.absolutePath).constData(),
                        R_OK | X_OK, AT_EACCESS) != 0) {
            result.error = QStringLiteral("Cannot read this folder's contents. Check its read and folder access permissions.");
            return result;
        }

        QDir::Filters filters = QDir::AllEntries | QDir::NoDotAndDotDot;
        if (showHidden)
            filters |= QDir::Hidden;

        QMimeDatabase mimeDb;
        const QFileInfoList list = dir.entryInfoList(filters, QDir::NoSort);
        result.items.reserve(list.size());
        for (const QFileInfo &fi : list) {
            Entry e;
            e.name = fi.fileName();
            e.url = QUrl::fromLocalFile(fi.absoluteFilePath());
            e.localPath = fi.absoluteFilePath();
            e.isDir = fi.isDir();
            e.size = fi.size();
            e.modified = fi.lastModified();
            e.created = fi.birthTime();
            if (!e.created.isValid()) e.created = e.modified;
            e.suffix = fi.suffix().toLower();
            e.hidden = fi.isHidden();
            const LocalLinkMetadata linkMetadata = localLinkMetadata(e.localPath, e.suffix);
            e.linkType = linkMetadata.type;
            e.linkTarget = linkMetadata.target;
            e.previewRevision = linkMetadata.previewRevision;
            const QMimeType mime = e.isDir
                ? mimeDb.mimeTypeForName(QStringLiteral("inode/directory"))
                : mimeDb.mimeTypeForFile(fi, QMimeDatabase::MatchExtension);
            e.mimeType = mime.name();
            if (e.isDir) {
                if (collectFolderDetails) {
                    QDir::Filters childFilters = QDir::AllEntries | QDir::NoDotAndDotDot;
                    if (showHidden)
                        childFilters |= QDir::Hidden;

                    // Counting names does not require statting/decoding every
                    // child. The count is independent of the preview toggle.
                    if (!collectFolderPreviews) {
                        e.childCount = QDir(fi.absoluteFilePath()).entryList(childFilters, QDir::NoSort).size();
                    } else {
                        QStringList previewBuckets[4];
                        QDirIterator childIt(fi.absoluteFilePath(), childFilters, QDirIterator::NoIteratorFlags);
                        int childCount = 0;
                        while (childIt.hasNext()) {
                            childIt.next();
                            ++childCount;
                            const QFileInfo childInfo = childIt.fileInfo();
                            if (!childInfo.isFile())
                                continue;
                            const int priority = folderPreviewPriority(childInfo.suffix().toLower());
                            if (priority >= 0 && previewBuckets[priority].size() < 4)
                                previewBuckets[priority].append(childInfo.absoluteFilePath());
                        }
                        e.childCount = childCount;
                        for (int priority = 0; priority < 4 && e.folderPreviewPaths.size() < 4; ++priority) {
                            for (const QString &path : std::as_const(previewBuckets[priority])) {
                                e.folderPreviewPaths.append(path);
                                if (e.folderPreviewPaths.size() == 4)
                                    break;
                            }
                        }
                    }
                }

                const QString metadataPath = QDir(fi.absoluteFilePath()).filePath(QStringLiteral(".directory"));
                if (QFileInfo::exists(metadataPath)) {
                    QSettings metadata(metadataPath, QSettings::IniFormat);
                    metadata.beginGroup(QStringLiteral("Desktop Entry"));
                    e.systemIconName = metadata.value(QStringLiteral("Icon")).toString().trimmed();
                    metadata.endGroup();
                }
                if (e.systemIconName.isEmpty())
                    e.systemIconName = QStringLiteral("folder");
            } else {
                e.systemIconName = mime.iconName();
            }
            if (e.systemIconName.isEmpty())
                e.systemIconName = mime.genericIconName();
            if (e.systemIconName.isEmpty())
                e.systemIconName = e.isDir ? QStringLiteral("folder") : QStringLiteral("text-x-generic");
            result.items.push_back(e);
        }

        QStorageInfo storage(result.absolutePath);
        storage.refresh();
        if (storage.isValid() && storage.isReady()) {
            result.storageTotalBytes = qMax<qint64>(0, storage.bytesTotal());
            result.storageAvailableBytes = qMax<qint64>(0, storage.bytesAvailable());
        }
        return result;
    }));
}

void DirectoryModel::sortEntriesVector(QVector<Entry> &entries) const
{
    const auto mode = m_sortMode;
    const bool ascending = m_sortAscending;
    const auto hiddenPlacement = m_hiddenPlacement;
    std::stable_sort(entries.begin(), entries.end(), [mode, ascending, hiddenPlacement](const Entry &a, const Entry &b) {
        if (hiddenPlacement != QStringLiteral("normal") && a.hidden != b.hidden) {
            if (hiddenPlacement == QStringLiteral("top"))
                return a.hidden;
            return !a.hidden;
        }

        if (a.isDir != b.isDir)
            return a.isDir;

        int result = 0;
        if (mode == QStringLiteral("date")) {
            result = a.modified < b.modified ? -1 : (a.modified > b.modified ? 1 : 0);
        } else if (mode == QStringLiteral("created")) {
            result = a.created < b.created ? -1 : (a.created > b.created ? 1 : 0);
        } else if (mode == QStringLiteral("size")) {
            result = a.size < b.size ? -1 : (a.size > b.size ? 1 : 0);
        } else if (mode == QStringLiteral("type")) {
            result = QString::localeAwareCompare(a.mimeType, b.mimeType);
            if (result == 0)
                result = QString::localeAwareCompare(a.name, b.name);
        } else {
            result = QString::localeAwareCompare(a.name, b.name);
        }
        return ascending ? result < 0 : result > 0;
    });
}

void DirectoryModel::reconcileLocalEntries(QVector<Entry> target)
{
    const int oldCount = m_items.size();
    bool structureChanged = false;

    // Remove paths that disappeared. Work backwards so existing indices remain
    // valid and visible delegates not affected by the removal stay alive.
    QSet<QString> targetUrls;
    targetUrls.reserve(target.size());
    for (const Entry &entry : std::as_const(target))
        targetUrls.insert(entry.url.toString());

    for (int row = m_items.size() - 1; row >= 0; --row) {
        if (targetUrls.contains(m_items.at(row).url.toString()))
            continue;
        beginRemoveRows(QModelIndex(), row, row);
        m_items.removeAt(row);
        endRemoveRows();
        structureChanged = true;
    }

    // Transform the surviving sequence into the freshly sorted directory
    // listing. New files are inserted in-place; surviving rows are moved only
    // when their actual sort position changed.
    for (int row = 0; row < target.size(); ++row) {
        Entry desired = target.at(row);
        int currentRow = -1;
        for (int probe = row; probe < m_items.size(); ++probe) {
            if (m_items.at(probe).url == desired.url) {
                currentRow = probe;
                break;
            }
        }

        if (currentRow < 0) {
            beginInsertRows(QModelIndex(), row, row);
            m_items.insert(row, desired);
            endInsertRows();
            structureChanged = true;
            continue;
        }

        if (currentRow != row) {
            beginMoveRows(QModelIndex(), currentRow, currentRow, QModelIndex(), row);
            m_items.move(currentRow, row);
            endMoveRows();
        }

        Entry &current = m_items[row];
        const QString oldPreview = current.previewRevision.isEmpty()
            ? QString::number(current.modified.toMSecsSinceEpoch()) + '-' + QString::number(current.size)
            : current.previewRevision;
        if (isVideoEntry(desired.mimeType, desired.suffix)) {
            desired.previewRevision = oldPreview;
            if (current.size != desired.size || current.modified != desired.modified)
                schedulePreviewSettlement(desired.localPath);
        }
        // Keep already-probed video metadata across watcher refreshes.
        if (desired.videoInfo.isEmpty())
            desired.videoInfo = current.videoInfo;

        QList<int> changedRoles;
        if (current.name != desired.name) changedRoles << NameRole;
        if (current.url != desired.url) changedRoles << UrlRole;
        if (current.localPath != desired.localPath) changedRoles << LocalPathRole;
        if (current.isDir != desired.isDir) changedRoles << IsDirRole;
        if (current.size != desired.size) changedRoles << SizeRole;
        if (current.modified != desired.modified) changedRoles << ModifiedRole;
        if (!isVideoEntry(desired.mimeType, desired.suffix)
                && (current.size != desired.size || current.modified != desired.modified
                    || current.previewRevision != desired.previewRevision))
            changedRoles << PreviewRevisionRole;
        if (current.created != desired.created) changedRoles << CreatedRole;
        if (current.suffix != desired.suffix) changedRoles << SuffixRole;
        if (current.hidden != desired.hidden) changedRoles << HiddenRole;
        if (current.mimeType != desired.mimeType) changedRoles << MimeTypeRole;
        if (current.systemIconName != desired.systemIconName) changedRoles << SystemIconNameRole;
        if (current.videoInfo != desired.videoInfo) changedRoles << VideoInfoRole;
        if (current.childCount != desired.childCount) changedRoles << ChildCountRole;
        if (current.folderPreviewPaths != desired.folderPreviewPaths) changedRoles << FolderPreviewPathsRole;
        if (current.linkType != desired.linkType) changedRoles << LinkTypeRole;
        if (current.linkTarget != desired.linkTarget) changedRoles << LinkTargetRole;

        if (!changedRoles.isEmpty()) {
            current = std::move(desired);
            const QModelIndex changed = index(row, 0);
            emit dataChanged(changed, changed, changedRoles);
        }
    }

    if (m_items.size() > target.size()) {
        const int first = target.size();
        const int last = m_items.size() - 1;
        beginRemoveRows(QModelIndex(), first, last);
        m_items.remove(first, last - first + 1);
        endRemoveRows();
        structureChanged = true;
    }

    if (structureChanged || m_items.size() != oldCount)
        emit countChanged();
}

void DirectoryModel::watchLocalDirectory(const QString &path)
{
    const QString cleanPath = QDir(path).absolutePath();
    const QStringList watched = m_localWatcher.directories();
    for (const QString &existing : watched) {
        if (existing != cleanPath)
            m_localWatcher.removePath(existing);
    }
    if (!cleanPath.isEmpty() && QFileInfo(cleanPath).isDir()
            && !m_localWatcher.directories().contains(cleanPath)) {
        m_localWatcher.addPath(cleanPath);
    }
}


void DirectoryModel::watchLocalFiles(const QVector<Entry> &entries)
{
    m_localWatchInstallTimer.stop();
    m_localWatchQueue.clear();
    // Watching every child in a very large directory is far more expensive
    // than watching the directory itself. In 10k+ item folders it also causes
    // a storm of metadata callbacks while KIO is copying files. Keep per-file
    // growth tracking only for reasonably sized folders.
    constexpr int kMaxIndividuallyWatchedFiles = 2048;
    if (m_heavyIoBusy || entries.size() > kMaxIndividuallyWatchedFiles) {
        const QStringList watched = m_localWatcher.files();
        if (!watched.isEmpty())
            m_localWatcher.removePaths(watched);
        return;
    }

    QSet<QString> wanted;
    wanted.reserve(entries.size());
    for (const Entry &entry : entries) {
        if (entry.isDir || entry.localPath.isEmpty())
            continue;
        wanted.insert(QDir::cleanPath(entry.localPath));
    }

    const QStringList watched = m_localWatcher.files();
    for (const QString &existing : watched) {
        if (!wanted.contains(QDir::cleanPath(existing)))
            m_localWatcher.removePath(existing);
    }

    QStringList missing;
    missing.reserve(wanted.size());
    const QStringList watchedNow = m_localWatcher.files();
    QSet<QString> currentlyWatched;
    currentlyWatched.reserve(watchedNow.size());
    for (const QString &path : watchedNow)
        currentlyWatched.insert(QDir::cleanPath(path));
    for (const QString &path : std::as_const(wanted)) {
        if (!currentlyWatched.contains(path))
            missing.append(path);
    }
    // The worker already classified these files. Do not stat every path and
    // register thousands of native watches on the GUI thread before painting.
    m_localWatchQueue = std::move(missing);
    if (!m_localWatchQueue.isEmpty())
        m_localWatchInstallTimer.start();
}

void DirectoryModel::installLocalFileWatchBatch()
{
    if (m_heavyIoBusy || !m_location.isLocalFile()) {
        m_localWatchQueue.clear();
        return;
    }
    constexpr int batchSize = 8;
    const int count = qMin(batchSize, m_localWatchQueue.size());
    if (count == 0)
        return;
    const QStringList batch = m_localWatchQueue.mid(0, count);
    m_localWatchQueue.remove(0, count);
    m_localWatcher.addPaths(batch);
    if (!m_localWatchQueue.isEmpty())
        m_localWatchInstallTimer.start();
}

void DirectoryModel::refreshLocalFileMetadata()
{
    if (!m_location.isLocalFile()) {
        m_pendingLocalMetadataPaths.clear();
        return;
    }

    const QSet<QString> changedPaths = m_pendingLocalMetadataPaths;
    m_pendingLocalMetadataPaths.clear();
    bool needsDirectoryRefresh = false;

    for (const QString &path : changedPaths) {
        QFileInfo info(path);
        if (!info.exists()) {
            needsDirectoryRefresh = true;
            continue;
        }

        int row = -1;
        for (int i = 0; i < m_items.size(); ++i) {
            if (QDir::cleanPath(m_items.at(i).localPath) == path) {
                row = i;
                break;
            }
        }
        if (row < 0) {
            needsDirectoryRefresh = true;
            continue;
        }

        Entry &entry = m_items[row];
        const qint64 newSize = info.isDir() ? 0 : info.size();
        const QDateTime newModified = info.lastModified();
        const QString svgRevision = entry.suffix == QStringLiteral("svg") ? svgPreviewIdentity(path) : QString();
        const bool previewChanged = entry.size != newSize || entry.modified != newModified;
        if (previewChanged && isVideoEntry(entry.mimeType, entry.suffix)) {
            if (entry.previewRevision.isEmpty())
                entry.previewRevision = QString::number(entry.modified.toMSecsSinceEpoch()) + '-' + QString::number(entry.size);
            schedulePreviewSettlement(path);
        }
        QList<int> roles;
        if (!svgRevision.isEmpty() && entry.previewRevision != svgRevision) {
            entry.previewRevision = svgRevision;
            roles.append(PreviewRevisionRole);
        }
        if (entry.size != newSize) {
            entry.size = newSize;
            roles.append(SizeRole);
        }
        if (entry.modified != newModified) {
            entry.modified = newModified;
            roles.append(ModifiedRole);
        }
        if (!roles.isEmpty()) {
            if (!isVideoEntry(entry.mimeType, entry.suffix)) roles.append(PreviewRevisionRole);
            const QModelIndex changed = index(row, 0);
            emit dataChanged(changed, changed, roles);
        }

        // Some backends drop a file watch after replace/rename semantics.  If
        // the path still exists, immediately arm it again for subsequent writes.
        if (!m_localWatcher.files().contains(path) && info.isFile())
            m_localWatcher.addPath(path);
    }

    if (needsDirectoryRefresh && !m_localRefreshTimer.isActive())
        m_localRefreshTimer.start();
}

void DirectoryModel::cancelKioListing()
{
    ++m_kioGeneration;
    if (m_kioJob) {
        m_kioJob->disconnect(this);
        m_kioJob->kill();
        m_kioJob.clear();
    }
}

void DirectoryModel::schedulePreviewSettlement(const QString &path)
{
    if (path.isEmpty()) return;
    m_pendingPreviewPaths.insert(path);
    m_previewSettleTimer.start();
}

void DirectoryModel::settleVideoPreviews()
{
    const auto paths = std::exchange(m_pendingPreviewPaths, {});
    for (int row = 0; row < m_items.size(); ++row) {
        auto &entry = m_items[row];
        if (!paths.contains(entry.localPath)) continue;
        const QFileInfo info(entry.localPath);
        if (!info.exists()) continue;
        if (info.size() != entry.size || info.lastModified() != entry.modified) {
            schedulePreviewSettlement(entry.localPath);
            continue;
        }
        const QString revision = QString::number(entry.modified.toMSecsSinceEpoch()) + '-' + QString::number(entry.size);
        if (entry.previewRevision == revision) continue;
        entry.previewRevision = revision;
        emit dataChanged(index(row, 0), index(row, 0), {PreviewRevisionRole});
    }
}

void DirectoryModel::appendKioEntries(QVector<Entry> entries)
{
    // A fast local Trash snapshot and KIO can report the same URL. Update
    // those rows in place so previews/selection survive the authoritative list.
    QHash<QUrl, int> rows;
    rows.reserve(m_items.size() + entries.size());
    for (int row = 0; row < m_items.size(); ++row)
        rows.insert(m_items.at(row).url, row);
    QVector<Entry> added;
    for (Entry &entry : entries) {
        rememberTrashLocalEntry(entry);
        const auto existing = rows.constFind(entry.url);
        if (existing == rows.constEnd()) {
            rows.insert(entry.url, m_items.size() + added.size());
            added.push_back(std::move(entry));
        } else if (*existing < m_items.size()) {
            m_items[*existing] = std::move(entry);
            const QModelIndex changed = index(*existing, 0);
            emit dataChanged(changed, changed);
        }
    }
    if (added.isEmpty())
        return;
    const int first = m_items.size();
    beginInsertRows(QModelIndex(), first, first + added.size() - 1);
    m_items += std::move(added);
    endInsertRows();
    emit countChanged();
}

void DirectoryModel::loadHomeTrashPreview(int generation)
{
    const QString trashPath = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                            + QStringLiteral("/Trash");
    auto *watcher = new QFutureWatcher<QVector<Entry>>(this);
    connect(watcher, &QFutureWatcher<QVector<Entry>>::finished, this, [this, watcher, generation] {
        QVector<Entry> entries = watcher->result();
        watcher->deleteLater();
        // Navigation and a completed KIO listing both supersede this snapshot.
        if (generation != m_kioGeneration || !m_kioJob || !m_items.isEmpty())
            return;
        sortEntriesVector(entries);
        appendKioEntries(std::move(entries));
        if (!m_items.isEmpty())
            setLoading(false);
    });
    watcher->setFuture(QtConcurrent::run([trashPath] {
        QVector<Entry> entries;
        QMimeDatabase mimeDb;
        const QDir infoDirectory(trashPath + QStringLiteral("/info"));
        const QStringList infoNames = infoDirectory.entryList({QStringLiteral("*.trashinfo")},
            QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDir::NoSort);
        entries.reserve(infoNames.size());
        for (const QString &infoName : infoNames) {
            QSettings infoFile(infoDirectory.filePath(infoName), QSettings::IniFormat);
            infoFile.beginGroup(QStringLiteral("Trash Info"));
            if (infoFile.value(QStringLiteral("Path")).toString().isEmpty())
                continue;
            const QString name = infoName.chopped(10); // .trashinfo
            const QString path = trashPath + QStringLiteral("/files/") + name;
            const QFileInfo info(path);
            if (!info.exists() && !info.isSymbolicLink())
                continue;
            Entry entry;
            entry.name = name;
            // KDE reserves trash ID 0 for the home trash. Keep the real trash
            // URL for every action; the physical path is only for thumbnails.
            entry.url.setScheme(QStringLiteral("trash"));
            entry.url.setPath(QStringLiteral("/0-") + name);
            entry.localPath = path;
            entry.isDir = info.isDir() && !info.isSymbolicLink();
            entry.size = info.size();
            entry.modified = info.lastModified();
            entry.created = entry.modified;
            entry.suffix = info.suffix().toLower();
            if (info.isSymbolicLink()) {
                entry.linkType = QStringLiteral("symlink");
                entry.linkTarget = info.symLinkTarget();
            }
            const QMimeType mime = entry.isDir
                ? mimeDb.mimeTypeForName(QStringLiteral("inode/directory"))
                : mimeDb.mimeTypeForFile(name, QMimeDatabase::MatchExtension);
            entry.mimeType = mime.name();
            entry.systemIconName = entry.isDir ? QStringLiteral("folder") : mime.iconName();
            if (entry.systemIconName.isEmpty())
                entry.systemIconName = mime.genericIconName();
            if (entry.systemIconName.isEmpty())
                entry.systemIconName = QStringLiteral("text-x-generic");
            entries.push_back(std::move(entry));
        }
        return entries;
    }));
}

QString DirectoryModel::resolveTrashLocalPath(const QUrl &trashUrl) const
{
    if (trashUrl.scheme() != QStringLiteral("trash"))
        return {};

    const QString trashPath = trashUrl.path();
    QString bestKey;
    QString bestLocal;
    for (auto it = m_trashLocalPaths.constBegin(); it != m_trashLocalPaths.constEnd(); ++it) {
        const QString &key = it.key();
        if (trashPath != key && !trashPath.startsWith(key + QLatin1Char('/')))
            continue;
        if (key.size() > bestKey.size()) {
            bestKey = key;
            bestLocal = it.value();
        }
    }
    if (!bestLocal.isEmpty()) {
        QString remainder = trashPath.mid(bestKey.size());
        if (remainder.startsWith(QLatin1Char('/')))
            remainder.remove(0, 1);
        return remainder.isEmpty() ? QDir::cleanPath(bestLocal)
                                   : QDir::cleanPath(QDir(bestLocal).filePath(remainder));
    }

    // Trash ID 0 is KDE's home trash. This fallback also makes a home-trash
    // folder opened directly in a new tab fast even when that DirectoryModel
    // did not see the root listing first.
    if (trashPath.startsWith(QStringLiteral("/0-"))) {
        const QString relative = trashPath.mid(3);
        const QString dataHome = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        if (!dataHome.isEmpty() && !relative.isEmpty())
            return QDir::cleanPath(QDir(dataHome + QStringLiteral("/Trash/files")).filePath(relative));
    }
    return {};
}

void DirectoryModel::rememberTrashLocalEntry(const Entry &entry)
{
    if (!entry.isDir || entry.url.scheme() != QStringLiteral("trash") || entry.localPath.isEmpty())
        return;
    const QString key = entry.url.path();
    if (!key.isEmpty())
        m_trashLocalPaths.insert(key, QDir::cleanPath(entry.localPath));
}

void DirectoryModel::loadTrashLocal(const QString &localPath, const QUrl &trashLocation, int generation)
{
    auto *watcher = new QFutureWatcher<QVector<Entry>>(this);
    connect(watcher, &QFutureWatcher<QVector<Entry>>::finished, this,
            [this, watcher, generation, localPath, trashLocation] {
        QVector<Entry> entries = watcher->result();
        watcher->deleteLater();
        if (generation != m_kioGeneration || m_location != trashLocation)
            return;

        sortEntriesVector(entries);
        for (const Entry &entry : std::as_const(entries))
            rememberTrashLocalEntry(entry);
        beginResetModel();
        m_items = std::move(entries);
        endResetModel();
        emit countChanged();
        watchLocalDirectory(localPath);
        setLoading(false);
        emit refreshCompleted();
    });

    const bool showHidden = m_showHidden;
    watcher->setFuture(QtConcurrent::run([localPath, trashLocation, showHidden] {
        QVector<Entry> entries;
        QDir dir(localPath);
        if (!dir.exists())
            return entries;

        QDir::Filters filters = QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System;
        if (showHidden)
            filters |= QDir::Hidden;
        const QFileInfoList list = dir.entryInfoList(filters, QDir::NoSort);
        entries.reserve(list.size());
        QMimeDatabase mimeDb;
        for (const QFileInfo &fi : list) {
            Entry e;
            e.name = fi.fileName();
            QUrl itemUrl = trashLocation;
            QString path = itemUrl.path();
            if (!path.endsWith(QLatin1Char('/')))
                path += QLatin1Char('/');
            path += fi.fileName();
            itemUrl.setPath(path);
            e.url = itemUrl;
            e.localPath = fi.absoluteFilePath();
            e.isDir = fi.isDir() && !fi.isSymbolicLink();
            e.size = fi.size();
            e.modified = fi.lastModified();
            e.created = fi.birthTime();
            if (!e.created.isValid())
                e.created = e.modified;
            e.suffix = fi.suffix().toLower();
            e.hidden = fi.isHidden();
            const LocalLinkMetadata linkMetadata = localLinkMetadata(e.localPath, e.suffix);
            e.linkType = linkMetadata.type;
            e.linkTarget = linkMetadata.target;
            e.previewRevision = linkMetadata.previewRevision;
            const QMimeType mime = e.isDir
                ? mimeDb.mimeTypeForName(QStringLiteral("inode/directory"))
                : mimeDb.mimeTypeForFile(fi, QMimeDatabase::MatchExtension);
            e.mimeType = mime.name();
            e.systemIconName = e.isDir ? QStringLiteral("folder") : mime.iconName();
            if (e.systemIconName.isEmpty())
                e.systemIconName = mime.genericIconName();
            if (e.systemIconName.isEmpty())
                e.systemIconName = e.isDir ? QStringLiteral("folder") : QStringLiteral("text-x-generic");
            entries.push_back(std::move(e));
        }
        return entries;
    }));
}

void DirectoryModel::loadKio()
{
    const int generation = m_kioGeneration;
    setLoading(true);
    beginResetModel();
    m_items.clear();
    endResetModel();
    emit countChanged();

    // Always ask the KIO worker for hidden entries and apply visibility in the
    // model.  This is especially important for admin:/ where the worker can be
    // kept alive across the Polkit authentication boundary; re-listing with a
    // different flag alone is not reliable on every kio-admin version.
    const auto flags = KIO::ListJob::ListFlag::IncludeHidden;
    const bool trashLocation = m_location.scheme() == QStringLiteral("trash");
    const bool trashRoot = trashLocation && (m_location.path().isEmpty() || m_location.path() == QStringLiteral("/"));
    if (trashLocation && !trashRoot) {
        const QString localTrashPath = resolveTrashLocalPath(m_location);
        if (!localTrashPath.isEmpty() && QFileInfo(localTrashPath).isDir()) {
            loadTrashLocal(localTrashPath, m_location, generation);
            return;
        }
    }
    const auto listedEntries = std::make_shared<QVector<Entry>>();
    auto *job = KIO::listDir(m_location, KIO::HideProgressInfo, flags);
    m_kioJob = job;
    if (trashRoot)
        loadHomeTrashPreview(generation);

    connect(job, &KIO::ListJob::entries, this,
            [this, generation, trashLocation, trashRoot, listedEntries](KIO::Job *, const KIO::UDSEntryList &entries) {
        if (generation != m_kioGeneration)
            return;
        QVector<Entry> incoming;
        QMimeDatabase mimeDb;
        incoming.reserve(entries.size());

        for (const KIO::UDSEntry &uds : entries) {
            const QString name = uds.stringValue(KIO::UDSEntry::UDS_NAME);
            if (name == QStringLiteral(".") || name == QStringLiteral("..") || name.isEmpty())
                continue;

            Entry e;
            e.name = uds.stringValue(KIO::UDSEntry::UDS_DISPLAY_NAME);
            if (e.name.isEmpty())
                e.name = name;

            const QString explicitUrl = uds.stringValue(KIO::UDSEntry::UDS_URL);
            // kio-admin delegates to the file worker and may report file:// in
            // UDS_URL.  Never let an admin:/ view silently fall back to normal
            // user permissions: keep every child on the admin:/ scheme.
            if (m_location.scheme() == QStringLiteral("admin")) {
                QUrl base = m_location;
                QString path = base.path();
                if (!path.endsWith(QLatin1Char('/')))
                    path += QLatin1Char('/');
                path += name;
                base.setPath(path);
                e.url = base;
            } else if (!explicitUrl.isEmpty()) {
                e.url = QUrl(explicitUrl);
            } else {
                QUrl base = m_location;
                QString path = base.path();
                if (!path.endsWith(QLatin1Char('/')))
                    path += QLatin1Char('/');
                path += name;
                base.setPath(path);
                e.url = base;
            }

            e.localPath = uds.stringValue(KIO::UDSEntry::UDS_LOCAL_PATH);
            e.isDir = uds.isDir();
            e.size = uds.numberValue(KIO::UDSEntry::UDS_SIZE, 0);
            e.modified = QDateTime::fromSecsSinceEpoch(
                uds.numberValue(KIO::UDSEntry::UDS_MODIFICATION_TIME, 0));
            // trash:/ can contain thousands of entries. Avoid a second local
            // stat/link-metadata pass for every KIO row on the GUI thread; the
            // worker already supplied the information needed to render it.
            if (!trashLocation && !e.localPath.isEmpty())
                e.created = QFileInfo(e.localPath).birthTime();
            if (!e.created.isValid()) e.created = e.modified;
            e.suffix = QFileInfo(name).suffix().toLower();
            if (!trashLocation && !e.localPath.isEmpty()) {
                const LocalLinkMetadata linkMetadata = localLinkMetadata(e.localPath, e.suffix);
                e.linkType = linkMetadata.type;
                e.linkTarget = linkMetadata.target;
                e.previewRevision = linkMetadata.previewRevision;
            } else {
                const QString linkDest = uds.stringValue(KIO::UDSEntry::UDS_LINK_DEST);
                if (!linkDest.isEmpty()) {
                    e.linkType = QStringLiteral("symlink");
                    e.linkTarget = linkDest;
                }
            }
            e.hidden = uds.numberValue(KIO::UDSEntry::UDS_HIDDEN, name.startsWith('.') ? 1 : 0) != 0;
            if (!m_showHidden && e.hidden)
                continue;
            e.mimeType = uds.stringValue(KIO::UDSEntry::UDS_MIME_TYPE);
            e.systemIconName = uds.stringValue(KIO::UDSEntry::UDS_ICON_NAME);
            if (e.systemIconName.isEmpty()) {
                const QMimeType mime = mimeDb.mimeTypeForName(e.mimeType);
                e.systemIconName = e.isDir ? QStringLiteral("folder") : mime.iconName();
                if (e.systemIconName.isEmpty())
                    e.systemIconName = mime.genericIconName();
            }
            if (e.systemIconName.isEmpty())
                e.systemIconName = e.isDir ? QStringLiteral("folder") : QStringLiteral("text-x-generic");
            incoming.push_back(e);
        }

        if (!incoming.isEmpty()) {
            if (trashLocation) {
                for (const Entry &entry : std::as_const(incoming))
                    rememberTrashLocalEntry(entry);
            }
            if (trashRoot) {
                *listedEntries += incoming;
                appendKioEntries(std::move(incoming));
            } else {
                const int first = m_items.size();
                beginInsertRows(QModelIndex(), first, first + incoming.size() - 1);
                m_items += std::move(incoming);
                endInsertRows();
                emit countChanged();
            }
            // Let the Trash page become interactive as soon as KIO yields its
            // first batch instead of hiding everything until the full listing
            // has completed. Remaining rows continue streaming in.
            if (trashLocation && m_loading)
                setLoading(false);
        }
    });

    connect(job, &KJob::result, this, [this, job, generation, trashLocation, trashRoot, listedEntries]() {
        if (generation != m_kioGeneration)
            return;
        m_kioJob.clear();
        if (job->error())
            setErrorString(job->errorString());
        if (trashRoot && !job->error()) {
            sortEntriesVector(*listedEntries);
            reconcileLocalEntries(std::move(*listedEntries));
        } else {
            sortEntries();
        }
        if (!trashLocation)
            scheduleVideoMetadata();
        setLoading(false);
        emit refreshCompleted();
    });
}

void DirectoryModel::updateStorageInfo()
{
    qint64 total = 0;
    qint64 available = 0;
    if (m_location.isLocalFile() || m_location.scheme() == QStringLiteral("category")) {
        QStorageInfo storage(m_location.isLocalFile() ? m_location.toLocalFile() : QDir::homePath());
        storage.refresh();
        if (storage.isValid() && storage.isReady()) {
            total = qMax<qint64>(0, storage.bytesTotal());
            available = qMax<qint64>(0, storage.bytesAvailable());
        }
    }

    if (total == m_storageTotalBytes && available == m_storageAvailableBytes)
        return;
    m_storageTotalBytes = total;
    m_storageAvailableBytes = available;
    emit storageInfoChanged();
}

void DirectoryModel::scheduleVideoMetadata()
{
    persistVideoMetadataCache();
    if (m_heavyIoBusy)
        return;
    ++m_videoProbeGeneration;
    m_videoProbeQueue.clear();
    m_videoProbePending.clear();
    if (m_activeVideoProbe) {
        m_activeVideoProbe->disconnect(this);
        m_activeVideoProbe->kill();
        m_activeVideoProbe->deleteLater();
        m_activeVideoProbe.clear();
    }
    m_videoProbeAvailable = !QStandardPaths::findExecutable(QStringLiteral("ffprobe")).isEmpty();

    bool changed = false;
    for (Entry &entry : m_items) {
        if (entry.isDir || entry.localPath.isEmpty())
            continue;
        if (!isVideoEntry(entry.mimeType, entry.suffix))
            continue;
        QString cached;
        if (cachedVideoMetadata(entry.localPath, entry.size,
                                entry.modified.toMSecsSinceEpoch(), &cached)
                && entry.videoInfo != cached) {
            entry.videoInfo = cached;
            changed = true;
        }
    }
    if (changed && !m_items.isEmpty())
        emit dataChanged(index(0, 0), index(m_items.size() - 1, 0), {VideoInfoRole});
}

void DirectoryModel::requestVideoMetadata(int row)
{
    if (m_heavyIoBusy || !m_videoProbeAvailable || row < 0 || row >= m_items.size())
        return;
    Entry &entry = m_items[row];
    if (entry.isDir || entry.localPath.isEmpty() || !entry.videoInfo.isEmpty()
            || !isVideoEntry(entry.mimeType, entry.suffix)
            || m_videoProbePending.contains(entry.localPath))
        return;

    QString cached;
    if (cachedVideoMetadata(entry.localPath, entry.size,
                            entry.modified.toMSecsSinceEpoch(), &cached)) {
        if (!cached.isEmpty()) {
            entry.videoInfo = cached;
            emit dataChanged(index(row, 0), index(row, 0), {VideoInfoRole});
        }
        return;
    }

    m_videoProbePending.insert(entry.localPath);
    m_videoProbeQueue.enqueue(entry.localPath);
    startNextVideoProbe(m_videoProbeGeneration);
}

void DirectoryModel::startNextVideoProbe(int generation)
{
    if (generation != m_videoProbeGeneration || m_activeVideoProbe)
        return;
    if (m_videoProbeQueue.isEmpty()) {
        persistVideoMetadataCache();
        return;
    }

    const QString localPath = m_videoProbeQueue.dequeue();
    const QFileInfo info(localPath);
    const qint64 sourceSize = info.size();
    const qint64 sourceModifiedMs = info.lastModified().toMSecsSinceEpoch();
    auto *probe = new QProcess(this);
    m_activeVideoProbe = probe;
    connect(probe, &QProcess::finished, this,
            [this, probe, localPath, sourceSize, sourceModifiedMs, generation]
            (int exitCode, QProcess::ExitStatus status) {
        QString videoInfo;
        if (generation == m_videoProbeGeneration && status == QProcess::NormalExit && exitCode == 0) {
            const QJsonObject root = QJsonDocument::fromJson(probe->readAllStandardOutput()).object();
            const QJsonArray streams = root.value(QStringLiteral("streams")).toArray();
            double fps = 0.0;
            if (!streams.isEmpty()) {
                const QJsonObject stream = streams.first().toObject();
                QString rate = stream.value(QStringLiteral("avg_frame_rate")).toString();
                if (rate.isEmpty() || rate == QStringLiteral("0/0"))
                    rate = stream.value(QStringLiteral("r_frame_rate")).toString();
                const QStringList parts = rate.split(QLatin1Char('/'));
                if (parts.size() == 2 && parts.at(1).toDouble() != 0.0)
                    fps = parts.at(0).toDouble() / parts.at(1).toDouble();
                else
                    fps = rate.toDouble();
            }

            const double durationSeconds = root.value(QStringLiteral("format")).toObject()
                                               .value(QStringLiteral("duration")).toString().toDouble();
            QStringList details;
            if (fps > 0.0) {
                const bool whole = qAbs(fps - qRound(fps)) < 0.01;
                details << QStringLiteral("%1 FPS").arg(QString::number(fps, 'f', whole ? 0 : 2));
            }
            if (durationSeconds > 0.0) {
                const qint64 totalSeconds = qRound64(durationSeconds);
                const qint64 hours = totalSeconds / 3600;
                const qint64 minutes = (totalSeconds % 3600) / 60;
                const qint64 seconds = totalSeconds % 60;
                details << (hours > 0
                    ? QStringLiteral("%1:%2:%3").arg(hours, 2, 10, QLatin1Char('0'))
                                                .arg(minutes, 2, 10, QLatin1Char('0'))
                                                .arg(seconds, 2, 10, QLatin1Char('0'))
                    : QStringLiteral("%1:%2").arg(minutes, 2, 10, QLatin1Char('0'))
                                               .arg(seconds, 2, 10, QLatin1Char('0')));
            }
            videoInfo = details.join(QStringLiteral(" · "));
        }
        if (m_activeVideoProbe == probe)
            m_activeVideoProbe.clear();
        m_videoProbePending.remove(localPath);
        probe->deleteLater();
        if (generation == m_videoProbeGeneration) {
            // Empty is a valid cached result too: corrupt/video-less files must
            // not start a fresh ffprobe every time their delegate becomes visible.
            storeVideoMetadata(localPath, sourceSize, sourceModifiedMs, videoInfo);
            applyVideoMetadata(localPath, videoInfo, generation);
        }
        startNextVideoProbe(generation);
    });
    probe->start(QStringLiteral("ffprobe"),
                 {QStringLiteral("-v"), QStringLiteral("error"),
                  QStringLiteral("-select_streams"), QStringLiteral("v:0"),
                  QStringLiteral("-show_entries"),
                  QStringLiteral("stream=avg_frame_rate,r_frame_rate:format=duration"),
                  QStringLiteral("-of"), QStringLiteral("json"), localPath});
}

void DirectoryModel::applyVideoMetadata(const QString &localPath, const QString &videoInfo, int generation)
{
    if (generation != m_videoProbeGeneration || videoInfo.isEmpty())
        return;
    for (int row = 0; row < m_items.size(); ++row) {
        Entry &entry = m_items[row];
        if (entry.localPath != localPath || entry.videoInfo == videoInfo)
            continue;
        entry.videoInfo = videoInfo;
        const QModelIndex changed = index(row, 0);
        emit dataChanged(changed, changed, {VideoInfoRole});
        return;
    }
}

void DirectoryModel::loadGoogleDrive()
{
    const int generation = ++m_googleGeneration;
    setLoading(true);

    beginResetModel();
    m_items.clear();
    endResetModel();
    emit countChanged();

    if (m_googleAccessToken.trimmed().isEmpty()) {
        setErrorString(QStringLiteral("Google Drive is not connected. Return to Home and connect your account."));
        setLoading(false);
        return;
    }

    if (m_location.host() == QStringLiteral("root") || m_location.host().isEmpty()) {
        m_googleFolderId = QStringLiteral("root");
        m_googleParentId.clear();
        m_googleFolderName = QStringLiteral("Google Drive");
    } else if (m_location.host() == QStringLiteral("folder")) {
        m_googleFolderId = m_location.path();
        if (m_googleFolderId.startsWith('/'))
            m_googleFolderId.remove(0, 1);
        m_googleParentId.clear();
        m_googleFolderName = QStringLiteral("Folder");
        requestGoogleDriveFolderMetadata(m_googleFolderId, generation);
    } else {
        setErrorString(QStringLiteral("Invalid Google Drive location."));
        setLoading(false);
        return;
    }

    requestGoogleDrivePage(m_googleFolderId, QString(), generation);
}

void DirectoryModel::requestGoogleDriveFolderMetadata(const QString &folderId, int generation)
{
    QUrl url(QStringLiteral("https://www.googleapis.com/drive/v3/files/%1").arg(folderId));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("fields"), QStringLiteral("id,name,parents"));
    query.addQueryItem(QStringLiteral("supportsAllDrives"), QStringLiteral("true"));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + m_googleAccessToken.toUtf8());
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Lurviko/1.0.0"));

    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation]() {
        const QByteArray payload = reply->readAll();
        const auto error = reply->error();
        reply->deleteLater();
        if (generation != m_googleGeneration || error != QNetworkReply::NoError)
            return;

        const QJsonObject obj = QJsonDocument::fromJson(payload).object();
        const QString name = obj.value(QStringLiteral("name")).toString();
        if (!name.isEmpty())
            m_googleFolderName = name;
        const QJsonArray parents = obj.value(QStringLiteral("parents")).toArray();
        if (!parents.isEmpty())
            m_googleParentId = parents.first().toString();
        else
            m_googleParentId.clear();
        emit locationChanged();
    });
}

void DirectoryModel::requestGoogleDrivePage(const QString &folderId, const QString &pageToken, int generation)
{
    QUrl url(QStringLiteral("https://www.googleapis.com/drive/v3/files"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("q"), QStringLiteral("'%1' in parents and trashed = false").arg(folderId));
    query.addQueryItem(QStringLiteral("fields"), QStringLiteral("nextPageToken,files(id,name,mimeType,size,modifiedTime,fileExtension,parents)"));
    query.addQueryItem(QStringLiteral("pageSize"), QStringLiteral("1000"));
    query.addQueryItem(QStringLiteral("orderBy"), QStringLiteral("folder,name"));
    query.addQueryItem(QStringLiteral("spaces"), QStringLiteral("drive"));
    query.addQueryItem(QStringLiteral("supportsAllDrives"), QStringLiteral("true"));
    query.addQueryItem(QStringLiteral("includeItemsFromAllDrives"), QStringLiteral("true"));
    if (!pageToken.isEmpty())
        query.addQueryItem(QStringLiteral("pageToken"), pageToken);
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + m_googleAccessToken.toUtf8());
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Lurviko/1.0.0"));

    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, folderId, generation]() {
        const QByteArray payload = reply->readAll();
        const auto networkError = reply->error();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString networkErrorText = reply->errorString();
        reply->deleteLater();

        if (generation != m_googleGeneration)
            return;

        if (networkError != QNetworkReply::NoError) {
            QString message = networkErrorText;
            const QJsonObject errorObj = QJsonDocument::fromJson(payload).object().value(QStringLiteral("error")).toObject();
            const QString apiMessage = errorObj.value(QStringLiteral("message")).toString();
            if (!apiMessage.isEmpty())
                message = apiMessage;
            if (status == 401)
                message = QStringLiteral("Google Drive session expired or is invalid. Reconnect Google Drive from Home.");
            setErrorString(message);
            setLoading(false);
            return;
        }

        const QJsonObject root = QJsonDocument::fromJson(payload).object();
        const QJsonArray files = root.value(QStringLiteral("files")).toArray();
        QVector<Entry> incoming;
        incoming.reserve(files.size());

        for (const QJsonValue &value : files) {
            const QJsonObject obj = value.toObject();
            const QString id = obj.value(QStringLiteral("id")).toString();
            const QString name = obj.value(QStringLiteral("name")).toString();
            const QString mimeType = obj.value(QStringLiteral("mimeType")).toString();
            if (id.isEmpty() || name.isEmpty())
                continue;

            Entry e;
            e.name = name;
            e.isDir = mimeType == QStringLiteral("application/vnd.google-apps.folder");
            e.url = QUrl(e.isDir
                         ? QStringLiteral("gdrive://folder/%1").arg(id)
                         : QStringLiteral("gdrive://file/%1").arg(id));
            e.localPath.clear();
            e.size = obj.value(QStringLiteral("size")).toString().toLongLong();
            e.modified = QDateTime::fromString(obj.value(QStringLiteral("modifiedTime")).toString(), Qt::ISODate);
            e.created = e.modified;
            e.suffix = obj.value(QStringLiteral("fileExtension")).toString().toLower();
            if (e.suffix.isEmpty())
                e.suffix = QFileInfo(name).suffix().toLower();
            e.hidden = false;
            e.mimeType = mimeType;
            if (e.isDir) {
                e.systemIconName = QStringLiteral("folder-cloud");
            } else {
                const QMimeType mime = QMimeDatabase().mimeTypeForName(mimeType);
                e.systemIconName = mime.iconName();
                if (e.systemIconName.isEmpty())
                    e.systemIconName = mime.genericIconName();
                if (e.systemIconName.isEmpty())
                    e.systemIconName = QStringLiteral("text-x-generic");
            }
            incoming.push_back(e);
        }

        if (!incoming.isEmpty()) {
            const int first = m_items.size();
            beginInsertRows(QModelIndex(), first, first + incoming.size() - 1);
            m_items += incoming;
            endInsertRows();
            emit countChanged();
        }

        const QString nextPageToken = root.value(QStringLiteral("nextPageToken")).toString();
        if (!nextPageToken.isEmpty()) {
            requestGoogleDrivePage(folderId, nextPageToken, generation);
            return;
        }

        sortEntries();
        setLoading(false);
    });
}

void DirectoryModel::loadOneDrive()
{
    const int generation = ++m_oneDriveGeneration;
    setLoading(true);
    beginResetModel();
    m_items.clear();
    endResetModel();
    emit countChanged();

    if (m_oneDriveAccessToken.trimmed().isEmpty()) {
        setErrorString(QStringLiteral("OneDrive bağlı değil. Ana Sayfa'dan hesabınızı bağlayın."));
        setLoading(false);
        return;
    }

    if (m_location.host() == QStringLiteral("root") || m_location.host().isEmpty()) {
        m_oneDriveFolderId = QStringLiteral("root");
        m_oneDriveParentId.clear();
        m_oneDriveFolderName = QStringLiteral("OneDrive");
    } else if (m_location.host() == QStringLiteral("folder")) {
        QString encoded = m_location.path();
        if (encoded.startsWith('/')) encoded.remove(0, 1);
        m_oneDriveFolderId = QUrl::fromPercentEncoding(encoded.toUtf8());
        m_oneDriveParentId.clear();
        m_oneDriveFolderName = QStringLiteral("Klasör");
        requestOneDriveFolderMetadata(m_oneDriveFolderId, generation);
    } else {
        setErrorString(QStringLiteral("Geçersiz OneDrive konumu."));
        setLoading(false);
        return;
    }

    requestOneDrivePage(m_oneDriveFolderId, QUrl(), generation);
}

void DirectoryModel::requestOneDriveFolderMetadata(const QString &folderId, int generation)
{
    const QString encodedId = QString::fromLatin1(QUrl::toPercentEncoding(folderId));
    QUrl url(QStringLiteral("https://graph.microsoft.com/v1.0/me/drive/items/%1?$select=id,name,parentReference").arg(encodedId));
    auto *reply = m_network.get(oneDriveRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation]() {
        const QByteArray payload = reply->readAll();
        const auto error = reply->error();
        reply->deleteLater();
        if (generation != m_oneDriveGeneration || error != QNetworkReply::NoError)
            return;
        const QJsonObject obj = QJsonDocument::fromJson(payload).object();
        const QString name = obj.value(QStringLiteral("name")).toString();
        if (!name.isEmpty()) m_oneDriveFolderName = name;
        const QJsonObject parent = obj.value(QStringLiteral("parentReference")).toObject();
        m_oneDriveParentId = parent.value(QStringLiteral("id")).toString();
        m_oneDriveDriveId = parent.value(QStringLiteral("driveId")).toString();
        emit locationChanged();
    });
}

void DirectoryModel::requestOneDrivePage(const QString &folderId, const QUrl &nextUrl, int generation)
{
    QUrl url = nextUrl;
    if (!url.isValid() || url.isEmpty()) {
        if (folderId == QStringLiteral("root"))
            url = QUrl(QStringLiteral("https://graph.microsoft.com/v1.0/me/drive/root/children?$top=200&$select=id,name,size,lastModifiedDateTime,file,folder,package,parentReference,webUrl"));
        else
            url = QUrl(QStringLiteral("https://graph.microsoft.com/v1.0/me/drive/items/%1/children?$top=200&$select=id,name,size,lastModifiedDateTime,file,folder,package,parentReference,webUrl").arg(QString::fromLatin1(QUrl::toPercentEncoding(folderId))));
    }

    auto *reply = m_network.get(oneDriveRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, folderId, generation]() {
        const QByteArray payload = reply->readAll();
        const auto networkError = reply->error();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString fallback = reply->errorString();
        reply->deleteLater();
        if (generation != m_oneDriveGeneration) return;
        if (networkError != QNetworkReply::NoError || status >= 400) {
            QString message = oneDriveApiError(payload, fallback);
            if (status == 401) message = QStringLiteral("OneDrive oturumu sona ermiş veya geçersiz. Ana Sayfa'dan OneDrive'a yeniden bağlanın.");
            setErrorString(message); setLoading(false); return;
        }

        const QJsonObject root = QJsonDocument::fromJson(payload).object();
        const QJsonArray items = root.value(QStringLiteral("value")).toArray();
        QVector<Entry> incoming; incoming.reserve(items.size());
        for (const QJsonValue &value : items) {
            const QJsonObject obj = value.toObject();
            const QString id = obj.value(QStringLiteral("id")).toString();
            const QString name = obj.value(QStringLiteral("name")).toString();
            if (id.isEmpty() || name.isEmpty()) continue;
            const bool isDir = obj.contains(QStringLiteral("folder")) || obj.contains(QStringLiteral("package"));
            const QJsonObject fileObj = obj.value(QStringLiteral("file")).toObject();
            const QString mimeType = isDir ? QStringLiteral("inode/directory") : fileObj.value(QStringLiteral("mimeType")).toString(QStringLiteral("application/octet-stream"));
            Entry e;
            e.name = name; e.isDir = isDir;
            const QString encodedId = QString::fromLatin1(QUrl::toPercentEncoding(id));
            e.url = QUrl(isDir ? QStringLiteral("onedrive://folder/%1").arg(encodedId) : QStringLiteral("onedrive://file/%1").arg(encodedId));
            e.size = obj.value(QStringLiteral("size")).toVariant().toLongLong();
            e.modified = QDateTime::fromString(obj.value(QStringLiteral("lastModifiedDateTime")).toString(), Qt::ISODate);
            e.created = e.modified;
            e.suffix = QFileInfo(name).suffix().toLower(); e.hidden = false; e.mimeType = mimeType;
            if (isDir) e.systemIconName = QStringLiteral("folder-cloud");
            else { QMimeType mime = QMimeDatabase().mimeTypeForName(mimeType); e.systemIconName = mime.iconName(); if(e.systemIconName.isEmpty()) e.systemIconName=mime.genericIconName(); if(e.systemIconName.isEmpty()) e.systemIconName=QStringLiteral("text-x-generic"); }
            incoming.push_back(e);
        }
        if (!incoming.isEmpty()) { const int first=m_items.size(); beginInsertRows(QModelIndex(), first, first+incoming.size()-1); m_items += incoming; endInsertRows(); emit countChanged(); }
        const QUrl next(root.value(QStringLiteral("@odata.nextLink")).toString());
        if (next.isValid() && !next.isEmpty()) { requestOneDrivePage(folderId, next, generation); return; }
        sortEntries(); setLoading(false);
    });
}

QString DirectoryModel::oneDriveFileIdFromUrl(const QString &itemUrl) const
{
    const QUrl url(itemUrl);
    if (url.scheme() != QStringLiteral("onedrive") || (url.host()!=QStringLiteral("file") && url.host()!=QStringLiteral("folder"))) return {};
    QString id=url.path(); if(id.startsWith('/')) id.remove(0,1); return QUrl::fromPercentEncoding(id.toUtf8());
}

QNetworkRequest DirectoryModel::oneDriveRequest(const QUrl &url) const
{
    QNetworkRequest req(url); req.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + m_oneDriveAccessToken.toUtf8()); req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Lurviko/1.0.0")); req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy); return req;
}

QString DirectoryModel::oneDriveApiError(const QByteArray &payload, const QString &fallback) const
{
    const QJsonDocument doc=QJsonDocument::fromJson(payload); if(doc.isObject()){const QJsonObject error=doc.object().value(QStringLiteral("error")).toObject();const QString message=error.value(QStringLiteral("message")).toString();if(!message.isEmpty())return message;}return fallback;
}

void DirectoryModel::finishOneDriveOperation(QNetworkReply *reply, const QString &successMessage, bool refreshAfter)
{
    connect(reply,&QNetworkReply::finished,this,[this,reply,successMessage,refreshAfter](){const QByteArray payload=reply->readAll();const auto error=reply->error();const int status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();const QString fallback=reply->errorString();reply->deleteLater();if(error!=QNetworkReply::NoError||status>=400){emit oneDriveOperationFinished(false,oneDriveApiError(payload,fallback));return;}emit oneDriveOperationFinished(true,successMessage);if(refreshAfter)refresh();});
}

void DirectoryModel::oneDriveCreateFolder(const QString &name)
{
    if(m_location.scheme()!=QStringLiteral("onedrive")||m_oneDriveAccessToken.isEmpty()){emit oneDriveOperationFinished(false,QStringLiteral("OneDrive bağlı değil."));return;}
    const QString trimmed=name.trimmed();if(trimmed.isEmpty()){emit oneDriveOperationFinished(false,QStringLiteral("Klasör adı boş olamaz."));return;}
    const QUrl url(m_oneDriveFolderId==QStringLiteral("root") ? QStringLiteral("https://graph.microsoft.com/v1.0/me/drive/root/children") : QStringLiteral("https://graph.microsoft.com/v1.0/me/drive/items/%1/children").arg(QString::fromLatin1(QUrl::toPercentEncoding(m_oneDriveFolderId))));
    QJsonObject body;body.insert(QStringLiteral("name"),trimmed);body.insert(QStringLiteral("folder"),QJsonObject{});body.insert(QStringLiteral("@microsoft.graph.conflictBehavior"),QStringLiteral("rename"));QNetworkRequest req=oneDriveRequest(url);req.setHeader(QNetworkRequest::ContentTypeHeader,QStringLiteral("application/json"));finishOneDriveOperation(m_network.post(req,QJsonDocument(body).toJson(QJsonDocument::Compact)),QStringLiteral("OneDrive klasörü oluşturuldu."));
}

void DirectoryModel::oneDriveRenameItem(const QString &itemUrl, const QString &newName)
{
    const QString id=oneDriveFileIdFromUrl(itemUrl);const QString trimmed=newName.trimmed();if(id.isEmpty()||trimmed.isEmpty()){emit oneDriveOperationFinished(false,QStringLiteral("Geçersiz OneDrive öğesi veya adı."));return;}QJsonObject body;body.insert(QStringLiteral("name"),trimmed);QNetworkRequest req=oneDriveRequest(QUrl(QStringLiteral("https://graph.microsoft.com/v1.0/me/drive/items/%1").arg(QString::fromLatin1(QUrl::toPercentEncoding(id)))));req.setHeader(QNetworkRequest::ContentTypeHeader,QStringLiteral("application/json"));finishOneDriveOperation(m_network.sendCustomRequest(req,QByteArrayLiteral("PATCH"),QJsonDocument(body).toJson(QJsonDocument::Compact)),QStringLiteral("Yeniden adlandırıldı."));
}

void DirectoryModel::oneDriveTrashItem(const QString &itemUrl)
{
    const QString id=oneDriveFileIdFromUrl(itemUrl);if(id.isEmpty()){emit oneDriveOperationFinished(false,QStringLiteral("Geçersiz OneDrive öğesi."));return;}finishOneDriveOperation(m_network.deleteResource(oneDriveRequest(QUrl(QStringLiteral("https://graph.microsoft.com/v1.0/me/drive/items/%1").arg(QString::fromLatin1(QUrl::toPercentEncoding(id)))))),QStringLiteral("OneDrive geri dönüşüm kutusuna taşındı."));
}

void DirectoryModel::oneDriveDeleteItem(const QString &itemUrl)
{
    const QString id=oneDriveFileIdFromUrl(itemUrl);if(id.isEmpty()){emit oneDriveOperationFinished(false,QStringLiteral("Geçersiz OneDrive öğesi."));return;}
    QNetworkReply *meta=m_network.get(oneDriveRequest(QUrl(QStringLiteral("https://graph.microsoft.com/v1.0/me/drive/items/%1?$select=parentReference").arg(QString::fromLatin1(QUrl::toPercentEncoding(id))))));
    connect(meta,&QNetworkReply::finished,this,[this,meta,id](){const QByteArray p=meta->readAll();const auto e=meta->error();const QString f=meta->errorString();meta->deleteLater();if(e!=QNetworkReply::NoError){emit oneDriveOperationFinished(false,oneDriveApiError(p,f));return;}const QString driveId=QJsonDocument::fromJson(p).object().value(QStringLiteral("parentReference")).toObject().value(QStringLiteral("driveId")).toString();if(driveId.isEmpty()){emit oneDriveOperationFinished(false,QStringLiteral("OneDrive drive kimliği alınamadı."));return;}QNetworkRequest req=oneDriveRequest(QUrl(QStringLiteral("https://graph.microsoft.com/v1.0/drives/%1/items/%2/permanentDelete").arg(QString::fromLatin1(QUrl::toPercentEncoding(driveId)),QString::fromLatin1(QUrl::toPercentEncoding(id)))));finishOneDriveOperation(m_network.post(req,QByteArray()),QStringLiteral("OneDrive'dan kalıcı olarak silindi."));});
}

QString DirectoryModel::googleFileIdFromUrl(const QString &itemUrl) const
{
    const QUrl url(itemUrl);
    if (url.scheme() != QStringLiteral("gdrive"))
        return {};
    if (url.host() != QStringLiteral("file") && url.host() != QStringLiteral("folder"))
        return {};
    QString id = url.path();
    if (id.startsWith('/'))
        id.remove(0, 1);
    return id;
}

const DirectoryModel::Entry *DirectoryModel::entryForUrl(const QString &itemUrl) const
{
    for (const Entry &entry : m_items) {
        if (entry.url.toString() == itemUrl)
            return &entry;
    }
    return nullptr;
}

QNetworkRequest DirectoryModel::googleRequest(const QUrl &url) const
{
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + m_googleAccessToken.toUtf8());
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Lurviko/1.0.0"));
    return request;
}

QString DirectoryModel::googleApiError(const QByteArray &payload, const QString &fallback) const
{
    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (doc.isObject()) {
        const QJsonObject errorObj = doc.object().value(QStringLiteral("error")).toObject();
        const QString message = errorObj.value(QStringLiteral("message")).toString();
        if (!message.isEmpty())
            return message;
    }
    return fallback;
}

void DirectoryModel::finishGoogleOperation(QNetworkReply *reply, const QString &successMessage, bool refreshAfter)
{
    connect(reply, &QNetworkReply::finished, this, [this, reply, successMessage, refreshAfter]() {
        const QByteArray payload = reply->readAll();
        const auto error = reply->error();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString fallback = reply->errorString();
        reply->deleteLater();

        if (error != QNetworkReply::NoError || status >= 400) {
            const QString message = googleApiError(payload, fallback);
            emit googleOperationFinished(false, message);
            return;
        }

        emit googleOperationFinished(true, successMessage);
        if (refreshAfter)
            refresh();
    });
}

void DirectoryModel::googleCreateFolder(const QString &name)
{
    if (m_location.scheme() != QStringLiteral("gdrive") || m_googleAccessToken.isEmpty()) {
        emit googleOperationFinished(false, QStringLiteral("Google Drive is not connected."));
        return;
    }
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        emit googleOperationFinished(false, QStringLiteral("Folder name cannot be empty."));
        return;
    }

    QUrl url(QStringLiteral("https://www.googleapis.com/drive/v3/files"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("supportsAllDrives"), QStringLiteral("true"));
    query.addQueryItem(QStringLiteral("fields"), QStringLiteral("id,name"));
    url.setQuery(query);

    QJsonObject metadata;
    metadata.insert(QStringLiteral("name"), trimmed);
    metadata.insert(QStringLiteral("mimeType"), QStringLiteral("application/vnd.google-apps.folder"));
    metadata.insert(QStringLiteral("parents"), QJsonArray{m_googleFolderId});

    QNetworkRequest request = googleRequest(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json; charset=UTF-8"));
    auto *reply = m_network.post(request, QJsonDocument(metadata).toJson(QJsonDocument::Compact));
    finishGoogleOperation(reply, QStringLiteral("Folder created."));
}

void DirectoryModel::googleRenameItem(const QString &itemUrl, const QString &newName)
{
    const QString id = googleFileIdFromUrl(itemUrl);
    const QString trimmed = newName.trimmed();
    if (id.isEmpty() || trimmed.isEmpty()) {
        emit googleOperationFinished(false, QStringLiteral("Invalid Google Drive item or name."));
        return;
    }

    QUrl url(QStringLiteral("https://www.googleapis.com/drive/v3/files/%1").arg(id));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("supportsAllDrives"), QStringLiteral("true"));
    query.addQueryItem(QStringLiteral("fields"), QStringLiteral("id,name"));
    url.setQuery(query);

    QJsonObject body;
    body.insert(QStringLiteral("name"), trimmed);
    QNetworkRequest request = googleRequest(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json; charset=UTF-8"));
    auto *reply = m_network.sendCustomRequest(request, QByteArrayLiteral("PATCH"), QJsonDocument(body).toJson(QJsonDocument::Compact));
    finishGoogleOperation(reply, QStringLiteral("Renamed."));
}

void DirectoryModel::googleTrashItem(const QString &itemUrl)
{
    const QString id = googleFileIdFromUrl(itemUrl);
    if (id.isEmpty()) {
        emit googleOperationFinished(false, QStringLiteral("Invalid Google Drive item."));
        return;
    }

    QUrl url(QStringLiteral("https://www.googleapis.com/drive/v3/files/%1").arg(id));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("supportsAllDrives"), QStringLiteral("true"));
    query.addQueryItem(QStringLiteral("fields"), QStringLiteral("id,trashed"));
    url.setQuery(query);

    QJsonObject body;
    body.insert(QStringLiteral("trashed"), true);
    QNetworkRequest request = googleRequest(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json; charset=UTF-8"));
    auto *reply = m_network.sendCustomRequest(request, QByteArrayLiteral("PATCH"), QJsonDocument(body).toJson(QJsonDocument::Compact));
    finishGoogleOperation(reply, QStringLiteral("Moved to Google Drive trash."));
}

void DirectoryModel::googleDeleteItem(const QString &itemUrl)
{
    const QString id = googleFileIdFromUrl(itemUrl);
    if (id.isEmpty()) {
        emit googleOperationFinished(false, QStringLiteral("Invalid Google Drive item."));
        return;
    }

    QUrl url(QStringLiteral("https://www.googleapis.com/drive/v3/files/%1").arg(id));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("supportsAllDrives"), QStringLiteral("true"));
    url.setQuery(query);
    auto *reply = m_network.deleteResource(googleRequest(url));
    finishGoogleOperation(reply, QStringLiteral("Permanently deleted from Google Drive."));
}

void DirectoryModel::googleUploadFile(const QString &localPathOrUrl)
{
    QString localPath = localPathOrUrl;
    const QUrl maybeUrl(localPathOrUrl);
    if (maybeUrl.isLocalFile())
        localPath = maybeUrl.toLocalFile();
    startGoogleResumableUpload(localPath);
}

void DirectoryModel::startGoogleResumableUpload(const QString &localPath)
{
    if (m_location.scheme() != QStringLiteral("gdrive") || m_googleAccessToken.isEmpty()) {
        emit googleOperationFinished(false, QStringLiteral("Open a Google Drive folder before uploading."));
        return;
    }

    QFileInfo info(localPath);
    if (!info.exists() || !info.isFile()) {
        emit googleOperationFinished(false, QStringLiteral("Upload source is not a file."));
        return;
    }

    QMimeDatabase db;
    const QString mimeType = db.mimeTypeForFile(info).name();
    QUrl url(QStringLiteral("https://www.googleapis.com/upload/drive/v3/files"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("uploadType"), QStringLiteral("resumable"));
    query.addQueryItem(QStringLiteral("supportsAllDrives"), QStringLiteral("true"));
    query.addQueryItem(QStringLiteral("fields"), QStringLiteral("id,name,size"));
    url.setQuery(query);

    QJsonObject metadata;
    metadata.insert(QStringLiteral("name"), info.fileName());
    metadata.insert(QStringLiteral("parents"), QJsonArray{m_googleFolderId});

    QNetworkRequest request = googleRequest(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json; charset=UTF-8"));
    request.setRawHeader("X-Upload-Content-Type", mimeType.toUtf8());
    request.setRawHeader("X-Upload-Content-Length", QByteArray::number(info.size()));

    auto *initReply = m_network.post(request, QJsonDocument(metadata).toJson(QJsonDocument::Compact));
    connect(initReply, &QNetworkReply::finished, this, [this, initReply, localPath, mimeType]() {
        const QByteArray payload = initReply->readAll();
        const auto error = initReply->error();
        const QUrl sessionUrl = initReply->header(QNetworkRequest::LocationHeader).toUrl();
        const QString fallback = initReply->errorString();
        initReply->deleteLater();

        if (error != QNetworkReply::NoError || !sessionUrl.isValid()) {
            emit googleOperationFinished(false, googleApiError(payload, fallback));
            return;
        }

        auto *file = new QFile(localPath);
        if (!file->open(QIODevice::ReadOnly)) {
            emit googleOperationFinished(false, file->errorString());
            delete file;
            return;
        }

        QNetworkRequest uploadRequest(sessionUrl);
        uploadRequest.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + m_googleAccessToken.toUtf8());
        uploadRequest.setHeader(QNetworkRequest::ContentTypeHeader, mimeType);
        uploadRequest.setHeader(QNetworkRequest::ContentLengthHeader, file->size());
        if (file->size() > 0) {
            uploadRequest.setRawHeader("Content-Range",
                QStringLiteral("bytes 0-%1/%2").arg(file->size() - 1).arg(file->size()).toUtf8());
        }
        auto *uploadReply = m_network.put(uploadRequest, file);
        file->setParent(uploadReply);
        finishGoogleOperation(uploadReply, QStringLiteral("Uploaded to Google Drive."));
    });
}

void DirectoryModel::googleOpenItem(const QString &itemUrl)
{
    const Entry *entry = entryForUrl(itemUrl);
    if (!entry || entry->isDir)
        return;

    const QString id = googleFileIdFromUrl(itemUrl);
    if (id.isEmpty())
        return;

    // Google-native documents are best opened by Drive itself. Regular files are
    // downloaded to Lurviko's cache and opened with the desktop default application.
    if (entry->mimeType.startsWith(QStringLiteral("application/vnd.google-apps."))) {
        QUrl webUrl(QStringLiteral("https://drive.google.com/open"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("id"), id);
        webUrl.setQuery(query);
        QDesktopServices::openUrl(webUrl);
        return;
    }

    const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                           + QStringLiteral("/google-drive-open");
    QDir().mkpath(cacheDir);
    QString safeName = entry->name;
    safeName.replace('/', '_');
    const QString destination = QDir(cacheDir).filePath(id + QStringLiteral("-") + safeName);

    QUrl url(QStringLiteral("https://www.googleapis.com/drive/v3/files/%1").arg(id));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("alt"), QStringLiteral("media"));
    query.addQueryItem(QStringLiteral("supportsAllDrives"), QStringLiteral("true"));
    url.setQuery(query);

    auto *reply = m_network.get(googleRequest(url));
    auto *file = new QFile(destination, reply);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit googleOperationFinished(false, file->errorString());
        reply->abort();
        reply->deleteLater();
        return;
    }

    connect(reply, &QIODevice::readyRead, this, [reply, file]() {
        file->write(reply->readAll());
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, file, destination]() {
        file->write(reply->readAll());
        file->close();
        const auto error = reply->error();
        const QString fallback = reply->errorString();
        reply->deleteLater();

        if (error != QNetworkReply::NoError) {
            QFile::remove(destination);
            emit googleOperationFinished(false, fallback);
            return;
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(destination));
    });
}

void DirectoryModel::sortEntries()
{
    if (m_items.size() < 2)
        return;

    beginResetModel();
    sortEntriesVector(m_items);
    endResetModel();
    emit countChanged();
}

QString DirectoryModel::formatBytes(qint64 bytes) const
{
    static const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(qMax<qint64>(0, bytes));
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    return QLocale().toString(value, 'f', unit == 0 ? 0 : 1) + " " + units[unit];
}

void DirectoryModel::setLoading(bool value)
{
    if (m_loading == value)
        return;
    m_loading = value;
    emit loadingChanged();
}

void DirectoryModel::setErrorString(const QString &value)
{
    if (m_errorString == value)
        return;
    m_errorString = value;
    emit errorStringChanged();
}
