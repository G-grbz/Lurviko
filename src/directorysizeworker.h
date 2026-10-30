#pragma once

#include <QString>
#include <QVariantMap>
#include <atomic>
#include <memory>

struct DirectorySizeResult {
    qint64 bytes = 0;
    int files = 0;
    int folders = 0;
    QVariantMap children;
};

struct DirectorySizeProgress {
    std::atomic<qint64> bytes{0};
    std::atomic<int> files{0};
    std::atomic<int> folders{0};
};

DirectorySizeResult scanDirectorySize(const QString &root,
    const std::shared_ptr<std::atomic_bool> &cancelled,
    const std::shared_ptr<std::atomic_int> &foregroundDemand,
    const std::shared_ptr<DirectorySizeProgress> &progress, bool background);
