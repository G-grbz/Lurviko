#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>

// Folders shared through Samba usershares, for the "shared" badge of the
// file views. Read off the GUI thread from the usershare directory (each
// share is one small file with a "path=" line) and kept current by watching
// that directory: no polling and no process per folder. When the directory
// cannot be read, "net usershare info" is used once per refresh instead.
class SambaShareIndex : public QObject
{
    Q_OBJECT
    // Changes whenever the set of shared folders changes (for bindings).
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(QStringList sharedPaths READ sharedPaths NOTIFY changed)
public:
    static SambaShareIndex *instance();
    explicit SambaShareIndex(QObject *parent = nullptr);

    int revision() const { return m_revision; }
    QStringList sharedPaths() const;
    Q_INVOKABLE bool isShared(const QString &pathOrUrl) const;
    Q_INVOKABLE void refresh();

    // Tests: read this directory, and nothing else.
    void setDirectoryForTest(const QString &directory);

signals:
    void changed();

private:
    QSet<QString> m_paths;
    QString m_directory;
    bool m_directoryResolved = false;
    bool m_forTest = false;
    bool m_loading = false;
    bool m_reloadPending = false;
    int m_revision = 0;
    quint64 m_generation = 0;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    void watchDirectory();
};
