#pragma once
#include <QObject>
#include <QHash>
#include <QSet>
#include <QFileSystemWatcher>
#include <QPointer>
#include <QTimer>
#include <memory>
class QProcess;

// Shared, read-only Git snapshots. DirectoryModel remains the file model;
// status updates only invalidate its Git role, never reset directory rows.
class GitStatusCache final : public QObject
{
    Q_OBJECT
public:
    struct Snapshot { QHash<QString, QString> paths; QString summary; bool valid = true; };
    static GitStatusCache *instance();
    explicit GitStatusCache(QObject *parent = nullptr);
    void request(QObject *owner, const QString &directory, bool force = false);
    QString status(const QString &root, const QString &path) const;
    QString directoryStatus(QObject *owner, const QString &directory);
    void resetChildren(QObject *owner);
    static Snapshot parsePorcelain(const QByteArray &data);
signals:
    void directoryReady(const QString &directory, const QString &root);
    void statusChanged(const QString &root);
    void directoryStatusChanged(const QString &directory);
private:
    struct Discovery { QString root, gitDirectory; qint64 time = 0; };
    static Discovery discover(const QString &directory, bool ancestors);
    struct Repository {
        QString root, gitDirectory;
        Snapshot snapshot;
        qint64 updated = 0, started = 0;
        QPointer<QProcess> process;
        QStringList watches;
        bool dirty = false, parsing = false;
    };
    void discovered(const QString &directory, Discovery discovery, bool force);
    bool interested(const QString &root) const;
    bool directlyInterested(const QString &root) const;
    void discoverChildren();
    void retryWaiting();
    void schedule(const QString &root, bool force);
    void pump();
    void watch(const std::shared_ptr<Repository> &repository);
    void unwatch(const std::shared_ptr<Repository> &repository);
    QString m_git;
    QHash<QObject *, QString> m_owners;
    QHash<QString, Discovery> m_directories;
    QHash<QString, bool> m_discovering;
    QHash<QObject *, QStringList> m_children;
    QHash<QString, Discovery> m_childDirectories, m_waiting;
    QHash<QString, QString> m_summaries;
    QSet<QString> m_childQueue;
    QTimer m_childTimer;
    bool m_childScan = false;
    QHash<QString, std::shared_ptr<Repository>> m_repositories;
    QMultiHash<QString, QString> m_watchRoots;
    QSet<QString> m_pending;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    int m_running = 0;
};
