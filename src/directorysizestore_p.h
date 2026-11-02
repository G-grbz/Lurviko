#pragma once

#include "directorysizeindex.h"
#include <QHash>
#include <QMutex>
#include <QSet>
#include <atomic>
#include <unistd.h>

// Private to the serial worker and the inotify reader. No database objects cross
// a thread boundary and no per-file data is retained in the GUI process cache.
struct DirectorySizeWatchState {
    QMutex mutex;
    QHash<int, QSet<QString>> paths;
    QSet<QString> names;
    QSet<QString> allowed;
    QHash<QString, int> descriptors;
    QHash<QString, QPair<quint64, quint64>> identities;
    int descriptor = -1;
    int limit = 256;
    ~DirectorySizeWatchState()
    {
        if (descriptor >= 0)
            ::close(descriptor);
    }
};
struct DirectorySizeWork {
    QString databasePath;
    QString session;
    QStringList requested, verify, peek, paths, directories;
    // Additional known mount boundaries (also useful for isolated topology tests).
    QStringList mountBoundaries;
    QStringList virtualRoots;
    QStringList invalidatedPaths;
    QStringList cachedRoots;
    QHash<QString, quint64> requestIds;
    bool invalidateAll = false;
    bool backgroundWarmup = false;
    QList<QPair<QString, QString>> moves;
    QHash<quint64, QStringList> selections;
    std::shared_ptr<std::atomic_bool> cancelled;
    std::shared_ptr<std::atomic_int> demand;
    std::shared_ptr<std::atomic_bool> ioBusy;
    std::shared_ptr<DirectorySizeProgress> progress;
    std::shared_ptr<DirectorySizeWatchState> watches;
};
struct DirectorySizeWorkResult {
    QHash<QString, DirectorySizeSnapshot> snapshots;
    qint64 fileStats = 0, directoryListings = 0;
    qint64 directoryStats = 0;
    QString error;
    QStringList recheck;
    QStringList verifyAgain;
    QStringList changedDirectories;
    QHash<quint64, DirectorySizeSnapshot> selections;
};
DirectorySizeWorkResult runDirectorySizeWork(const DirectorySizeWork& work);
DirectorySizeWorkResult readDirectorySizeSnapshots(const DirectorySizeWork& work);
QSet<QString> directorySizeMountBoundaries();
