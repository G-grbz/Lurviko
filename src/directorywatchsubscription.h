#pragma once
#include "directorysizeindex.h"
#include <QObject>
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
signals:
    void directoryChanged(const QString& path);
    void fileChanged(const QString& path);
private:
    void update() { DirectorySizeIndex::instance()->setWatchedDirectories(this, m_enabled ? m_paths : QStringList()); }
    QStringList m_paths;
    bool m_enabled = true;
};
