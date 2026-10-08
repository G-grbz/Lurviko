#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <cstdio>
#include "appmigration.h"
#include "privatevaultmanager.h"
#include "updatechecker.h"

static void require(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static void write(const QString &path, const QByteArray &bytes) {
    require(QDir().mkpath(QFileInfo(path).absolutePath()), "create fixture directory");
    QFile file(path); require(file.open(QIODevice::WriteOnly), "open fixture");
    require(file.write(bytes) == bytes.size(), "write fixture");
}
static QByteArray read(const QString &path) {
    QFile file(path); require(file.open(QIODevice::ReadOnly), "read retained data"); return file.readAll();
}
static QByteArray release(const char *tag, const char *url, bool preview = false) {
    return QJsonDocument(QJsonObject{{"tag_name", tag}, {"html_url", url}, {"prerelease", preview}, {"draft", false}}).toJson();
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("g-File"); app.setApplicationName("g-File");
    app.setApplicationVersion("1.0.0");
    const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    {
        QSettings old; old.setValue("ui/language", "tr"); old.setValue("BrowserView/musicIconSize", 287);
        old.sync(); require(old.status() == QSettings::NoError, "save legacy settings");
        PrivateVaultManager vault;
        require(vault.createVault(QStringLiteral("Şifre-123456")), "create actual legacy encrypted vault");
        require(vault.createFolder(QStringLiteral("Özel dosyalar")), "retain encrypted manifest folder");
        vault.lock();
    }
    const QString oldVault = data + "/g-File/g-File/.private-vault/vault.json";
    const QByteArray vaultBytes = read(oldVault);
    write(config + "/g-file/subtitle-ai/settings.json", "{\"model\":\"retained\"}");
    write(cache + "/g-File/g-File/thumbs/retained.png", "cached thumbnail");
    write(cache + "/g-file/subtitle-ai/models/model.bin", "downloaded model");
    write(data + "/g-File/music-library.json", "music history");
    write(data + "/Lurviko/conflict.txt", "new content");
    write(data + "/g-File/conflict.txt", "old content");
    QString error;
    require(migrateLegacyUserData(config, data, cache, &error), qPrintable(error));
    app.setOrganizationName("Lurviko"); app.setApplicationName("Lurviko");
    QSettings current;
    require(current.value("ui/language").toString() == "tr", "language migrated");
    require(current.value("BrowserView/musicIconSize").toInt() == 287, "view preference migrated");
    require(read(data + "/Lurviko/Lurviko/.private-vault/vault.json") == vaultBytes, "encrypted header unchanged");
    require(read(data + "/Lurviko/music-library.json") == "music history", "music history retained");
    require(read(config + "/Lurviko/subtitle-ai/settings.json").contains("retained"), "subtitle settings retained");
    require(read(cache + "/Lurviko/Lurviko/thumbs/retained.png") == "cached thumbnail", "thumbnail retained");
    require(read(cache + "/Lurviko/subtitle-ai/models/model.bin") == "downloaded model", "models retained");
    require(relocatedAppPath("file://" + config + "/g-File/tmdb/images/cover.png") == "file://" + config + "/Lurviko/tmdb/images/cover.png", "cached artwork URL follows migration");
    require(relocatedAppPath(cache + "/g-File/g-File/thumbs/retained.png") == cache + "/Lurviko/Lurviko/thumbs/retained.png", "nested cached path follows migration");
    require(read(data + "/Lurviko/conflict.txt") == "new content", "no overwrite of existing new file");
    require(read(data + "/Lurviko/migration-backup/g-File/conflict.txt") == "old content", "colliding legacy file retained");
    for (const auto &root : {config, data, cache}) {
        require(!QFileInfo::exists(root + "/g-File") && !QFileInfo::exists(root + "/g-file"), "old roots removed after move");
    }
    require(migrateLegacyUserData(config, data, cache, &error), "migration is idempotent");
    {
        PrivateVaultManager vault;
        require(vault.exists(), "legacy vault recognized after rename");
        require(vault.unlock(QStringLiteral("Şifre-123456")), "legacy encryption still decrypts");
        require(vault.totalItemCount() == 1, "encrypted manifest retained");
    }
    const QString external = cache + "/external";
    write(external + "/g-File/never-move.txt", "do not move");
    require(QFile::link(external, cache + "/g-file"), "create symlink-root fixture");
    require(!migrateLegacyUserData(config, data, cache, &error), "reject symlink root before traversing it");
    require(read(external + "/g-File/never-move.txt") == "do not move", "external directory untouched");

    UpdateChecker updates;
    require(!updates.updateAvailable(), "initial release has no update");
    require(updates.acceptRelease(release("v1.1.0", "https://github.com/G-grbz/Lurviko/releases/tag/v1.1.0")), "stable release accepted");
    require(updates.updateAvailable() && updates.latestVersion() == "1.1.0", "newer version detected");
    require(!updates.acceptRelease(release("v9.0.0", "https://evil.example/releases/tag/v9.0.0")), "foreign URL rejected");
    require(updates.latestVersion() == "1.1.0", "invalid release does not clear last valid release");
    require(!updates.acceptRelease(release("v9.0.0", "https://github.com/G-grbz/Lurviko/releases/tag/v9.0.0", true)), "prerelease ignored");
    require(!updates.acceptRelease("{bad json"), "invalid JSON ignored");
    require(!updates.acceptRelease(release("v1.2.0-beta", "https://github.com/G-grbz/Lurviko/releases/tag/v1.2.0-beta")), "nonstable tag ignored");
    require(updates.acceptRelease(release("Lurviko-v1.0.0", "https://github.com/G-grbz/Lurviko/releases/tag/Lurviko-v1.0.0")), "named tag accepted");
    require(!updates.updateAvailable(), "same version has no dot");
    require(updates.acceptRelease(release("v0.9.0", "https://github.com/G-grbz/Lurviko/releases/tag/v0.9.0")), "older valid release accepted");
    require(!updates.updateAvailable(), "older release has no dot");
    current.setValue("updates/lastCheck", QDateTime::currentSecsSinceEpoch());
    updates.checkForUpdates(); require(!updates.checking(), "recent checks do not make network requests");
    std::puts("PASS: legacy data/settings/cache/encrypted vault migration, collision retention, symlink guard, stable release detection and URL validation");
}
