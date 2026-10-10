#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <cstdio>
#include <functional>
#include "backend_test_types.h"
#include "vaultsecurity.h"
#include <QJsonDocument>
#include <QTemporaryDir>

static void require(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static void pause(int ms) {
    QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec();
}
static bool until(const std::function<bool()> &check, int timeout = 5500) {
    QElapsedTimer timer; timer.start();
    while (!check() && timer.elapsed() < timeout) pause(10);
    return check();
}
static QObject *child(QObject *root, const char *name) {
    auto object = root->findChild<QObject *>(QString::fromLatin1(name));
    require(object, name); return object;
}
static void restartProbe(const QString &kind, int count) {
    QProcess process;
    process.start(QCoreApplication::applicationFilePath(), {kind, QString::number(count)});
    require(process.waitForFinished(10000) && process.exitCode() == 0, "authentication survives an actual process restart");
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("Lurviko-QA"); app.setApplicationName("Vault");
    QQuickStyle::setStyle("Basic");
    PrivateVaultManager vault;
    const QStringList arguments = app.arguments();
    if (arguments.size() > 2) {
        require(!vault.unlocked(), "restarted vault is locked");
        require(vault.failedPasswordAttempts() == arguments[2].toInt(), "failed-attempt count survives restart");
        require(vault.passwordProtectionEnabled(), "protection setting survives restart");
        if (arguments[1] == "--probe-lockout")
            require(vault.passwordLockedOut() && vault.passwordWaitSeconds() > 0, "lockout deadline survives restart");
        return 0;
    }
    // REGISTER_BACKEND_TYPES
    const QString password = QStringLiteral("Correct-password-ı-2026");
    const QString changedPassword = QStringLiteral("Changed-password-ğ-2026");
    std::fprintf(stderr, "Vault test: encrypted fixture and retry policy\n");
    require(vault.createVault(password), "real encrypted fixture vault is created");
    require(vault.createFolder("Kept encrypted folder"), "fixture manifest contains a real item");
    const QByteArray payload = QStringLiteral("Private test payload: İşler güçler, şarkılar.\n").toUtf8();
    const QString payloadPath = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath("payload.txt");
    QFile plaintext(payloadPath);
    require(plaintext.open(QIODevice::WriteOnly) && plaintext.write(payload) == payload.size(), "temporary plaintext fixture writes");
    plaintext.close();
    require(vault.importUrls({QUrl::fromLocalFile(payloadPath)}), "real file is imported into encrypted storage");
    require(vault.passwordProtectionEnabled() && vault.maxPasswordAttempts() == 5
            && vault.passwordLockoutSeconds() == 300 && vault.passwordRetrySeconds() == 2, "default protection policy");
    require(vault.setPasswordProtection(true, 3, 4, 1), "configure attempts, lockout and retry delay");
    require(!vault.setPasswordProtection(true, 0, 4, 1), "invalid policy is rejected");
    vault.lock();
    require(!vault.unlock("wrong") && vault.failedPasswordAttempts() == 1, "wrong password increments the counter");
    require(vault.passwordWaitSeconds() > 0 && !vault.passwordLockedOut(), "per-attempt delay starts");
    require(!vault.unlock(password) && !vault.unlock("wrong"), "correct and incorrect passwords both wait for the retry delay");
    require(vault.failedPasswordAttempts() == 1, "blocked attempts do not inflate the counter");
    require(!vault.setPasswordProtection(false, 3, 4, 1), "locked vault cannot disable its protection");
    restartProbe("--probe-count", 1);
    require(until([&] { return vault.passwordWaitSeconds() == 0; }), "retry delay expires");
    require(!vault.unlock("wrong") && vault.failedPasswordAttempts() == 2, "counter is retained between attempts");
    require(until([&] { return vault.passwordWaitSeconds() == 0; }), "second delay expires");
    require(vault.unlock(password), "correct password succeeds after the delay");
    require(vault.failedPasswordAttempts() == 0 && vault.passwordWaitSeconds() == 0, "successful login resets all previous failures");
    require(vault.totalItemCount() == 2, "encrypted manifest is preserved");
    std::fprintf(stderr, "PASS: retry delay, persistent count and successful login reset\n");

    require(vault.setPasswordProtection(true, 2, 4, 0), "configure immediate attempts and temporary lockout");
    vault.lock();
    require(!vault.unlock("wrong") && !vault.unlock("wrong"), "attempt limit is reached");
    require(vault.failedPasswordAttempts() == 2 && vault.passwordLockedOut(), "attempt limit activates lockout");
    require(!vault.unlock(password) && !vault.quickUnlock(), "password and KWallet paths cannot bypass lockout");
    require(vault.failedPasswordAttempts() == 2, "lockout does not consume extra attempts");
    restartProbe("--probe-lockout", 2);
    require(until([&] { return vault.failedPasswordAttempts() == 0 && vault.passwordWaitSeconds() == 0; }), "expiry resets the counter without navigating away");
    require(vault.unlock(password), "vault opens after lockout expiry");
    std::fprintf(stderr, "PASS: lockout, KWallet guard, restart and automatic expiry\n");
    require(vault.setPasswordProtection(false, 2, 1, 5), "disable protection");
    vault.lock();
    for (int i = 0; i < 3; ++i) require(!vault.unlock("wrong"), "disabled protection still rejects wrong passwords");
    require(vault.failedPasswordAttempts() == 0 && vault.passwordWaitSeconds() == 0, "disabled policy adds no count or delay");
    require(vault.unlock(password), "disabled policy permits immediate correct login");

    require(vault.setPasswordProtection(true, 2, 1, 0), "protect password-change verification too");
    require(!vault.changePassword("wrong", changedPassword) && vault.failedPasswordAttempts() == 1, "incorrect current password counts");
    require(vault.changePassword(password, changedPassword) && vault.failedPasswordAttempts() == 0, "successful password verification resets failures");
    vault.lock();
    require(vault.unlock(changedPassword) && vault.totalItemCount() == 2, "changed password preserves encrypted contents");
    QString payloadId;
    for (int row = 0; row < vault.rowCount(); ++row)
        if (vault.data(vault.index(row), PrivateVaultManager::NameRole).toString() == "payload.txt")
            payloadId = vault.data(vault.index(row), PrivateVaultManager::ObjectIdRole).toString();
    require(!payloadId.isEmpty(), "imported file remains in the manifest");
    const QString opened = vault.materializeForOpen(payloadId);
    QFile decrypted(QUrl(opened).toLocalFile());
    require(decrypted.open(QIODevice::ReadOnly) && decrypted.readAll() == payload, "encrypted file bytes survive password changes and authentication resets");
    decrypted.close();
    require(VaultSecurity::isMemoryFilesystem(QFileInfo(decrypted).absolutePath()), "vault plaintext uses a verified memory filesystem");
    require(!(QFileInfo(decrypted).permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther)), "plaintext has owner-only permissions");
    {
        PrivateVaultManager second;
        require(second.unlock(changedPassword), "second vault instance opens independently");
        const QString another = second.materializeForOpen(payloadId);
        require(!another.isEmpty() && another != opened, "each vault instance has independent runtime storage");
        second.lock();
        require(QFileInfo::exists(QUrl(opened).toLocalFile()), "locking another instance preserves the first instance's plaintext");
    }
    QTemporaryDir sibling(QFileInfo(decrypted).absolutePath() + "-sibling-XXXXXX");
    QFile siblingFile(sibling.filePath("keep.txt")); require(siblingFile.open(QIODevice::WriteOnly), "sibling fixture opens");
    siblingFile.write("keep"); siblingFile.close();
    require(!vault.isRuntimeUrl(siblingFile.fileName()), "runtime sibling prefix is rejected");
    vault.releaseMaterialized(siblingFile.fileName());
    require(QFileInfo::exists(siblingFile.fileName()), "releasing a sibling path cannot delete it");
    vault.releaseMaterialized(opened);
    const QString privateRoot = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(".private-vault");
    const QString blobPath = QDir(privateRoot + "/objects").entryInfoList(QDir::Files).constFirst().absoluteFilePath();
    QFile blob(blobPath); require(blob.open(QIODevice::ReadOnly), "ciphertext fixture opens");
    const QByteArray originalBlob = blob.readAll(); blob.close();
    QByteArray damagedBlob = originalBlob; damagedBlob[damagedBlob.size()-1] ^= 1;
    require(blob.open(QIODevice::WriteOnly | QIODevice::Truncate), "ciphertext tamper fixture opens");
    blob.write(damagedBlob); blob.close();
    require(vault.materializeForOpen(payloadId).isEmpty(), "tampered GCM tag never produces a plaintext file");
    require(blob.open(QIODevice::WriteOnly | QIODevice::Truncate), "ciphertext fixture is restored");
    blob.write(originalBlob); blob.close();
    const QString recovered = vault.materializeForOpen(payloadId);
    QFile recoveredFile(QUrl(recovered).toLocalFile());
    require(recoveredFile.open(QIODevice::ReadOnly) && recoveredFile.readAll() == payload, "valid authenticated ciphertext remains compatible");
    recoveredFile.close(); vault.releaseMaterialized(recovered);
    QFile oversized(QDir(QString::fromLocal8Bit(qgetenv("LURVIKO_VAULT_TEST_ROOT"))).filePath("oversized-sparse.bin"));
    require(oversized.open(QIODevice::WriteOnly) && oversized.resize(VaultSecurity::MaxGcmPlaintextBytes+1), "sparse oversized fixture is created without allocating data");
    oversized.close();
    require(!vault.importUrls({QUrl::fromLocalFile(oversized.fileName())}) && vault.totalItemCount() == 2, "GCM length limit rejects oversized files before reading or modifying the vault");
    oversized.remove();
    require(vault.setPasswordProtection(false, 2, 1, 0), "disable counters for malformed-header checks");
    vault.lock();
    QFile headerFile(privateRoot + "/vault.json"); require(headerFile.open(QIODevice::ReadOnly), "header fixture opens");
    const QByteArray originalHeader = headerFile.readAll(); headerFile.close();
    const QJsonObject originalObject = QJsonDocument::fromJson(originalHeader).object();
    for (const QString &field : {QStringLiteral("iterations"), QStringLiteral("memCostKiB"), QStringLiteral("lanes"), QStringLiteral("threads")}) {
        QJsonObject corrupt = originalObject; corrupt[field] = -1;
        require(headerFile.open(QIODevice::WriteOnly | QIODevice::Truncate), "corrupt KDF header opens");
        headerFile.write(QJsonDocument(corrupt).toJson()); headerFile.close();
        QElapsedTimer timer; timer.start();
        require(!vault.unlock(changedPassword) && timer.elapsed() < 500, "corrupt KDF header is rejected before allocating Argon2 resources");
    }
    require(headerFile.open(QIODevice::WriteOnly | QIODevice::Truncate), "oversized header fixture opens");
    headerFile.write(QByteArray(65537, ' ')); headerFile.close();
    require(!vault.unlock(changedPassword), "oversized header is rejected before parsing");
    require(headerFile.open(QIODevice::WriteOnly | QIODevice::Truncate), "valid header is restored");
    headerFile.write(originalHeader); headerFile.close();
    require(vault.unlock(changedPassword), "original encrypted vault still unlocks after header checks");
    std::fprintf(stderr, "PASS: memory-backed private storage, instance isolation, sibling paths, authenticated tamper rejection, GCM size limit and malformed KDF headers\n");
    std::fprintf(stderr, "PASS: disabled policy and password change preserve encrypted data\n");
    const QString authFile = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                                 .filePath(".private-vault/authentication.json");
    require((QFile::permissions(authFile) & (QFileDevice::ReadGroup | QFileDevice::WriteGroup
             | QFileDevice::ReadOther | QFileDevice::WriteOther)) == 0, "attempt metadata is private to the owner");
    if (arguments.contains("--backend-only")) {
        std::puts("PASS: vault authentication and encrypted file preservation without desktop/media services");
        return 0;
    }

    QQmlApplicationEngine engine;
    std::fprintf(stderr, "Vault test: loading security UI\n");
    engine.addImageProvider("bundledicon", new BundledIconProvider);
    engine.addImageProvider("systemicon", new SystemIconProvider);
    engine.addImageProvider("gfilethumb", new ThumbnailProvider);
    engine.rootContext()->setContextProperty("fixtureVault", &vault);
    engine.loadData(R"qml(import QtQuick
import QtQuick.Controls
import Lurviko.App
import Lurviko.Backend
ApplicationWindow {
    width: 1000; height: 720; visible: true
    LanguageManager { id: language; objectName: "language" }
    QtObject { id: player; property string currentUrl: ""; function closePlayer() {} }
    PrivateVaultPage { anchors.fill: parent; lang: language; vault: fixtureVault; musicPlayer: player }
})qml");
    require(!engine.rootObjects().isEmpty(), "real vault page loads");
    auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    require(window, "vault window exists");
    auto popup = child(window, "vaultSecurityPopup");
    auto scroll = child(window, "vaultSecurityScroll");
    auto enabled = child(window, "vaultPasswordProtection");
    auto attempts = child(window, "vaultPasswordAttemptLimit");
    auto lockout = child(window, "vaultPasswordLockoutDuration");
    auto delay = child(window, "vaultPasswordRetryDelay");
    auto save = child(window, "vaultSavePasswordProtection");
    auto language = child(window, "language");
    for (bool dark : {false, true}) {
        QQmlExpression theme(qmlContext(window), window, dark
            ? "(function(){if(!AppTheme.dark)AppTheme.toggle()})()"
            : "(function(){if(AppTheme.dark)AppTheme.toggle()})()");
        theme.evaluate();
        language->setProperty("language", dark ? "tr" : "en");
        require(QMetaObject::invokeMethod(popup, "open"), "security popup opens");
        pause(180);
        require(scroll->property("contentHeight").toReal() > scroll->property("height").toReal(), "security options have a scrollable viewport");
        scroll->setProperty("contentY", scroll->property("contentHeight").toReal() - scroll->property("height").toReal());
        attempts->setProperty("value", 3); lockout->setProperty("value", 20); delay->setProperty("value", 1);
        enabled->setProperty("checked", true);
        require(QMetaObject::invokeMethod(save, "clicked"), "protection settings are saved from the modal");
        require(vault.maxPasswordAttempts() == 3 && vault.passwordLockoutSeconds() == 20 && vault.passwordRetrySeconds() == 1, "modal controls update real policy");
        pause(180);
        require(window->grabWindow().save(dark ? "/tmp/gfile-vault-security-dark.png" : "/tmp/gfile-vault-security-light.png"), "security modal screenshot saves");
        require(QMetaObject::invokeMethod(popup, "close"), "security popup closes");
    }
    require(vault.setPasswordProtection(true, 1, 2, 0), "configure UI lockout");
    vault.lock();
    auto input = child(window, "vaultUnlockPassword");
    auto unlockButton = child(window, "vaultUnlockButton");
    input->setProperty("text", changedPassword);
    require(unlockButton->property("enabled").toBool(), "unlock control starts enabled with a password");
    require(!vault.unlock("wrong"), "start UI lockout");
    require(!unlockButton->property("enabled").toBool(), "unlock control disables during lockout");
    pause(180);
    require(window->grabWindow().save("/tmp/gfile-vault-locked.png"), "locked state and countdown screenshot saves");
    require(until([&] { return unlockButton->property("enabled").toBool(); }), "unlock control re-enables automatically when countdown expires");
    require(QMetaObject::invokeMethod(unlockButton, "clicked") && vault.unlocked(), "unlock works again without reopening the page");
    std::puts("PASS: vault retry delay, limits, restart persistence, success/expiry reset, disabled mode, encrypted data and security modal");
    return 0;
}
