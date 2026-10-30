#include "directorysizeworker.h"

#include <QDir>
#include <QFile>
#include <QThread>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <cstring>
#include <set>
#include <utility>
#include <vector>

namespace {
qint64 threadCpuTime()
{
    timespec time{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &time);
    return qint64(time.tv_sec) * 1000000000 + time.tv_nsec;
}
struct Child {
    QString path;
    qint64 bytes = 0;
    int files = 0, folders = 0;
    bool directory = false;
};
using Identity = std::pair<dev_t, ino_t>;
struct Frame {
    DIR *directory;
    int child;
    Identity identity;
};
}

DirectorySizeResult scanDirectorySize(const QString &root,
    const std::shared_ptr<std::atomic_bool> &cancelled,
    const std::shared_ptr<std::atomic_int> &foregroundDemand,
    const std::shared_ptr<DirectorySizeProgress> &progress, bool background)
{
    DirectorySizeResult result;
    std::vector<Child> children;
    std::vector<Frame> stack;
    std::set<Identity> ancestors;
    const auto push = [&](int descriptor, int child) {
        if (descriptor < 0) return;
        struct stat info{};
        if (::fstat(descriptor, &info) != 0 || !S_ISDIR(info.st_mode)
                || ancestors.count({info.st_dev, info.st_ino})) {
            ::close(descriptor);
            return;
        }
        DIR *directory = ::fdopendir(descriptor);
        if (!directory) { ::close(descriptor); return; }
        const Identity identity{info.st_dev, info.st_ino};
        ancestors.insert(identity);
        stack.push_back({directory, child, identity});
    };
    // The explicitly opened root may be an alias. Descendants are opened
    // relative to directory descriptors with O_NOFOLLOW, including replacements.
    push(::open(QFile::encodeName(root).constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC), -1);
    qint64 budgetStart = threadCpuTime();
    int batch = 0;
    while (!stack.empty() && !cancelled->load(std::memory_order_relaxed)) {
        const auto frame = stack.back();
        dirent *entry = ::readdir(frame.directory);
        if (!entry) {
            ::closedir(frame.directory);
            ancestors.erase(frame.identity);
            stack.pop_back();
            continue;
        }
        if (!std::strcmp(entry->d_name, ".") || !std::strcmp(entry->d_name, "..")) continue;
        int childIndex = frame.child;
        if (childIndex == -1) {
            if (!background || children.size() < 4096) {
                childIndex = int(children.size());
                children.push_back({QDir(root).filePath(QFile::decodeName(entry->d_name))});
            } else {
                childIndex = -2; // Total remains exact; omit excess cached subtotals.
            }
        }
        Child *child = childIndex >= 0 ? &children[childIndex] : nullptr;
        const int descriptor = ::dirfd(frame.directory);
        struct stat info{};
        // d_type saves stat calls for links, directories and special files.
        // A regular file still needs one non-following stat for its logical size.
        const bool haveInfo = (entry->d_type == DT_REG || entry->d_type == DT_UNKNOWN)
            && ::fstatat(descriptor, entry->d_name, &info, AT_SYMLINK_NOFOLLOW) == 0;
        const bool directory = entry->d_type == DT_DIR || (haveInfo && S_ISDIR(info.st_mode));
        if (directory) {
            if (child && frame.child == -1) child->directory = true;
            ++result.folders;
            if (child) ++child->folders;
            push(::openat(descriptor, entry->d_name,
                O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW), childIndex);
        } else if (haveInfo && S_ISREG(info.st_mode)) {
            ++result.files; result.bytes += info.st_size;
            if (child) { ++child->files; child->bytes += info.st_size; }
        }
        // Budget CPU time, not time spent waiting on storage. No QString,
        // QFileInfo, path normalization or map lookup is needed per descendant.
        if (background && ++batch >= 64) {
            batch = 0;
            const auto now = threadCpuTime();
            if (now - budgetStart >= 2000000) {
                progress->bytes.store(result.bytes, std::memory_order_relaxed);
                progress->files.store(result.files, std::memory_order_relaxed);
                progress->folders.store(result.folders, std::memory_order_relaxed);
                if (foregroundDemand->load(std::memory_order_relaxed) == 0)
                    QThread::msleep(18);
                budgetStart = threadCpuTime();
            }
        }
    }
    for (const auto &frame : stack) ::closedir(frame.directory);
    if (cancelled->load(std::memory_order_relaxed)) return {};
    for (const auto &child : children)
        result.children.insert(child.path, QVariantMap{{"bytes", child.bytes}, {"files", child.files},
            {"folders", child.folders}, {"directory", child.directory}});
    return result;
}
