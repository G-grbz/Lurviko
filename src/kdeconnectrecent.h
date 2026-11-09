#pragma once

#include <QObject>

// Records files received over KDE Connect in the desktop's shared recent
// documents list (recently-used.xbel), which Lurviko's Recent Files category
// and other applications read. KDE Connect itself does not add them there.
class KdeConnectRecent : public QObject
{
    Q_OBJECT
public:
    explicit KdeConnectRecent(QObject *parent = nullptr);

private Q_SLOTS:
    void shareReceived(const QString &url);
};
