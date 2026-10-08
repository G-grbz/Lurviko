#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QVariantList>

class QTimer;
namespace KWallet { class Wallet; }

class PrivateVaultManager : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(bool exists READ exists NOTIFY stateChanged)
    Q_PROPERTY(bool unlocked READ unlocked NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(int itemCount READ itemCount NOTIFY itemCountChanged)
    Q_PROPERTY(int totalItemCount READ totalItemCount NOTIFY itemCountChanged)
    Q_PROPERTY(QString currentFolderId READ currentFolderId NOTIFY currentFolderChanged)
    Q_PROPERTY(QString currentPathLabel READ currentPathLabel NOTIFY currentFolderChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(bool kwalletEnabled READ kwalletEnabled NOTIFY securitySettingsChanged)
    Q_PROPERTY(bool autoLockEnabled READ autoLockEnabled NOTIFY securitySettingsChanged)
    Q_PROPERTY(int autoLockMinutes READ autoLockMinutes NOTIFY securitySettingsChanged)
    Q_PROPERTY(bool passwordProtectionEnabled READ passwordProtectionEnabled NOTIFY securitySettingsChanged)
    Q_PROPERTY(int maxPasswordAttempts READ maxPasswordAttempts NOTIFY securitySettingsChanged)
    Q_PROPERTY(int passwordLockoutSeconds READ passwordLockoutSeconds NOTIFY securitySettingsChanged)
    Q_PROPERTY(int passwordRetrySeconds READ passwordRetrySeconds NOTIFY securitySettingsChanged)
    Q_PROPERTY(int failedPasswordAttempts READ failedPasswordAttempts NOTIFY authenticationStateChanged)
    Q_PROPERTY(int passwordWaitSeconds READ passwordWaitSeconds NOTIFY authenticationStateChanged)
    Q_PROPERTY(bool passwordLockedOut READ passwordLockedOut NOTIFY authenticationStateChanged)

public:
    enum Roles {
        ObjectIdRole = Qt::UserRole + 1,
        NameRole,
        IsDirRole,
        SizeRole,
        MimeTypeRole,
        ModifiedMsRole,
        IconNameRole
    };
    Q_ENUM(Roles)

    explicit PrivateVaultManager(QObject *parent = nullptr);
    ~PrivateVaultManager() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool exists() const;
    bool unlocked() const { return m_unlocked; }
    bool busy() const { return m_busy; }
    int itemCount() const { return m_visibleItems.size(); }
    int totalItemCount() const;
    QString currentFolderId() const { return m_currentFolderId; }
    QString currentPathLabel() const;
    QString lastError() const { return m_lastError; }
    QString statusMessage() const { return m_statusMessage; }
    bool kwalletEnabled() const { return m_kwalletEnabled; }
    bool autoLockEnabled() const { return m_autoLockEnabled; }
    int autoLockMinutes() const { return m_autoLockMinutes; }
    bool passwordProtectionEnabled() const { return m_passwordProtectionEnabled; }
    int maxPasswordAttempts() const { return m_maxPasswordAttempts; }
    int passwordLockoutSeconds() const { return m_passwordLockoutSeconds; }
    int passwordRetrySeconds() const { return m_passwordRetrySeconds; }
    int failedPasswordAttempts() const { return m_failedPasswordAttempts; }
    int passwordWaitSeconds() const;
    bool passwordLockedOut() const;

    Q_INVOKABLE bool createVault(const QString &password);
    Q_INVOKABLE bool unlock(const QString &password);
    Q_INVOKABLE bool quickUnlock();
    Q_INVOKABLE void lock();
    Q_INVOKABLE bool changePassword(const QString &currentPassword, const QString &newPassword);
    Q_INVOKABLE bool setKWalletEnabled(bool enabled);
    Q_INVOKABLE bool setAutoLockEnabled(bool enabled);
    Q_INVOKABLE bool setAutoLockMinutes(int minutes);
    Q_INVOKABLE bool setPasswordProtection(bool enabled, int attempts, int lockoutSeconds, int retrySeconds);
    Q_INVOKABLE QString thumbnailUrl(const QString &objectId);
    Q_INVOKABLE bool createFolder(const QString &name);
    Q_INVOKABLE bool renameItem(const QString &objectId, const QString &newName);
    Q_INVOKABLE bool deleteItem(const QString &objectId);
    Q_INVOKABLE bool enterFolder(const QString &objectId);
    Q_INVOKABLE bool goUp();
    Q_INVOKABLE void goRoot();
    Q_INVOKABLE bool importUrls(const QVariantList &urls);
    Q_INVOKABLE bool exportItem(const QString &objectId, const QString &targetFolder);
    Q_INVOKABLE QString materializeForOpen(const QString &objectId);
    Q_INVOKABLE void releaseMaterialized(const QString &urlOrPath);
    Q_INVOKABLE bool isRuntimeUrl(const QString &urlOrPath) const;
    Q_INVOKABLE void clearStatus();

signals:
    void aboutToLock();
    void stateChanged();
    void busyChanged();
    void itemCountChanged();
    void currentFolderChanged();
    void lastErrorChanged();
    void statusMessageChanged();
    void operationFinished(bool success, const QString &message);
    void securitySettingsChanged();
    void authenticationStateChanged();

private:
    static constexpr int KeySize = 32;
    static constexpr int SaltSize = 16;
    static constexpr int IvSize = 12;
    static constexpr int TagSize = 16;

    QString vaultRoot() const;
    QString objectsRoot() const;
    QString thumbnailsRoot() const;
    QString headerPath() const;
    QString runtimeRoot() const;

    void setBusy(bool value);
    void setError(const QString &message);
    void setStatus(const QString &message);
    void secureClear(QByteArray &bytes);
    void cleanupRuntimeFiles();
    void cleanupOrphanBlobs();
    void cleanupOrphanThumbnails();
    bool ensureStorageLayout();

    bool derivePasswordKey(const QString &password, const QByteArray &salt, QByteArray *outKey, const QJsonObject *params = nullptr) const;
    bool aesGcmEncrypt(const QByteArray &plain, const QByteArray &key, const QByteArray &aad,
                       QByteArray *iv, QByteArray *cipher, QByteArray *tag) const;
    bool aesGcmDecrypt(const QByteArray &cipher, const QByteArray &key, const QByteArray &aad,
                       const QByteArray &iv, const QByteArray &tag, QByteArray *plain) const;
    bool encryptFileToBlob(const QString &sourcePath, const QString &objectId, const QString &blobName, const QByteArray &key) const;
    bool decryptBlobToFile(const QString &objectId, const QString &blobName, const QString &targetPath, const QByteArray &key) const;

    bool readHeader(QJsonObject *header) const;
    bool writeHeader(const QJsonObject &header) const;
    bool saveManifest();
    bool loadManifest(const QJsonObject &header, const QByteArray &masterKey, QJsonObject *manifest) const;
    bool unwrapMasterKey(const QJsonObject &header, const QString &password, QByteArray *masterKey) const;
    void loadSecurityPreferences();
    bool persistSecurityPreferences();
    void loadAuthenticationState();
    bool persistAuthenticationState();
    void refreshAuthenticationState();
    bool allowPasswordAttempt();
    void recordPasswordFailure();
    bool resetPasswordFailures();
    void startAutoLockCountdown();
    QString walletEntryKey(const QJsonObject &header) const;
    KWallet::Wallet *openVaultWallet();
    bool storeMasterKeyInWallet();
    void removeMasterKeyFromWallet();
    QByteArray makeThumbnailPng(const QString &sourcePath, const QString &mimeType) const;
    bool cacheThumbnailFromSource(const QString &objectId, const QString &sourcePath, const QString &mimeType);
    bool writeEncryptedThumbnail(const QString &objectId, const QByteArray &pngBytes);
    bool readEncryptedThumbnail(const QString &objectId, QByteArray *pngBytes) const;
    QString thumbnailCachePath(const QString &objectId) const;

    QJsonObject itemById(const QString &id) const;
    int itemIndexById(const QString &id) const;
    QString uniqueName(const QString &parentId, const QString &preferred) const;
    QString sanitizedName(const QString &name) const;
    QString iconForItem(const QJsonObject &item) const;
    QString parentPathLabel(const QString &folderId) const;
    void refreshVisibleItems();
    bool importPathRecursive(const QString &path, const QString &parentId, QStringList *createdBlobNames);
    bool exportItemRecursive(const QJsonObject &item, const QString &targetDirectory);
    void collectDescendants(const QString &id, QStringList *ids) const;

    bool m_unlocked = false;
    bool m_busy = false;
    QByteArray m_masterKey;
    QJsonObject m_header;
    QJsonObject m_manifest;
    QList<QJsonObject> m_visibleItems;
    QString m_currentFolderId = QStringLiteral("root");
    QString m_lastError;
    QString m_statusMessage;
    QHash<QString, QString> m_materializedPaths;
    QHash<QString, QString> m_thumbnailMaterializedPaths;
    bool m_kwalletEnabled = false;
    bool m_autoLockEnabled = false;
    int m_autoLockMinutes = 15;
    QTimer *m_autoLockTimer = nullptr;
    bool m_passwordProtectionEnabled = true;
    int m_maxPasswordAttempts = 5;
    int m_passwordLockoutSeconds = 300;
    int m_passwordRetrySeconds = 2;
    int m_failedPasswordAttempts = 0;
    qint64 m_passwordLockoutUntil = 0;
    qint64 m_passwordRetryUntil = 0;
    QTimer *m_authenticationTimer = nullptr;
    KWallet::Wallet *m_wallet = nullptr;
};
