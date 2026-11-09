#include "kdeconnectrecent.h"

#include <KRecentDocument>
#include <QDBusConnection>
#include <QDir>
#include <QFileInfo>
#include <QUrl>

KdeConnectRecent::KdeConnectRecent(QObject *parent)
    : QObject(parent)
{
    // kdeconnectd's share plugin emits shareReceived(url) on
    // /modules/kdeconnect/devices/<id>/share for every finished incoming file.
    // Empty service and path match all paired devices.
    QDBusConnection::sessionBus().connect(QString(), QString(),
        QStringLiteral("org.kde.kdeconnect.device.share"), QStringLiteral("shareReceived"),
        this, SLOT(shareReceived(QString)));
}

void KdeConnectRecent::shareReceived(const QString &url)
{
    const QUrl received = QUrl::fromUserInput(url);
    if (!received.isLocalFile() || !QFileInfo(received.toLocalFile()).isFile())
        return; // shared links and text are not files
    KRecentDocument::add(QUrl::fromLocalFile(QDir::cleanPath(received.toLocalFile())),
                         QStringLiteral("org.kde.kdeconnect.app"));
}
