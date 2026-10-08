#include "mpriscontroller.h"
#include "thumbnailprovider.h"

#include <QCryptographicHash>
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QRegularExpression>
#include <QFileInfo>
#include <QUrl>
#include <QtGlobal>

namespace {
constexpr auto kObjectPath = "/org/mpris/MediaPlayer2";
constexpr auto kRootInterface = "org.mpris.MediaPlayer2";
constexpr auto kPlayerInterface = "org.mpris.MediaPlayer2.Player";
constexpr auto kServiceName = "org.mpris.MediaPlayer2.lurviko";

QVariantMap changedMap(const char *name, const QVariant &value)
{
    QVariantMap map;
    map.insert(QString::fromLatin1(name), value);
    return map;
}
}

class MprisRootAdaptor final : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool CanQuit READ canQuit CONSTANT)
    Q_PROPERTY(bool Fullscreen READ fullscreen CONSTANT)
    Q_PROPERTY(bool CanSetFullscreen READ canSetFullscreen CONSTANT)
    Q_PROPERTY(bool CanRaise READ canRaise CONSTANT)
    Q_PROPERTY(bool HasTrackList READ hasTrackList CONSTANT)
    Q_PROPERTY(QString Identity READ identity CONSTANT)
    Q_PROPERTY(QString DesktopEntry READ desktopEntry CONSTANT)
    Q_PROPERTY(QStringList SupportedUriSchemes READ supportedUriSchemes CONSTANT)
    Q_PROPERTY(QStringList SupportedMimeTypes READ supportedMimeTypes CONSTANT)
public:
    explicit MprisRootAdaptor(MprisController *parent) : QDBusAbstractAdaptor(parent), m_controller(parent) {}
    bool canQuit() const { return true; }
    bool fullscreen() const { return false; }
    bool canSetFullscreen() const { return false; }
    bool canRaise() const { return true; }
    bool hasTrackList() const { return false; }
    QString identity() const { return QStringLiteral("Lurviko"); }
    QString desktopEntry() const { return QStringLiteral("lurviko"); }
    QStringList supportedUriSchemes() const { return {QStringLiteral("file"), QStringLiteral("http"), QStringLiteral("https")}; }
    QStringList supportedMimeTypes() const {
        return {QStringLiteral("audio/mpeg"), QStringLiteral("audio/flac"), QStringLiteral("audio/ogg"),
                QStringLiteral("audio/mp4"), QStringLiteral("audio/x-wav"), QStringLiteral("audio/aac"),
                QStringLiteral("video/mp4"), QStringLiteral("video/x-matroska"), QStringLiteral("video/webm"),
                QStringLiteral("video/quicktime"), QStringLiteral("video/x-msvideo")};
    }
public slots:
    void Raise() { emit m_controller->raiseRequested(); }
    void Quit() { emit m_controller->quitRequested(); }
private:
    MprisController *m_controller;
};

class MprisPlayerAdaptor final : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(QString LoopStatus READ loopStatus WRITE setLoopStatus)
    Q_PROPERTY(double Rate READ rate CONSTANT)
    Q_PROPERTY(bool Shuffle READ shuffle WRITE setShuffle)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(double MinimumRate READ minimumRate CONSTANT)
    Q_PROPERTY(double MaximumRate READ maximumRate CONSTANT)
    Q_PROPERTY(bool CanGoNext READ canGoNext)
    Q_PROPERTY(bool CanGoPrevious READ canGoPrevious)
    Q_PROPERTY(bool CanPlay READ canPlay)
    Q_PROPERTY(bool CanPause READ canPause)
    Q_PROPERTY(bool CanSeek READ canSeek)
    Q_PROPERTY(bool CanControl READ canControl CONSTANT)
public:
    explicit MprisPlayerAdaptor(MprisController *parent) : QDBusAbstractAdaptor(parent), m_controller(parent) {}
    QString playbackStatus() const { return m_controller->playbackStatus(); }
    QString loopStatus() const { return m_controller->loopStatus(); }
    double rate() const { return 1.0; }
    bool shuffle() const { return m_controller->shuffle(); }
    QVariantMap metadata() const { return m_controller->metadata(); }
    double volume() const { return m_controller->volume(); }
    qlonglong position() const { return qlonglong(m_controller->position()) * 1000LL; }
    double minimumRate() const { return 1.0; }
    double maximumRate() const { return 1.0; }
    bool canGoNext() const { return m_controller->active(); }
    bool canGoPrevious() const { return m_controller->active(); }
    bool canPlay() const { return m_controller->active(); }
    bool canPause() const { return m_controller->active(); }
    bool canSeek() const { return m_controller->active() && m_controller->duration() > 0; }
    bool canControl() const { return true; }

    void setLoopStatus(const QString &status) {
        int mode = 0;
        if (status == QStringLiteral("Playlist")) mode = 1;
        else if (status == QStringLiteral("Track")) mode = 2;
        emit m_controller->repeatModeRequested(mode);
    }
    void setShuffle(bool enabled) { emit m_controller->shuffleRequested(enabled); }
    void setVolume(double value) { emit m_controller->volumeRequested(qBound(0.0, value, 1.0)); }

public slots:
    void Next() { emit m_controller->nextRequested(); }
    void Previous() { emit m_controller->previousRequested(); }
    void Pause() { emit m_controller->pauseRequested(); }
    void PlayPause() { emit m_controller->playPauseRequested(); }
    void Stop() { emit m_controller->stopRequested(); }
    void Play() { emit m_controller->playRequested(); }
    void Seek(qlonglong offset) { emit m_controller->seekRequested(offset / 1000LL); }
    void SetPosition(const QDBusObjectPath &track, qlonglong position) {
        if (!canSeek() || track.path() != m_controller->trackObjectPath()
            || position < 0 || position / 1000LL > m_controller->duration()) return;
        const qint64 ms = position / 1000LL;
        emit m_controller->setPositionRequested(ms);
        m_controller->emitSeeked(ms);
    }
    void OpenUri(const QString &) {}

signals:
    void Seeked(qlonglong Position);
private:
    MprisController *m_controller;
};

MprisController::MprisController(QObject *parent)
    : QObject(parent)
{
    new MprisRootAdaptor(this);
    new MprisPlayerAdaptor(this);

    auto bus = QDBusConnection::sessionBus();
    bus.registerObject(QString::fromLatin1(kObjectPath), this, QDBusConnection::ExportAdaptors);
    if (!bus.registerService(QString::fromLatin1(kServiceName)))
        qWarning("Lurviko MPRIS service could not be registered");
}

MprisController::~MprisController()
{
    cancelArtworkRequest();
    auto bus = QDBusConnection::sessionBus();
    bus.unregisterObject(QString::fromLatin1(kObjectPath));
    bus.unregisterService(QString::fromLatin1(kServiceName));
}

void MprisController::setActive(bool value)
{
    if (m_active == value) return;
    m_active = value;
    emit activeChanged();
    QVariantMap changed;
    changed.insert(QStringLiteral("CanGoNext"), value);
    changed.insert(QStringLiteral("CanGoPrevious"), value);
    changed.insert(QStringLiteral("CanPlay"), value);
    changed.insert(QStringLiteral("CanPause"), value);
    changed.insert(QStringLiteral("CanSeek"), value && m_durationMs > 0);
    changed.insert(QStringLiteral("Metadata"), metadata());
    emitPlayerProperties(changed);
}

void MprisController::setPlaybackStatus(const QString &value)
{
    const QString normalized = value == QStringLiteral("Playing") || value == QStringLiteral("Paused")
        ? value : QStringLiteral("Stopped");
    if (m_playbackStatus == normalized) return;
    m_playbackStatus = normalized;
    emit playbackStatusChanged();
    emitPlayerProperties(changedMap("PlaybackStatus", m_playbackStatus));
}

void MprisController::setTrackMetadata(const QVariantMap &value)
{
    if (m_trackMetadata == value) return;
    m_trackMetadata = value;
    emit metadataChanged();
    emitPlayerProperties(changedMap("Metadata", metadata()));
}

void MprisController::setTitle(const QString &value)
{
    if (m_title == value) return;
    m_title = value;
    emit metadataChanged();
    emitPlayerProperties(changedMap("Metadata", metadata()));
}

void MprisController::setTrackUrl(const QString &value)
{
    if (m_trackUrl == value) return;
    cancelArtworkRequest();
    m_trackUrl = value;
    m_artUrl.clear();
    refreshArtwork();
    emit metadataChanged();
    emitPlayerProperties(changedMap("Metadata", metadata()));
}

void MprisController::setArtworkHint(const QString &value)
{
    if (m_artworkHint == value) return;
    m_artworkHint = value;
    refreshArtwork();
    emit metadataChanged();
    emitPlayerProperties(changedMap("Metadata", metadata()));
}

void MprisController::setDuration(qint64 value)
{
    value = qMax<qint64>(0, value);
    if (m_durationMs == value) return;
    m_durationMs = value;
    emit metadataChanged();
    QVariantMap changed;
    changed.insert(QStringLiteral("Metadata"), metadata());
    changed.insert(QStringLiteral("CanSeek"), m_active && m_durationMs > 0);
    emitPlayerProperties(changed);
}

void MprisController::setPosition(qint64 value)
{
    value = qMax<qint64>(0, value);
    if (m_positionMs == value) return;
    m_positionMs = value;
    emit positionChanged();
}

void MprisController::setVolume(double value)
{
    value = qBound(0.0, value, 1.0);
    if (qFuzzyCompare(m_volume, value)) return;
    m_volume = value;
    emit volumeChanged();
    emitPlayerProperties(changedMap("Volume", m_volume));
}

void MprisController::setShuffle(bool value)
{
    if (m_shuffle == value) return;
    m_shuffle = value;
    emit shuffleChanged();
    emitPlayerProperties(changedMap("Shuffle", m_shuffle));
}

void MprisController::setRepeatMode(int value)
{
    value = qBound(0, value, 2);
    if (m_repeatMode == value) return;
    m_repeatMode = value;
    emit repeatModeChanged();
    emitPlayerProperties(changedMap("LoopStatus", loopStatus()));
}

QString MprisController::loopStatus() const
{
    if (m_repeatMode == 2) return QStringLiteral("Track");
    if (m_repeatMode == 1) return QStringLiteral("Playlist");
    return QStringLiteral("None");
}

QString MprisController::trackObjectPath() const
{
    QByteArray digest = QCryptographicHash::hash(m_trackUrl.toUtf8(), QCryptographicHash::Sha1).toHex().left(20);
    if (digest.isEmpty()) digest = QByteArrayLiteral("none");
    return QStringLiteral("/org/mpris/MediaPlayer2/Track/%1").arg(QString::fromLatin1(digest));
}

QVariantMap MprisController::metadata() const
{
    QVariantMap result;
    if (!m_active || m_trackUrl.isEmpty()) {
        result.insert(QStringLiteral("mpris:trackid"),
                      QVariant::fromValue(QDBusObjectPath(QStringLiteral("/org/mpris/MediaPlayer2/TrackList/NoTrack"))));
        return result;
    }

    result.insert(QStringLiteral("mpris:trackid"), QVariant::fromValue(QDBusObjectPath(trackObjectPath())));
    QString title = m_trackMetadata.value(QStringLiteral("title")).toString().trimmed();
    QString artist = m_trackMetadata.value(QStringLiteral("artist")).toString().trimmed();
    if (title.isEmpty()) {
        // Strip an extension only from a filename, never from a tagged title.
        title = m_title.trimmed();
        const QString fileName = QFileInfo(QUrl(m_trackUrl).path()).fileName();
        if (title.isEmpty()) title = fileName;
        if (title == fileName) title = QFileInfo(title).completeBaseName();
        const int separator = title.indexOf(QStringLiteral(" - "));
        if (separator > 0) {
            if (artist.isEmpty()) artist = title.left(separator).trimmed();
            title = title.mid(separator + 3).trimmed();
        }
    }
    result.insert(QStringLiteral("xesam:title"), title);
    if (!artist.isEmpty()) result.insert(QStringLiteral("xesam:artist"), QStringList{artist});
    const QString album = m_trackMetadata.value(QStringLiteral("album")).toString().trimmed();
    if (!album.isEmpty()) result.insert(QStringLiteral("xesam:album"), album);
    const QVariant genreValue = m_trackMetadata.value(QStringLiteral("genre"));
    QStringList genres;
    if (genreValue.metaType().id() == QMetaType::QStringList) {
        genres = genreValue.toStringList();
    } else if (genreValue.metaType().id() == QMetaType::QVariantList) {
        for (const QVariant &genre : genreValue.toList()) genres.append(genre.toString().trimmed());
    } else {
        genres.append(genreValue.toString().trimmed());
    }
    genres.removeAll(QString());
    if (!genres.isEmpty()) result.insert(QStringLiteral("xesam:genre"), genres);
    const QString year = m_trackMetadata.value(QStringLiteral("year")).toString();
    const auto yearMatch = QRegularExpression(QStringLiteral(R"(\b(\d{4})\b)")).match(year);
    if (yearMatch.hasMatch())
        result.insert(QStringLiteral("xesam:contentCreated"), yearMatch.captured(1) + QStringLiteral("-01-01T00:00:00Z"));
    result.insert(QStringLiteral("xesam:url"), QUrl(m_trackUrl).toString(QUrl::FullyEncoded));
    if (m_durationMs > 0) result.insert(QStringLiteral("mpris:length"), QVariant::fromValue<qlonglong>(m_durationMs * 1000LL));
    if (!m_artUrl.isEmpty()) result.insert(QStringLiteral("mpris:artUrl"), m_artUrl);
    return result;
}

void MprisController::cancelArtworkRequest()
{
    if (m_artworkResponse) {
        m_artworkResponse->cancel();
        m_artworkResponse->deleteLater();
        m_artworkResponse = nullptr;
    }
    m_artworkRequestUrl.clear();
}

void MprisController::refreshArtwork()
{
    const QUrl hint(m_artworkHint);
    if ((hint.isLocalFile() && QFileInfo::exists(hint.toLocalFile()))
        || hint.scheme() == QStringLiteral("http") || hint.scheme() == QStringLiteral("https")) {
        m_artUrl = hint.toString(QUrl::FullyEncoded);
        cancelArtworkRequest();
        return;
    }
    const QUrl track(m_trackUrl);
    if (!track.isLocalFile()) { m_artUrl.clear(); return; }
    const QString path = track.toLocalFile();
    if (!QFileInfo(path).isFile()) { m_artUrl.clear(); return; }
    const QString cached = ThumbnailProvider::cachedFilePath(path);
    if (QFileInfo::exists(cached)) {
        m_artUrl = QUrl::fromLocalFile(cached).toString(QUrl::FullyEncoded);
        return;
    }
    m_artUrl.clear();
    // A playlist may not display a thumbnail delegate. Generate its cover once,
    // asynchronously, using the same bounded worker pool and disk cache.
    if (m_artworkRequestUrl == m_trackUrl) return;
    cancelArtworkRequest();
    m_artworkRequestUrl = m_trackUrl;
    ThumbnailProvider provider;
    auto *response = provider.requestImageResponse(
        QString::fromLatin1(QUrl::toPercentEncoding(path)) + QStringLiteral("|mpris"), QSize(256, 256));
    m_artworkResponse = response;
    const QString requestedUrl = m_trackUrl;
    connect(response, &QQuickImageResponse::finished, this, [this, response, requestedUrl, cached]() {
        if (m_artworkResponse != response || m_trackUrl != requestedUrl) return;
        m_artworkResponse = nullptr;
        response->deleteLater();
        if (!QFileInfo::exists(cached)) return;
        m_artUrl = QUrl::fromLocalFile(cached).toString(QUrl::FullyEncoded);
        emit metadataChanged();
        emitPlayerProperties(changedMap("Metadata", metadata()));
    });
}

void MprisController::emitRootProperties(const QVariantMap &changed)
{
    QDBusMessage message = QDBusMessage::createSignal(QString::fromLatin1(kObjectPath),
                                                       QStringLiteral("org.freedesktop.DBus.Properties"),
                                                       QStringLiteral("PropertiesChanged"));
    message << QString::fromLatin1(kRootInterface) << changed << QStringList{};
    QDBusConnection::sessionBus().send(message);
}

void MprisController::emitPlayerProperties(const QVariantMap &changed)
{
    if (changed.isEmpty()) return;
    QDBusMessage message = QDBusMessage::createSignal(QString::fromLatin1(kObjectPath),
                                                       QStringLiteral("org.freedesktop.DBus.Properties"),
                                                       QStringLiteral("PropertiesChanged"));
    message << QString::fromLatin1(kPlayerInterface) << changed << QStringList{};
    QDBusConnection::sessionBus().send(message);
}

void MprisController::emitSeeked(qint64 positionMs)
{
    QDBusMessage message = QDBusMessage::createSignal(QString::fromLatin1(kObjectPath),
                                                       QString::fromLatin1(kPlayerInterface),
                                                       QStringLiteral("Seeked"));
    message << qlonglong(qMax<qint64>(0, positionMs) * 1000LL);
    QDBusConnection::sessionBus().send(message);
}

#include "mpriscontroller.moc"
