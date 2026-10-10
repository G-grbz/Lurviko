#pragma once

#include <QObject>
#include <QDateTime>
#include <QPointer>
#include <QUrl>
#include <QStringList>
#include <memory>

class KJob;
class QTimer;
struct FileAccessTask;

class FilePropertiesManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString path READ path NOTIFY propertiesChanged)
    Q_PROPERTY(QString name READ name NOTIFY propertiesChanged)
    Q_PROPERTY(bool directory READ directory NOTIFY propertiesChanged)
    Q_PROPERTY(qint64 totalSize READ totalSize NOTIFY propertiesChanged)
    Q_PROPERTY(int fileCount READ fileCount NOTIFY propertiesChanged)
    Q_PROPERTY(int folderCount READ folderCount NOTIFY propertiesChanged)
    Q_PROPERTY(QDateTime created READ created NOTIFY propertiesChanged)
    Q_PROPERTY(QDateTime modified READ modified NOTIFY propertiesChanged)
    Q_PROPERTY(QDateTime accessed READ accessed NOTIFY propertiesChanged)
    Q_PROPERTY(QString permissionsText READ permissionsText NOTIFY propertiesChanged)
    Q_PROPERTY(int permissionMode READ permissionMode NOTIFY permissionsChanged)
    Q_PROPERTY(QString ownerName READ ownerName NOTIFY permissionsChanged)
    Q_PROPERTY(QString groupName READ groupName NOTIFY permissionsChanged)
    Q_PROPERTY(bool permissionsEditable READ permissionsEditable NOTIFY permissionsChanged)
    Q_PROPERTY(bool administratorAvailable READ administratorAvailable CONSTANT)
    Q_PROPERTY(bool accessBusy READ accessBusy NOTIFY accessProgressChanged)
    Q_PROPERTY(int accessCompleted READ accessCompleted NOTIFY accessProgressChanged)
    Q_PROPERTY(int accessFailed READ accessFailed NOTIFY accessProgressChanged)
    Q_PROPERTY(QString accessError READ accessError NOTIFY accessProgressChanged)
    Q_PROPERTY(QString iconName READ iconName NOTIFY propertiesChanged)
    Q_PROPERTY(QString linkType READ linkType NOTIFY propertiesChanged)
    Q_PROPERTY(QString linkTarget READ linkTarget NOTIFY propertiesChanged)
    Q_PROPERTY(bool linkTargetExists READ linkTargetExists NOTIFY propertiesChanged)
    Q_PROPERTY(qulonglong inode READ inode NOTIFY propertiesChanged)
    Q_PROPERTY(int linkCount READ linkCount NOTIFY propertiesChanged)
    Q_PROPERTY(bool calculating READ calculating NOTIFY calculatingChanged)

public:
    explicit FilePropertiesManager(QObject *parent = nullptr);
    ~FilePropertiesManager() override;

    QString path() const { return m_path; }
    QString name() const { return m_name; }
    bool directory() const { return m_directory; }
    qint64 totalSize() const { return m_totalSize; }
    int fileCount() const { return m_fileCount; }
    int folderCount() const { return m_folderCount; }
    QDateTime created() const { return m_created; }
    QDateTime modified() const { return m_modified; }
    QDateTime accessed() const { return m_accessed; }
    QString permissionsText() const { return m_permissionsText; }
    int permissionMode() const { return m_permissionMode; }
    QString ownerName() const { return m_ownerName; }
    QString groupName() const { return m_groupName; }
    bool permissionsEditable() const { return m_permissionsEditable; }
    bool administratorAvailable() const;
    bool accessBusy() const { return m_accessBusy; }
    int accessCompleted() const;
    int accessFailed() const;
    QString accessError() const { return m_accessError; }
    QString iconName() const { return m_iconName; }
    QString linkType() const { return m_linkType; }
    QString linkTarget() const { return m_linkTarget; }
    bool linkTargetExists() const { return m_linkTargetExists; }
    qulonglong inode() const { return m_inode; }
    int linkCount() const { return m_linkCount; }
    bool calculating() const { return m_calculating; }

    Q_INVOKABLE void inspect(const QString &urlOrPath);
    Q_INVOKABLE bool setFolderIcon(const QString &iconName);
    Q_INVOKABLE bool setLinkTarget(const QString &target);
    Q_INVOKABLE bool setPermissionMode(int mode);
    Q_INVOKABLE QStringList availableOwners(bool administrator = false) const;
    Q_INVOKABLE QStringList availableGroups(bool administrator = false) const;
    Q_INVOKABLE bool applyAccess(int mode, const QString &owner, const QString &group,
                                 bool recursive = false, bool administrator = false);
    Q_INVOKABLE void cancelAccess();
    Q_INVOKABLE QString formatBytes(qint64 bytes) const;

signals:
    void propertiesChanged();
    void permissionsChanged();
    void permissionsApplied(const QString &path);
    void accessProgressChanged();
    void accessFinished(bool success, bool cancelled, int changed, int failed);
    void calculatingChanged();
    void folderIconChanged(const QString &path, const QString &iconName);
    void error(const QString &message);

private:
    void setCalculating(bool value);
    void refreshPermissions();
    void initializeAccess();
    void finishAccess(bool cancelled);
    void startAdministratorAccess(bool recursive);
    void continueAdministratorAccess();
    static QString readFolderIcon(const QString &path);

    QString m_path;
    QString m_name;
    bool m_directory = false;
    qint64 m_totalSize = 0;
    int m_fileCount = 0;
    int m_folderCount = 0;
    QDateTime m_created;
    QDateTime m_modified;
    QDateTime m_accessed;
    QString m_permissionsText;
    int m_permissionMode = 0;
    QString m_ownerName;
    QString m_groupName;
    bool m_permissionsEditable = false;
    bool m_localFile = false;
    bool m_accessBusy = false;
    QString m_accessError;
    QString m_accessPath;
    QString m_accessOwner;
    QString m_accessGroup;
    int m_accessMode = 0;
    struct AccessEntry { QUrl url; bool descendant = false; };
    QList<AccessEntry> m_accessEntries;
    std::shared_ptr<FileAccessTask> m_accessTask;
    QPointer<KJob> m_accessJob;
    QTimer *m_accessTimer = nullptr;
    QString m_iconName;
    QString m_linkType;
    QString m_linkTarget;
    bool m_linkTargetExists = false;
    qulonglong m_inode = 0;
    int m_linkCount = 1;
    bool m_calculating = false;
    int m_generation = 0;
};
