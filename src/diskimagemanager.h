#pragma once

#include <QAbstractListModel>
#include <QFutureWatcher>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include <functional>
#include <memory>

// Mounts disk image files (ISO, raw .img, SquashFS, qcow2, VMDK, ...) and
// lists the images that are currently mounted. The list is discovered from the
// running system (lsblk + sysfs), so images mounted earlier, by another
// session or by another tool also show up, and nothing needs to be persisted.
//
// Plain/loop-mountable images use UDisks (udisksctl loop-setup + mount), which
// needs no root password for an active desktop session. Virtual-machine disk
// formats (qcow2, vmdk, vdi, vhd, vhdx, ...) need qemu-nbd, which only root can
// run, so those go through pkexec and are offered only when qemu-nbd exists.
class DiskImageManager : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    // Canonical paths of the image files that are mounted right now.
    Q_PROPERTY(QStringList mountedPaths READ mountedPaths NOTIFY imagesChanged)
    // Canonical paths of the image files with a mount/unmount in progress.
    Q_PROPERTY(QStringList busyPaths READ busyPaths NOTIFY busyChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        ImageNameRole,
        ImagePathRole,
        RootPathRole,
        FileSystemRole,
        TotalBytesRole,
        FreeBytesRole,
        UsedBytesRole,
        UsedRatioRole,
        ReadOnlyRole,
        DeviceRole,
        BusyRole,
        StableIdRole
    };

    explicit DiskImageManager(QObject *parent = nullptr);
    ~DiskImageManager() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QStringList mountedPaths() const;
    QStringList busyPaths() const;

    // True when the file name has a disk-image extension this class knows.
    Q_INVOKABLE bool isSupportedImage(const QString &pathOrName) const;
    // Supported, and the helper programs needed for that format are installed.
    Q_INVOKABLE bool canMount(const QString &pathOrName) const;
    Q_INVOKABLE bool isMounted(const QString &pathOrUrl) const;
    Q_INVOKABLE bool isBusy(const QString &pathOrUrl) const;
    Q_INVOKABLE QString mountPointFor(const QString &pathOrUrl) const;

    Q_INVOKABLE void mountImage(const QString &pathOrUrl);
    Q_INVOKABLE void unmountImage(const QString &pathOrUrl);
    Q_INVOKABLE void refresh();

signals:
    void countChanged();
    void imagesChanged();
    void busyChanged();
    void operationStarted(bool mounting, const QString &imageName);
    void operationFinished(bool success, bool mountedNow, const QString &imageName,
                           const QString &detail, const QString &mountPoint);

private:
    struct Entry {
        QString imagePath;      // canonical path of the image file
        QString imageName;      // file name of the image
        QString device;         // block device holding the mounted file system
        QString parentDevice;   // /dev/loopN or /dev/nbdN the image is attached to
        QString rootPath;       // mount point
        QString fileSystem;
        QString label;
        qint64 total = 0;
        qint64 free = 0;
        bool readOnly = false;
        bool nbd = false;
        int siblings = 1;       // mounted file systems of the same image
        QString stableId;
    };
    struct ProcessResult {
        bool started = false;
        bool normalExit = false;
        int exitCode = -1;
        QString out;
        QString err;
        bool ok() const { return started && normalExit && exitCode == 0; }
    };
    using ProcessCallback = std::function<void(const ProcessResult &)>;
    struct MountJob;
    struct UnmountJob;

    static QVector<Entry> discoverEntries(const QHash<QString, QString> &nbdFiles);
    static QString describeFailure(const ProcessResult &result);
    static QString normalizedPath(const QString &pathOrUrl);

    void runProcess(const QString &program, const QStringList &arguments, int timeoutMs,
                    ProcessCallback done);
    void applyEntries(QVector<Entry> entries);
    void queueRefresh(int delayMs);
    void setBusy(const QString &path, bool busy);
    void removeEntriesFor(const QString &path);
    void finishOperation(const QString &path, const QString &name, bool success,
                         bool mountedNow, const QString &detail, const QString &mountPoint);

    void mountViaLoop(const QString &path, const QString &name, bool readOnlyFormat);
    void mountViaNbd(const QString &path, const QString &name, const QString &qemuFormat,
                     bool readOnlyFormat);
    void settleAndProbe(const std::shared_ptr<MountJob> &job);
    void probeAndMount(const std::shared_ptr<MountJob> &job);
    void mountNext(const std::shared_ptr<MountJob> &job);
    void completeMount(const std::shared_ptr<MountJob> &job);
    void abortMount(const std::shared_ptr<MountJob> &job);
    void detachDevice(const QString &device, bool nbd, std::function<void()> done);

    void unmountNext(const std::shared_ptr<UnmountJob> &job);
    void detachNext(const std::shared_ptr<UnmountJob> &job);

    QVector<Entry> m_entries;
    QSet<QString> m_busy;
    // /dev/nbdN -> image file, for the devices this instance attached itself.
    QHash<QString, QString> m_nbdFiles;
    QFutureWatcher<QVector<Entry>> m_watcher;
    QTimer m_refreshTimer;
    bool m_refreshQueued = false;
};
