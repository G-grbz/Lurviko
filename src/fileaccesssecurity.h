#pragma once
#include <QString>
#include <atomic>
#include <sys/types.h>

namespace FileAccessSecurity {
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
