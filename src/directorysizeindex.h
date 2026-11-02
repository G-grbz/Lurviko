#pragma once

#include "directorysizeworker.h"
#include <QObject>
#include <QDateTime>
#include <QVariantMap>
#include <QStringList>
#include <memory>

struct DirectorySizeSnapshot {
    QString path;
    QString canonicalPath;
    DirectorySizeResult totals;
    bool available = false;
    bool complete = false;
    bool calculating = false;
    QString state = QStringLiteral("missing");
    QString error;
    QDateTime checkedAt;
};

// One application-wide owner. Writes/scans are serial; cached reads have a
// separate bounded worker so navigation never waits behind a tree scan.
// Producers report filesystem hints, never arithmetic deltas or index membership.
class DirectorySizeIndex : public QObject {
    Q_OBJECT
public:
    explicit DirectorySizeIndex(
        const QString& databasePath = { }, QObject* parent = nullptr, int directoryWatchLimit = 256);
    ~DirectorySizeIndex() override;
    static DirectorySizeIndex* instance();
    static DirectorySizeIndex* existing();

    DirectorySizeSnapshot snapshot(const QString& path) const;
    bool request(const QString& path, bool forceVerification = false);
    // One initial background fill; persisted totals avoid another full scan at startup.
    void warmup(const QString& path = QStringLiteral("/"));
    void retain(QObject* consumer, const QString& path);
    void release(QObject* consumer);
    void setWatchedDirectories(QObject* owner, const QStringList& paths);
    void setIoBusy(QObject* producer, bool busy);
    void observePaths(const QStringList& paths);
    void observeOperation(const QStringList& paths, const QStringList& directories = {});
    void observeDirectory(const QString& path, bool forceReconciliation = false);
    void observeMove(const QString& source, const QString& destination);
    void verifyAll();
    void invalidateAllCached();
    int activeWatchCount() const;
    quint64 inspectSelection(const QStringList& paths);
    void cancelSelection(quint64 request);
    bool busy() const;
    std::shared_ptr<std::atomic_int> foregroundDemand() const;

signals:
    void directoryChanged(const QString& path);
    void fileChanged(const QString& directory, const QString& path);
    void snapshotChanged(const QString& path);
    // Diagnostics also make it possible to test that updates do not rescan trees.
    void workCompleted(qint64 fileStats, qint64 directoryListings);
    void selectionReady(quint64 request, const DirectorySizeSnapshot& snapshot);

private:
    struct Private;
    std::unique_ptr<Private> d;
    void startPending();
    void startPeek();
    void updateWatches();
    void readEvents();
};
