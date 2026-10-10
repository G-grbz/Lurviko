#include "privatevaultmanager.h"
#include "vaultsecurity.h"

#include <QCoreApplication>
#include <QBuffer>
#include <QImage>
#include <QImageReader>
#include <QProcess>
#include <QTimer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMimeDatabase>
#include <QSet>
#include <QSaveFile>
#include <QSettings>
#include "appmigration.h"
#include <QLocale>
#include <QStandardPaths>
#include <QUrl>
#include <KWallet>
#include <QUuid>
#include <algorithm>
#include <limits>
#include <utility>

#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/params.h>
#include <openssl/rand.h>

namespace {
// DO NOT MODIFY legacy magic/AAD identifiers: existing encrypted vaults
// authenticate these exact bytes, regardless of the application's display name.
constexpr char kVaultMagic[] = "g-file-private-vault";
constexpr char kBlobMagic[] = "GFBLOB01";
constexpr char kThumbMagic[] = "GFTHMB01";
constexpr qsizetype kThumbMagicSize = 8;
constexpr qsizetype kBlobMagicSize = 8;
constexpr qint64 kChunkSize = 1024 * 1024;

QString securityMessage(const char *english, const char *turkish)
{
    const QString fallback = QLocale::system().language() == QLocale::Turkish ? QStringLiteral("tr") : QStringLiteral("en");
    return QString::fromUtf8(QSettings().value(QStringLiteral("ui/language"), fallback).toString() == QStringLiteral("tr")
                                ? turkish : english);
}

QByteArray b64(const QByteArray &value)
{
    return value.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

QByteArray unb64(const QJsonValue &value)
{
    return QByteArray::fromBase64(value.toString().toUtf8(), QByteArray::Base64UrlEncoding);
}

QString urlToLocalPath(const QVariant &value)
{
    const QString raw = value.toString();
    const QUrl url(raw);
    if (url.isValid() && url.isLocalFile())
        return url.toLocalFile();
    return raw;
}
}

PrivateVaultManager::PrivateVaultManager(QObject *parent)
    : QAbstractListModel(parent)
{
    ensureStorageLayout();
    cleanupRuntimeFiles();
    loadSecurityPreferences();
    m_authenticationTimer = new QTimer(this);
    m_authenticationTimer->setInterval(1000);
    connect(m_authenticationTimer, &QTimer::timeout, this, &PrivateVaultManager::refreshAuthenticationState);
    loadAuthenticationState();
    m_autoLockTimer = new QTimer(this);
    m_autoLockTimer->setSingleShot(true);
    connect(m_autoLockTimer, &QTimer::timeout, this, [this]() {
        if (m_unlocked) {
            setStatus(tr("Sana Özel kasası süre dolduğu için otomatik kilitlendi."));
            lock();
        }
    });
}

PrivateVaultManager::~PrivateVaultManager()
{
    lock();
    if (!m_runtimeRoot.isEmpty())
        QDir(m_runtimeRoot).removeRecursively();
    if (m_wallet) {
        delete m_wallet;
        m_wallet = nullptr;
    }
}

int PrivateVaultManager::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_visibleItems.size();
}

QVariant PrivateVaultManager::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_visibleItems.size())
        return {};
    const QJsonObject item = m_visibleItems.at(index.row());
    switch (role) {
    case ObjectIdRole: return item.value(QStringLiteral("id")).toString();
    case NameRole: return item.value(QStringLiteral("name")).toString();
    case IsDirRole: return item.value(QStringLiteral("dir")).toBool();
    case SizeRole: return item.value(QStringLiteral("size")).toVariant();
    case MimeTypeRole: return item.value(QStringLiteral("mime")).toString();
    case ModifiedMsRole: return item.value(QStringLiteral("modifiedMs")).toVariant();
    case IconNameRole: return iconForItem(item);
    default: return {};
    }
}

QHash<int, QByteArray> PrivateVaultManager::roleNames() const
{
    return {
        { ObjectIdRole, "objectId" },
        { NameRole, "name" },
        { IsDirRole, "isDir" },
        { SizeRole, "size" },
        { MimeTypeRole, "mimeType" },
        { ModifiedMsRole, "modifiedMs" },
        { IconNameRole, "iconName" }
    };
}

QString PrivateVaultManager::vaultRoot() const
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral(".private-vault"));
}

QString PrivateVaultManager::objectsRoot() const
{
    return QDir(vaultRoot()).filePath(QStringLiteral("objects"));
}

QString PrivateVaultManager::thumbnailsRoot() const
{
    return QDir(vaultRoot()).filePath(QStringLiteral("thumbnails"));
}

QString PrivateVaultManager::headerPath() const
{
    return QDir(vaultRoot()).filePath(QStringLiteral("vault.json"));
}

QString PrivateVaultManager::runtimeRoot() const
{
    if (m_runtimeRoot.isEmpty())
        m_runtimeRoot = VaultSecurity::createRuntimeDirectory(
                QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation));
    return m_runtimeRoot;
}

bool PrivateVaultManager::ensureRuntimeDirectory()
{
    if (!runtimeRoot().isEmpty())
        return true;
    setError(securityMessage("A memory-backed temporary directory is required to preview private files.",
                             "Özel dosyaları önizlemek için bellekte tutulan bir geçici dizin gerekli."));
    return false;
}

bool PrivateVaultManager::exists() const
{
    return QFileInfo::exists(headerPath());
}

int PrivateVaultManager::totalItemCount() const
{
    return m_unlocked ? m_manifest.value(QStringLiteral("items")).toArray().size() : 0;
}

QString PrivateVaultManager::currentPathLabel() const
{
    if (!m_unlocked)
        return QStringLiteral("Sana Özel");
    return parentPathLabel(m_currentFolderId);
}

void PrivateVaultManager::setBusy(bool value)
{
    if (m_busy == value)
        return;
    m_busy = value;
    emit busyChanged();
}

void PrivateVaultManager::setError(const QString &message)
{
    if (m_lastError == message)
        return;
    m_lastError = message;
    emit lastErrorChanged();
}

void PrivateVaultManager::setStatus(const QString &message)
{
    if (m_statusMessage == message)
        return;
    m_statusMessage = message;
    emit statusMessageChanged();
}

void PrivateVaultManager::clearStatus()
{
    setError({});
    setStatus({});
}

void PrivateVaultManager::secureClear(QByteArray &bytes)
{
    if (!bytes.isEmpty())
        OPENSSL_cleanse(bytes.data(), static_cast<size_t>(bytes.size()));
    bytes.clear();
    bytes.squeeze();
}

bool PrivateVaultManager::ensureStorageLayout()
{
    QDir root;
    if (!root.mkpath(vaultRoot()) || !root.mkpath(objectsRoot()) || !root.mkpath(thumbnailsRoot()))
        return false;
    QFile::setPermissions(vaultRoot(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    QFile::setPermissions(objectsRoot(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    QFile::setPermissions(thumbnailsRoot(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    return true;
}

void PrivateVaultManager::cleanupRuntimeFiles()
{
    // Keep the private directory itself until destruction; deleting and
    // recreating it would introduce a race in a shared /dev/shm parent.
    if (!m_runtimeRoot.isEmpty()) {
        const QFileInfoList files = QDir(m_runtimeRoot).entryInfoList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
        for (const QFileInfo &file : files)
            QFile::remove(file.absoluteFilePath());
    }
    m_materializedPaths.clear();
    m_thumbnailMaterializedPaths.clear();
}

void PrivateVaultManager::cleanupOrphanBlobs()
{
    if (!m_unlocked)
        return;
    QSet<QString> referenced;
    const QJsonArray items = m_manifest.value(QStringLiteral("items")).toArray();
    for (const QJsonValue &value : items) {
        const QString blob = value.toObject().value(QStringLiteral("blob")).toString();
        if (!blob.isEmpty())
            referenced.insert(blob);
    }
    const QFileInfoList files = QDir(objectsRoot()).entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &file : files) {
        if (!referenced.contains(file.fileName()))
            QFile::remove(file.absoluteFilePath());
    }
}

void PrivateVaultManager::cleanupOrphanThumbnails()
{
    if (!m_unlocked)
        return;
    QSet<QString> referenced;
    const QJsonArray items = m_manifest.value(QStringLiteral("items")).toArray();
    for (const QJsonValue &value : items) {
        const QJsonObject item = value.toObject();
        if (!item.value(QStringLiteral("dir")).toBool())
            referenced.insert(item.value(QStringLiteral("id")).toString() + QStringLiteral(".gfthumb"));
    }
    const QFileInfoList files = QDir(thumbnailsRoot()).entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &file : files) {
        if (!referenced.contains(file.fileName()))
            QFile::remove(file.absoluteFilePath());
    }
}

bool PrivateVaultManager::derivePasswordKey(const QString &password, const QByteArray &salt,
                                            QByteArray *outKey, const QJsonObject *params) const
{
    if (!outKey || salt.size() != SaltSize)
        return false;
    VaultSecurity::Argon2Parameters parameters;
    if (params && !VaultSecurity::parseArgon2Parameters(*params, &parameters))
        return false;
    QByteArray pass = password.toUtf8();
    if (pass.isEmpty())
        return false;

    EVP_KDF *kdf = EVP_KDF_fetch(nullptr, "ARGON2ID", nullptr);
    if (!kdf) {
        OPENSSL_cleanse(pass.data(), static_cast<size_t>(pass.size()));
        return false;
    }
    EVP_KDF_CTX *ctx = EVP_KDF_CTX_new(kdf);
    EVP_KDF_free(kdf);
    if (!ctx) {
        OPENSSL_cleanse(pass.data(), static_cast<size_t>(pass.size()));
        return false;
    }

    QByteArray key(KeySize, Qt::Uninitialized);
    OSSL_PARAM kdfParams[] = {
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_PASSWORD, pass.data(), static_cast<size_t>(pass.size())),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT, const_cast<char *>(salt.constData()), static_cast<size_t>(salt.size())),
        OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ITER, &parameters.iterations),
        OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ARGON2_MEMCOST, &parameters.memoryKiB),
        OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ARGON2_LANES, &parameters.lanes),
        OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_THREADS, &parameters.threads),
        OSSL_PARAM_construct_end()
    };
    const int ok = EVP_KDF_derive(ctx, reinterpret_cast<unsigned char *>(key.data()), KeySize, kdfParams);
    EVP_KDF_CTX_free(ctx);
    OPENSSL_cleanse(pass.data(), static_cast<size_t>(pass.size()));
    if (ok != 1) {
        OPENSSL_cleanse(key.data(), static_cast<size_t>(key.size()));
        return false;
    }
    *outKey = key;
    return true;
}

bool PrivateVaultManager::aesGcmEncrypt(const QByteArray &plain, const QByteArray &key, const QByteArray &aad,
                                        QByteArray *iv, QByteArray *cipher, QByteArray *tag) const
{
    if (!iv || !cipher || !tag || key.size() != KeySize
            || plain.size() > std::numeric_limits<int>::max() - EVP_MAX_BLOCK_LENGTH)
        return false;
    QByteArray localIv(IvSize, Qt::Uninitialized);
    if (RAND_bytes(reinterpret_cast<unsigned char *>(localIv.data()), IvSize) != 1)
        return false;
    QByteArray out(plain.size() + 16, Qt::Uninitialized);
    QByteArray localTag(TagSize, Qt::Uninitialized);
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return false;
    int len = 0;
    int total = 0;
    bool ok = EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1
           && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, IvSize, nullptr) == 1
           && EVP_EncryptInit_ex(ctx, nullptr, nullptr,
                                 reinterpret_cast<const unsigned char *>(key.constData()),
                                 reinterpret_cast<const unsigned char *>(localIv.constData())) == 1;
    if (ok && !aad.isEmpty())
        ok = EVP_EncryptUpdate(ctx, nullptr, &len,
                               reinterpret_cast<const unsigned char *>(aad.constData()), aad.size()) == 1;
    if (ok && !plain.isEmpty()) {
        ok = EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char *>(out.data()), &len,
                               reinterpret_cast<const unsigned char *>(plain.constData()), plain.size()) == 1;
        total += len;
    }
    if (ok) {
        ok = EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char *>(out.data()) + total, &len) == 1;
        total += len;
    }
    if (ok)
        ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, TagSize, localTag.data()) == 1;
    EVP_CIPHER_CTX_free(ctx);
    if (!ok)
        return false;
    out.resize(total);
    *iv = localIv;
    *cipher = out;
    *tag = localTag;
    return true;
}

bool PrivateVaultManager::aesGcmDecrypt(const QByteArray &cipher, const QByteArray &key, const QByteArray &aad,
                                        const QByteArray &iv, const QByteArray &tag, QByteArray *plain) const
{
    if (!plain || key.size() != KeySize || iv.size() != IvSize || tag.size() != TagSize
            || cipher.size() > std::numeric_limits<int>::max() - EVP_MAX_BLOCK_LENGTH)
        return false;
    QByteArray out(cipher.size() + 16, Qt::Uninitialized);
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return false;
    int len = 0;
    int total = 0;
    bool ok = EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1
           && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, IvSize, nullptr) == 1
           && EVP_DecryptInit_ex(ctx, nullptr, nullptr,
                                 reinterpret_cast<const unsigned char *>(key.constData()),
                                 reinterpret_cast<const unsigned char *>(iv.constData())) == 1;
    if (ok && !aad.isEmpty())
        ok = EVP_DecryptUpdate(ctx, nullptr, &len,
                               reinterpret_cast<const unsigned char *>(aad.constData()), aad.size()) == 1;
    if (ok && !cipher.isEmpty()) {
        ok = EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char *>(out.data()), &len,
                               reinterpret_cast<const unsigned char *>(cipher.constData()), cipher.size()) == 1;
        total += len;
    }
    if (ok)
        ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, TagSize, const_cast<char *>(tag.constData())) == 1;
    if (ok) {
        const int finalResult = EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char *>(out.data()) + total, &len);
        ok = finalResult == 1;
        total += ok ? len : 0;
    }
    EVP_CIPHER_CTX_free(ctx);
    if (!ok) {
        if (!out.isEmpty())
            OPENSSL_cleanse(out.data(), static_cast<size_t>(out.size()));
        return false;
    }
    out.resize(total);
    *plain = out;
    return true;
}

bool PrivateVaultManager::readHeader(QJsonObject *header) const
{
    QFile file(headerPath());
    if (!file.open(QIODevice::ReadOnly) || file.size() > 65536)
        return false;
    const QByteArray bytes = file.read(65537);
    if (bytes.size() > 65536 || file.error() != QFileDevice::NoError || !file.atEnd())
        return false;
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    const QJsonObject object = doc.object();
    if (object.value(QStringLiteral("magic")).toString() != QString::fromLatin1(kVaultMagic)
            || object.value(QStringLiteral("version")).toInt() != 1)
        return false;
    VaultSecurity::Argon2Parameters parameters;
    if (!VaultSecurity::parseArgon2Parameters(object, &parameters))
        return false;
    *header = object;
    return true;
}

bool PrivateVaultManager::writeHeader(const QJsonObject &header) const
{
    QSaveFile file(headerPath());
    if (!file.open(QIODevice::WriteOnly))
        return false;
    const QByteArray bytes = QJsonDocument(header).toJson(QJsonDocument::Compact);
    if (file.write(bytes) != bytes.size())
        return false;
    if (!file.commit())
        return false;
    QFile::setPermissions(headerPath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

bool PrivateVaultManager::unwrapMasterKey(const QJsonObject &header, const QString &password, QByteArray *masterKey) const
{
    const QByteArray salt = unb64(header.value(QStringLiteral("salt")));
    QByteArray passwordKey;
    if (!derivePasswordKey(password, salt, &passwordKey, &header))
        return false;
    QByteArray plain;
    const bool ok = aesGcmDecrypt(unb64(header.value(QStringLiteral("wrappedKey"))), passwordKey,
                                  QByteArrayLiteral("g-file-private/master-v1"),
                                  unb64(header.value(QStringLiteral("wrapIv"))),
                                  unb64(header.value(QStringLiteral("wrapTag"))), &plain)
                 && plain.size() == KeySize;
    if (!passwordKey.isEmpty())
        OPENSSL_cleanse(passwordKey.data(), static_cast<size_t>(passwordKey.size()));
    if (!ok) {
        if (!plain.isEmpty())
            OPENSSL_cleanse(plain.data(), static_cast<size_t>(plain.size()));
        return false;
    }
    *masterKey = plain;
    return true;
}


void PrivateVaultManager::loadSecurityPreferences()
{
    QJsonObject header;
    if (!readHeader(&header)) {
        m_kwalletEnabled = false;
        m_autoLockEnabled = false;
        m_autoLockMinutes = 15;
        m_passwordProtectionEnabled = true;
        m_maxPasswordAttempts = 5;
        m_passwordLockoutSeconds = 300;
        m_passwordRetrySeconds = 2;
        return;
    }
    m_kwalletEnabled = header.value(QStringLiteral("kwalletEnabled")).toBool(false);
    m_autoLockEnabled = header.value(QStringLiteral("autoLockEnabled")).toBool(false);
    const int minutes = header.value(QStringLiteral("autoLockMinutes")).toInt(15);
    m_autoLockMinutes = (minutes == 5 || minutes == 15 || minutes == 30 || minutes == 60) ? minutes : 15;
    m_passwordProtectionEnabled = header.value(QStringLiteral("passwordProtectionEnabled")).toBool(true);
    m_maxPasswordAttempts = std::clamp(header.value(QStringLiteral("maxPasswordAttempts")).toInt(5), 1, 100);
    m_passwordLockoutSeconds = std::clamp(header.value(QStringLiteral("passwordLockoutSeconds")).toInt(300), 1, 86400);
    m_passwordRetrySeconds = std::clamp(header.value(QStringLiteral("passwordRetrySeconds")).toInt(2), 0, 3600);
}

bool PrivateVaultManager::persistSecurityPreferences()
{
    QJsonObject header = m_header;
    if (header.isEmpty() && !readHeader(&header))
        return false;
    if (!header.contains(QStringLiteral("vaultId")))
        header.insert(QStringLiteral("vaultId"), QUuid::createUuid().toString(QUuid::WithoutBraces));
    header.insert(QStringLiteral("kwalletEnabled"), m_kwalletEnabled);
    header.insert(QStringLiteral("autoLockEnabled"), m_autoLockEnabled);
    header.insert(QStringLiteral("autoLockMinutes"), m_autoLockMinutes);
    header.insert(QStringLiteral("passwordProtectionEnabled"), m_passwordProtectionEnabled);
    header.insert(QStringLiteral("maxPasswordAttempts"), m_maxPasswordAttempts);
    header.insert(QStringLiteral("passwordLockoutSeconds"), m_passwordLockoutSeconds);
    header.insert(QStringLiteral("passwordRetrySeconds"), m_passwordRetrySeconds);
    if (!writeHeader(header))
        return false;
    if (m_unlocked)
        m_header = header;
    return true;
}

int PrivateVaultManager::passwordWaitSeconds() const
{
    if (!m_passwordProtectionEnabled)
        return 0;
    const qint64 remaining = std::max(m_passwordLockoutUntil, m_passwordRetryUntil)
                             - QDateTime::currentMSecsSinceEpoch();
    return static_cast<int>(std::clamp<qint64>((remaining + 999) / 1000, 0, 86400));
}

bool PrivateVaultManager::passwordLockedOut() const
{
    return m_passwordProtectionEnabled && m_passwordLockoutUntil > QDateTime::currentMSecsSinceEpoch();
}

void PrivateVaultManager::loadAuthenticationState()
{
    QFile file(QDir(vaultRoot()).filePath(QStringLiteral("authentication.json")));
    QJsonObject header;
    if (file.open(QIODevice::ReadOnly) && readHeader(&header)) {
        const QJsonObject state = QJsonDocument::fromJson(file.readAll()).object();
        if (state.value(QStringLiteral("vaultId")) == header.value(QStringLiteral("vaultId"))) {
            m_failedPasswordAttempts = std::clamp(state.value(QStringLiteral("failures")).toInt(), 0, 100);
            m_passwordLockoutUntil = state.value(QStringLiteral("lockoutUntil")).toVariant().toLongLong();
            m_passwordRetryUntil = state.value(QStringLiteral("retryUntil")).toVariant().toLongLong();
        }
    }
    if (!m_passwordProtectionEnabled) {
        m_failedPasswordAttempts = 0;
        m_passwordLockoutUntil = 0;
        m_passwordRetryUntil = 0;
    }
    refreshAuthenticationState();
}

bool PrivateVaultManager::persistAuthenticationState()
{
    QJsonObject header;
    if (!readHeader(&header))
        return false;
    const QJsonObject state {
        {QStringLiteral("vaultId"), header.value(QStringLiteral("vaultId"))},
        {QStringLiteral("failures"), m_failedPasswordAttempts},
        {QStringLiteral("lockoutUntil"), static_cast<double>(m_passwordLockoutUntil)},
        {QStringLiteral("retryUntil"), static_cast<double>(m_passwordRetryUntil)}
    };
    QSaveFile file(QDir(vaultRoot()).filePath(QStringLiteral("authentication.json")));
    if (!file.open(QIODevice::WriteOnly)
            || !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner))
        return false;
    const QByteArray bytes = QJsonDocument(state).toJson(QJsonDocument::Compact);
    return file.write(bytes) == bytes.size() && file.commit();
}

void PrivateVaultManager::refreshAuthenticationState()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool changed = false;
    if (m_passwordLockoutUntil > 0 && now >= m_passwordLockoutUntil) {
        m_failedPasswordAttempts = 0;
        m_passwordLockoutUntil = 0;
        m_passwordRetryUntil = 0;
        setError({});
        changed = true;
    } else if (m_passwordRetryUntil > 0 && now >= m_passwordRetryUntil) {
        m_passwordRetryUntil = 0;
        changed = true;
    }
    if (changed && !persistAuthenticationState())
        setError(securityMessage("Could not save password attempt information.", "Parola deneme bilgisi kaydedilemedi."));
    if (passwordWaitSeconds() > 0)
        m_authenticationTimer->start();
    else
        m_authenticationTimer->stop();
    emit authenticationStateChanged();
}

bool PrivateVaultManager::allowPasswordAttempt()
{
    refreshAuthenticationState();
    if (passwordWaitSeconds() == 0)
        return true;
    setError(passwordLockedOut()
                 ? securityMessage("Too many incorrect passwords. Try again after the lockout expires.", "Çok fazla hatalı parola denemesi. Bekleme süresi dolunca tekrar deneyebilirsin.")
                 : securityMessage("Wait for the retry delay before trying another password.", "Yeni parola denemesi için bekleme süresinin dolmasını bekle."));
    return false;
}

void PrivateVaultManager::recordPasswordFailure()
{
    if (!m_passwordProtectionEnabled)
        return;
    ++m_failedPasswordAttempts;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_failedPasswordAttempts >= m_maxPasswordAttempts) {
        m_passwordLockoutUntil = now + static_cast<qint64>(m_passwordLockoutSeconds) * 1000;
        m_passwordRetryUntil = 0;
        if (m_unlocked)
            lock();
    } else {
        m_passwordRetryUntil = now + static_cast<qint64>(m_passwordRetrySeconds) * 1000;
    }
    if (!persistAuthenticationState())
        setError(securityMessage("Could not save password attempt information.", "Parola deneme bilgisi kaydedilemedi."));
    refreshAuthenticationState();
}

bool PrivateVaultManager::resetPasswordFailures()
{
    m_failedPasswordAttempts = 0;
    m_passwordLockoutUntil = 0;
    m_passwordRetryUntil = 0;
    m_authenticationTimer->stop();
    const bool saved = persistAuthenticationState();
    emit authenticationStateChanged();
    if (!saved)
        setError(securityMessage("Could not reset password attempts.", "Parola deneme bilgisi sıfırlanamadı."));
    return saved;
}

bool PrivateVaultManager::setPasswordProtection(bool enabled, int attempts, int lockoutSeconds, int retrySeconds)
{
    if (!m_unlocked || m_busy || attempts < 1 || attempts > 100
            || lockoutSeconds < 1 || lockoutSeconds > 86400 || retrySeconds < 0 || retrySeconds > 3600)
        return false;
    const bool oldEnabled = m_passwordProtectionEnabled;
    const int oldAttempts = m_maxPasswordAttempts;
    const int oldLockout = m_passwordLockoutSeconds;
    const int oldRetry = m_passwordRetrySeconds;
    m_passwordProtectionEnabled = enabled;
    m_maxPasswordAttempts = attempts;
    m_passwordLockoutSeconds = lockoutSeconds;
    m_passwordRetrySeconds = retrySeconds;
    if (!persistSecurityPreferences()) {
        m_passwordProtectionEnabled = oldEnabled;
        m_maxPasswordAttempts = oldAttempts;
        m_passwordLockoutSeconds = oldLockout;
        m_passwordRetrySeconds = oldRetry;
        setError(securityMessage("Could not save security settings.", "Güvenlik ayarı kaydedilemedi."));
        return false;
    }
    emit securitySettingsChanged();
    if (!enabled && !resetPasswordFailures())
        return false;
    clearStatus();
    setStatus(securityMessage("Password attempt protection settings saved.", "Parola deneme koruması ayarları kaydedildi."));
    return true;
}

void PrivateVaultManager::startAutoLockCountdown()
{
    if (!m_autoLockTimer)
        return;
    m_autoLockTimer->stop();
    if (m_unlocked && m_autoLockEnabled)
        m_autoLockTimer->start(m_autoLockMinutes * 60 * 1000);
}

QString PrivateVaultManager::walletEntryKey(const QJsonObject &header) const
{
    const QString vaultId = header.value(QStringLiteral("vaultId")).toString();
    if (vaultId.isEmpty())
        return {};
    return QStringLiteral("privateVaultMasterKey:") + vaultId;
}

KWallet::Wallet *PrivateVaultManager::openVaultWallet()
{
    if (m_wallet && m_wallet->isOpen()) {
        if (selectLurvikoWalletFolder(m_wallet))
            return m_wallet;
        delete m_wallet;
        m_wallet = nullptr;
    }
    if (!KWallet::Wallet::isEnabled())
        return nullptr;
    m_wallet = KWallet::Wallet::openWallet(KWallet::Wallet::LocalWallet(), 0, KWallet::Wallet::Synchronous);
    if (!m_wallet || !m_wallet->isOpen()) {
        delete m_wallet;
        m_wallet = nullptr;
        return nullptr;
    }
    if (!selectLurvikoWalletFolder(m_wallet)) {
        delete m_wallet;
        m_wallet = nullptr;
        return nullptr;
    }
    return m_wallet;
}

bool PrivateVaultManager::storeMasterKeyInWallet()
{
    if (!m_unlocked || m_masterKey.size() != KeySize)
        return false;
    const QString key = walletEntryKey(m_header);
    KWallet::Wallet *wallet = openVaultWallet();
    return wallet && !key.isEmpty() && wallet->writeEntry(key, m_masterKey, KWallet::Wallet::Stream) == 0;
}

void PrivateVaultManager::removeMasterKeyFromWallet()
{
    QJsonObject header = m_header;
    if (header.isEmpty())
        readHeader(&header);
    const QString key = walletEntryKey(header);
    KWallet::Wallet *wallet = openVaultWallet();
    if (wallet && !key.isEmpty())
        wallet->removeEntry(key);
}

bool PrivateVaultManager::setKWalletEnabled(bool enabled)
{
    clearStatus();
    if (!m_unlocked) {
        setError(tr("KWallet ayarı için önce kasanın kilidini aç."));
        return false;
    }
    if (enabled == m_kwalletEnabled)
        return true;
    if (enabled) {
        if (!storeMasterKeyInWallet()) {
            setError(tr("KWallet açılamadı veya kasa anahtarı kaydedilemedi."));
            return false;
        }
        m_kwalletEnabled = true;
    } else {
        removeMasterKeyFromWallet();
        m_kwalletEnabled = false;
    }
    if (!persistSecurityPreferences()) {
        setError(tr("Güvenlik ayarı kaydedilemedi."));
        return false;
    }
    setStatus(enabled ? tr("Bu cihazda KWallet ile hızlı kilit açma etkin.")
                      : tr("KWallet ile hızlı kilit açma kapatıldı."));
    emit securitySettingsChanged();
    return true;
}

bool PrivateVaultManager::setAutoLockEnabled(bool enabled)
{
    if (!m_unlocked)
        return false;
    m_autoLockEnabled = enabled;
    if (!persistSecurityPreferences())
        return false;
    startAutoLockCountdown();
    emit securitySettingsChanged();
    return true;
}

bool PrivateVaultManager::setAutoLockMinutes(int minutes)
{
    if (!m_unlocked || (minutes != 5 && minutes != 15 && minutes != 30 && minutes != 60))
        return false;
    m_autoLockMinutes = minutes;
    if (!persistSecurityPreferences())
        return false;
    startAutoLockCountdown();
    emit securitySettingsChanged();
    return true;
}

bool PrivateVaultManager::changePassword(const QString &currentPassword, const QString &newPassword)
{
    clearStatus();
    if (!m_unlocked || m_masterKey.size() != KeySize) {
        setError(tr("Parolayı değiştirmek için kasa açık olmalı."));
        return false;
    }
    if (newPassword.size() < 8) {
        setError(tr("Yeni parola en az 8 karakter olmalı."));
        return false;
    }
    if (!allowPasswordAttempt())
        return false;
    QByteArray verified;
    if (!unwrapMasterKey(m_header, currentPassword, &verified)
            || verified.size() != m_masterKey.size()
            || CRYPTO_memcmp(verified.constData(), m_masterKey.constData(), KeySize) != 0) {
        secureClear(verified);
        setError(tr("Mevcut parola yanlış."));
        recordPasswordFailure();
        return false;
    }
    secureClear(verified);
    if (!resetPasswordFailures())
        return false;

    QByteArray salt(SaltSize, Qt::Uninitialized);
    if (RAND_bytes(reinterpret_cast<unsigned char *>(salt.data()), SaltSize) != 1) {
        setError(tr("Yeni parola için güvenli salt üretilemedi."));
        return false;
    }
    QByteArray passwordKey;
    if (!derivePasswordKey(newPassword, salt, &passwordKey, &m_header)) {
        setError(tr("Yeni parola anahtarı türetilemedi."));
        return false;
    }
    QByteArray iv, wrapped, tag;
    const bool encrypted = aesGcmEncrypt(m_masterKey, passwordKey, QByteArrayLiteral("g-file-private/master-v1"), &iv, &wrapped, &tag);
    secureClear(passwordKey);
    if (!encrypted) {
        setError(tr("Kasa anahtarı yeni parolayla sarılamadı."));
        return false;
    }
    QJsonObject updated = m_header;
    updated.insert(QStringLiteral("salt"), QString::fromUtf8(b64(salt)));
    updated.insert(QStringLiteral("wrapIv"), QString::fromUtf8(b64(iv)));
    updated.insert(QStringLiteral("wrappedKey"), QString::fromUtf8(b64(wrapped)));
    updated.insert(QStringLiteral("wrapTag"), QString::fromUtf8(b64(tag)));
    if (!writeHeader(updated)) {
        setError(tr("Yeni parola bilgisi diske yazılamadı."));
        return false;
    }
    m_header = updated;
    if (m_kwalletEnabled)
        storeMasterKeyInWallet();
    setStatus(tr("Kasa parolası değiştirildi."));
    emit operationFinished(true, m_statusMessage);
    return true;
}

bool PrivateVaultManager::quickUnlock()
{
    clearStatus();
    if (!exists()) {
        setError(tr("Kasa bulunamadı."));
        return false;
    }
    if (!allowPasswordAttempt())
        return false;
    QJsonObject header;
    if (!readHeader(&header) || !header.value(QStringLiteral("kwalletEnabled")).toBool(false)) {
        setError(tr("Bu kasa için KWallet hızlı açma etkin değil."));
        return false;
    }
    const QString key = walletEntryKey(header);
    KWallet::Wallet *wallet = openVaultWallet();
    QByteArray master;
    if (!wallet || key.isEmpty() || wallet->readEntry(key, master) != 0 || master.size() != KeySize) {
        secureClear(master);
        setError(tr("KWallet'ta geçerli kasa anahtarı bulunamadı."));
        return false;
    }
    QJsonObject manifest;
    if (!loadManifest(header, master, &manifest)) {
        secureClear(master);
        setError(tr("KWallet anahtarı bu kasayla eşleşmiyor."));
        return false;
    }
    if (!resetPasswordFailures()) {
        secureClear(master);
        return false;
    }
    lock();
    m_masterKey = master;
    m_masterKey.detach();
    secureClear(master);
    m_header = header;
    m_manifest = manifest;
    m_unlocked = true;
    m_currentFolderId = QStringLiteral("root");
    loadSecurityPreferences();
    cleanupOrphanBlobs();
    cleanupOrphanThumbnails();
    refreshVisibleItems();
    startAutoLockCountdown();
    setStatus(tr("Kasa KWallet ile açıldı."));
    emit stateChanged();
    emit currentFolderChanged();
    emit securitySettingsChanged();
    return true;
}

bool PrivateVaultManager::loadManifest(const QJsonObject &header, const QByteArray &masterKey, QJsonObject *manifest) const
{
    QByteArray plain;
    if (!aesGcmDecrypt(unb64(header.value(QStringLiteral("manifestCipher"))), masterKey,
                       QByteArrayLiteral("g-file-private/manifest-v1"),
                       unb64(header.value(QStringLiteral("manifestIv"))),
                       unb64(header.value(QStringLiteral("manifestTag"))), &plain))
        return false;
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(plain, &error);
    if (!plain.isEmpty())
        OPENSSL_cleanse(plain.data(), static_cast<size_t>(plain.size()));
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    const QJsonObject result = doc.object();
    if (result.value(QStringLiteral("version")).toInt() != 1 || !result.value(QStringLiteral("items")).isArray())
        return false;
    *manifest = result;
    return true;
}

bool PrivateVaultManager::saveManifest()
{
    if (!m_unlocked || m_masterKey.size() != KeySize)
        return false;
    QByteArray plain = QJsonDocument(m_manifest).toJson(QJsonDocument::Compact);
    QByteArray iv, cipher, tag;
    const bool encrypted = aesGcmEncrypt(plain, m_masterKey, QByteArrayLiteral("g-file-private/manifest-v1"), &iv, &cipher, &tag);
    if (!plain.isEmpty())
        OPENSSL_cleanse(plain.data(), static_cast<size_t>(plain.size()));
    if (!encrypted)
        return false;
    m_header.insert(QStringLiteral("manifestIv"), QString::fromUtf8(b64(iv)));
    m_header.insert(QStringLiteral("manifestCipher"), QString::fromUtf8(b64(cipher)));
    m_header.insert(QStringLiteral("manifestTag"), QString::fromUtf8(b64(tag)));
    return writeHeader(m_header);
}

bool PrivateVaultManager::createVault(const QString &password)
{
    clearStatus();
    if (exists()) {
        setError(tr("Sana Özel kasası zaten oluşturulmuş."));
        return false;
    }
    if (password.size() < 8) {
        setError(tr("Parola en az 8 karakter olmalı."));
        return false;
    }
    if (!ensureStorageLayout()) {
        setError(tr("Kasa dizini oluşturulamadı."));
        return false;
    }
    setBusy(true);
    QByteArray salt(SaltSize, Qt::Uninitialized);
    QByteArray master(KeySize, Qt::Uninitialized);
    if (RAND_bytes(reinterpret_cast<unsigned char *>(salt.data()), SaltSize) != 1
            || RAND_bytes(reinterpret_cast<unsigned char *>(master.data()), KeySize) != 1) {
        setBusy(false);
        setError(tr("Güvenli rastgele anahtar üretilemedi."));
        return false;
    }
    QJsonObject header {
        { QStringLiteral("magic"), QString::fromLatin1(kVaultMagic) },
        { QStringLiteral("version"), 1 },
        { QStringLiteral("kdf"), QStringLiteral("argon2id") },
        { QStringLiteral("iterations"), 3 },
        { QStringLiteral("memCostKiB"), 65536 },
        { QStringLiteral("lanes"), 1 },
        { QStringLiteral("threads"), 1 },
        { QStringLiteral("salt"), QString::fromUtf8(b64(salt)) },
        { QStringLiteral("vaultId"), QUuid::createUuid().toString(QUuid::WithoutBraces) },
        { QStringLiteral("kwalletEnabled"), false },
        { QStringLiteral("autoLockEnabled"), false },
        { QStringLiteral("autoLockMinutes"), 15 },
        { QStringLiteral("passwordProtectionEnabled"), true },
        { QStringLiteral("maxPasswordAttempts"), 5 },
        { QStringLiteral("passwordLockoutSeconds"), 300 },
        { QStringLiteral("passwordRetrySeconds"), 2 }
    };
    QByteArray passwordKey;
    if (!derivePasswordKey(password, salt, &passwordKey, &header)) {
        secureClear(master);
        setBusy(false);
        setError(tr("Argon2id anahtar türetme başarısız."));
        return false;
    }
    QByteArray wrapIv, wrapped, wrapTag;
    if (!aesGcmEncrypt(master, passwordKey, QByteArrayLiteral("g-file-private/master-v1"), &wrapIv, &wrapped, &wrapTag)) {
        secureClear(passwordKey);
        secureClear(master);
        setBusy(false);
        setError(tr("Kasa anahtarı şifrelenemedi."));
        return false;
    }
    secureClear(passwordKey);
    header.insert(QStringLiteral("wrapIv"), QString::fromUtf8(b64(wrapIv)));
    header.insert(QStringLiteral("wrappedKey"), QString::fromUtf8(b64(wrapped)));
    header.insert(QStringLiteral("wrapTag"), QString::fromUtf8(b64(wrapTag)));

    m_masterKey = master;
    m_masterKey.detach();
    secureClear(master);
    m_header = header;
    m_manifest = QJsonObject {
        { QStringLiteral("version"), 1 },
        { QStringLiteral("createdMs"), static_cast<double>(QDateTime::currentMSecsSinceEpoch()) },
        { QStringLiteral("items"), QJsonArray() }
    };
    m_unlocked = true;
    m_kwalletEnabled = false;
    m_autoLockEnabled = false;
    m_autoLockMinutes = 15;
    m_passwordProtectionEnabled = true;
    m_maxPasswordAttempts = 5;
    m_passwordLockoutSeconds = 300;
    m_passwordRetrySeconds = 2;
    m_currentFolderId = QStringLiteral("root");
    const bool saved = saveManifest();
    setBusy(false);
    if (!saved) {
        lock();
        QFile::remove(headerPath());
        setError(tr("Kasa bilgileri diske yazılamadı."));
        return false;
    }
    if (!resetPasswordFailures()) {
        lock();
        return false;
    }
    refreshVisibleItems();
    startAutoLockCountdown();
    setStatus(tr("Sana Özel kasası oluşturuldu ve kilidi açıldı."));
    emit stateChanged();
    emit currentFolderChanged();
    emit securitySettingsChanged();
    emit operationFinished(true, m_statusMessage);
    return true;
}

bool PrivateVaultManager::unlock(const QString &password)
{
    clearStatus();
    if (!exists()) {
        setError(tr("Kasa bulunamadı."));
        return false;
    }
    if (m_busy || !allowPasswordAttempt())
        return false;
    setBusy(true);
    QJsonObject header;
    QByteArray master;
    QJsonObject manifest;
    if (!readHeader(&header)) {
        setBusy(false);
        setError(securityMessage("Could not read vault information.", "Kasa bilgisi okunamadı."));
        return false;
    }
    if (!unwrapMasterKey(header, password, &master)) {
        secureClear(master);
        setBusy(false);
        setError(securityMessage("Could not unlock the vault. The password may be incorrect or the vault data damaged.", "Kasa açılamadı. Parola yanlış veya kasa verisi bozulmuş olabilir."));
        recordPasswordFailure();
        return false;
    }
    if (!loadManifest(header, master, &manifest) || !resetPasswordFailures()) {
        secureClear(master);
        setBusy(false);
        if (m_lastError.isEmpty())
            setError(securityMessage("Could not read vault data.", "Kasa verisi okunamadı."));
        return false;
    }
    if (!header.contains(QStringLiteral("vaultId"))) {
        header.insert(QStringLiteral("vaultId"), QUuid::createUuid().toString(QUuid::WithoutBraces));
        writeHeader(header);
    }
    lock();
    m_masterKey = master;
    m_masterKey.detach();
    secureClear(master);
    m_header = header;
    m_manifest = manifest;
    m_unlocked = true;
    loadSecurityPreferences();
    m_currentFolderId = QStringLiteral("root");
    cleanupOrphanBlobs();
    cleanupOrphanThumbnails();
    setBusy(false);
    refreshVisibleItems();
    startAutoLockCountdown();
    setStatus(tr("Kasa kilidi açıldı."));
    emit stateChanged();
    emit currentFolderChanged();
    emit securitySettingsChanged();
    emit operationFinished(true, m_statusMessage);
    return true;
}

void PrivateVaultManager::lock()
{
    if (m_autoLockTimer)
        m_autoLockTimer->stop();
    // Give Lurviko-owned viewers/players a chance to stop and release their
    // runtime files before the plaintext runtime directory is wiped.
    if (m_unlocked)
        emit aboutToLock();
    cleanupRuntimeFiles();
    beginResetModel();
    m_visibleItems.clear();
    endResetModel();
    secureClear(m_masterKey);
    m_header = {};
    m_manifest = {};
    m_currentFolderId = QStringLiteral("root");
    const bool wasUnlocked = m_unlocked;
    m_unlocked = false;
    if (wasUnlocked) {
        emit stateChanged();
        emit itemCountChanged();
        emit currentFolderChanged();
    }
}


QString PrivateVaultManager::thumbnailCachePath(const QString &objectId) const
{
    return QDir(thumbnailsRoot()).filePath(objectId + QStringLiteral(".gfthumb"));
}

bool PrivateVaultManager::writeEncryptedThumbnail(const QString &objectId, const QByteArray &pngBytes)
{
    if (!m_unlocked || m_masterKey.size() != KeySize || pngBytes.isEmpty())
        return false;
    QByteArray iv, cipher, tag;
    const QByteArray aad = QByteArrayLiteral("g-file-private/thumb-v1:") + objectId.toUtf8();
    if (!aesGcmEncrypt(pngBytes, m_masterKey, aad, &iv, &cipher, &tag))
        return false;
    QSaveFile file(thumbnailCachePath(objectId));
    if (!file.open(QIODevice::WriteOnly))
        return false;
    if (file.write(kThumbMagic, kThumbMagicSize) != kThumbMagicSize
            || file.write(iv) != iv.size()
            || file.write(tag) != tag.size()
            || file.write(cipher) != cipher.size()
            || !file.commit())
        return false;
    QFile::setPermissions(thumbnailCachePath(objectId), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

bool PrivateVaultManager::readEncryptedThumbnail(const QString &objectId, QByteArray *pngBytes) const
{
    if (!pngBytes || !m_unlocked || m_masterKey.size() != KeySize)
        return false;
    QFile file(thumbnailCachePath(objectId));
    if (!file.open(QIODevice::ReadOnly))
        return false;
    if (file.read(kThumbMagicSize) != QByteArray(kThumbMagic, kThumbMagicSize))
        return false;
    const QByteArray iv = file.read(IvSize);
    const QByteArray tag = file.read(TagSize);
    const QByteArray cipher = file.readAll();
    const QByteArray aad = QByteArrayLiteral("g-file-private/thumb-v1:") + objectId.toUtf8();
    return aesGcmDecrypt(cipher, m_masterKey, aad, iv, tag, pngBytes);
}

QByteArray PrivateVaultManager::makeThumbnailPng(const QString &sourcePath, const QString &mimeType) const
{
    QByteArray png;
    if (mimeType.startsWith(QStringLiteral("image/"))) {
        QImageReader reader(sourcePath);
        reader.setAutoTransform(true);
        const QSize original = reader.size();
        if (original.isValid())
            reader.setScaledSize(original.scaled(QSize(480, 360), Qt::KeepAspectRatio));
        QImage image = reader.read();
        if (image.isNull())
            return {};
        QBuffer buffer(&png);
        if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG", 88))
            return {};
        return png;
    }
    if (!mimeType.startsWith(QStringLiteral("video/")))
        return {};

    if (runtimeRoot().isEmpty())
        return {};
    const QString outPath = QDir(runtimeRoot()).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".png"));
    const QString thumbnailer = QStandardPaths::findExecutable(QStringLiteral("ffmpegthumbnailer"));
    QProcess process;
    if (!thumbnailer.isEmpty()) {
        process.start(thumbnailer, { QStringLiteral("-i"), sourcePath, QStringLiteral("-o"), outPath,
                                     QStringLiteral("-s"), QStringLiteral("480"), QStringLiteral("-q"), QStringLiteral("8") });
    } else {
        const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
        if (ffmpeg.isEmpty())
            return {};
        process.start(ffmpeg, { QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-ss"), QStringLiteral("2"),
                                QStringLiteral("-i"), sourcePath, QStringLiteral("-frames:v"), QStringLiteral("1"),
                                QStringLiteral("-vf"), QStringLiteral("scale=480:-2"), QStringLiteral("-y"), outPath });
    }
    if (!process.waitForStarted(2500) || !process.waitForFinished(15000) || process.exitCode() != 0) {
        process.kill();
        QFile::remove(outPath);
        return {};
    }
    QFile thumb(outPath);
    if (thumb.open(QIODevice::ReadOnly))
        png = thumb.readAll();
    QFile::remove(outPath);
    return png;
}

bool PrivateVaultManager::cacheThumbnailFromSource(const QString &objectId, const QString &sourcePath, const QString &mimeType)
{
    if (!mimeType.startsWith(QStringLiteral("image/")) && !mimeType.startsWith(QStringLiteral("video/")))
        return false;
    QByteArray png = makeThumbnailPng(sourcePath, mimeType);
    const bool ok = !png.isEmpty() && writeEncryptedThumbnail(objectId, png);
    secureClear(png);
    return ok;
}

QString PrivateVaultManager::thumbnailUrl(const QString &objectId)
{
    if (!m_unlocked || objectId.isEmpty())
        return {};
    if (!ensureRuntimeDirectory())
        return {};
    const QString existing = m_thumbnailMaterializedPaths.value(objectId);
    if (!existing.isEmpty() && QFileInfo::exists(existing))
        return QUrl::fromLocalFile(existing).toString();

    const QJsonObject item = itemById(objectId);
    if (item.isEmpty() || item.value(QStringLiteral("dir")).toBool())
        return {};
    const QString mime = item.value(QStringLiteral("mime")).toString();
    if (!mime.startsWith(QStringLiteral("image/")) && !mime.startsWith(QStringLiteral("video/")))
        return {};

    QByteArray png;
    if (!readEncryptedThumbnail(objectId, &png)) {
        const QString suffix = QFileInfo(item.value(QStringLiteral("name")).toString()).suffix();
        QString sourcePath = QDir(runtimeRoot()).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces));
        if (!suffix.isEmpty())
            sourcePath += QLatin1Char('.') + suffix;
        if (!decryptBlobToFile(objectId, item.value(QStringLiteral("blob")).toString(), sourcePath, m_masterKey))
            return {};
        cacheThumbnailFromSource(objectId, sourcePath, mime);
        QFile::remove(sourcePath);
        if (!readEncryptedThumbnail(objectId, &png))
            return {};
    }

    const QString runtimeThumb = QDir(runtimeRoot()).filePath(QStringLiteral("thumb-") + QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".png"));
    QSaveFile out(runtimeThumb);
    if (!out.open(QIODevice::WriteOnly) || out.write(png) != png.size() || !out.commit()) {
        secureClear(png);
        return {};
    }
    secureClear(png);
    QFile::setPermissions(runtimeThumb, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    m_thumbnailMaterializedPaths.insert(objectId, runtimeThumb);
    return QUrl::fromLocalFile(runtimeThumb).toString();
}

QString PrivateVaultManager::sanitizedName(const QString &name) const
{
    QString out = name.trimmed();
    out.replace(QLatin1Char('/'), QChar(0x2215));
    out.remove(QChar::Null);
    if (out == QStringLiteral(".") || out == QStringLiteral(".."))
        out.prepend(QLatin1Char('_'));
    return out.left(255);
}

QString PrivateVaultManager::uniqueName(const QString &parentId, const QString &preferred) const
{
    const QString clean = sanitizedName(preferred);
    QSet<QString> names;
    const QJsonArray items = m_manifest.value(QStringLiteral("items")).toArray();
    for (const QJsonValue &value : items) {
        const QJsonObject item = value.toObject();
        if (item.value(QStringLiteral("parent")).toString() == parentId)
            names.insert(item.value(QStringLiteral("name")).toString().toCaseFolded());
    }
    if (!names.contains(clean.toCaseFolded()))
        return clean;
    const QFileInfo info(clean);
    const QString base = info.completeBaseName().isEmpty() ? clean : info.completeBaseName();
    const QString suffix = info.completeSuffix();
    for (int n = 2; n < 100000; ++n) {
        const QString candidate = suffix.isEmpty()
            ? QStringLiteral("%1 (%2)").arg(base).arg(n)
            : QStringLiteral("%1 (%2).%3").arg(base).arg(n).arg(suffix);
        if (!names.contains(candidate.toCaseFolded()))
            return candidate;
    }
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QJsonObject PrivateVaultManager::itemById(const QString &id) const
{
    const int index = itemIndexById(id);
    if (index < 0)
        return {};
    return m_manifest.value(QStringLiteral("items")).toArray().at(index).toObject();
}

int PrivateVaultManager::itemIndexById(const QString &id) const
{
    const QJsonArray items = m_manifest.value(QStringLiteral("items")).toArray();
    for (int i = 0; i < items.size(); ++i) {
        if (items.at(i).toObject().value(QStringLiteral("id")).toString() == id)
            return i;
    }
    return -1;
}

QString PrivateVaultManager::iconForItem(const QJsonObject &item) const
{
    if (item.value(QStringLiteral("dir")).toBool())
        return QStringLiteral("folder.svg");
    const QString mime = item.value(QStringLiteral("mime")).toString();
    if (mime.startsWith(QStringLiteral("image/"))) return QStringLiteral("image.svg");
    if (mime.startsWith(QStringLiteral("video/"))) return QStringLiteral("video.svg");
    if (mime.startsWith(QStringLiteral("audio/"))) return QStringLiteral("audio.svg");
    if (mime == QStringLiteral("application/pdf")) return QStringLiteral("pdf.svg");
    if (mime.contains(QStringLiteral("zip")) || mime.contains(QStringLiteral("tar")) || mime.contains(QStringLiteral("archive")))
        return QStringLiteral("archive.svg");
    return QStringLiteral("file.svg");
}

QString PrivateVaultManager::parentPathLabel(const QString &folderId) const
{
    if (folderId == QStringLiteral("root"))
        return QStringLiteral("Sana Özel");
    QStringList parts;
    QString id = folderId;
    QSet<QString> guard;
    while (id != QStringLiteral("root") && !id.isEmpty() && !guard.contains(id)) {
        guard.insert(id);
        const QJsonObject item = itemById(id);
        if (item.isEmpty())
            break;
        parts.prepend(item.value(QStringLiteral("name")).toString());
        id = item.value(QStringLiteral("parent")).toString();
        if (id.isEmpty()) id = QStringLiteral("root");
    }
    return QStringLiteral("Sana Özel / ") + parts.join(QStringLiteral(" / "));
}

void PrivateVaultManager::refreshVisibleItems()
{
    beginResetModel();
    m_visibleItems.clear();
    if (m_unlocked) {
        const QJsonArray items = m_manifest.value(QStringLiteral("items")).toArray();
        for (const QJsonValue &value : items) {
            const QJsonObject item = value.toObject();
            if (item.value(QStringLiteral("parent")).toString() == m_currentFolderId)
                m_visibleItems.append(item);
        }
        std::sort(m_visibleItems.begin(), m_visibleItems.end(), [](const QJsonObject &a, const QJsonObject &b) {
            const bool ad = a.value(QStringLiteral("dir")).toBool();
            const bool bd = b.value(QStringLiteral("dir")).toBool();
            if (ad != bd)
                return ad > bd;
            return QString::localeAwareCompare(a.value(QStringLiteral("name")).toString(),
                                               b.value(QStringLiteral("name")).toString()) < 0;
        });
    }
    endResetModel();
    emit itemCountChanged();
    emit currentFolderChanged();
}

bool PrivateVaultManager::createFolder(const QString &name)
{
    if (!m_unlocked || m_busy)
        return false;
    const QString clean = sanitizedName(name);
    if (clean.isEmpty()) {
        setError(tr("Klasör adı boş olamaz."));
        return false;
    }
    QJsonArray items = m_manifest.value(QStringLiteral("items")).toArray();
    items.append(QJsonObject {
        { QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces) },
        { QStringLiteral("parent"), m_currentFolderId },
        { QStringLiteral("name"), uniqueName(m_currentFolderId, clean) },
        { QStringLiteral("dir"), true },
        { QStringLiteral("size"), 0 },
        { QStringLiteral("mime"), QStringLiteral("inode/directory") },
        { QStringLiteral("modifiedMs"), static_cast<double>(QDateTime::currentMSecsSinceEpoch()) }
    });
    m_manifest.insert(QStringLiteral("items"), items);
    if (!saveManifest()) {
        setError(tr("Klasör bilgisi kaydedilemedi."));
        return false;
    }
    refreshVisibleItems();
    return true;
}

bool PrivateVaultManager::renameItem(const QString &objectId, const QString &newName)
{
    if (!m_unlocked || m_busy)
        return false;
    const int index = itemIndexById(objectId);
    if (index < 0)
        return false;
    QJsonArray items = m_manifest.value(QStringLiteral("items")).toArray();
    QJsonObject item = items.at(index).toObject();
    const QString clean = sanitizedName(newName);
    if (clean.isEmpty())
        return false;
    const QString oldName = item.value(QStringLiteral("name")).toString();
    if (clean == oldName)
        return true;
    item.insert(QStringLiteral("name"), uniqueName(item.value(QStringLiteral("parent")).toString(), clean));
    item.insert(QStringLiteral("modifiedMs"), static_cast<double>(QDateTime::currentMSecsSinceEpoch()));
    items.replace(index, item);
    m_manifest.insert(QStringLiteral("items"), items);
    if (!saveManifest())
        return false;
    refreshVisibleItems();
    return true;
}

void PrivateVaultManager::collectDescendants(const QString &id, QStringList *ids) const
{
    if (!ids)
        return;
    const QJsonArray items = m_manifest.value(QStringLiteral("items")).toArray();
    for (const QJsonValue &value : items) {
        const QJsonObject item = value.toObject();
        if (item.value(QStringLiteral("parent")).toString() == id) {
            const QString child = item.value(QStringLiteral("id")).toString();
            collectDescendants(child, ids);
            ids->append(child);
        }
    }
}

bool PrivateVaultManager::deleteItem(const QString &objectId)
{
    if (!m_unlocked || m_busy)
        return false;
    if (itemIndexById(objectId) < 0)
        return false;
    QStringList removeIds;
    collectDescendants(objectId, &removeIds);
    removeIds.append(objectId);
    QSet<QString> removeSet;
    for (const QString &removeId : std::as_const(removeIds)) removeSet.insert(removeId);
    QJsonArray kept;
    QStringList blobsToDelete;
    const QJsonArray items = m_manifest.value(QStringLiteral("items")).toArray();
    for (const QJsonValue &value : items) {
        const QJsonObject item = value.toObject();
        if (!removeSet.contains(item.value(QStringLiteral("id")).toString())) {
            kept.append(item);
            continue;
        }
        const QString blob = item.value(QStringLiteral("blob")).toString();
        if (!blob.isEmpty())
            blobsToDelete.append(blob);
    }
    const QJsonObject originalManifest = m_manifest;
    m_manifest.insert(QStringLiteral("items"), kept);
    if (!saveManifest()) {
        m_manifest = originalManifest;
        return false;
    }
    // Commit metadata first. Leftover ciphertext is harmless and can be
    // garbage-collected later; deleting a blob before an atomic manifest save
    // could instead leave a live item permanently unreadable after a crash.
    for (const QString &blob : std::as_const(blobsToDelete))
        QFile::remove(QDir(objectsRoot()).filePath(blob));
    for (const QString &removeId : std::as_const(removeIds)) {
        QFile::remove(thumbnailCachePath(removeId));
        const QString runtimeThumb = m_thumbnailMaterializedPaths.take(removeId);
        if (!runtimeThumb.isEmpty())
            QFile::remove(runtimeThumb);
    }
    refreshVisibleItems();
    return true;
}

bool PrivateVaultManager::enterFolder(const QString &objectId)
{
    if (!m_unlocked)
        return false;
    const QJsonObject item = itemById(objectId);
    if (item.isEmpty() || !item.value(QStringLiteral("dir")).toBool())
        return false;
    m_currentFolderId = objectId;
    refreshVisibleItems();
    return true;
}

bool PrivateVaultManager::goUp()
{
    if (!m_unlocked || m_currentFolderId == QStringLiteral("root"))
        return false;
    const QJsonObject current = itemById(m_currentFolderId);
    m_currentFolderId = current.value(QStringLiteral("parent")).toString();
    if (m_currentFolderId.isEmpty()) m_currentFolderId = QStringLiteral("root");
    refreshVisibleItems();
    return true;
}

void PrivateVaultManager::goRoot()
{
    if (!m_unlocked)
        return;
    m_currentFolderId = QStringLiteral("root");
    refreshVisibleItems();
}

bool PrivateVaultManager::encryptFileToBlob(const QString &sourcePath, const QString &objectId,
                                            const QString &blobName, const QByteArray &key) const
{
    QFile in(sourcePath);
    if (!in.open(QIODevice::ReadOnly) || in.size() > VaultSecurity::MaxGcmPlaintextBytes)
        return false;
    QSaveFile out(QDir(objectsRoot()).filePath(blobName));
    if (!out.open(QIODevice::WriteOnly))
        return false;
    QByteArray iv(IvSize, Qt::Uninitialized);
    if (RAND_bytes(reinterpret_cast<unsigned char *>(iv.data()), IvSize) != 1)
        return false;
    if (out.write(kBlobMagic, kBlobMagicSize) != kBlobMagicSize || out.write(iv) != iv.size())
        return false;

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return false;
    const QByteArray aad = QByteArrayLiteral("g-file-private/blob-v1:") + objectId.toUtf8();
    int len = 0;
    bool ok = EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1
           && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, IvSize, nullptr) == 1
           && EVP_EncryptInit_ex(ctx, nullptr, nullptr,
                                 reinterpret_cast<const unsigned char *>(key.constData()),
                                 reinterpret_cast<const unsigned char *>(iv.constData())) == 1
           && EVP_EncryptUpdate(ctx, nullptr, &len,
                                reinterpret_cast<const unsigned char *>(aad.constData()), aad.size()) == 1;
    QByteArray input(kChunkSize, Qt::Uninitialized);
    QByteArray output(kChunkSize + EVP_MAX_BLOCK_LENGTH, Qt::Uninitialized);
    qint64 processed = 0;
    while (ok && !in.atEnd()) {
        const qint64 read = in.read(input.data(), input.size());
        if (read < 0) { ok = false; break; }
        if (read == 0) break;
        if (read > VaultSecurity::MaxGcmPlaintextBytes - processed) { ok = false; break; }
        processed += read;
        if (EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char *>(output.data()), &len,
                              reinterpret_cast<const unsigned char *>(input.constData()), static_cast<int>(read)) != 1) {
            ok = false; break;
        }
        if (len > 0 && out.write(output.constData(), len) != len) { ok = false; break; }
    }
    if (ok) {
        ok = EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char *>(output.data()), &len) == 1;
        if (ok && len > 0)
            ok = out.write(output.constData(), len) == len;
    }
    QByteArray tag(TagSize, Qt::Uninitialized);
    if (ok)
        ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, TagSize, tag.data()) == 1;
    EVP_CIPHER_CTX_free(ctx);
    OPENSSL_cleanse(input.data(), static_cast<size_t>(input.size()));
    if (ok)
        ok = out.write(tag) == tag.size() && out.commit();
    if (ok)
        QFile::setPermissions(QDir(objectsRoot()).filePath(blobName), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return ok;
}

bool PrivateVaultManager::decryptBlobToFile(const QString &objectId, const QString &blobName,
                                            const QString &targetPath, const QByteArray &key) const
{
    QFile in(QDir(objectsRoot()).filePath(blobName));
    if (!in.open(QIODevice::ReadOnly) || in.size() < kBlobMagicSize + IvSize + TagSize)
        return false;
    if (in.read(kBlobMagicSize) != QByteArray(kBlobMagic, kBlobMagicSize))
        return false;
    const QByteArray iv = in.read(IvSize);
    const qint64 cipherBytes = in.size() - kBlobMagicSize - IvSize - TagSize;
    if (cipherBytes < 0 || cipherBytes > VaultSecurity::MaxGcmPlaintextBytes)
        return false;
    if (!in.seek(in.size() - TagSize))
        return false;
    const QByteArray tag = in.read(TagSize);
    if (!in.seek(kBlobMagicSize + IvSize))
        return false;

    QSaveFile out(targetPath);
    if (!out.open(QIODevice::WriteOnly))
        return false;
    if (!out.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner))
        return false;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return false;
    const QByteArray aad = QByteArrayLiteral("g-file-private/blob-v1:") + objectId.toUtf8();
    int len = 0;
    bool ok = EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1
           && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, IvSize, nullptr) == 1
           && EVP_DecryptInit_ex(ctx, nullptr, nullptr,
                                 reinterpret_cast<const unsigned char *>(key.constData()),
                                 reinterpret_cast<const unsigned char *>(iv.constData())) == 1
           && EVP_DecryptUpdate(ctx, nullptr, &len,
                                reinterpret_cast<const unsigned char *>(aad.constData()), aad.size()) == 1;
    QByteArray input(kChunkSize, Qt::Uninitialized);
    QByteArray output(kChunkSize + EVP_MAX_BLOCK_LENGTH, Qt::Uninitialized);
    qint64 remaining = cipherBytes;
    while (ok && remaining > 0) {
        const qint64 wanted = qMin<qint64>(remaining, input.size());
        const qint64 read = in.read(input.data(), wanted);
        if (read <= 0) { ok = false; break; }
        remaining -= read;
        if (EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char *>(output.data()), &len,
                              reinterpret_cast<const unsigned char *>(input.constData()), static_cast<int>(read)) != 1) {
            ok = false; break;
        }
        if (len > 0 && out.write(output.constData(), len) != len) { ok = false; break; }
    }
    if (ok)
        ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, TagSize, const_cast<char *>(tag.constData())) == 1;
    if (ok) {
        ok = EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char *>(output.data()), &len) == 1;
        if (ok && len > 0)
            ok = out.write(output.constData(), len) == len;
    }
    EVP_CIPHER_CTX_free(ctx);
    OPENSSL_cleanse(output.data(), static_cast<size_t>(output.size()));
    if (ok)
        ok = out.commit();
    if (ok)
        QFile::setPermissions(targetPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    // QSaveFile discards unauthenticated plaintext automatically. Never delete
    // a pre-existing destination when authentication or writing fails.
    return ok;
}

bool PrivateVaultManager::importPathRecursive(const QString &path, const QString &parentId, QStringList *createdBlobNames)
{
    const QFileInfo info(path);
    if (!info.exists())
        return false;
    // Never follow symlinks into arbitrary locations. They are intentionally
    // skipped instead of making an otherwise valid folder import fail.
    if (info.isSymLink())
        return true;
    QJsonArray items = m_manifest.value(QStringLiteral("items")).toArray();
    if (info.isDir()) {
        const QString folderId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString name = uniqueName(parentId, info.fileName());
        items.append(QJsonObject {
            { QStringLiteral("id"), folderId }, { QStringLiteral("parent"), parentId },
            { QStringLiteral("name"), name }, { QStringLiteral("dir"), true },
            { QStringLiteral("size"), 0 }, { QStringLiteral("mime"), QStringLiteral("inode/directory") },
            { QStringLiteral("modifiedMs"), static_cast<double>(info.lastModified().toMSecsSinceEpoch()) }
        });
        m_manifest.insert(QStringLiteral("items"), items);
        const QFileInfoList children = QDir(path).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &child : children) {
            if (!importPathRecursive(child.absoluteFilePath(), folderId, createdBlobNames))
                return false;
        }
        return true;
    }
    if (!info.isFile())
        return true;
    if (info.size() > VaultSecurity::MaxGcmPlaintextBytes) {
        setError(securityMessage("This vault format supports files smaller than 64 GiB.",
                                 "Bu kasa biçimi 64 GiB'den küçük dosyaları destekliyor."));
        return false;
    }
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString blob = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!encryptFileToBlob(info.absoluteFilePath(), id, blob, m_masterKey))
        return false;
    if (createdBlobNames)
        createdBlobNames->append(blob);
    QMimeDatabase db;
    const QString mime = db.mimeTypeForFile(info, QMimeDatabase::MatchContent).name();
    // Build the preview while the source file is already plaintext, then store
    // only the AES-GCM encrypted PNG cache inside the vault.
    cacheThumbnailFromSource(id, info.absoluteFilePath(), mime);
    items = m_manifest.value(QStringLiteral("items")).toArray();
    items.append(QJsonObject {
        { QStringLiteral("id"), id }, { QStringLiteral("parent"), parentId },
        { QStringLiteral("name"), uniqueName(parentId, info.fileName()) }, { QStringLiteral("dir"), false },
        { QStringLiteral("size"), static_cast<double>(info.size()) }, { QStringLiteral("mime"), mime },
        { QStringLiteral("modifiedMs"), static_cast<double>(info.lastModified().toMSecsSinceEpoch()) },
        { QStringLiteral("blob"), blob }
    });
    m_manifest.insert(QStringLiteral("items"), items);
    return true;
}

bool PrivateVaultManager::importUrls(const QVariantList &urls)
{
    if (!m_unlocked || m_busy || urls.isEmpty())
        return false;
    clearStatus();
    setBusy(true);
    const QJsonObject originalManifest = m_manifest;
    QStringList createdBlobs;
    int imported = 0;
    for (const QVariant &value : urls) {
        const QString path = urlToLocalPath(value);
        if (path.isEmpty())
            continue;
        if (!importPathRecursive(path, m_currentFolderId, &createdBlobs)) {
            for (const QString &blob : std::as_const(createdBlobs))
                QFile::remove(QDir(objectsRoot()).filePath(blob));
            m_manifest = originalManifest;
            cleanupOrphanThumbnails();
            setBusy(false);
            if (m_lastError.isEmpty())
                setError(tr("İçe aktarma tamamlanamadı. Kasa değiştirilmedi."));
            emit operationFinished(false, m_lastError);
            return false;
        }
        ++imported;
    }
    if (!saveManifest()) {
        for (const QString &blob : std::as_const(createdBlobs))
            QFile::remove(QDir(objectsRoot()).filePath(blob));
        m_manifest = originalManifest;
        cleanupOrphanThumbnails();
        setBusy(false);
        setError(tr("Şifreli manifest kaydedilemedi."));
        emit operationFinished(false, m_lastError);
        return false;
    }
    setBusy(false);
    refreshVisibleItems();
    setStatus(tr("%1 öğe Sana Özel kasasına şifrelenerek eklendi.").arg(imported));
    emit operationFinished(true, m_statusMessage);
    return true;
}

bool PrivateVaultManager::exportItemRecursive(const QJsonObject &item, const QString &targetDirectory)
{
    const QString name = sanitizedName(item.value(QStringLiteral("name")).toString());
    if (item.value(QStringLiteral("dir")).toBool()) {
        const QString dirPath = QDir(targetDirectory).filePath(name);
        if (!QDir().mkpath(dirPath))
            return false;
        const QString id = item.value(QStringLiteral("id")).toString();
        const QJsonArray items = m_manifest.value(QStringLiteral("items")).toArray();
        for (const QJsonValue &value : items) {
            const QJsonObject child = value.toObject();
            if (child.value(QStringLiteral("parent")).toString() == id
                    && !exportItemRecursive(child, dirPath))
                return false;
        }
        return true;
    }
    QString outputPath = QDir(targetDirectory).filePath(name);
    if (QFileInfo::exists(outputPath)) {
        const QFileInfo fi(outputPath);
        const QString base = fi.completeBaseName();
        const QString suffix = fi.completeSuffix();
        for (int n = 2; ; ++n) {
            const QString candidate = suffix.isEmpty()
                ? QStringLiteral("%1 (%2)").arg(base).arg(n)
                : QStringLiteral("%1 (%2).%3").arg(base).arg(n).arg(suffix);
            outputPath = QDir(targetDirectory).filePath(candidate);
            if (!QFileInfo::exists(outputPath))
                break;
        }
    }
    return decryptBlobToFile(item.value(QStringLiteral("id")).toString(),
                             item.value(QStringLiteral("blob")).toString(), outputPath, m_masterKey);
}

bool PrivateVaultManager::exportItem(const QString &objectId, const QString &targetFolder)
{
    if (!m_unlocked || m_busy)
        return false;
    const QJsonObject item = itemById(objectId);
    if (item.isEmpty())
        return false;
    QString target = urlToLocalPath(targetFolder);
    if (target.isEmpty() || !QFileInfo(target).isDir()) {
        setError(tr("Geçerli bir dışa aktarma klasörü seçilmedi."));
        return false;
    }
    setBusy(true);
    const bool ok = exportItemRecursive(item, target);
    setBusy(false);
    if (ok)
        setStatus(tr("Öğe kasadan dışa aktarıldı."));
    else
        setError(tr("Öğe dışa aktarılamadı."));
    emit operationFinished(ok, ok ? m_statusMessage : m_lastError);
    return ok;
}

QString PrivateVaultManager::materializeForOpen(const QString &objectId)
{
    if (!m_unlocked || m_busy)
        return {};
    const QJsonObject item = itemById(objectId);
    if (item.isEmpty() || item.value(QStringLiteral("dir")).toBool())
        return {};
    if (!ensureStorageLayout())
        return {};
    if (!ensureRuntimeDirectory())
        return {};
    const QString originalName = item.value(QStringLiteral("name")).toString();
    const QString suffix = QFileInfo(originalName).suffix();
    QString tempName = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!suffix.isEmpty())
        tempName += QLatin1Char('.') + suffix;
    const QString path = QDir(runtimeRoot()).filePath(tempName);
    if (!decryptBlobToFile(objectId, item.value(QStringLiteral("blob")).toString(), path, m_masterKey)) {
        setError(tr("Dosya geçici önizleme alanına çözülemedi."));
        return {};
    }
    m_materializedPaths.insert(objectId, path);
    return QUrl::fromLocalFile(path).toString();
}

void PrivateVaultManager::releaseMaterialized(const QString &urlOrPath)
{
    const QString path = urlToLocalPath(urlOrPath);
    if (VaultSecurity::containsFile(m_runtimeRoot, path))
        QFile::remove(path);
    for (auto it = m_materializedPaths.begin(); it != m_materializedPaths.end(); ) {
        if (it.value() == path)
            it = m_materializedPaths.erase(it);
        else
            ++it;
    }
}

bool PrivateVaultManager::isRuntimeUrl(const QString &urlOrPath) const
{
    const QString path = urlToLocalPath(urlOrPath);
    if (path.isEmpty())
        return false;
    return VaultSecurity::containsFile(m_runtimeRoot, path);
}
