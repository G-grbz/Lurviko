#include "cloudauthmanager.h"

#include <QDesktopServices>
#include <QAbstractOAuthReplyHandler>
#include <QJsonDocument>
#include <QJsonObject>
#include <QAbstractOAuth>
#include <QAbstractOAuth2>
#include <QHostAddress>
#include <KWallet>
#include <QOAuth2AuthorizationCodeFlow>
#include <QOAuthHttpServerReplyHandler>
#include <QSettings>
#include <QSet>
#include <QUrl>
#include <QUrlQuery>
#include <QTimer>

CloudAuthManager::CloudAuthManager(QObject *parent)
    : QObject(parent)
{
    QSettings settings;
    m_googleClientId = settings.value(QStringLiteral("cloud/googleClientId"),
                                      qEnvironmentVariable("GFILE_GOOGLE_CLIENT_ID", qEnvironmentVariable("AETHER_GOOGLE_CLIENT_ID"))).toString();
    // v0.4.x stored the Google client secret in QSettings. Read it once so we can
    // migrate it into KWallet, then remove the plaintext copy.
    m_googleClientSecret = settings.value(QStringLiteral("cloud/googleClientSecret"),
                                          qEnvironmentVariable("GFILE_GOOGLE_CLIENT_SECRET")).toString();
    m_oneDriveClientId = settings.value(QStringLiteral("cloud/oneDriveClientId"),
                                        qEnvironmentVariable("GFILE_ONEDRIVE_CLIENT_ID", qEnvironmentVariable("AETHER_ONEDRIVE_CLIENT_ID"))).toString();
    m_oneDriveTenant = settings.value(QStringLiteral("cloud/oneDriveTenant"),
                                      qEnvironmentVariable("GFILE_ONEDRIVE_TENANT", QStringLiteral("common"))).toString().trimmed();
    if (m_oneDriveTenant.isEmpty())
        m_oneDriveTenant = QStringLiteral("common");

    setupGoogle();
    setupOneDrive();
    // KWallet can wait for its daemon or an unlock dialog. Start after the
    // window's first render, and restore sessions when the wallet opens.
    QTimer::singleShot(500, this, [this] { ensureWallet(); });
}

CloudAuthManager::~CloudAuthManager()
{
    delete m_wallet;
    m_wallet = nullptr;
}

bool CloudAuthManager::googleConfigured() const { return !m_googleClientId.trimmed().isEmpty(); }
bool CloudAuthManager::oneDriveConfigured() const { return !m_oneDriveClientId.trimmed().isEmpty(); }
bool CloudAuthManager::googleConnected() const { return m_google && m_google->status() == QAbstractOAuth::Status::Granted && !m_google->token().isEmpty(); }
bool CloudAuthManager::oneDriveConnected() const { return m_oneDrive && m_oneDrive->status() == QAbstractOAuth::Status::Granted && !m_oneDrive->token().isEmpty(); }
QString CloudAuthManager::googleClientId() const { return m_googleClientId; }
QString CloudAuthManager::googleAccessToken() const { return m_google ? m_google->token() : QString(); }
QString CloudAuthManager::oneDriveClientId() const { return m_oneDriveClientId; }
QString CloudAuthManager::oneDriveAccessToken() const { return m_oneDrive ? m_oneDrive->token() : QString(); }

void CloudAuthManager::setGoogleAuthState(const QString &state)
{
    if (m_googleAuthState == state)
        return;
    m_googleAuthState = state;
    emit googleAuthStateChanged();
    emit authProgress(QStringLiteral("google"), state);
}

void CloudAuthManager::setOneDriveAuthState(const QString &state)
{
    if (m_oneDriveAuthState == state)
        return;
    m_oneDriveAuthState = state;
    emit oneDriveAuthStateChanged();
    emit authProgress(QStringLiteral("onedrive"), state);
}

void CloudAuthManager::setGoogleLastError(const QString &message)
{
    if (m_googleLastError == message)
        return;
    m_googleLastError = message;
    emit googleLastErrorChanged();
}

void CloudAuthManager::setOneDriveLastError(const QString &message)
{
    if (m_oneDriveLastError == message)
        return;
    m_oneDriveLastError = message;
    emit oneDriveLastErrorChanged();
}

QString CloudAuthManager::oauthErrorText(int error) const
{
    switch (static_cast<QAbstractOAuth::Error>(error)) {
    case QAbstractOAuth::Error::NoError: return QStringLiteral("No OAuth error.");
    case QAbstractOAuth::Error::NetworkError: return QStringLiteral("OAuth network error while contacting the token endpoint.");
    case QAbstractOAuth::Error::ServerError: return QStringLiteral("OAuth server error during token exchange.");
    case QAbstractOAuth::Error::OAuthTokenNotFoundError: return QStringLiteral("OAuth token response did not contain an access token.");
    case QAbstractOAuth::Error::OAuthTokenSecretNotFoundError: return QStringLiteral("OAuth token response was missing token credentials.");
    case QAbstractOAuth::Error::OAuthCallbackNotVerified: return QStringLiteral("OAuth callback could not be verified. Check the OAuth client type and loopback redirect configuration.");
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    case QAbstractOAuth::Error::ClientError: return QStringLiteral("OAuth client configuration error.");
    case QAbstractOAuth::Error::ExpiredError: return QStringLiteral("OAuth authorization expired before completion.");
#endif
    }
    return QStringLiteral("Unknown OAuth error (%1).").arg(error);
}

void CloudAuthManager::setGoogleClientId(const QString &value)
{
    const QString trimmed = value.trimmed();
    if (trimmed == m_googleClientId)
        return;
    m_googleClientId = trimmed;
    QSettings().setValue(QStringLiteral("cloud/googleClientId"), m_googleClientId);
    setupGoogle();
    emit configurationChanged();
}

void CloudAuthManager::setGoogleClientSecret(const QString &value)
{
    const QString trimmed = value.trimmed();
    if (trimmed == m_googleClientSecret)
        return;
    m_googleClientSecret = trimmed;
    storeGoogleSecret(m_googleClientSecret);
    QSettings().remove(QStringLiteral("cloud/googleClientSecret"));
    setupGoogle();
    if (!m_googleRefreshToken.isEmpty())
        QTimer::singleShot(0, this, &CloudAuthManager::restoreGoogleSession);
    emit configurationChanged();
}

void CloudAuthManager::setOneDriveClientId(const QString &value)
{
    const QString trimmed = value.trimmed();
    if (trimmed == m_oneDriveClientId)
        return;
    m_oneDriveClientId = trimmed;
    m_oneDriveRefreshToken.clear();
    clearOneDriveStoredSession();
    QSettings().setValue(QStringLiteral("cloud/oneDriveClientId"), m_oneDriveClientId);
    setupOneDrive();
    emit oneDriveConnectedChanged();
    emit configurationChanged();
}

void CloudAuthManager::setOneDriveTenant(const QString &value)
{
    QString trimmed = value.trimmed();
    if (trimmed.isEmpty())
        trimmed = QStringLiteral("common");
    if (trimmed == m_oneDriveTenant)
        return;
    m_oneDriveTenant = trimmed;
    m_oneDriveRefreshToken.clear();
    clearOneDriveStoredSession();
    QSettings().setValue(QStringLiteral("cloud/oneDriveTenant"), m_oneDriveTenant);
    setupOneDrive();
    emit oneDriveConnectedChanged();
    emit configurationChanged();
}

bool CloudAuthManager::ensureWallet()
{
    if (m_wallet && m_wallet->isOpen() && m_walletReady)
        return true;
    if (m_walletOpening)
        return false;
    if (m_wallet) {
        m_wallet->deleteLater();
        m_wallet = nullptr;
    }
    m_walletReady = false;

    m_wallet = KWallet::Wallet::openWallet(KWallet::Wallet::LocalWallet(), 0, KWallet::Wallet::Asynchronous);
    if (!m_wallet)
        return false;
    m_walletOpening = true;
    connect(m_wallet, &KWallet::Wallet::walletOpened, this, [this](bool success) {
        m_walletOpening = false;
        if (!success || !m_wallet || !m_wallet->isOpen()) {
            if (m_wallet) {
                m_wallet->deleteLater();
                m_wallet = nullptr;
            }
            return;
        }
        const QString folder = QStringLiteral("g-File");
        if ((!m_wallet->hasFolder(folder) && !m_wallet->createFolder(folder))
                || !m_wallet->setFolder(folder)) {
            m_wallet->deleteLater();
            m_wallet = nullptr;
            return;
        }
        m_walletReady = true;
        loadGoogleSecretsFromWallet();
        if (m_google && !m_googleClientSecret.isEmpty())
            m_google->setClientIdentifierSharedKey(m_googleClientSecret);
        if (!m_googleClientSecret.isEmpty())
            queueWalletChange(QStringLiteral("googleClientSecret"), m_googleClientSecret);
        flushWalletChanges();
        QTimer::singleShot(0, this, &CloudAuthManager::restoreGoogleSession);
        QTimer::singleShot(0, this, &CloudAuthManager::restoreOneDriveSession);
    });
    return false;
}

void CloudAuthManager::loadGoogleSecretsFromWallet()
{
    if (!m_wallet || !m_walletReady || !m_wallet->isOpen())
        return;

    QString value;
    if (!m_pendingWalletWrites.contains(QStringLiteral("googleClientSecret"))
            && !m_pendingWalletRemovals.contains(QStringLiteral("googleClientSecret"))
            && m_wallet->readPassword(QStringLiteral("googleClientSecret"), value) == 0 && !value.isEmpty())
        m_googleClientSecret = value;
    value.clear();
    if (!m_pendingWalletWrites.contains(QStringLiteral("googleRefreshToken"))
            && !m_pendingWalletRemovals.contains(QStringLiteral("googleRefreshToken"))
            && m_wallet->readPassword(QStringLiteral("googleRefreshToken"), value) == 0 && !value.isEmpty())
        m_googleRefreshToken = value;
    value.clear();
    if (!m_pendingWalletWrites.contains(QStringLiteral("oneDriveRefreshToken"))
            && !m_pendingWalletRemovals.contains(QStringLiteral("oneDriveRefreshToken"))
            && m_wallet->readPassword(QStringLiteral("oneDriveRefreshToken"), value) == 0 && !value.isEmpty())
        m_oneDriveRefreshToken = value;
}

void CloudAuthManager::queueWalletChange(const QString &key, const QString &value)
{
    if (value.isEmpty()) {
        m_pendingWalletWrites.remove(key);
        m_pendingWalletRemovals.insert(key);
    } else {
        m_pendingWalletRemovals.remove(key);
        m_pendingWalletWrites.insert(key, value);
    }
    if (ensureWallet())
        flushWalletChanges();
}

void CloudAuthManager::flushWalletChanges()
{
    if (!m_wallet || !m_walletReady || !m_wallet->isOpen())
        return;
    for (const QString &key : m_pendingWalletRemovals.values()) {
        if (m_wallet->removeEntry(key) == 0)
            m_pendingWalletRemovals.remove(key);
    }
    for (const QString &key : m_pendingWalletWrites.keys()) {
        if (m_wallet->writePassword(key, m_pendingWalletWrites.value(key)) != 0)
            continue;
        m_pendingWalletWrites.remove(key);
        if (key == QStringLiteral("googleClientSecret"))
            QSettings().remove(QStringLiteral("cloud/googleClientSecret"));
    }
}

void CloudAuthManager::storeGoogleSecret(const QString &secret)
{
    queueWalletChange(QStringLiteral("googleClientSecret"), secret);
}

void CloudAuthManager::storeGoogleRefreshToken(const QString &token)
{
    queueWalletChange(QStringLiteral("googleRefreshToken"), token);
}

void CloudAuthManager::clearGoogleStoredSession()
{
    queueWalletChange(QStringLiteral("googleRefreshToken"), QString());
}

bool CloudAuthManager::handleGoogleRestoreFailure()
{
    // A Testing-mode Google OAuth refresh token can expire after seven days.
    // If automatic session restore fails, do not trap the UI in an error state:
    // discard the stale refresh token and let the next card click start a fresh
    // browser authorization flow. Keep the special state long enough to absorb
    // duplicate error signals emitted for the same failed refresh request.
    if (m_googleAuthState != QStringLiteral("restoring")
            && m_googleAuthState != QStringLiteral("reauthorize_required"))
        return false;

    if (m_google) {
        m_google->setToken(QString());
        m_google->setRefreshToken(QString());
    }
    m_googleRefreshToken.clear();
    clearGoogleStoredSession();
    setGoogleLastError(QString());
    setGoogleAuthState(QStringLiteral("reauthorize_required"));
    emit googleConnectedChanged();
    emit googleAccessTokenChanged();
    return true;
}

void CloudAuthManager::storeOneDriveRefreshToken(const QString &token)
{
    queueWalletChange(QStringLiteral("oneDriveRefreshToken"), token);
}

void CloudAuthManager::clearOneDriveStoredSession()
{
    queueWalletChange(QStringLiteral("oneDriveRefreshToken"), QString());
}

bool CloudAuthManager::handleOneDriveRestoreFailure()
{
    // If Microsoft rejects the stored refresh token during silent restore,
    // discard it instead of trapping the UI in the same error state. The next
    // OneDrive card click will then start a fresh browser authorization flow.
    if (m_oneDriveAuthState != QStringLiteral("restoring")
            && m_oneDriveAuthState != QStringLiteral("reauthorize_required"))
        return false;

    if (m_oneDrive) {
        m_oneDrive->setToken(QString());
        m_oneDrive->setRefreshToken(QString());
    }
    m_oneDriveRefreshToken.clear();
    clearOneDriveStoredSession();
    setOneDriveLastError(QString());
    setOneDriveAuthState(QStringLiteral("reauthorize_required"));
    emit oneDriveConnectedChanged();
    emit oneDriveAccessTokenChanged();
    return true;
}

void CloudAuthManager::restoreGoogleSession()
{
    if (!m_google || !googleConfigured() || m_googleRefreshToken.isEmpty())
        return;

    setGoogleLastError(QString());
    setGoogleAuthState(QStringLiteral("restoring"));
    m_google->setClientIdentifier(m_googleClientId);
    m_google->setClientIdentifierSharedKey(m_googleClientSecret);
    m_google->setRefreshToken(m_googleRefreshToken);
    m_google->refreshTokens();
}

void CloudAuthManager::restoreOneDriveSession()
{
    if (!m_oneDrive || !oneDriveConfigured() || m_oneDriveRefreshToken.isEmpty())
        return;

    setOneDriveLastError(QString());
    setOneDriveAuthState(QStringLiteral("restoring"));
    m_oneDrive->setClientIdentifier(m_oneDriveClientId);
    m_oneDrive->setRefreshToken(m_oneDriveRefreshToken);
    m_oneDrive->refreshTokens();
}

void CloudAuthManager::setupGoogle()
{
    if (m_google)
        m_google->deleteLater();
    if (m_googleReply)
        m_googleReply->deleteLater();

    m_google = new QOAuth2AuthorizationCodeFlow(this);
    m_googleReply = new QOAuthHttpServerReplyHandler(this);
    // Google recommends the loopback IP flow for Linux/Windows/macOS desktop apps.
    // Use the literal IPv4 loopback host and root callback path for maximum compatibility.
    m_googleReply->setCallbackHost(QStringLiteral("127.0.0.1"));
    m_googleReply->setCallbackPath(QStringLiteral("/"));
    m_googleReply->setCallbackText(QStringLiteral("g-File: Google Drive authorization completed. You can close this tab and return to g-File."));

    m_google->setAuthorizationUrl(QUrl(QStringLiteral("https://accounts.google.com/o/oauth2/v2/auth")));
    m_google->setTokenUrl(QUrl(QStringLiteral("https://oauth2.googleapis.com/token")));
    m_google->setClientIdentifier(m_googleClientId);
    m_google->setClientIdentifierSharedKey(m_googleClientSecret);
    m_google->setRequestedScopeTokens(QSet<QByteArray>{
        QByteArrayLiteral("https://www.googleapis.com/auth/drive")
    });
    m_google->setPkceMethod(QOAuth2AuthorizationCodeFlow::PkceMethod::S256);
    m_google->setAutoRefresh(true);
    m_google->setReplyHandler(m_googleReply);
    if (!m_googleRefreshToken.isEmpty())
        m_google->setRefreshToken(m_googleRefreshToken);
    m_google->setModifyParametersFunction([](QAbstractOAuth::Stage stage, QMultiMap<QString, QVariant> *parameters) {
        if (!parameters)
            return;
        if (stage == QAbstractOAuth::Stage::RequestingAuthorization) {
            parameters->insert(QStringLiteral("access_type"), QStringLiteral("offline"));
            parameters->insert(QStringLiteral("prompt"), QStringLiteral("consent"));
        }
    });

    connect(m_google, &QAbstractOAuth::authorizeWithBrowser, this, [this](const QUrl &url) {
        setGoogleAuthState(QStringLiteral("waiting_browser"));
        QDesktopServices::openUrl(url);
    });
    connect(m_googleReply, &QOAuthHttpServerReplyHandler::callbackReceived, this,
            [this](const QVariantMap &) {
        setGoogleAuthState(QStringLiteral("exchanging_token"));
    });
    connect(m_google, &QAbstractOAuth::tokenChanged, this, [this](const QString &token) {
        if (!token.isEmpty()) {
            setGoogleAuthState(QStringLiteral("connected"));
            emit googleConnectedChanged();
            emit googleAccessTokenChanged();
        }
    });
    connect(m_google, &QAbstractOAuth2::refreshTokenChanged, this, [this](const QString &token) {
        if (token.isEmpty())
            return;
        m_googleRefreshToken = token;
        storeGoogleRefreshToken(token);
    });
    connect(m_google, &QAbstractOAuth::statusChanged, this, [this](QAbstractOAuth::Status status) {
        if (status == QAbstractOAuth::Status::Granted) {
            setGoogleAuthState(QStringLiteral("connected"));
            emit googleConnectedChanged();
            emit googleAccessTokenChanged();
        }
    });
    connect(m_google, &QAbstractOAuth::granted, this, [this]() {
        m_googleReply->close();
        setGoogleAuthState(QStringLiteral("connected"));
        emit googleConnectedChanged();
        emit googleAccessTokenChanged();
    });
    connect(m_googleReply, &QAbstractOAuthReplyHandler::replyDataReceived, this, [this](const QByteArray &data) {
        const QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isObject())
            return;
        const QJsonObject obj = doc.object();
        if (!obj.contains(QStringLiteral("error")))
            return;
        const QString error = obj.value(QStringLiteral("error")).toString();
        const QString description = obj.value(QStringLiteral("error_description")).toString();
        const QString detail = description.isEmpty() ? error : QStringLiteral("%1: %2").arg(error, description);
        setGoogleLastError(detail);
    });
    connect(m_googleReply, &QAbstractOAuthReplyHandler::tokenRequestErrorOccurred, this,
            [this](QAbstractOAuth::Error error, const QString &errorString) {
        if (handleGoogleRestoreFailure())
            return;
        const QString generic = oauthErrorText(static_cast<int>(error));
        const QString detail = !m_googleLastError.isEmpty() ? m_googleLastError
                              : (!errorString.trimmed().isEmpty() ? errorString.trimmed() : generic);
        setGoogleLastError(detail);
        m_googleReply->close();
        setGoogleAuthState(QStringLiteral("error"));
        emit authError(QStringLiteral("google"), detail);
    });
    connect(m_google, &QAbstractOAuth::requestFailed, this, [this](QAbstractOAuth::Error error) {
        if (handleGoogleRestoreFailure())
            return;
        m_googleReply->close();
        setGoogleAuthState(QStringLiteral("error"));
        emit googleConnectedChanged();
        if (m_googleLastError.isEmpty()) {
            const QString message = oauthErrorText(static_cast<int>(error));
            setGoogleLastError(message);
            emit authError(QStringLiteral("google"), message);
        }
    });
    connect(m_google, &QAbstractOAuth2::serverReportedErrorOccurred, this,
            [this](const QString &error, const QString &description, const QUrl &) {
        if (handleGoogleRestoreFailure())
            return;
        m_googleReply->close();
        setGoogleAuthState(QStringLiteral("error"));
        const QString message = description.trimmed().isEmpty() ? error : QStringLiteral("%1: %2").arg(error, description);
        setGoogleLastError(message);
        emit authError(QStringLiteral("google"), message);
    });

    setGoogleAuthState(QStringLiteral("idle"));
}

void CloudAuthManager::setupOneDrive()
{
    if (m_oneDrive)
        m_oneDrive->deleteLater();
    if (m_oneDriveReply)
        m_oneDriveReply->deleteLater();

    m_oneDrive = new QOAuth2AuthorizationCodeFlow(this);
    m_oneDriveReply = new QOAuthHttpServerReplyHandler(this);
    // Microsoft desktop apps using the system browser should register http://localhost.
    // Entra ignores the ephemeral localhost port while matching the redirect URI.
    m_oneDriveReply->setCallbackHost(QStringLiteral("localhost"));
    m_oneDriveReply->setCallbackPath(QStringLiteral("/"));
    m_oneDriveReply->setCallbackText(QStringLiteral("g-File: OneDrive authorization completed. You can close this tab and return to g-File."));

    const QString tenant = m_oneDriveTenant.trimmed().isEmpty() ? QStringLiteral("common") : m_oneDriveTenant.trimmed();
    const QString authorityBase = QStringLiteral("https://login.microsoftonline.com/%1/oauth2/v2.0").arg(tenant);
    m_oneDrive->setAuthorizationUrl(QUrl(authorityBase + QStringLiteral("/authorize")));
    m_oneDrive->setTokenUrl(QUrl(authorityBase + QStringLiteral("/token")));
    m_oneDrive->setClientIdentifier(m_oneDriveClientId);
    m_oneDrive->setRequestedScopeTokens(QSet<QByteArray>{
        QByteArrayLiteral("Files.ReadWrite"), QByteArrayLiteral("offline_access"),
        QByteArrayLiteral("User.Read"), QByteArrayLiteral("openid")
    });
    m_oneDrive->setPkceMethod(QOAuth2AuthorizationCodeFlow::PkceMethod::S256);
    m_oneDrive->setAutoRefresh(true);
    m_oneDrive->setReplyHandler(m_oneDriveReply);
    if (!m_oneDriveRefreshToken.isEmpty())
        m_oneDrive->setRefreshToken(m_oneDriveRefreshToken);

    connect(m_oneDrive, &QAbstractOAuth::authorizeWithBrowser, this, [this](const QUrl &url) {
        setOneDriveAuthState(QStringLiteral("waiting_browser"));
        QDesktopServices::openUrl(url);
    });
    connect(m_oneDriveReply, &QOAuthHttpServerReplyHandler::callbackReceived, this,
            [this](const QVariantMap &) { setOneDriveAuthState(QStringLiteral("exchanging_token")); });
    connect(m_oneDrive, &QAbstractOAuth::tokenChanged, this, [this](const QString &token) {
        if (!token.isEmpty()) {
            setOneDriveAuthState(QStringLiteral("connected"));
            emit oneDriveConnectedChanged();
            emit oneDriveAccessTokenChanged();
        }
    });
    connect(m_oneDrive, &QAbstractOAuth2::refreshTokenChanged, this, [this](const QString &token) {
        if (token.isEmpty())
            return;
        m_oneDriveRefreshToken = token;
        storeOneDriveRefreshToken(token);
    });
    connect(m_oneDrive, &QAbstractOAuth::statusChanged, this, [this](QAbstractOAuth::Status status) {
        if (status == QAbstractOAuth::Status::Granted) {
            setOneDriveAuthState(QStringLiteral("connected"));
            emit oneDriveConnectedChanged();
            emit oneDriveAccessTokenChanged();
        }
    });
    connect(m_oneDrive, &QAbstractOAuth::granted, this, [this]() {
        m_oneDriveReply->close();
        setOneDriveAuthState(QStringLiteral("connected"));
        emit oneDriveConnectedChanged();
        emit oneDriveAccessTokenChanged();
    });
    connect(m_oneDriveReply, &QAbstractOAuthReplyHandler::replyDataReceived, this, [this](const QByteArray &data) {
        const QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isObject())
            return;
        const QJsonObject obj = doc.object();
        if (!obj.contains(QStringLiteral("error")))
            return;
        const QString error = obj.value(QStringLiteral("error")).toString();
        const QString description = obj.value(QStringLiteral("error_description")).toString();
        setOneDriveLastError(description.isEmpty() ? error : QStringLiteral("%1: %2").arg(error, description));
    });
    connect(m_oneDriveReply, &QAbstractOAuthReplyHandler::tokenRequestErrorOccurred, this,
            [this](QAbstractOAuth::Error error, const QString &errorString) {
        if (handleOneDriveRestoreFailure())
            return;
        const QString generic = oauthErrorText(static_cast<int>(error));
        const QString detail = !m_oneDriveLastError.isEmpty() ? m_oneDriveLastError
                              : (!errorString.trimmed().isEmpty() ? errorString.trimmed() : generic);
        setOneDriveLastError(detail);
        m_oneDriveReply->close();
        setOneDriveAuthState(QStringLiteral("error"));
        emit authError(QStringLiteral("onedrive"), detail);
    });
    connect(m_oneDrive, &QAbstractOAuth::requestFailed, this, [this](QAbstractOAuth::Error error) {
        if (handleOneDriveRestoreFailure())
            return;
        m_oneDriveReply->close();
        setOneDriveAuthState(QStringLiteral("error"));
        emit oneDriveConnectedChanged();
        if (m_oneDriveLastError.isEmpty()) {
            const QString message = oauthErrorText(static_cast<int>(error));
            setOneDriveLastError(message);
            emit authError(QStringLiteral("onedrive"), message);
        }
    });
    connect(m_oneDrive, &QAbstractOAuth2::serverReportedErrorOccurred, this,
            [this](const QString &error, const QString &description, const QUrl &) {
        if (handleOneDriveRestoreFailure())
            return;
        m_oneDriveReply->close();
        setOneDriveAuthState(QStringLiteral("error"));
        const QString message = description.trimmed().isEmpty() ? error : QStringLiteral("%1: %2").arg(error, description);
        setOneDriveLastError(message);
        emit authError(QStringLiteral("onedrive"), message);
    });

    setOneDriveAuthState(QStringLiteral("idle"));
}

void CloudAuthManager::connectGoogle()
{
    if (!googleConfigured()) {
        emit authError(QStringLiteral("google"), QStringLiteral("Google OAuth client ID is not configured."));
        return;
    }

    m_googleReply->close();
    setGoogleLastError(QString());
    if (!m_googleReply->listen(QHostAddress::LocalHost, 0)) {
        setGoogleAuthState(QStringLiteral("error"));
        emit authError(QStringLiteral("google"), QStringLiteral("g-File could not start the local OAuth callback listener on 127.0.0.1."));
        return;
    }

    setGoogleAuthState(QStringLiteral("starting"));
    emit authStarted(QStringLiteral("google"));
    m_google->grant();
}

void CloudAuthManager::connectOneDrive()
{
    if (!oneDriveConfigured()) {
        emit authError(QStringLiteral("onedrive"), QStringLiteral("OneDrive OAuth client ID is not configured."));
        return;
    }

    m_oneDriveReply->close();
    setOneDriveLastError(QString());
    if (m_oneDriveAuthState == QStringLiteral("reauthorize_required")) {
        m_oneDrive->setToken(QString());
        m_oneDrive->setRefreshToken(QString());
        m_oneDriveRefreshToken.clear();
        clearOneDriveStoredSession();
    }
    if (!m_oneDriveReply->listen(QHostAddress::LocalHost, 0)) {
        setOneDriveAuthState(QStringLiteral("error"));
        emit authError(QStringLiteral("onedrive"), QStringLiteral("g-File could not start the local OAuth callback listener on localhost."));
        return;
    }

    setOneDriveAuthState(QStringLiteral("starting"));
    emit authStarted(QStringLiteral("onedrive"));
    m_oneDrive->grant();
}

void CloudAuthManager::disconnectGoogle()
{
    if (m_google) {
        m_google->setToken(QString());
        m_google->setRefreshToken(QString());
    }
    m_googleRefreshToken.clear();
    clearGoogleStoredSession();
    setGoogleAuthState(QStringLiteral("idle"));
    emit googleConnectedChanged();
    emit googleAccessTokenChanged();
}

void CloudAuthManager::openGoogleDriveItem(const QString &urlString)
{
    const QUrl url(urlString);
    if (url.scheme() != QStringLiteral("gdrive"))
        return;

    QString fileId;
    if (url.host() == QStringLiteral("file"))
        fileId = url.path().mid(1);
    else if (url.host() == QStringLiteral("folder"))
        fileId = url.path().mid(1);

    if (fileId.isEmpty())
        return;

    QUrl webUrl(QStringLiteral("https://drive.google.com/open"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("id"), fileId);
    webUrl.setQuery(query);
    QDesktopServices::openUrl(webUrl);
}

void CloudAuthManager::resetOneDriveConfiguration()
{
    if (m_oneDrive) {
        m_oneDrive->setToken(QString());
        m_oneDrive->setRefreshToken(QString());
    }
    m_oneDriveClientId.clear();
    m_oneDriveTenant = QStringLiteral("common");
    m_oneDriveRefreshToken.clear();
    clearOneDriveStoredSession();
    QSettings settings;
    settings.remove(QStringLiteral("cloud/oneDriveClientId"));
    settings.remove(QStringLiteral("cloud/oneDriveTenant"));
    setOneDriveLastError(QString());
    setupOneDrive();
    emit configurationChanged();
    emit oneDriveConnectedChanged();
    emit oneDriveAccessTokenChanged();
}

void CloudAuthManager::disconnectOneDrive()
{
    if (m_oneDrive) {
        m_oneDrive->setToken(QString());
        m_oneDrive->setRefreshToken(QString());
    }
    m_oneDriveRefreshToken.clear();
    clearOneDriveStoredSession();
    setOneDriveAuthState(QStringLiteral("idle"));
    emit oneDriveConnectedChanged();
    emit oneDriveAccessTokenChanged();
}
