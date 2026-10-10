#include "vaultsecurity.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <cmath>
#include <utility>
#ifdef Q_OS_LINUX
#include <linux/magic.h>
#include <sys/vfs.h>
#include <unistd.h>
#endif

bool VaultSecurity::parseArgon2Parameters(const QJsonObject &header, Argon2Parameters *result)
{
    if (!result)
        return false;
    auto read = [&header](const char *name, quint32 minimum, quint32 maximum, quint32 *out) {
        const QJsonValue value = header.value(QString::fromLatin1(name));
        if (!value.isDouble())
            return false;
        const double number = value.toDouble();
        if (!std::isfinite(number) || number != std::floor(number)
                || number < minimum || number > maximum)
            return false;
        *out = static_cast<quint32>(number);
        return true;
    };
    Argon2Parameters parameters;
    // Reject rather than clamp: changing KDF parameters silently derives a
    // different key. All vaults created by Lurviko use 3 passes / 64 MiB.
    if (!read("iterations", 1, 10, &parameters.iterations)
            || !read("memCostKiB", 8192, 262144, &parameters.memoryKiB)
            || !read("lanes", 1, 8, &parameters.lanes)
            || !read("threads", 1, parameters.lanes, &parameters.threads))
        return false;
    *result = parameters;
    return true;
}

bool VaultSecurity::isMemoryFilesystem(const QString &path)
{
#ifdef Q_OS_LINUX
    struct statfs filesystem;
    const QByteArray encoded = QFile::encodeName(path);
    return !path.isEmpty() && ::statfs(encoded.constData(), &filesystem) == 0
            && (filesystem.f_type == TMPFS_MAGIC || filesystem.f_type == RAMFS_MAGIC);
#else
    Q_UNUSED(path)
    return false;
#endif
}

QString VaultSecurity::createRuntimeDirectory(const QString &preferredParent)
{
    QStringList parents;
    const QFileInfo preferred(preferredParent);
#ifdef Q_OS_LINUX
    const auto sharedPermissions = QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup
            | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
    if (!preferredParent.isEmpty() && preferred.isDir() && !preferred.isSymLink()
            && preferred.ownerId() == ::getuid() && !(preferred.permissions() & sharedPermissions))
        parents.append(preferred.absoluteFilePath());
#endif
    parents.append(QStringLiteral("/dev/shm"));
    for (const QString &parent : std::as_const(parents)) {
        if (!isMemoryFilesystem(parent))
            continue;
        QTemporaryDir directory(QDir(parent).filePath(QStringLiteral("lurviko-private-XXXXXX")));
        if (!directory.isValid())
            continue;
        directory.setAutoRemove(false);
        return directory.path();
    }
    return {};
}

bool VaultSecurity::containsFile(const QString &directory, const QString &path)
{
    if (directory.isEmpty() || path.isEmpty())
        return false;
    const QString root = QFileInfo(directory).canonicalFilePath();
    const QFileInfo candidate(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
    if (root.isEmpty() || candidate.isSymLink())
        return false;
    const QString parent = QFileInfo(candidate.absolutePath()).canonicalFilePath();
    return parent == root || parent.startsWith(root + QDir::separator());
}
