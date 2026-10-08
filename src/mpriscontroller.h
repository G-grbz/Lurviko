#pragma once

#include <QObject>
#include <QVariantMap>
#include <QPointer>

class QQuickImageResponse;

class MprisController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(QString playbackStatus READ playbackStatus WRITE setPlaybackStatus NOTIFY playbackStatusChanged)
    Q_PROPERTY(QVariantMap trackMetadata READ trackMetadata WRITE setTrackMetadata NOTIFY metadataChanged)
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY metadataChanged)
    Q_PROPERTY(QString trackUrl READ trackUrl WRITE setTrackUrl NOTIFY metadataChanged)
    Q_PROPERTY(QString artworkHint READ artworkHint WRITE setArtworkHint NOTIFY metadataChanged)
    Q_PROPERTY(qint64 duration READ duration WRITE setDuration NOTIFY metadataChanged)
    Q_PROPERTY(qint64 position READ position WRITE setPosition NOTIFY positionChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool shuffle READ shuffle WRITE setShuffle NOTIFY shuffleChanged)
    Q_PROPERTY(int repeatMode READ repeatMode WRITE setRepeatMode NOTIFY repeatModeChanged)

public:
    explicit MprisController(QObject *parent = nullptr);
    ~MprisController() override;

    bool active() const { return m_active; }
    QString playbackStatus() const { return m_playbackStatus; }
    QVariantMap trackMetadata() const { return m_trackMetadata; }
    QString title() const { return m_title; }
    QString trackUrl() const { return m_trackUrl; }
    QString artworkHint() const { return m_artworkHint; }
    qint64 duration() const { return m_durationMs; }
    qint64 position() const { return m_positionMs; }
    double volume() const { return m_volume; }
    bool shuffle() const { return m_shuffle; }
    int repeatMode() const { return m_repeatMode; }

    void setActive(bool value);
    void setPlaybackStatus(const QString &value);
    void setTrackMetadata(const QVariantMap &value);
    void setTitle(const QString &value);
    void setTrackUrl(const QString &value);
    void setArtworkHint(const QString &value);
    void setDuration(qint64 value);
    void setPosition(qint64 value);
    void setVolume(double value);
    void setShuffle(bool value);
    void setRepeatMode(int value);

    QVariantMap metadata() const;
    QString loopStatus() const;
    QString artUrl() const { return m_artUrl; }

    void emitSeeked(qint64 positionMs);

signals:
    void activeChanged();
    void playbackStatusChanged();
    void metadataChanged();
    void positionChanged();
    void volumeChanged();
    void shuffleChanged();
    void repeatModeChanged();

    void playRequested();
    void pauseRequested();
    void playPauseRequested();
    void stopRequested();
    void nextRequested();
    void previousRequested();
    void seekRequested(qint64 offsetMs);
    void setPositionRequested(qint64 positionMs);
    void volumeRequested(double volume);
    void shuffleRequested(bool enabled);
    void repeatModeRequested(int repeatMode);
    void raiseRequested();
    void quitRequested();

private:
    friend class MprisRootAdaptor;
    friend class MprisPlayerAdaptor;

    void emitRootProperties(const QVariantMap &changed);
    void emitPlayerProperties(const QVariantMap &changed);
    void refreshArtwork();
    void cancelArtworkRequest();
    QString trackObjectPath() const;

    bool m_active = false;
    QString m_playbackStatus = QStringLiteral("Stopped");
    QVariantMap m_trackMetadata;
    QString m_title;
    QString m_trackUrl;
    QString m_artworkHint;
    QString m_artUrl;
    qint64 m_durationMs = 0;
    qint64 m_positionMs = 0;
    double m_volume = 0.82;
    bool m_shuffle = false;
    int m_repeatMode = 0;
    QPointer<QQuickImageResponse> m_artworkResponse;
    QString m_artworkRequestUrl;
};
