#include "musiclibrarymanager.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent>

#include <algorithm>

namespace {
QString tagValue(const QJsonObject &tags, const QStringList &wanted)
{
    for (auto it = tags.constBegin(); it != tags.constEnd(); ++it) {
        const QString key = it.key().trimmed().toLower();
        for (const QString &candidate : wanted) {
            if (key == candidate.toLower())
                return it.value().toVariant().toString().trimmed();
        }
    }
    return {};
}

QString normalizedYear(QString value)
{
    const QRegularExpression yearRx(QStringLiteral(R"((19|20)\d{2})"));
    const auto match = yearRx.match(value);
    return match.hasMatch() ? match.captured(0) : value.trimmed();
}
}

MusicLibraryManager::MusicLibraryManager(QObject *parent)
    : QObject(parent)
{
    m_trackNotifyTimer.setSingleShot(true);
    m_trackNotifyTimer.setInterval(250);
    connect(&m_trackNotifyTimer, &QTimer::timeout, this, &MusicLibraryManager::tracksChanged);
    loadState();
}

int MusicLibraryManager::favoriteCount() const
{
    int count = 0;
    for (const QVariant &value : m_tracks) {
        if (value.toMap().value(QStringLiteral("favorite")).toBool())
            ++count;
    }
    return count;
}

QString MusicLibraryManager::localPathForUrl(const QString &url)
{
    const QUrl parsed(url);
    if (parsed.isLocalFile())
        return parsed.toLocalFile();
    if (parsed.scheme().isEmpty())
        return url;
    return {};
}

QString MusicLibraryManager::normalizedUrl(const QVariantMap &item)
{
    return item.value(QStringLiteral("itemUrl")).toString().trimmed();
}

QString MusicLibraryManager::cacheKeyForItem(const QVariantMap &item)
{
    const QString url = normalizedUrl(item);
    const qint64 modified = item.value(QStringLiteral("modifiedMs")).toLongLong();
    const qint64 size = item.value(QStringLiteral("size")).toLongLong();
    return url + QLatin1Char('|') + QString::number(modified) + QLatin1Char('|') + QString::number(size);
}

QVariantMap MusicLibraryManager::fallbackMetadata(const QVariantMap &item)
{
    QVariantMap result;
    QString fileName = item.value(QStringLiteral("name")).toString().trimmed();
    const QString localPath = localPathForUrl(normalizedUrl(item));
    if (!localPath.isEmpty())
        fileName = QFileInfo(localPath).completeBaseName();
    else if (fileName.contains(QLatin1Char('.')))
        fileName = QFileInfo(fileName).completeBaseName();

    QString artist;
    QString title = fileName;
    const int split = fileName.indexOf(QStringLiteral(" - "));
    if (split > 0) {
        artist = fileName.left(split).trimmed();
        title = fileName.mid(split + 3).trimmed();
    }
    result.insert(QStringLiteral("title"), title);
    result.insert(QStringLiteral("artist"), artist);
    result.insert(QStringLiteral("album"), QString());
    result.insert(QStringLiteral("year"), QString());
    result.insert(QStringLiteral("genre"), QString());
    return result;
}

QVariantMap MusicLibraryManager::mapFromCached(const CachedMeta &cached)
{
    QVariantMap metadata;
    metadata.insert(QStringLiteral("title"), cached.title);
    metadata.insert(QStringLiteral("artist"), cached.artist);
    metadata.insert(QStringLiteral("album"), cached.album);
    metadata.insert(QStringLiteral("year"), cached.year);
    metadata.insert(QStringLiteral("genre"), cached.genre);
    return metadata;
}

QVariantMap MusicLibraryManager::probeMetadata(const QVariantMap &item)
{
    QVariantMap result = fallbackMetadata(item);
    const QString path = localPathForUrl(normalizedUrl(item));
    if (path.isEmpty() || !QFileInfo::exists(path))
        return result;

    QProcess process;
    process.start(QStringLiteral("ffprobe"), {
        QStringLiteral("-v"), QStringLiteral("quiet"),
        QStringLiteral("-print_format"), QStringLiteral("json"),
        QStringLiteral("-show_entries"),
        QStringLiteral("format_tags=title,artist,album,album_artist,albumartist,date,year,genre"),
        path
    });
    if (!process.waitForStarted(1000) || !process.waitForFinished(2800)) {
        process.kill();
        return result;
    }

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(process.readAllStandardOutput(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return result;

    const QJsonObject tags = doc.object().value(QStringLiteral("format")).toObject()
                                     .value(QStringLiteral("tags")).toObject();
    const QString title = tagValue(tags, {QStringLiteral("title")});
    const QString artist = tagValue(tags, {QStringLiteral("artist"), QStringLiteral("album_artist"), QStringLiteral("albumartist")});
    const QString album = tagValue(tags, {QStringLiteral("album")});
    const QString year = normalizedYear(tagValue(tags, {QStringLiteral("date"), QStringLiteral("year")}));
    const QString genre = tagValue(tags, {QStringLiteral("genre")});

    if (!title.isEmpty()) result.insert(QStringLiteral("title"), title);
    if (!artist.isEmpty()) result.insert(QStringLiteral("artist"), artist);
    if (!album.isEmpty()) result.insert(QStringLiteral("album"), album);
    if (!year.isEmpty()) result.insert(QStringLiteral("year"), year);
    if (!genre.isEmpty()) result.insert(QStringLiteral("genre"), genre);
    return result;
}

QVariantMap MusicLibraryManager::mergedTrack(const QVariantMap &item, const QVariantMap &metadata,
                                             const QSet<QString> &favorites,
                                             const QHash<QString, QVariantMap> &history)
{
    QVariantMap track = item;
    const QString url = normalizedUrl(item);
    for (auto it = metadata.constBegin(); it != metadata.constEnd(); ++it)
        track.insert(it.key(), it.value());
    track.insert(QStringLiteral("itemUrl"), url);
    track.insert(QStringLiteral("favorite"), favorites.contains(url));
    const QVariantMap playback = history.value(url);
    track.insert(QStringLiteral("playCount"), playback.value(QStringLiteral("playCount"), 0));
    track.insert(QStringLiteral("lastPlayed"), playback.value(QStringLiteral("lastPlayed"), 0));
    return track;
}

void MusicLibraryManager::setItems(const QVariantList &items)
{
    QByteArray signatureData;
    QVariantList normalizedItems;
    normalizedItems.reserve(items.size());
    for (const QVariant &value : items) {
        const QVariantMap item = value.toMap();
        const QString url = normalizedUrl(item);
        if (url.isEmpty())
            continue;
        signatureData += cacheKeyForItem(item).toUtf8();
        signatureData += '\n';
        normalizedItems.push_back(item);
    }

    const QString signature = QString::fromLatin1(
        QCryptographicHash::hash(signatureData, QCryptographicHash::Sha1).toHex());
    if (signature == m_sourceSignature && !m_tracks.isEmpty())
        return;

    m_sourceSignature = signature;
    m_trackNotifyTimer.stop();
    ++m_generation;
    m_scanQueue.clear();
    // The previous worker must finish before another generation starts.
    setScanPaused(false);

    QVariantList immediate;
    immediate.reserve(normalizedItems.size());
    int cachedCount = 0;
    QSet<QString> liveKeys;
    for (const QVariant &value : normalizedItems) {
        const QVariantMap item = value.toMap();
        const QString key = cacheKeyForItem(item);
        liveKeys.insert(key);
        QVariantMap metadata;
        if (m_metadataCache.contains(key)) {
            metadata = mapFromCached(m_metadataCache.value(key));
            ++cachedCount;
        } else {
            metadata = fallbackMetadata(item);
            m_scanQueue.push_back(item);
        }
        immediate.push_back(mergedTrack(item, metadata, m_favorites, m_history));
    }

    m_tracks = immediate;
    for (auto it = m_metadataCache.begin(); it != m_metadataCache.end();) {
        if (!liveKeys.contains(it.key())) it = m_metadataCache.erase(it);
        else ++it;
    }
    setScanProgress(normalizedItems.size(), cachedCount);
    emit tracksChanged();

    if (m_scanQueue.isEmpty()) {
        setLoading(false);
        return;
    }

    setLoading(true);
    startNextMetadataScan();
}

void MusicLibraryManager::startNextMetadataScan()
{
    if (!m_loading || m_scanPaused || m_scanWorkerActive)
        return;
    if (m_scanQueue.isEmpty()) {
        saveState();
        setScanPaused(false);
        setLoading(false);
        return;
    }

    const QVariantMap item = m_scanQueue.takeFirst();
    const quint64 generation = m_generation;
    m_scanWorkerActive = true;

    auto *watcher = new QFutureWatcher<QVariantMap>(this);
    connect(watcher, &QFutureWatcher<QVariantMap>::finished, this,
            [this, watcher, item, generation] {
        const QVariantMap metadata = watcher->result();
        watcher->deleteLater();
        m_scanWorkerActive = false;
        if (generation != m_generation) {
            startNextMetadataScan();
            return;
        }
        storeMetadata(item, metadata);
        setScanProgress(m_scanTotal, qMin(m_scanTotal, m_scanCompleted + 1));
        // Publishing the whole library for every result repeatedly rebuilds
        // thousands of JS objects. Coalesce updates while retaining live scan
        // progress and publish the final result immediately.
        if (m_scanQueue.isEmpty()) {
            m_trackNotifyTimer.stop();
            emit tracksChanged();
        } else if (!m_trackNotifyTimer.isActive()) {
            m_trackNotifyTimer.start();
        }

        if ((m_scanCompleted % 25) == 0 || m_scanQueue.isEmpty())
            saveState();

        if (m_scanQueue.isEmpty()) {
            setScanPaused(false);
            setLoading(false);
            return;
        }
        if (!m_scanPaused)
            QTimer::singleShot(0, this, &MusicLibraryManager::startNextMetadataScan);
    });

    watcher->setFuture(QtConcurrent::run([item] {
        return MusicLibraryManager::probeMetadata(item);
    }));
}

void MusicLibraryManager::storeMetadata(const QVariantMap &item, const QVariantMap &metadata)
{
    const QString key = cacheKeyForItem(item);
    CachedMeta cached;
    cached.modifiedMs = item.value(QStringLiteral("modifiedMs")).toLongLong();
    cached.size = item.value(QStringLiteral("size")).toLongLong();
    cached.title = metadata.value(QStringLiteral("title")).toString();
    cached.artist = metadata.value(QStringLiteral("artist")).toString();
    cached.album = metadata.value(QStringLiteral("album")).toString();
    cached.year = metadata.value(QStringLiteral("year")).toString();
    cached.genre = metadata.value(QStringLiteral("genre")).toString();
    m_metadataCache.insert(key, cached);

    const QString url = normalizedUrl(item);
    for (QVariant &value : m_tracks) {
        const QVariantMap existing = value.toMap();
        if (existing.value(QStringLiteral("itemUrl")).toString() != url)
            continue;
        value = mergedTrack(item, metadata, m_favorites, m_history);
        break;
    }
}

void MusicLibraryManager::pauseMetadataScan()
{
    if (!m_loading)
        return;
    setScanPaused(true);
    saveState();
}

void MusicLibraryManager::resumeMetadataScan()
{
    if (!m_loading)
        return;
    setScanPaused(false);
    startNextMetadataScan();
}

bool MusicLibraryManager::isFavorite(const QString &url) const
{
    return m_favorites.contains(url.trimmed());
}

void MusicLibraryManager::toggleFavorite(const QString &url)
{
    const QString key = url.trimmed();
    if (key.isEmpty())
        return;
    if (m_favorites.contains(key))
        m_favorites.remove(key);
    else
        m_favorites.insert(key);

    for (QVariant &value : m_tracks) {
        QVariantMap track = value.toMap();
        if (track.value(QStringLiteral("itemUrl")).toString() == key) {
            track.insert(QStringLiteral("favorite"), m_favorites.contains(key));
            value = track;
        }
    }
    saveState();
    emit tracksChanged();
}

void MusicLibraryManager::recordPlayed(const QString &url)
{
    const QString key = url.trimmed();
    if (key.isEmpty())
        return;
    QVariantMap playback = m_history.value(key);
    playback.insert(QStringLiteral("playCount"), playback.value(QStringLiteral("playCount"), 0).toInt() + 1);
    playback.insert(QStringLiteral("lastPlayed"), QDateTime::currentMSecsSinceEpoch());
    m_history.insert(key, playback);

    for (QVariant &value : m_tracks) {
        QVariantMap track = value.toMap();
        if (track.value(QStringLiteral("itemUrl")).toString() == key) {
            track.insert(QStringLiteral("playCount"), playback.value(QStringLiteral("playCount")));
            track.insert(QStringLiteral("lastPlayed"), playback.value(QStringLiteral("lastPlayed")));
            value = track;
            break;
        }
    }
    saveState();
    emit tracksChanged();
}

QString MusicLibraryManager::statePath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return QDir(dir).filePath(QStringLiteral("music-library.json"));
}

void MusicLibraryManager::loadState()
{
    QFile file(statePath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return;
    const QJsonObject root = doc.object();
    for (const QJsonValue &value : root.value(QStringLiteral("favorites")).toArray())
        m_favorites.insert(value.toString());

    const QJsonObject history = root.value(QStringLiteral("history")).toObject();
    for (auto it = history.constBegin(); it != history.constEnd(); ++it)
        m_history.insert(it.key(), it.value().toObject().toVariantMap());

    const QJsonObject metadata = root.value(QStringLiteral("metadata")).toObject();
    for (auto it = metadata.constBegin(); it != metadata.constEnd(); ++it) {
        const QJsonObject obj = it.value().toObject();
        CachedMeta cached;
        cached.modifiedMs = obj.value(QStringLiteral("modifiedMs")).toVariant().toLongLong();
        cached.size = obj.value(QStringLiteral("size")).toVariant().toLongLong();
        cached.title = obj.value(QStringLiteral("title")).toString();
        cached.artist = obj.value(QStringLiteral("artist")).toString();
        cached.album = obj.value(QStringLiteral("album")).toString();
        cached.year = obj.value(QStringLiteral("year")).toString();
        cached.genre = obj.value(QStringLiteral("genre")).toString();
        m_metadataCache.insert(it.key(), cached);
    }
}

void MusicLibraryManager::saveState() const
{
    QJsonObject root;
    QJsonArray favorites;
    QStringList sortedFavorites = m_favorites.values();
    std::sort(sortedFavorites.begin(), sortedFavorites.end());
    for (const QString &url : sortedFavorites)
        favorites.append(url);
    root.insert(QStringLiteral("favorites"), favorites);

    QJsonObject history;
    for (auto it = m_history.constBegin(); it != m_history.constEnd(); ++it)
        history.insert(it.key(), QJsonObject::fromVariantMap(it.value()));
    root.insert(QStringLiteral("history"), history);

    QJsonObject metadata;
    for (auto it = m_metadataCache.constBegin(); it != m_metadataCache.constEnd(); ++it) {
        QJsonObject obj;
        obj.insert(QStringLiteral("modifiedMs"), QString::number(it.value().modifiedMs));
        obj.insert(QStringLiteral("size"), QString::number(it.value().size));
        obj.insert(QStringLiteral("title"), it.value().title);
        obj.insert(QStringLiteral("artist"), it.value().artist);
        obj.insert(QStringLiteral("album"), it.value().album);
        obj.insert(QStringLiteral("year"), it.value().year);
        obj.insert(QStringLiteral("genre"), it.value().genre);
        metadata.insert(it.key(), obj);
    }
    root.insert(QStringLiteral("metadata"), metadata);

    QSaveFile file(statePath());
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    file.commit();
}

void MusicLibraryManager::setLoading(bool loading)
{
    if (m_loading == loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

void MusicLibraryManager::setScanPaused(bool paused)
{
    if (m_scanPaused == paused)
        return;
    m_scanPaused = paused;
    emit scanPausedChanged();
}

void MusicLibraryManager::setScanProgress(int total, int completed)
{
    total = qMax(0, total);
    completed = qBound(0, completed, total);
    if (m_scanTotal == total && m_scanCompleted == completed)
        return;
    m_scanTotal = total;
    m_scanCompleted = completed;
    emit scanProgressChanged();
}
