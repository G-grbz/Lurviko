#include "fileaccesssecurity.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <cerrno>
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
