#include "vaultsecurity.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <cmath>
#include <utility>
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef Q_OS_LINUX
#include <linux/magic.h>
#include <linux/openat2.h>
#include <sys/syscall.h>
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

namespace {
constexpr char SessionLock[] = ".lurviko-session.lock";

bool sameEntry(int parent, const QByteArray &name, int descriptor)
{
    struct stat path {}, pinned {};
    return ::fstatat(parent, name.constData(), &path, AT_SYMLINK_NOFOLLOW) == 0
        && ::fstat(descriptor, &pinned) == 0 && path.st_dev == pinned.st_dev && path.st_ino == pinned.st_ino;
}

int openDirectoryAt(int parent, const char *name)
{
#ifdef Q_OS_LINUX
    // Refuse symlinks and mount/bind-mount crossings during deletion. If the
    // kernel cannot enforce these constraints, leave the directory untouched.
    open_how how {};
    how.flags = O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC;
    how.resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_XDEV;
    return static_cast<int>(::syscall(SYS_openat2, parent, name, &how, sizeof(how)));
#else
    Q_UNUSED(parent) Q_UNUSED(name)
    errno = ENOTSUP;
    return -1;
#endif
}

bool removeContents(int directory, bool keepLock)
{
    // A separate open description avoids changing the caller's directory offset.
    const int scanFd = ::openat(directory, ".", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (scanFd < 0) return false;
    DIR *stream = ::fdopendir(scanFd);
    if (!stream) { ::close(scanFd); return false; }
    bool ok = true;
    errno = 0;
    while (dirent *entry = ::readdir(stream)) {
        const QByteArray name(entry->d_name);
        if (name == "." || name == ".." || (keepLock && name == SessionLock)) continue;
        struct stat st {};
        if (::fstatat(directory, name.constData(), &st, AT_SYMLINK_NOFOLLOW) != 0) { ok = false; break; }
        if (S_ISDIR(st.st_mode)) {
            const int child = openDirectoryAt(directory, name.constData());
            if (child < 0) { ok = false; break; }
            const bool removed = sameEntry(directory, name, child) && removeContents(child, false)
                && sameEntry(directory, name, child) && ::unlinkat(directory, name.constData(), AT_REMOVEDIR) == 0;
            ::close(child);
            if (!removed) { ok = false; break; }
        } else if (::unlinkat(directory, name.constData(), 0) != 0) {
            ok = false; break; // Unlink symlinks themselves, never their targets.
        }
        errno = 0;
    }
    if (errno) ok = false;
    ::closedir(stream);
    return ok;
}

bool removePinnedDirectory(int parent, const QByteArray &name, int directory, int lock)
{
    if (!sameEntry(parent, name, directory) || !removeContents(directory, true)) return false;
    if (lock >= 0 && !sameEntry(directory, QByteArray(SessionLock), lock)) return false;
    if (!sameEntry(parent, name, directory)) return false;
    if (::unlinkat(directory, SessionLock, 0) != 0 && errno != ENOENT) return false;
    return ::unlinkat(parent, name.constData(), AT_REMOVEDIR) == 0;
}

int openSessionLock(int directory, bool createOnly)
{
    int fd = ::openat(directory, SessionLock, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0600);
    if (fd < 0 && errno == EEXIST && !createOnly)
        fd = ::openat(directory, SessionLock, O_RDWR | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) return -1;
    struct stat st {};
    if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != ::getuid() || st.st_nlink != 1
            || (st.st_mode & (S_IRWXG | S_IRWXO)) || ::flock(fd, LOCK_EX | LOCK_NB) != 0
            || !sameEntry(directory, QByteArray(SessionLock), fd)) {
        ::close(fd); return -1;
    }
    return fd;
}
}

VaultSecurity::RuntimeDirectory::RuntimeDirectory(int parent, int directory, int lock, QString name, QString path)
    : m_parentFd(parent), m_directoryFd(directory), m_lockFd(lock), m_name(std::move(name)), m_path(std::move(path)) {}

VaultSecurity::RuntimeDirectory::~RuntimeDirectory()
{
    // Keep the advisory lock until plaintext and the owned directory are gone.
    removePinnedDirectory(m_parentFd, QFile::encodeName(m_name), m_directoryFd, m_lockFd);
    ::close(m_lockFd); ::close(m_directoryFd); ::close(m_parentFd);
}

bool VaultSecurity::RuntimeDirectory::clearContents() const
{
    return sameEntry(m_parentFd, QFile::encodeName(m_name), m_directoryFd) && removeContents(m_directoryFd, true);
}

std::unique_ptr<VaultSecurity::RuntimeDirectory> VaultSecurity::createRuntimeDirectory(const QString &preferredParent)
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
        if (!isMemoryFilesystem(parent)) continue;
        const int parentFd = ::open(QFile::encodeName(parent).constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (parentFd < 0) continue;
        QTemporaryDir temporary(QDir(parent).filePath(QStringLiteral("lurviko-private-XXXXXX")));
        if (!temporary.isValid()) { ::close(parentFd); continue; }
        temporary.setAutoRemove(false);
        const QString name = QFileInfo(temporary.path()).fileName();
        const QByteArray nativeName = QFile::encodeName(name);
        // This is a newly created, empty owner-only directory. Opening it does
        // not require openat2; strict mount-aware openat2 is used for sweeps.
        const int directoryFd = ::openat(parentFd, nativeName.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        struct stat created {}, parentStat {};
        const bool pinned = directoryFd >= 0 && ::fstat(directoryFd, &created) == 0
            && ::fstat(parentFd, &parentStat) == 0 && created.st_uid == ::getuid()
            && created.st_dev == parentStat.st_dev && sameEntry(parentFd, nativeName, directoryFd);
        const int lock = pinned ? openSessionLock(directoryFd, true) : -1;
        if (lock >= 0)
            return std::unique_ptr<RuntimeDirectory>(new RuntimeDirectory(parentFd, directoryFd, lock, name, temporary.path()));
        if (directoryFd >= 0) { if (pinned) removePinnedDirectory(parentFd, nativeName, directoryFd, -1); ::close(directoryFd); }
        ::close(parentFd);
    }
    return {};
}

QStringList VaultSecurity::runtimeCleanupParents(const QString &preferredParent)
{
    QStringList parents;
    if (!preferredParent.isEmpty()) parents << preferredParent;
    parents << QStringLiteral("/dev/shm") << QDir::tempPath();
    parents.removeDuplicates();
    return parents;
}

VaultSecurity::CleanupResult VaultSecurity::cleanupStaleRuntimeDirectories(
    const QStringList &parents, const QString &currentDirectory)
{
    CleanupResult result;
    for (const QString &parentPath : parents) {
        const int parent = ::open(QFile::encodeName(parentPath).constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (parent < 0) continue;
        DIR *stream = ::fdopendir(::fcntl(parent, F_DUPFD_CLOEXEC, 0));
        if (!stream) { ::close(parent); continue; }
        while (dirent *entry = ::readdir(stream)) {
            const QByteArray name(entry->d_name);
            const bool legacy = name == "lurviko-private";
            if (!legacy && (!name.startsWith("lurviko-private-") || parentPath == QDir::tempPath())) continue;
            const QString path = QDir(parentPath).filePath(QFile::decodeName(name));
            if (!currentDirectory.isEmpty() && QDir::cleanPath(path) == QDir::cleanPath(currentDirectory)) {
                ++result.skipped; continue;
            }
            const int directory = openDirectoryAt(parent, name.constData());
            if (directory < 0) { ++result.skipped; continue; }
            struct stat st {};
            if (::fstat(directory, &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != ::getuid()
                    || !sameEntry(parent, name, directory)) {
                ::close(directory); ++result.skipped; continue;
            }
            // Old releases had no lock. Create one atomically before deleting
            // their orphan, so simultaneous startup sweeps coordinate too.
            const int lock = openSessionLock(directory, false);
            if (lock < 0) { ::close(directory); ++result.skipped; continue; }
            if (removePinnedDirectory(parent, name, directory, lock)) ++result.removed;
            else ++result.failed;
            ::close(lock); ::close(directory);
        }
        ::closedir(stream); ::close(parent);
    }
    return result;
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

bool VaultSecurity::removeOwnedPrivateDirectory(int parent, const QByteArray &name, int directory)
{
    if (name.isEmpty() || name == "." || name == ".." || name.contains('/') || name.contains('\0'))
        return false;
    struct stat st {};
    return ::fstat(directory, &st) == 0 && S_ISDIR(st.st_mode) && st.st_uid == ::getuid()
        && !(st.st_mode & (S_IRWXG | S_IRWXO)) && sameEntry(parent, name, directory)
        && removeContents(directory, false) && sameEntry(parent, name, directory)
        && ::unlinkat(parent, name.constData(), AT_REMOVEDIR) == 0;
}
