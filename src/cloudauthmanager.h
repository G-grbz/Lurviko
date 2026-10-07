#pragma once

#include <QObject>
#include <QHash>
#include <QSet>

class QOAuth2AuthorizationCodeFlow;
class QOAuthHttpServerReplyHandler;

namespace KWallet { class Wallet; }

class CloudAuthManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool googleConfigured READ googleConfigured NOTIFY configurationChanged)
    Q_PROPERTY(bool oneDriveConfigured READ oneDriveConfigured NOTIFY configurationChanged)
    Q_PROPERTY(bool googleConnected READ googleConnected NOTIFY googleConnectedChanged)
    Q_PROPERTY(bool oneDriveConnected READ oneDriveConnected NOTIFY oneDriveConnectedChanged)
    Q_PROPERTY(QString googleClientId READ googleClientId WRITE setGoogleClientId NOTIFY configurationChanged)
    Q_PROPERTY(QString googleClientSecret READ googleClientSecret WRITE setGoogleClientSecret NOTIFY configurationChanged)
    Q_PROPERTY(QString googleAccessToken READ googleAccessToken NOTIFY googleAccessTokenChanged)
    Q_PROPERTY(QString oneDriveClientId READ oneDriveClientId WRITE setOneDriveClientId NOTIFY configurationChanged)
    Q_PROPERTY(QString oneDriveAccessToken READ oneDriveAccessToken NOTIFY oneDriveAccessTokenChanged)
    Q_PROPERTY(QString oneDriveTenant READ oneDriveTenant WRITE setOneDriveTenant NOTIFY configurationChanged)
    Q_PROPERTY(QString oneDriveRedirectUri READ oneDriveRedirectUri CONSTANT)
    Q_PROPERTY(QString googleAuthState READ googleAuthState NOTIFY googleAuthStateChanged)
    Q_PROPERTY(QString googleLastError READ googleLastError NOTIFY googleLastErrorChanged)
    Q_PROPERTY(QString oneDriveAuthState READ oneDriveAuthState NOTIFY oneDriveAuthStateChanged)
    Q_PROPERTY(QString oneDriveLastError READ oneDriveLastError NOTIFY oneDriveLastErrorChanged)

public:
    explicit CloudAuthManager(QObject *parent = nullptr);
    ~CloudAuthManager() override;

    bool googleConfigured() const;
    bool oneDriveConfigured() const;
    bool googleConnected() const;
    bool oneDriveConnected() const;

    QString googleClientId() const;
    QString googleClientSecret() const { return m_googleClientSecret; }
    QString googleAccessToken() const;
    QString oneDriveClientId() const;
    QString oneDriveAccessToken() const;
    QString oneDriveTenant() const { return m_oneDriveTenant; }
    QString oneDriveRedirectUri() const { return QStringLiteral("http://localhost"); }
    QString googleAuthState() const { return m_googleAuthState; }
    QString googleLastError() const { return m_googleLastError; }
    QString oneDriveAuthState() const { return m_oneDriveAuthState; }
    QString oneDriveLastError() const { return m_oneDriveLastError; }

    void setGoogleClientId(const QString &value);
    void setGoogleClientSecret(const QString &value);
    void setOneDriveClientId(const QString &value);
    void setOneDriveTenant(const QString &value);

    Q_INVOKABLE void connectGoogle();
    Q_INVOKABLE void connectOneDrive();
    Q_INVOKABLE void disconnectGoogle();
    Q_INVOKABLE void disconnectOneDrive();
    Q_INVOKABLE void resetOneDriveConfiguration();
    Q_INVOKABLE void openGoogleDriveItem(const QString &urlString);

signals:
    void configurationChanged();
    void googleConnectedChanged();
    void googleAccessTokenChanged();
    void oneDriveConnectedChanged();
    void oneDriveAccessTokenChanged();
    void googleAuthStateChanged();
    void googleLastErrorChanged();
    void oneDriveAuthStateChanged();
    void oneDriveLastErrorChanged();
    void authError(const QString &provider, const QString &message);
    void authStarted(const QString &provider);
    void authProgress(const QString &provider, const QString &state);

private:
    void setupGoogle();
    void setupOneDrive();
    void restoreGoogleSession();
    void restoreOneDriveSession();
    bool ensureWallet();
    void queueWalletChange(const QString &key, const QString &value);
    void flushWalletChanges();
    void loadGoogleSecretsFromWallet();
    void storeGoogleSecret(const QString &secret);
    void storeGoogleRefreshToken(const QString &token);
    void clearGoogleStoredSession();
    bool handleGoogleRestoreFailure();
    void storeOneDriveRefreshToken(const QString &token);
    void clearOneDriveStoredSession();
    bool handleOneDriveRestoreFailure();
    void setGoogleAuthState(const QString &state);
    void setGoogleLastError(const QString &message);
    void setOneDriveAuthState(const QString &state);
    void setOneDriveLastError(const QString &message);
    QString oauthErrorText(int error) const;

    QOAuth2AuthorizationCodeFlow *m_google = nullptr;
    QOAuth2AuthorizationCodeFlow *m_oneDrive = nullptr;
    QOAuthHttpServerReplyHandler *m_googleReply = nullptr;
    QOAuthHttpServerReplyHandler *m_oneDriveReply = nullptr;
    KWallet::Wallet *m_wallet = nullptr;
    bool m_walletOpening = false;
    bool m_walletReady = false;
    QHash<QString, QString> m_pendingWalletWrites;
    QSet<QString> m_pendingWalletRemovals;
    QString m_googleClientId;
    QString m_googleClientSecret;
    QString m_googleRefreshToken;
    QString m_oneDriveClientId;
    QString m_oneDriveTenant = QStringLiteral("common");
    QString m_oneDriveRefreshToken;
    QString m_googleAuthState = QStringLiteral("idle");
    QString m_googleLastError;
    QString m_oneDriveAuthState = QStringLiteral("idle");
    QString m_oneDriveLastError;
};
