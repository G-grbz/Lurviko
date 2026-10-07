#include "frameratematchclient.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QUuid>
#include <cmath>

FrameRateMatchClient::FrameRateMatchClient(QObject *parent)
    : QObject(parent), m_session(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    m_heartbeat.setInterval(5000);
    connect(&m_heartbeat, &QTimer::timeout, this, &FrameRateMatchClient::refresh);
    connect(qApp, &QCoreApplication::aboutToQuit, this, &FrameRateMatchClient::shutdown);
}

FrameRateMatchClient::~FrameRateMatchClient()
{
    // Normal popup close releases asynchronously; a process crash is covered
    // by the helper's lease timeout even if the event loop is already gone.
    m_heartbeat.stop();
}

QObject *FrameRateMatchClient::player() const { return m_player; }

void FrameRateMatchClient::setPlayer(QObject *value)
{
    auto *player = qobject_cast<QMediaPlayer *>(value);
    if (m_player == player) return;
    if (m_player) disconnect(m_player, nullptr, this, nullptr);
    release();
    m_player = player;
    if (m_player) {
        connect(m_player, &QMediaPlayer::metaDataChanged, this, &FrameRateMatchClient::refresh);
        connect(m_player, &QMediaPlayer::playbackStateChanged, this, &FrameRateMatchClient::refresh);
        connect(m_player, &QMediaPlayer::playbackRateChanged, this, &FrameRateMatchClient::refresh);
        connect(m_player, &QMediaPlayer::errorChanged, this, &FrameRateMatchClient::refresh);
        connect(m_player, &QObject::destroyed, this, [this] { release(); });
    }
    emit playerChanged();
    refresh();
}

void FrameRateMatchClient::setActive(bool value)
{
    if (m_active == value) return;
    m_active = value;
    // Try again when opening a new player session, but not on every heartbeat.
    if (value) m_unavailable = false;
    emit activeChanged();
    refresh();
}

void FrameRateMatchClient::setOutput(const QString &value)
{
    if (m_output == value) return;
    m_output = value;
    emit outputChanged();
    refresh();
}

QNetworkReply *FrameRateMatchClient::post(const QString &path, const QByteArray &payload)
{
    QFile token(QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation)
                + QStringLiteral("/gig-frame-match.token"));
    if (!token.open(QIODevice::ReadOnly)) return nullptr;
    const QByteArray secret = token.read(128).trimmed();
    if (secret.size() != 64) return nullptr;
    QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:37491/v1/") + path));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", "Bearer " + secret);
    request.setTransferTimeout(2500);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    return m_network.post(request, payload);
}

void FrameRateMatchClient::refresh()
{
    if (!m_active || !m_player || m_player->playbackState() == QMediaPlayer::StoppedState
            || m_player->error() != QMediaPlayer::NoError) {
        m_heartbeat.stop();
        release();
        return;
    }
    // Qt exposes the decoded stream's exact rate for local files and DLNA HTTP
    // streams; there is no separate ffprobe, download, or transcoding job.
    const double fps = m_player->metaData().value(QMediaMetaData::VideoFrameRate).toDouble()
            * m_player->playbackRate();
    if (!std::isfinite(fps) || fps < 10 || fps > 240) {
        release();
        return;
    }
    if (m_unavailable) return;
    QJsonObject body{{QStringLiteral("session"), m_session}, {QStringLiteral("fps"), fps}};
    if (!m_output.isEmpty()) body.insert(QStringLiteral("output"), m_output);
    m_pending = QJsonDocument(body).toJson(QJsonDocument::Compact);
    m_heartbeat.start();
    pump();
}

void FrameRateMatchClient::pump()
{
    if (m_busy || m_pending.isEmpty()) return;
    const QByteArray payload = m_pending;
    m_pending.clear();
    const bool releasing = !QJsonDocument::fromJson(payload).object().contains(QStringLiteral("fps"));
    QNetworkReply *reply = post(releasing ? QStringLiteral("release") : QStringLiteral("session"), payload);
    if (!reply) {
        m_unavailable = true;
        m_heartbeat.stop();
        return;
    }
    m_busy = true;
    if (!releasing) m_acquired = true;
    connect(reply, &QNetworkReply::finished, this, [this, reply, releasing] {
        const auto status = QJsonDocument::fromJson(reply->readAll()).object();
        if (reply->error() != QNetworkReply::NoError
                || status.value(QStringLiteral("service")).toString() != QStringLiteral("gig-frame-match")) {
            m_unavailable = true;
            m_heartbeat.stop();
            // Preserve a queued release if an acquire succeeded server-side
            // but its response was lost. Otherwise the timeout restores it.
            const auto pending = QJsonDocument::fromJson(m_pending).object();
            if (pending.contains(QStringLiteral("fps"))) m_pending.clear();
        }
        if (releasing) m_acquired = false;
        m_busy = false;
        reply->deleteLater();
        pump();
    });
}

void FrameRateMatchClient::release()
{
    m_pending.clear();
    if (!m_acquired && !m_busy) return;
    m_pending = QJsonDocument(QJsonObject{{QStringLiteral("session"), m_session}}).toJson(QJsonDocument::Compact);
    pump();
}

void FrameRateMatchClient::shutdown()
{
    m_active = false;
    m_heartbeat.stop();
    release();
    if (!m_busy) return;
    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    connect(&m_network, &QNetworkAccessManager::finished, &loop, [this, &loop] {
        if (!m_busy) loop.quit();
    });
    deadline.start(600);
    loop.exec(QEventLoop::ExcludeUserInputEvents);
}
