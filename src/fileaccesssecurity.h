#pragma once
#include <QString>
#include <atomic>
#include <memory>
#include <sys/types.h>

struct FileAccessTask {
    std::atomic<bool> cancelled{false};
    std::atomic<int> changed{0};
    std::atomic<int> failed{0};
    std::atomic<int> skipped{0};
    QString firstError; // Read by the UI after the future completes.
};

namespace FileAccessSecurity {
void applyLocalAccess(const QString &path, int mode, uid_t owner, gid_t group, bool recursive,
                      const std::shared_ptr<FileAccessTask> &task);
struct TreeCheck {
    bool safe = false;
    QString path;
    int error = 0;
};
// Conservative preflight for the remaining path-based KIO administrator jobs.
// Root/target-owned directories only; no group/other writers or symlink traversal.
TreeCheck checkAdministratorTree(const QString &path, uid_t targetOwner,
                                 const std::atomic_bool *cancelled = nullptr);
}
