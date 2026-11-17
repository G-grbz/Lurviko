#pragma once

#include <QObject>
#include <QHash>
#include <QPointer>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

class QProcess;

// On-demand usershare operations. No polling, directory scans or shell
// commands: every helper runs as an argument list, and the Samba password
// travels on the helper's stdin only.
class SambaShareManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool shared READ shared NOTIFY changed)
    Q_PROPERTY(bool editable READ editable NOTIFY changed)
    Q_PROPERTY(bool readOnly READ readOnly NOTIFY changed)
    Q_PROPERTY(QString path READ path NOTIFY changed)
    Q_PROPERTY(QString shareName READ shareName NOTIFY changed)
    Q_PROPERTY(QString comment READ comment NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString address READ address NOTIFY changed)
    Q_PROPERTY(QStringList shareNames READ shareNames NOTIFY changed)
    Q_PROPERTY(QVariantList localShares READ localShares NOTIFY changed)
    // Access table of the selected share, or the defaults for a new one:
    // [{ name, access ("R", "F", "D" or ""), kind ("everyone", "user",
    //    "custom"), current (the signed-in user) }].
    Q_PROPERTY(QVariantList aclEntries READ aclEntries NOTIFY changed)
    Q_PROPERTY(bool guestOk READ guestOk NOTIFY changed)
    // From smb.conf ("usershare allow guests"); unknown counts as false.
    Q_PROPERTY(bool guestsAllowed READ guestsAllowed NOTIFY environmentChanged)
    Q_PROPERTY(int maxShares READ maxShares NOTIFY environmentChanged)
    // "active", "inactive" or "unknown" for smb/smbd.
    Q_PROPERTY(QString serviceState READ serviceState NOTIFY environmentChanged)
    Q_PROPERTY(QString userName READ userName CONSTANT)
    Q_PROPERTY(bool passwordChangeAvailable READ passwordChangeAvailable CONSTANT)
    Q_PROPERTY(bool passwordBusy READ passwordBusy NOTIFY passwordBusyChanged)
    Q_PROPERTY(bool statusBusy READ statusBusy NOTIFY statusChanged)
    // { sessions: [...], connections: [...], openFiles, requiresAdmin, error, version }
    Q_PROPERTY(QVariantMap status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool permissionsBusy READ permissionsBusy NOTIFY permissionsBusyChanged)
public:
    explicit SambaShareManager(QObject* parent = nullptr);
    SambaShareManager(const QString& netProgram, QObject* parent);
    ~SambaShareManager() override;
    bool busy() const { return m_busy; }
    bool available() const { return m_available; }
    bool shared() const { return !m_selected.isEmpty(); }
    bool editable() const;
    bool readOnly() const;
    QString path() const { return m_path; }
    QString shareName() const;
    QString comment() const;
    QString error() const { return m_error; }
    QString address() const;
    QStringList shareNames() const;
    QVariantList localShares() const;
    QVariantList aclEntries() const;
    bool guestOk() const;
    bool guestsAllowed() const { return m_guestsAllowed; }
    int maxShares() const { return m_maxShares; }
    QString serviceState() const { return m_serviceState; }
    QString userName() const { return m_userName; }
    bool passwordChangeAvailable() const;
    bool passwordBusy() const { return m_passwordBusy; }
    bool statusBusy() const { return m_statusBusy; }
    QVariantMap status() const { return m_status; }
    bool permissionsBusy() const { return m_permissionsBusy; }

    Q_INVOKABLE void inspect(const QString& location);
    Q_INVOKABLE void listShares();
    Q_INVOKABLE void clear();
    Q_INVOKABLE void selectShare(const QString& name);
    // Everyone read-only / full control. Refuses to replace custom rules.
    Q_INVOKABLE void save(const QString& name, const QString& comment, bool readOnly);
    // Full access table, as edited in the panel.
    Q_INVOKABLE void saveShare(const QString& name, const QString& comment,
                               const QVariantList& entries, bool guestOk);
    Q_INVOKABLE void remove();
    // Every address the share can be reached at: host name, .local and
    // each network interface, as smb:// URLs and Windows UNC paths.
    Q_INVOKABLE QVariantList shareAddresses() const;
    // Sets the Samba password of the signed-in user (adding the Samba
    // account when needed) through polkit. Never passed on a command line.
    Q_INVOKABLE void changePassword(const QString& password);
    // Connected clients and open shares; asAdministrator asks polkit.
    Q_INVOKABLE void loadStatus(bool asAdministrator = false);
    Q_INVOKABLE void refreshEnvironment();
    // Filesystem permissions the access table needs. People connecting
    // as anyone but the folder's owner use the "others" bits: read access
    // needs r-x on the folder, full control rwx, and every parent folder
    // needs x to be passed through. Only missing bits are ever added.
    // { needed, level ("none", "read", "full"), requiresAdmin, items: [
    //   { path, currentMode, newMode, currentText, newText, owned, role } ] }
    Q_INVOKABLE QVariantMap permissionPlan(const QVariantList& entries, bool guestOk) const;
    // Applies permissionPlan() (recomputed here): own folders directly,
    // others through polkit. Emits permissionsFinished.
    Q_INVOKABLE void fixPermissions(const QVariantList& entries, bool guestOk);
    // Bits this application added for the share (recorded by
    // fixPermissions) that the given access no longer needs, or all of them
    // when removing the share. Folders changed by hand since then are left
    // alone ("skipped"); a parent another share still passes through keeps
    // its x. { needed, items: [...as permissionPlan], skipped: [paths] }
    Q_INVOKABLE QVariantMap restorePlan(const QVariantList& entries, bool guestOk, bool removing) const;
    Q_INVOKABLE void restorePermissions(const QVariantList& entries, bool guestOk, bool removing);
    // Validated name for an access-table entry, or "" when invalid.
    Q_INVOKABLE QString normalizedAccount(const QString& name) const;

    // Tests: replace the helper programs.
    void setHelperPrograms(const QString& pkexec, const QString& smbpasswd, const QString& smbstatus,
                           const QString& testparm, const QString& systemctl);
    void setLocalUsersForTest(const QStringList& users);
    void setChmodProgramForTest(const QString& chmod) { m_chmod = chmod; }
    void setOtherSharesForTest(const QStringList& paths) { m_otherSharesForTest = paths; m_otherSharesOverride = true; }
    static QString aclString(const QVariantList& entries, QString* error = nullptr);

signals:
    void changed();
    void environmentChanged();
    void statusChanged();
    void passwordBusyChanged();
    void operationFinished(bool success, const QString& message);
    void passwordFinished(bool success, const QString& message);
    void permissionsBusyChanged();
    void permissionsFinished(bool success, const QString& message);
    void permissionsRestored(bool success, const QString& message);
private:
    struct Share { QString name, path, comment, acl; bool guest = false; };
    QString m_netProgram, m_path, m_selected, m_error, m_pendingLocation;
    QString m_pkexec, m_smbpasswd, m_smbstatus, m_testparm, m_systemctl, m_chmod;
    QString m_userName;
    QStringList m_localUsers;
    QHash<QString, Share> m_shares;
    QPointer<QProcess> m_process;
    quint64 m_generation = 0;
    quint64 m_statusGeneration = 0;
    bool m_busy = false, m_available = false, m_writing = false;
    bool m_guestsAllowed = false, m_passwordBusy = false, m_statusBusy = false;
    bool m_environmentLoaded = false;
    bool m_permissionsBusy = false;
    bool m_otherSharesOverride = false;
    QStringList m_otherSharesForTest;
    QStringList otherSharedPaths() const;
    void runPermissionHelper(const QStringList& chmodArgs, const QString& success,
                             std::function<void(bool, const QString&)> done);
    int m_maxShares = -1;
    QString m_serviceState = QStringLiteral("unknown");
    QVariantMap m_status;
    void run(const QStringList& args, bool writing,
             std::function<void(bool, const QByteArray&, const QString&)> done);
    void fail(const QString& error);
    void readShares(quint64 token);
    void mutate(const QStringList& args, const QString& successMessage);
    void initialiseHelpers();
};
