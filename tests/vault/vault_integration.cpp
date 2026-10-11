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
#include <QWheelEvent>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QUrl>
#include <cstdio>
#include <functional>
#include "backend_test_types.h"
#include "vaultsecurity.h"
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QLockFile>
#include "vaultrecovery.h"
#include <unistd.h>

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
    if (arguments.size() == 4 && arguments[1] == "--crash-preview") {
        require(vault.unlock(arguments[2]), "crash probe unlocks existing compatible vault");
        const QString preview = vault.materializeForOpen(arguments[3]);
        require(!preview.isEmpty(), "crash probe materializes plaintext");
        std::printf("%s\n", qPrintable(QUrl(preview).toLocalFile())); std::fflush(stdout);
        while (true) ::pause();
    }
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
    require(!vault.recoveryEnabled(), "recovery is opt-in and disabled by default");
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
    const QString sessionLock = QFileInfo(decrypted).absolutePath() + "/.lurviko-session.lock";
    vault.releaseMaterialized(sessionLock);
    require(QFileInfo::exists(sessionLock), "releasing arbitrary runtime paths cannot remove the session lock");
    {
        PrivateVaultManager second;
        require(second.unlock(changedPassword), "second vault instance opens independently");
        const QString another = second.materializeForOpen(payloadId);
        require(!another.isEmpty() && another != opened, "each vault instance has independent runtime storage");
        second.lock();
        require(QFileInfo::exists(QUrl(opened).toLocalFile()), "locking another instance preserves the first instance's plaintext");
    }
    {
        QProcess crashed;
        crashed.start(QCoreApplication::applicationFilePath(), {"--crash-preview", changedPassword, payloadId});
        require(crashed.waitForReadyRead(10000), "second process prepares a real plaintext preview");
        const QString crashPreview = QString::fromUtf8(crashed.readLine()).trimmed();
        require(QFileInfo::exists(crashPreview), "crash preview exists");
        const QString crashDirectory = QFileInfo(crashPreview).absolutePath();
        {
            PrivateVaultManager sweepWhileLive;
            require(QFileInfo::exists(crashPreview) && QFileInfo::exists(QUrl(opened).toLocalFile()),
                    "startup sweeps preserve live processes and the current instance");
        }
        crashed.kill(); require(crashed.waitForFinished(), "crash process is terminated without cleanup");
        require(QFileInfo::exists(crashPreview), "process crash leaves a testable plaintext orphan");
        {
            PrivateVaultManager sweepAfterCrash;
            require(!QFileInfo::exists(crashDirectory), "next vault startup removes the crashed process directory");
            require(QFileInfo::exists(QUrl(opened).toLocalFile()), "crash recovery leaves the live instance untouched");
        }
    }
    std::fprintf(stderr, "PASS: live session locks, killed-process plaintext recovery and instance isolation\n");
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

    const auto readRecoveryHeader = [&] {
        QFile file(privateRoot + "/vault.json"); require(file.open(QIODevice::ReadOnly), "recovery header opens");
        return file.readAll();
    };
    const QByteArray disabledHeader = readRecoveryHeader();
    require(vault.prepareRecoveryKey(), "prepare optional recovery without activating it");
    const QString cancelledInitial = vault.takeRecoveryKey();
    require(!cancelledInitial.isEmpty() && !vault.recoveryEnabled() && readRecoveryHeader() == disabledHeader,
            "displaying an unconfirmed key leaves the disk and disabled state unchanged");
    vault.cancelRecoveryKey();
    require(!vault.commitRecoveryKey() && readRecoveryHeader() == disabledHeader,
            "cancelled initial key cannot later be activated");
    require(vault.regenerateRecoveryKey() && vault.recoveryEnabled(), "unlocked vault enables recovery");
    const QString firstKey = vault.takeRecoveryKey();
    require(!firstKey.isEmpty() && vault.takeRecoveryKey().isEmpty(), "generated key can be retrieved exactly once");
    require(!readRecoveryHeader().contains(firstKey.toUtf8()), "display key is never stored in the header");
    const QByteArray activeHeader = readRecoveryHeader();
    require(vault.prepareRecoveryKey(), "prepare replacement without revoking existing key");
    require(vault.takeRecoveryKey() != firstKey, "staged replacement is independent");
    vault.cancelRecoveryKey();
    require(readRecoveryHeader() == activeHeader && vault.recoveryEnabled(), "cancelled rotation preserves existing key and exact header");
    require(vault.prepareRecoveryKey(), "prepare rotation for concurrency check");
    vault.takeRecoveryKey();
    require(vault.setAutoLockEnabled(!vault.autoLockEnabled()), "change header while replacement is staged");
    const QByteArray newerHeader = readRecoveryHeader();
    require(!vault.commitRecoveryKey() && readRecoveryHeader() == newerHeader,
            "confirmation cannot overwrite intervening vault changes");
    vault.cancelRecoveryKey();
    PrivateVaultManager stale;
    require(stale.unlock(changedPassword), "another instance holds the old envelope");
    {
        const QByteArray before = readRecoveryHeader();
        QLockFile held(privateRoot + "/header-write.lock"); require(held.tryLock(), "hold write lock without root");
        require(!vault.regenerateRecoveryKey() && vault.takeRecoveryKey().isEmpty(), "failed replacement exposes no key");
        require(readRecoveryHeader() == before, "failed replacement preserves the previous envelope");
    }
    require(vault.regenerateRecoveryKey(), "replacement succeeds atomically");
    const QString rotatedKey = vault.takeRecoveryKey(); require(rotatedKey != firstKey, "replacement uses an independent random key");
    require(stale.setAutoLockEnabled(false), "stale instance saves ordinary preferences");
    QByteArray rawFirst, rawRotated, masterProbe;
    require(VaultRecovery::decodeKey(firstKey, &rawFirst) && VaultRecovery::decodeKey(rotatedKey, &rawRotated), "display keys parse");
    require(!VaultRecovery::unwrap(QJsonDocument::fromJson(readRecoveryHeader()).object(), rawFirst, &masterProbe), "stale writes cannot resurrect a revoked key");
    require(VaultRecovery::unwrap(QJsonDocument::fromJson(readRecoveryHeader()).object(), rawRotated, &masterProbe), "stale writes retain the current envelope");
    require(vault.disableRecoveryKey(), "unlocked vault disables recovery");
    require(stale.setAutoLockEnabled(false), "stale instance writes after disable");
    require(!QJsonDocument::fromJson(readRecoveryHeader()).object().contains("recovery"), "stale writes cannot resurrect disabled recovery");
    stale.lock();
    vault.lock(); require(!vault.regenerateRecoveryKey() && !vault.disableRecoveryKey(), "locked users cannot manage recovery");
    require(!vault.recoverVault(rotatedKey, password), "disabled key is rejected");
    require(vault.unlock(changedPassword), "disabling recovery preserves the password");
    require(vault.regenerateRecoveryKey(), "enable a new recovery envelope for offline export");
    const QString exportKey = vault.takeRecoveryKey();
    const QByteArray beforeExport = readRecoveryHeader();
    QProcess offline;
    const QString output = QString::fromLocal8Bit(qgetenv("LURVIKO_VAULT_TEST_ROOT")) + "/offline-export";
    offline.start("python3", {"-c", "import importlib.util,sys; s=importlib.util.spec_from_file_location('recovery',sys.argv[1]); m=importlib.util.module_from_spec(s); s.loader.exec_module(m); m.recover(sys.argv[2],sys.stdin.readline().strip(),sys.argv[3])",
        QStringLiteral(LURVIKO_SOURCE_DIR) + "/tools/recover-vault.py", privateRoot, output});
    require(offline.waitForStarted(), "independent Python recovery tool starts");
    offline.write(exportKey.toUtf8() + '\n'); offline.closeWriteChannel();
    require(offline.waitForFinished(15000) && offline.exitCode() == 0, "independent Python code decrypts actual C++ vault");
    QFile exported(output + "/payload.txt");
    require(exported.open(QIODevice::ReadOnly) && exported.readAll() == payload, "offline recovered file matches original bytes");
    require(readRecoveryHeader() == beforeExport, "offline export leaves vault header untouched");
    require(vault.setPasswordProtection(true, 2, 1, 0), "recovery shares retry protection");
    vault.lock();
    require(!vault.recoverVault(firstKey, password) && vault.failedPasswordAttempts() == 1, "revoked recovery counts as a failed attempt");
    require(!vault.recoverVault("LRK1-invalid", password) && vault.passwordLockedOut(), "bad keys trigger the same lockout");
    require(!vault.recoverVault(exportKey, password), "valid recovery cannot bypass active lockout");
    require(until([&] { return vault.passwordWaitSeconds() == 0; }), "recovery lockout expires");
    const QString recoveredPassword = "Recovered-password-2026";
    const QByteArray beforeRecovery = readRecoveryHeader();
    {
        QLockFile held(privateRoot + "/header-write.lock"); require(held.tryLock(), "hold recovery write lock");
        require(!vault.recoverVault(exportKey, recoveredPassword), "recovery fails closed when atomic update is blocked");
        require(readRecoveryHeader() == beforeRecovery && !vault.unlocked(), "failed recovery preserves header and locked state");
    }
    require(vault.recoverVault(exportKey, recoveredPassword), "saved recovery key resets forgotten password");
    require(vault.unlocked() && !vault.recoveryEnabled() && vault.failedPasswordAttempts() == 0, "successful recovery resets attempts and revokes used key");
    const QString restored = vault.materializeForOpen(payloadId);
    QFile restoredFile(QUrl(restored).toLocalFile()); require(restoredFile.open(QIODevice::ReadOnly) && restoredFile.readAll() == payload, "recovery preserves encrypted content");
    restoredFile.close(); vault.releaseMaterialized(restored);
    require(vault.setPasswordProtection(false, 2, 1, 0), "disable delays for final credential checks");
    vault.lock();
    require(!vault.recoverVault(exportKey, password) && !vault.unlock(changedPassword), "consumed key and forgotten password are both rejected");
    require(vault.unlock(recoveredPassword), "replacement password opens the unchanged encrypted files");
    require(vault.changePassword(recoveredPassword, changedPassword), "restore fixture password for existing UI tests");
    require(vault.regenerateRecoveryKey(), "generate pending key for memory lifecycle check"); vault.lock();
    require(vault.takeRecoveryKey().isEmpty(), "locking wipes an undisplayed pending key");
    require(vault.unlock(changedPassword) && vault.disableRecoveryKey(), "fixture remains usable after wiping pending key");
    app.setApplicationName("RecoveryCreation");
    {
        PrivateVaultManager created;
        require(created.createVault(password, true) && created.recoveryEnabled(), "new vault can opt into recovery during creation");
        const QString initial = created.takeRecoveryKey(); require(!initial.isEmpty() && created.takeRecoveryKey().isEmpty(), "creation key is displayed only once");
        created.lock(); require(created.recoverVault(initial, changedPassword), "creation recovery key works without the original password");
    }
    app.setApplicationName("Vault");
    std::puts("PASS: optional recovery, one-time display, rotation, disable, stale-instance protection, failed-write rollback, lockout and independent offline export");
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
    require(!child(window, "vaultCreateRecoverySwitch")->property("checked").toBool(), "creation switch defaults off in real QML");
    for (bool dark : {false, true}) {
        QQmlExpression theme(qmlContext(window), window, dark
            ? "(function(){if(!AppTheme.dark)AppTheme.toggle()})()"
            : "(function(){if(AppTheme.dark)AppTheme.toggle()})()");
        theme.evaluate();
        language->setProperty("language", dark ? "tr" : "en");
        require(QMetaObject::invokeMethod(popup, "open"), "security popup opens");
        pause(180);
        require(scroll->property("contentHeight").toReal() > scroll->property("height").toReal(), "security options have a scrollable viewport");
        scroll->setProperty("contentY", 0);
        auto scrollItem = qobject_cast<QQuickItem *>(scroll);
        require(scrollItem, "security viewport is a real quick item");
        const QPointF wheelPoint = scrollItem->mapToScene(QPointF(scrollItem->width() / 2, 80));
        QWheelEvent wheel(wheelPoint, window->mapToGlobal(wheelPoint.toPoint()), QPoint(), QPoint(0, -120),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(window, &wheel);
        pause(200);
        const qreal maxScroll = scroll->property("contentHeight").toReal() - scroll->property("height").toReal();
        require(scroll->property("contentY").toReal() >= qMin(qreal(240), maxScroll) - 2,
                "one real mouse notch uses the application wheel distance in Security");
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
    require(QMetaObject::invokeMethod(popup, "open"), "open Security to enable recovery");
    auto recoveryToggle = child(window, "vaultRecoverySwitch");
    recoveryToggle->setProperty("checked", true);
    require(QMetaObject::invokeMethod(recoveryToggle, "clicked"), "recovery toggle asks for confirmation");
    require(!vault.recoveryEnabled(), "toggle alone does not enable recovery before consent");
    auto confirmation = child(window, "vaultConfirmRecoveryChange");
    require(QMetaObject::invokeMethod(confirmation, "clicked"), "confirm recovery generation");
    auto shownKey = child(window, "vaultRecoveryKeyDisplay");
    auto keyPopup = child(window, "vaultRecoveryKeyPopup");
    QString uiKey = shownKey->property("text").toString();
    require(!vault.recoveryEnabled() && uiKey.startsWith("LRK1-") && vault.takeRecoveryKey().isEmpty(), "actual UI displays key once without activating before Done");
    auto keyTextItem = qobject_cast<QQuickItem *>(shownKey);
    require(keyTextItem, "key display is a real quick item");
    pause(200); // Let the popup transition and ColumnLayout establish the hit area.
    const QPointF menuPoint = keyTextItem->mapToScene(QPointF(keyTextItem->width() / 2, keyTextItem->height() / 2));
    QMouseEvent press(QEvent::MouseButtonPress, menuPoint, window->mapToGlobal(menuPoint.toPoint()),
                      Qt::RightButton, Qt::RightButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, menuPoint, window->mapToGlobal(menuPoint.toPoint()),
                        Qt::RightButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(window, &press); QApplication::sendEvent(window, &release);
    // Native platform right-click dispatch also delivers QContextMenuEvent;
    // sending QMouseEvent directly does not synthesize that platform event.
    QContextMenuEvent contextEvent(QContextMenuEvent::Mouse, menuPoint.toPoint(), window->mapToGlobal(menuPoint.toPoint()));
    QApplication::sendEvent(window, &contextEvent);
    auto keyMenu = shownKey->property("applicationContextMenu").value<QObject *>();
    require(keyMenu, "recovery TextArea owns its custom context menu");
    require(until([&] { return keyMenu->property("visible").toBool(); }), "right click opens the application text menu");
    QQmlExpression copyLabel(qmlContext(keyMenu), keyMenu, "itemAt(4).text");
    require(copyLabel.evaluate().toString().contains(QString::fromUtf8("Kopyala")), "key context menu uses the selected Turkish language");
    QQmlExpression readOnlyActions(qmlContext(keyMenu), keyMenu, "!itemAt(0).enabled && !itemAt(1).enabled && !itemAt(3).enabled && !itemAt(5).enabled && !itemAt(6).enabled");
    require(readOnlyActions.evaluate().toBool(), "read-only recovery field disables editing actions");
    require(QMetaObject::invokeMethod(keyMenu, "close"), "close text menu");
    require(QMetaObject::invokeMethod(child(window, "vaultRecoveryKeyCancel"), "clicked"), "cancel recovery before initial activation");
    require(!vault.commitRecoveryKey() && !vault.recoveryEnabled(), "Cancel discards pending activation before the exit animation finishes");
    require(until([&] { return shownKey->property("text").toString().isEmpty(); }) && !vault.recoveryEnabled(),
            "cancel clears the key and leaves recovery off");
    require(QMetaObject::invokeMethod(popup, "open"), "reopen Security after cancellation");
    recoveryToggle->setProperty("checked", true);
    require(QMetaObject::invokeMethod(recoveryToggle, "clicked") && QMetaObject::invokeMethod(confirmation, "clicked"), "generate after cancellation");
    uiKey = shownKey->property("text").toString();
    // This screenshot contains only a disposable isolated test-vault key.
    pause(180); require(window->grabWindow().save("/tmp/lurviko-recovery-key-ui.png"), "recovery warning and key preview render");
    child(window, "vaultRecoveryKeySaved")->setProperty("checked", true);
    require(QMetaObject::invokeMethod(child(window, "vaultRecoveryKeyDone"), "clicked"), "acknowledge safe storage");
    require(until([&] { return shownKey->property("text").toString().isEmpty(); }) && vault.recoveryEnabled(), "Done activates and clears the UI text");
    require(QMetaObject::invokeMethod(popup, "open"), "open settings to cancel rotation");
    require(QMetaObject::invokeMethod(child(window, "vaultRotateRecoveryKey"), "clicked")
            && QMetaObject::invokeMethod(confirmation, "clicked"), "preview replacement in UI");
    require(QMetaObject::invokeMethod(child(window, "vaultRecoveryKeyCancel"), "clicked"), "cancel UI rotation");
    require(until([&] { return shownKey->property("text").toString().isEmpty(); }), "cancel clears replacement text");
    QByteArray savedRaw, savedMaster;
    require(VaultRecovery::decodeKey(uiKey, &savedRaw)
            && VaultRecovery::unwrap(QJsonDocument::fromJson(readRecoveryHeader()).object(), savedRaw, &savedMaster),
            "cancelled UI rotation keeps the saved previous key valid");
    require(QMetaObject::invokeMethod(popup, "open"), "open settings to rotate key");
    require(QMetaObject::invokeMethod(child(window, "vaultRotateRecoveryKey"), "clicked")
            && QMetaObject::invokeMethod(confirmation, "clicked"), "rotate through the actual settings UI");
    const QString nextUiKey = shownKey->property("text").toString();
    require(!nextUiKey.isEmpty() && nextUiKey != uiKey, "rotation shows a new independent key");
    child(window, "vaultRecoveryKeySaved")->setProperty("checked", true);
    require(QMetaObject::invokeMethod(child(window, "vaultRecoveryKeyDone"), "clicked"), "confirm UI rotation before locking");
    vault.lock();
    require(until([&] { return !keyPopup->property("visible").toBool() && shownKey->property("text").toString().isEmpty(); }), "locking closes and clears the key display");
    require(QMetaObject::invokeMethod(child(window, "vaultOpenRecovery"), "clicked"), "forgot-password action opens recovery UI");
    child(window, "vaultRecoveryInput")->setProperty("text", nextUiKey);
    auto recoveryPopup = child(window, "vaultRecoveryUnlockPopup");
    child(window, "vaultRecoveryNewPassword")->setProperty("text", password);
    child(window, "vaultRecoveryNewPasswordAgain")->setProperty("text", password);
    require(QMetaObject::invokeMethod(child(window, "vaultRecoverButton"), "clicked") && vault.unlocked() && !vault.recoveryEnabled(), "actual recovery UI resets the password and disables used key");
    require(until([&] { return !recoveryPopup->property("visible").toBool() && child(window, "vaultRecoveryInput")->property("text").toString().isEmpty(); }), "recovery inputs clear on closure");
    std::puts("PASS: vault retry delay, limits, restart persistence, success/expiry reset, disabled mode, encrypted data and security modal");
    return 0;
}
