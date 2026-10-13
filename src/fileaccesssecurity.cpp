#include "fileaccesssecurity.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

FileAccessSecurity::TreeCheck FileAccessSecurity::checkAdministratorTree(
    const QString &path, uid_t targetOwner, const std::atomic_bool *cancelled)
{
    const auto stopped = [&] { return cancelled && cancelled->load(); };
    const int fd = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return {false, path, errno};
    DIR *stream = ::fdopendir(fd);
    if (!stream) { const int error = errno; ::close(fd); return {false, path, error}; }
    struct Frame { DIR *stream; QString path; };
    std::vector<Frame> stack{{stream, path}};
    TreeCheck result{true, {}, 0};
    while (!stack.empty()) {
        const Frame &frame = stack.back();
        if (stopped()) { result = {false, frame.path, ECANCELED}; break; }
        struct stat directory {};
        if (::fstat(::dirfd(frame.stream), &directory) != 0) {
            result = {false, frame.path, errno}; break;
        }
        // Reject group-write too: ordinary mode bits cannot prove every member
        // of the owning group is the trusted target user. Sticky bits do not
        // make path-based privileged chmod/chown safe.
        if ((directory.st_uid != 0 && directory.st_uid != targetOwner)
                || (directory.st_mode & (S_IWGRP | S_IWOTH))) {
            result = {false, frame.path, EPERM}; break;
        }
        errno = 0;
        const dirent *entry = ::readdir(frame.stream);
        if (!entry) {
            if (errno) { result = {false, frame.path, errno}; break; }
            ::closedir(frame.stream); stack.pop_back(); continue;
        }
        const QByteArray name(entry->d_name);
        if (name == "." || name == "..") continue;
        const QString child = QDir(frame.path).filePath(QFile::decodeName(name));
        struct stat st {};
        if (::fstatat(::dirfd(frame.stream), name.constData(), &st, AT_SYMLINK_NOFOLLOW) != 0) {
            result = {false, child, errno}; break;
        }
        if (!S_ISDIR(st.st_mode)) continue; // Includes symlinks; never follow them.
        const int childFd = ::openat(::dirfd(frame.stream), name.constData(),
                                    O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (childFd < 0) { result = {false, child, errno}; break; }
        DIR *childStream = ::fdopendir(childFd);
        if (!childStream) { const int error = errno; ::close(childFd); result = {false, child, error}; break; }
        stack.push_back({childStream, child});
    }
    for (const Frame &frame : stack) ::closedir(frame.stream);
    return result;
}

namespace {
void recordFailure(const std::shared_ptr<FileAccessTask> &task, const QString &path, int error)
{
    ++task->failed;
    if (task->firstError.isEmpty())
        task->firstError = path + QStringLiteral(": ") + QString::fromLocal8Bit(std::strerror(error));
}

}

void FileAccessSecurity::applyDescriptorAccess(int descriptor, const QString &path, int mode, uid_t owner, gid_t group, bool recursive,
                      const std::shared_ptr<FileAccessTask> &task, int modeMask)
{
    struct stat selected {};
    if (::fstat(descriptor, &selected) != 0) { recordFailure(task, path, errno); return; }
    const auto apply = [&](int parent, const QByteArray &name, const QString &display, bool descendant, bool direct = false) {
        if (task->cancelled) return;
        // Pin the actual inode, including files that cannot be opened for
        // reading. Never resolve its name again between stat/chown/chmod.
        const int fd = direct ? ::fcntl(parent, F_DUPFD_CLOEXEC, 0)
                              : ::openat(parent, name.constData(), O_PATH | O_NOFOLLOW | O_CLOEXEC);
        if (fd < 0) { recordFailure(task, display, errno); return; }
        struct Descriptor { int fd; ~Descriptor() { ::close(fd); } } pinned{fd};
        struct stat st {};
        if (::fstat(fd, &st) != 0) {
            recordFailure(task, display, errno); return;
        }
        if (S_ISLNK(st.st_mode) || (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode))) return;
        if (descendant && st.st_dev != selected.st_dev) { ++task->skipped; return; }
        if ((owner != static_cast<uid_t>(-1) && owner != st.st_uid)
            || (group != static_cast<gid_t>(-1) && group != st.st_gid)) {
            if (::fchownat(fd, "", owner, group, AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW) != 0) {
                recordFailure(task, display, errno); return;
            }
            // chown may clear setuid/setgid. Do not restore cleared privilege bits.
            if (::fstat(fd, &st) != 0) {
                recordFailure(task, display, errno); return;
            }
            if (S_ISLNK(st.st_mode)) { recordFailure(task, display, ELOOP); return; }
        }
        // Merge only explicitly edited bits, using the current item rather than
        // the first item in a mixed selection. Ownership-only edits skip chmod.
        if (!modeMask) { ++task->changed; return; }
        int requested = (st.st_mode & 0777 & ~modeMask) | (mode & modeMask);
        // Match Dolphin's recursive +X behavior: ordinary documents stay
        // non-executable; directories and existing programs receive execute bits.
        if (descendant && S_ISREG(st.st_mode) && !(st.st_mode & 0111) && (requested & 0111))
            requested &= ~0111;
        // This editor changes only ordinary rwx bits. Preserve existing special
        // bits deliberately; never restore privilege bits already cleared by chown.
        requested |= st.st_mode & (S_ISUID | S_ISGID | S_ISVTX);
        // fchmodat2/AT_EMPTY_PATH is required on Linux. Unsupported kernels
        // fail safely; do not fall back to privileged path-based chmod.
        if (::fchmodat(fd, "", requested, AT_EMPTY_PATH) != 0)
            recordFailure(task, display, errno);
        else
            ++task->changed;
    };
    if (!recursive) { apply(descriptor, {}, path, false, true); return; }

    const int root = ::openat(descriptor, ".", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (root < 0) { recordFailure(task, path, errno); return; }
    struct stat rootStat {};
    if (::fstat(root, &rootStat) != 0) {
        const int error = errno; ::close(root); recordFailure(task, path, error); return;
    }
    const dev_t rootDevice = rootStat.st_dev;
    DIR *rootStream = ::fdopendir(root);
    if (!rootStream) { const int error = errno; ::close(root); recordFailure(task, path, error); return; }
    struct Frame { DIR *stream; QString path; };
    std::vector<Frame> stack{{rootStream, path}};
    while (!stack.empty() && !task->cancelled) {
        const Frame frame = stack.back();
        errno = 0;
        dirent *entry = ::readdir(frame.stream);
        if (!entry) {
            if (errno) recordFailure(task, frame.path, errno);
            // Open directory descriptors keep traversal inside the selected
            // tree. Change directories after their children, even for 0000.
            apply(::dirfd(frame.stream), QByteArray("."), frame.path, stack.size() > 1);
            ::closedir(frame.stream);
            stack.pop_back();
            continue;
        }
        const QByteArray name(entry->d_name);
        if (name == "." || name == "..") continue;
        const QString display = QDir(frame.path).filePath(QFile::decodeName(name));
        struct stat st {};
        const int parent = ::dirfd(frame.stream);
        if (::fstatat(parent, name.constData(), &st, AT_SYMLINK_NOFOLLOW) != 0) {
            recordFailure(task, display, errno); continue;
        }
        if (S_ISLNK(st.st_mode)) continue;
        if (st.st_dev != rootDevice) { ++task->skipped; continue; }
        if (S_ISDIR(st.st_mode)) {
            const int fd = ::openat(parent, name.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (fd < 0) { recordFailure(task, display, errno); continue; }
            struct stat pinned {};
            if (::fstat(fd, &pinned) != 0) {
                const int error = errno; ::close(fd); recordFailure(task, display, error); continue;
            }
            // Recheck the opened descriptor too: a mount may replace a path
            // between fstatat and openat. Never modify the crossed mount's root.
            if (pinned.st_dev != rootDevice) { ++task->skipped; ::close(fd); continue; }
            DIR *stream = ::fdopendir(fd);
            if (!stream) { const int error = errno; ::close(fd); recordFailure(task, display, error); continue; }
            stack.push_back({stream, display});
        } else {
            apply(parent, name, display, true);
        }
    }
    for (const auto &frame : stack) ::closedir(frame.stream);
}

void FileAccessSecurity::applyLocalAccess(const QString &path, int mode, uid_t owner, gid_t group, bool recursive,
                                        const std::shared_ptr<FileAccessTask> &task, int modeMask)
{
    const int fd = ::open(QFile::encodeName(path).constData(), O_PATH | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) { recordFailure(task, path, errno); return; }
    applyDescriptorAccess(fd, path, mode, owner, group, recursive, task, modeMask);
    ::close(fd);
}
