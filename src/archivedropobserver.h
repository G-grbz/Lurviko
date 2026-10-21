#pragma once

#include <QObject>
#include <QFileSystemWatcher>
#include <QHash>
#include <QSet>
#include <QTimer>

// Ark's DndExtract method acknowledges a request, not its extraction result.
// Observe actual destination changes without inventing a completed KJob.
// Watches are temporary and shallow: no recursive home-directory traversal.
class ArchiveDropObserver : public QObject
{
    Q_OBJECT
public:
    explicit ArchiveDropObserver(const QString &destination, QObject *parent = nullptr);
    void start();

signals:
    void ready();
    void unavailable();
    void itemsChanged(const QStringList &urls, bool firstChange);

private:
    struct Snapshot {
        bool valid = false;
        QHash<QString, QByteArray> stamps;
        QStringList watchPaths;
    };
    static Snapshot snapshot(const QString &path);
    void scan();
    void installWatchBatch();
    void changed(const QString &path);
    void publish();

    QString m_destination;
    QString m_watchRoot;
    QFileSystemWatcher m_watcher;
    QTimer m_scanTimer;
    QTimer m_installTimer;
    QTimer m_publishTimer;
    QTimer m_idleTimer;
    QTimer m_lifetimeTimer;
    QHash<QString, QByteArray> m_stamps;
    QSet<QString> m_changedNames;
    QStringList m_pendingWatches;
    bool m_initial = true;
    bool m_scanning = false;
    bool m_rescan = false;
    bool m_reported = false;
};
