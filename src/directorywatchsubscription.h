#pragma once
#include "directorysizeindex.h"
#include <QObject>
#include <QFileSystemWatcher>
#include <QFileInfo>
#include <QStringList>

// Compatibility facade for DirectoryModel's existing refresh/metadata slots.
// No per-file native watches: the central directory watch supplies child events.
class DirectoryWatchSubscription : public QObject {
    Q_OBJECT
public:
    explicit DirectoryWatchSubscription(QObject* parent = nullptr) : QObject(parent) {
        auto* service = DirectorySizeIndex::instance();
        connect(service, &DirectorySizeIndex::directoryChanged, this, [this](const QString& path) {
            if (m_enabled && m_paths.contains(path)) emit directoryChanged(path);
        });
        connect(service, &DirectorySizeIndex::fileChanged, this, [this](const QString& directory, const QString& path) {
            if (m_enabled && m_paths.contains(directory)) emit fileChanged(path);
        });
        // Independent visible-directory notification path: size-index jobs can
        // be busy, cancelled or rearming their own inotify subscriptions while
        // an external process atomically publishes a new file. A single native
        // QFileSystemWatcher per visible directory keeps the listing live.
        // Both signals feed the model's existing coalesced refresh timer.
        connect(&m_native, &QFileSystemWatcher::directoryChanged, this, [this](const QString& path) {
            if (!m_enabled || !m_paths.contains(path)) return;
            emit directoryChanged(path);
            if (!m_native.directories().contains(path) && QFileInfo(path).isDir())
                m_native.addPath(path); // QFileSystemWatcher can drop moved watches.
        });
    }
    ~DirectoryWatchSubscription() override { if (auto* s = DirectorySizeIndex::existing()) s->setWatchedDirectories(this, {}); }
    QStringList directories() const { return m_paths; }
    QStringList files() const { return {}; }
    void addPath(const QString& path) { if (!m_paths.contains(path)) m_paths.append(path); update(); }
    void addPaths(const QStringList&) {} // old per-file registration is deliberately unnecessary
    void addFile(const QString&) {}
    void removePath(const QString& path) { m_paths.removeAll(path); update(); }
    void removePaths(const QStringList& paths) { for (const auto& p : paths) m_paths.removeAll(p); update(); }
    void setEnabled(bool enabled) { if (m_enabled != enabled) { m_enabled = enabled; update(); } }
    // A native watch can be dropped by a directory move/unmount or backend
    // resource pressure. Re-arm missing paths without touching active watches.
    void ensureNativeWatches() {
        if (!m_enabled) return;
        for (const auto &path : m_paths)
            if (!m_native.directories().contains(path) && QFileInfo(path).isDir())
                m_native.addPath(path);
    }
signals:
    void directoryChanged(const QString& path);
    void fileChanged(const QString& path);
private:
    void update() {
        DirectorySizeIndex::instance()->setWatchedDirectories(this, m_enabled ? m_paths : QStringList());
        const QStringList old = m_native.directories();
        for (const auto& path : old)
            if (!m_enabled || !m_paths.contains(path)) m_native.removePath(path);
        if (m_enabled) {
            for (const auto& path : m_paths)
                if (!m_native.directories().contains(path) && QFileInfo(path).isDir())
                    m_native.addPath(path);
        }
    }
    QFileSystemWatcher m_native;
    QStringList m_paths;
    bool m_enabled = true;
};
