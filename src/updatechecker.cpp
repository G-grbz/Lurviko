#include "updatechecker.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSettings>
#include <QUrl>
#include <QVersionNumber>

UpdateChecker::UpdateChecker(QObject *parent) : QObject(parent)
{
    QSettings settings;
    const QByteArray saved = settings.value(QStringLiteral("updates/release")).toByteArray();
    if (!saved.isEmpty()) acceptRelease(saved);
    m_timer.setInterval(6 * 60 * 60 * 1000);
    connect(&m_timer, &QTimer::timeout, this, &UpdateChecker::checkForUpdates);
    m_timer.start();
}

QString UpdateChecker::currentVersion() const { return QCoreApplication::applicationVersion(); }
bool UpdateChecker::updateAvailable() const
{
    return !m_latest.isEmpty() && QVersionNumber::fromString(m_latest) > QVersionNumber::fromString(currentVersion());
}

QString UpdateChecker::stableVersion(const QString &tag)
{
    static const QRegularExpression version(QStringLiteral("^(?:Lurviko[-_ ]+)?v?(\\d+\\.\\d+\\.\\d+)$"));
    return version.match(tag).captured(1);
}

bool UpdateChecker::acceptRelease(const QByteArray &json)
{
    const auto doc = QJsonDocument::fromJson(json);
    if (!doc.isObject()) return false;
    const auto release = doc.object();
    if (release.value(QStringLiteral("draft")).toBool() || release.value(QStringLiteral("prerelease")).toBool()) return false;
    const QString version = stableVersion(release.value(QStringLiteral("tag_name")).toString());
    const QUrl url(release.value(QStringLiteral("html_url")).toString());
    // Only open this project's official GitHub releases, never arbitrary API URLs.
    if (version.isEmpty() || url.scheme() != QStringLiteral("https") || url.host() != QStringLiteral("github.com")
            || !url.userInfo().isEmpty() || url.port() != -1
            || !url.path().startsWith(QStringLiteral("/G-grbz/Lurviko/releases/tag/"))) return false;
    m_latest = version;
    m_url = url.toString();
    emit changed();
    return true;
}

void UpdateChecker::checkForUpdates()
{
    if (m_checking) return;
    QSettings settings;
    const qint64 last = settings.value(QStringLiteral("updates/lastCheck")).toLongLong();
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (last > 0 && now >= last && now - last < 6 * 60 * 60) return;
    QNetworkRequest request(QUrl(QStringLiteral("https://api.github.com/repos/G-grbz/Lurviko/releases/latest")));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Lurviko/") + currentVersion());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(10000);
    m_checking = true;
    emit changed();
    auto *reply = m_network.get(request);
    reply->setReadBufferSize(256 * 1024);
    connect(reply, &QNetworkReply::readyRead, this, [reply] {
        if (reply->bytesAvailable() > 128 * 1024) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QSettings settings;
        // Cache successes and "no release yet"; transient failures retry next launch.
        if (reply->error() == QNetworkReply::NoError) {
            const QByteArray body = reply->readAll();
            if (body.size() <= 128 * 1024 && acceptRelease(body)) {
                settings.setValue(QStringLiteral("updates/release"), body);
                settings.setValue(QStringLiteral("updates/lastCheck"), QDateTime::currentSecsSinceEpoch());
            }
        } else if (status == 404) {
            settings.setValue(QStringLiteral("updates/lastCheck"), QDateTime::currentSecsSinceEpoch());
        }
        m_checking = false;
        reply->deleteLater();
        emit changed();
    });
}

void UpdateChecker::openReleasePage() { QDesktopServices::openUrl(QUrl(m_url)); }
