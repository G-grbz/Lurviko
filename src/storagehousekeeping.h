#pragma once

#include <QFutureWatcher>
#include <QObject>

// Sizes the Discover information panel shows: the preview cache and the
// user's trash. Both are measured on a worker thread so opening the panel
// never blocks the UI; clearing the preview cache also drops in-memory previews.
class StorageHousekeeping : public QObject
{
    Q_OBJECT
    Q_PROPERTY(qint64 previewCacheBytes READ previewCacheBytes NOTIFY changed)
    Q_PROPERTY(int previewCacheFiles READ previewCacheFiles NOTIFY changed)
    Q_PROPERTY(qint64 trashBytes READ trashBytes NOTIFY changed)
    Q_PROPERTY(int trashItems READ trashItems NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool measured READ measured NOTIFY changed)

public:
    struct Usage {
        qint64 previewBytes = 0;
        int previewFiles = 0;
        qint64 trashBytes = 0;
        int trashItems = 0;
    };

    explicit StorageHousekeeping(QObject *parent = nullptr);
    ~StorageHousekeeping() override;

    qint64 previewCacheBytes() const { return m_usage.previewBytes; }
    int previewCacheFiles() const { return m_usage.previewFiles; }
    qint64 trashBytes() const { return m_usage.trashBytes; }
    int trashItems() const { return m_usage.trashItems; }
    bool busy() const { return m_busy; }
    bool measured() const { return m_measured; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void clearPreviewCache();

    static QString trashFilesDirectory();

signals:
    void changed();
    void busyChanged();

private:
    void start(bool clearPreviews);

    QFutureWatcher<Usage> m_watcher;
    Usage m_usage;
    bool m_busy = false;
    bool m_measured = false;
    bool m_pending = false;
    bool m_pendingClear = false;
};
