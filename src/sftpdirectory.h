#pragma once

#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>

class QProcess;

// Start page data for sftp:// without a host: servers from the SSH client
// configuration (~/.ssh/config, with Include), hosts already trusted in
// known_hosts, and SSH servers announced on the local network (mDNS via
// avahi-browse). Also turns typed text ("user@host:2222/path", scp-style
// "host:path", "[v6]:port") into a valid sftp:// URL.
class SftpDirectory : public QObject
{
    Q_OBJECT
    // [{ alias, hostName, user, port, url }]
    Q_PROPERTY(QVariantList configHosts READ configHosts NOTIFY hostsChanged)
    // [{ host, port, url }]
    Q_PROPERTY(QVariantList knownHosts READ knownHosts NOTIFY hostsChanged)
    // [{ name, host, address, port, url }]
    Q_PROPERTY(QVariantList discovered READ discovered NOTIFY discoveredChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY hostsChanged)
    Q_PROPERTY(bool discovering READ discovering NOTIFY discoveredChanged)
    // "" when discovery works; otherwise why it is unavailable.
    Q_PROPERTY(QString discoveryError READ discoveryError NOTIFY discoveredChanged)
    Q_PROPERTY(QString localUser READ localUser CONSTANT)
public:
    explicit SftpDirectory(QObject *parent = nullptr);
    ~SftpDirectory() override;

    QVariantList configHosts() const { return m_configHosts; }
    QVariantList knownHosts() const { return m_knownHosts; }
    QVariantList discovered() const { return m_discovered; }
    bool loading() const { return m_loading; }
    bool discovering() const { return m_discovering; }
    QString discoveryError() const { return m_discoveryError; }
    QString localUser() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void discover();
    // { valid, host, user, port, path, error }
    Q_INVOKABLE QVariantMap parseTarget(const QString &text) const;
    // sftp://user@host:port/path, or "" when a part is invalid. Port 22 and
    // an empty path are left out (an empty path opens the home folder).
    Q_INVOKABLE QString buildUrl(const QString &host, const QString &user, int port, const QString &path) const;

    void setSshDirectoryForTest(const QString &directory) { m_sshDirectory = directory; }
    void setAvahiProgramForTest(const QString &program) { m_avahi = program; }

    static QVariantList parseConfig(const QString &file, const QString &sshDirectory = QString(), int depth = 0);
    static QVariantList parseKnownHosts(const QString &file);

signals:
    void hostsChanged();
    void discoveredChanged();

private:
    QVariantList m_configHosts;
    QVariantList m_knownHosts;
    QVariantList m_discovered;
    QString m_discoveryError;
    QString m_sshDirectory;
    QString m_avahi;
    bool m_loading = false;
    bool m_discovering = false;
    quint64 m_generation = 0;
    QList<QPointer<QProcess>> m_processes;
    int m_pendingDiscovery = 0;
    void stopDiscovery();
};
