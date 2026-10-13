#include "appmigration.h"
#include "iconreference.h"
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUrl>
#include <QSettings>
#include <functional>
#include <KWallet>

namespace {
const QStringList legacyNames = {QStringLiteral("g-File"), QStringLiteral("g-file"), QStringLiteral("GFile")};

bool moveTree(const QString &source, const QString &destination, const QString &backup,
              QString *error)
{
    const QFileInfo from(source), to(destination);
    if (!from.exists() && !from.isSymLink()) return true;
    if (from.isSymLink() || to.isSymLink()) {
        *error = QStringLiteral("Cannot migrate a symbolic link: %1").arg(source);
        return false;
    }
    if (!to.exists()) {
        if (QDir().mkpath(to.absolutePath()) && QDir().rename(source, destination)) return true;
    } else if (from.isDir() && to.isDir()) {
        const auto entries = QDir(source).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
        for (const auto &entry : entries) {
            if (!moveTree(entry.absoluteFilePath(), QDir(destination).filePath(entry.fileName()),
                          QDir(backup).filePath(entry.fileName()), error)) return false;
        }
        if (QDir().rmdir(source)) return true;
    } else {
        // A pre-existing Lurviko file wins. Preserve the old file for recovery.
        QString unique = backup;
        for (int suffix = 1; QFileInfo::exists(unique); ++suffix)
            unique = backup + QStringLiteral(".%1").arg(suffix);
        if (QDir().mkpath(QFileInfo(unique).absolutePath()) && QDir().rename(source, unique)) return true;
    }
    *error = QStringLiteral("Could not move %1 to %2. The source has been retained.").arg(source, destination);
    return false;
}

bool migrateRoot(const QString &root, QString *error)
{
    const QString canonical = QDir(root).filePath(QStringLiteral("Lurviko"));
    for (const auto &name : legacyNames) {
        const QString legacy = QDir(root).filePath(name);
        if (!QFileInfo::exists(legacy) && !QFileInfo(legacy).isSymLink()) continue;
        if (QFileInfo(legacy).isSymLink() || QFileInfo(canonical).isSymLink()) {
            *error = QStringLiteral("Application data roots must be real directories: %1").arg(legacy);
            return false;
        }
        // Normalize only the two application levels used by Qt; user file names
        // inside the vault, media libraries and caches remain untouched.
        for (const auto &old : legacyNames) {
            const QString nested = QDir(legacy).filePath(old);
            if (!moveTree(nested, QDir(legacy).filePath(QStringLiteral("Lurviko")),
                          QDir(canonical).filePath(QStringLiteral("migration-backup/nested-") + name + QLatin1Char('/') + old), error)) return false;
            for (const auto &base : {legacy, QDir(legacy).filePath(QStringLiteral("Lurviko"))}) {
                if (!moveTree(QDir(base).filePath(old + QStringLiteral(".conf")),
                              QDir(base).filePath(QStringLiteral("Lurviko.conf")),
                              QDir(canonical).filePath(QStringLiteral("migration-backup/settings-") + name + QLatin1Char('/') + old + QStringLiteral(".conf")), error)) return false;
            }
        }
        if (!moveTree(legacy, canonical, QDir(canonical).filePath(QStringLiteral("migration-backup/") + name), error)) return false;
    }
    return true;
}
}

bool migrateLegacyUserData(const QString &configRoot, const QString &dataRoot,
                           const QString &cacheRoot, QString *error)
{
    return migrateRoot(configRoot, error) && migrateRoot(dataRoot, error) && migrateRoot(cacheRoot, error);
}

bool selectLurvikoWalletFolder(KWallet::Wallet *wallet)
{
    if (!wallet || !wallet->isOpen()) return false;
    const QString target = QStringLiteral("Lurviko");
    if (!wallet->hasFolder(target) && !wallet->createFolder(target)) return false;
    for (const auto &legacy : legacyNames) {
        if (!wallet->hasFolder(legacy)) continue;
        if (!wallet->setFolder(legacy)) return false;
        const auto entries = wallet->entryList();
        for (const auto &key : entries) {
            QByteArray value;
            if (!wallet->setFolder(legacy)) return false;
            const auto type = wallet->entryType(key);
            if (wallet->readEntry(key, value) != 0 || !wallet->setFolder(target)) return false;
            if (wallet->hasEntry(key)) {
                QByteArray current;
                if (wallet->readEntry(key, current) != 0 || current != value) continue;
            } else if (wallet->writeEntry(key, value, type) != 0) return false;
            QByteArray verified;
            if (wallet->readEntry(key, verified) != 0 || verified != value) return false;
            if (!wallet->setFolder(legacy) || wallet->removeEntry(key) != 0) return false;
        }
        if (!wallet->setFolder(legacy)) return false;
        if (wallet->entryList().isEmpty()) wallet->removeFolder(legacy);
    }
    return wallet->setFolder(target);
}

QString relocatedAppPath(const QString &pathOrUrl)
{
    const QUrl url(pathOrUrl);
    const bool localUrl = url.isLocalFile();
    const QString path = localUrl ? url.toLocalFile() : pathOrUrl;
    for (const auto location : {QStandardPaths::GenericConfigLocation, QStandardPaths::GenericDataLocation, QStandardPaths::GenericCacheLocation}) {
        const QString root = QStandardPaths::writableLocation(location);
        for (const auto &name : legacyNames) {
            const QString old = QDir(root).filePath(name) + QLatin1Char('/');
            if (!path.startsWith(old)) continue;
            QString rest = path.mid(old.size());
            for (const auto &nested : legacyNames) {
                if (rest.startsWith(nested + QLatin1Char('/'))) {
                    rest = QStringLiteral("Lurviko/") + rest.mid(nested.size() + 1);
                    break;
                }
            }
            const QString relocated = QDir(root).filePath(QStringLiteral("Lurviko/") + rest);
            return localUrl ? QUrl::fromLocalFile(relocated).toString() : relocated;
        }
    }
    return pathOrUrl;
}

void migrateGroupedIconReferences(QSettings &settings, const QString &iconRoot)
{
    const QString base = QDir(iconRoot).absolutePath();
    const auto relocate = [&base](const QString &reference) {
        const QString local = IconReference::localFile(reference);
        if (local.isEmpty()) return reference;
        const QFileInfo old(QUrl(local).toLocalFile());
        // Only retired flat files from this source tree qualify. Existing
        // custom artwork, sibling folders and unrelated local files keep their paths.
        if (old.absolutePath() != base || old.exists()) return reference;
        for (const auto &group : {"png", "symbolic", "color"}) {
            const QString next = QDir(base).filePath(QString::fromLatin1(group) + '/' + old.fileName());
            if (QFileInfo(next).isFile())
                return reference.startsWith('/') ? next : QUrl::fromLocalFile(next).toString(QUrl::FullyEncoded);
        }
        return reference;
    };
    std::function<QVariant(const QVariant &, bool)> update;
    update = [&](const QVariant &value, bool iconField) -> QVariant {
        if (value.metaType().id() == QMetaType::QString)
            return iconField ? QVariant(relocate(value.toString())) : value;
        if (value.metaType().id() == QMetaType::QVariantList) {
            auto list = value.toList();
            for (auto &entry : list) entry = update(entry, iconField);
            return list;
        }
        if (value.metaType().id() == QMetaType::QVariantMap) {
            auto map = value.toMap();
            for (auto it = map.begin(); it != map.end(); ++it)
                it.value() = update(it.value(), iconField || it.key().contains("icon", Qt::CaseInsensitive));
            return map;
        }
        if (iconField && value.metaType().id() == QMetaType::QStringList) {
            auto list = value.toStringList();
            for (auto &entry : list) entry = relocate(entry);
            return list;
        }
        return value;
    };
    bool changed = false;
    for (const auto &key : settings.allKeys()) {
        const bool iconField = key.contains("icon", Qt::CaseInsensitive);
        if (!iconField && key != "quickAccess/items" && key != "favorites/items") continue;
        const QVariant before = settings.value(key), after = update(before, iconField);
        if (before != after) { settings.setValue(key, after); changed = true; }
    }
    if (changed) settings.sync();
}
