#pragma once

#include <QObject>
#include <QDateTime>
#include <QPointer>
#include <QUrl>
#include <QStringList>
#include <QVariantMap>
#include <QMetaObject>
#include <memory>
#include <atomic>

class QTimer;
class QProcess;
struct FileAccessTask;

class FilePropertiesManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString path READ path NOTIFY propertiesChanged)
    Q_PROPERTY(QString name READ name NOTIFY propertiesChanged)
    Q_PROPERTY(bool directory READ directory NOTIFY propertiesChanged)
    Q_PROPERTY(FilePropertiesManager *statsSource READ statsSource WRITE setStatsSource)
    Q_PROPERTY(bool statsReady READ statsReady NOTIFY propertiesChanged)
    Q_PROPERTY(bool backgroundStats MEMBER m_backgroundStats)
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
    Q_PROPERTY(int accessSkipped READ accessSkipped NOTIFY accessProgressChanged)
    Q_PROPERTY(QString accessError READ accessError NOTIFY accessProgressChanged)
    Q_PROPERTY(QString iconName READ iconName NOTIFY propertiesChanged)
    Q_PROPERTY(QString linkType READ linkType NOTIFY propertiesChanged)
    Q_PROPERTY(QString linkTarget READ linkTarget NOTIFY propertiesChanged)
    Q_PROPERTY(bool linkTargetExists READ linkTargetExists NOTIFY propertiesChanged)
    Q_PROPERTY(qulonglong inode READ inode NOTIFY propertiesChanged)
    Q_PROPERTY(int linkCount READ linkCount NOTIFY propertiesChanged)
    Q_PROPERTY(bool calculating READ calculating NOTIFY calculatingChanged)
    Q_PROPERTY(bool multiple READ multiple NOTIFY propertiesChanged)
    Q_PROPERTY(bool allLocal READ allLocal NOTIFY propertiesChanged)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY propertiesChanged)
    Q_PROPERTY(int selectedFolders READ selectedFolders NOTIFY propertiesChanged)
    Q_PROPERTY(int selectedFiles READ selectedFiles NOTIFY propertiesChanged)
    Q_PROPERTY(int selectedLinks READ selectedLinks NOTIFY propertiesChanged)
    Q_PROPERTY(QString commonLocation READ commonLocation NOTIFY propertiesChanged)
    Q_PROPERTY(int mixedPermissionMask READ mixedPermissionMask NOTIFY permissionsChanged)

public:
    explicit FilePropertiesManager(QObject *parent = nullptr);
    ~FilePropertiesManager() override;

    FilePropertiesManager *statsSource() const { return m_statsSource; }
    void setStatsSource(FilePropertiesManager *source) { m_statsSource = source == this ? nullptr : source; }
    bool statsReady() const { return m_statsReady; }
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
    int accessSkipped() const;
    Q_INVOKABLE QString specialPermissionsHint() const;
    Q_INVOKABLE QString accessSkippedSummary() const;
    QString accessError() const { return m_accessError; }
    QString iconName() const { return m_iconName; }
    QString linkType() const { return m_linkType; }
    QString linkTarget() const { return m_linkTarget; }
    bool linkTargetExists() const { return m_linkTargetExists; }
    qulonglong inode() const { return m_inode; }
    int linkCount() const { return m_linkCount; }
    bool calculating() const { return m_calculating; }
    bool multiple() const { return m_selection.size() > 1; }
    bool allLocal() const { return m_localFile; }
    int selectionCount() const { return m_selection.size(); }
    int selectedFolders() const { return m_selectedFolders; }
    int selectedFiles() const { return m_selectedFiles; }
    int selectedLinks() const { return m_selectedLinks; }
    QString commonLocation() const { return m_commonLocation; }
    int mixedPermissionMask() const { return m_mixedPermissionMask; }

    Q_INVOKABLE void inspect(const QString &urlOrPath);
    Q_INVOKABLE void inspectCached(const QString &urlOrPath);
    Q_INVOKABLE bool restoreCachedStats(const QString &urlOrPath);
    Q_INVOKABLE void invalidateCachedStats(const QString &urlOrPath);
    Q_INVOKABLE void inspectSelection(const QStringList &urls);
    Q_INVOKABLE void cancelInspection();
    Q_INVOKABLE bool setFolderIcon(const QString &iconName);
    Q_INVOKABLE bool setLinkTarget(const QString &target);
    Q_INVOKABLE bool setPermissionMode(int mode);
    Q_INVOKABLE QStringList availableOwners(bool administrator = false) const;
    Q_INVOKABLE QStringList availableGroups(bool administrator = false) const;
    Q_INVOKABLE bool applyAccess(int mode, const QString &owner, const QString &group,
                                 bool recursive = false, bool administrator = false);
    Q_INVOKABLE bool applySelectionAccess(int mode, int modeMask, const QString &owner,
                                          const QString &group, bool recursive = false,
                                          bool administrator = false);
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
    static QString readFolderIcon(const QString &path);

    bool reuseStatsOrWait(const QStringList &paths, bool selection, int generation);
    void inspectInternal(const QString &urlOrPath, bool cached);
    static FilePropertiesManager *cachedStats(const QString &path, bool lookupOnly = false);
    QPointer<FilePropertiesManager> m_cachedSource;
    QMetaObject::Connection m_cachedPropertiesConnection, m_cachedCalculatingConnection;
    int m_cachedReaders = 0;
    QPointer<FilePropertiesManager> m_statsSource;
    QMetaObject::Connection m_statsConnection;
    bool m_statsReady = false;
    bool m_backgroundStats = false;
    QVariantMap m_childStats;
    QString m_path;
    QStringList m_selection;
    QString m_commonLocation;
    int m_selectedFolders = 0;
    int m_selectedFiles = 0;
    int m_selectedLinks = 0;
    int m_mixedPermissionMask = 0;
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
    qint64 m_accessOwnerId = -1;
    qint64 m_accessGroupId = -1;
    int m_accessModeMask = 0777;
    int m_accessGeneration = 0;
    QStringList m_accessPaths;
    std::shared_ptr<FileAccessTask> m_accessTask;
    QPointer<QProcess> m_accessProcess;
    QTimer *m_accessTimer = nullptr;
    QString m_iconName;
    QString m_linkType;
    QString m_linkTarget;
    bool m_linkTargetExists = false;
    qulonglong m_inode = 0;
    int m_linkCount = 1;
    bool m_calculating = false;
    int m_generation = 0;
    std::shared_ptr<std::atomic_bool> m_statsCancelled;
    QPointer<QTimer> m_statsProgressTimer;
    std::shared_ptr<std::atomic_int> m_statsDemand;
    std::shared_ptr<std::atomic_int> m_waitingStatsDemand;
};
