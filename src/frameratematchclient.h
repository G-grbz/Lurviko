#pragma once

#include <QObject>
#include <QPointer>
#include <QNetworkAccessManager>
#include <QTimer>

class QMediaPlayer;
class QNetworkReply;

class FrameRateMatchClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject *player READ player WRITE setPlayer NOTIFY playerChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(QString output READ output WRITE setOutput NOTIFY outputChanged)

public:
    explicit FrameRateMatchClient(QObject *parent = nullptr);
    ~FrameRateMatchClient() override;
    QObject *player() const;
    bool active() const { return m_active; }
    QString output() const { return m_output; }
    void setPlayer(QObject *player);
    void setActive(bool active);
    void setOutput(const QString &output);

signals:
    void playerChanged();
    void activeChanged();
    void outputChanged();

private:
    void refresh();
    void pump();
    void release();
    QNetworkReply *post(const QString &path, const QByteArray &payload);
    void shutdown();

    QPointer<QMediaPlayer> m_player;
    QNetworkAccessManager m_network;
    QTimer m_heartbeat;
    QString m_session;
    QString m_output;
    QByteArray m_pending;
    bool m_active = false;
    bool m_acquired = false;
    bool m_busy = false;
    bool m_unavailable = false;
};
