#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QTimer>

class UpdateChecker : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY changed)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY changed)
    Q_PROPERTY(bool checking READ checking NOTIFY changed)
    Q_PROPERTY(QString releaseUrl READ releaseUrl NOTIFY changed)
public:
    explicit UpdateChecker(QObject *parent = nullptr);
    QString currentVersion() const;
    QString latestVersion() const { return m_latest; }
    bool updateAvailable() const;
    bool checking() const { return m_checking; }
    QString releaseUrl() const { return m_url; }
    Q_INVOKABLE void checkForUpdates();
    Q_INVOKABLE void openReleasePage();
    // Public parser permits tests without reaching GitHub or altering settings.
    static QString stableVersion(const QString &tag);
    bool acceptRelease(const QByteArray &json);
signals:
    void changed();
private:
    QNetworkAccessManager m_network;
    QTimer m_timer;
    QString m_latest;
    QString m_url = QStringLiteral("https://github.com/G-grbz/Lurviko/releases");
    bool m_checking = false;
};
