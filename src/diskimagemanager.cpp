#include "diskimagemanager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QUrl>
#include <QtConcurrent>

#include <algorithm>
#include <utility>

namespace {

// ---------------------------------------------------------------------------
// Supported image formats
// ---------------------------------------------------------------------------

enum class Backend {
    Loop,   // kernel loop device through UDisks, no root password needed
    Nbd     // qemu-nbd through pkexec: virtual-machine disk formats
};

struct FormatInfo {
    const char *suffix;
    Backend backend;
    const char *qemuFormat;   // only for Backend::Nbd
    bool readOnly;            // the format can only be attached read-only
};

const FormatInfo kFormats[] = {
    // Optical / read-only file system images.
    {"iso", Backend::Loop, nullptr, true},
    {"udf", Backend::Loop, nullptr, true},
    {"cdr", Backend::Loop, nullptr, true},
    {"squashfs", Backend::Loop, nullptr, true},
    {"sqfs", Backend::Loop, nullptr, true},
    {"sfs", Backend::Loop, nullptr, true},
    {"erofs", Backend::Loop, nullptr, true},
    {"cramfs", Backend::Loop, nullptr, true},
    // Raw disk, partition and floppy images.
    {"img", Backend::Loop, nullptr, false},
    {"ima", Backend::Loop, nullptr, false},
    {"raw", Backend::Loop, nullptr, false},
    {"dd", Backend::Loop, nullptr, false},
    {"dsk", Backend::Loop, nullptr, false},
    {"flp", Backend::Loop, nullptr, false},
    {"wic", Backend::Loop, nullptr, false},
    {"ext2", Backend::Loop, nullptr, false},
    {"ext3", Backend::Loop, nullptr, false},
    {"ext4", Backend::Loop, nullptr, false},
    {"btrfs", Backend::Loop, nullptr, false},
    {"xfs", Backend::Loop, nullptr, false},
    {"f2fs", Backend::Loop, nullptr, false},
    {"fat", Backend::Loop, nullptr, false},
    {"vfat", Backend::Loop, nullptr, false},
    {"exfat", Backend::Loop, nullptr, false},
    {"ntfs", Backend::Loop, nullptr, false},
    // Virtual-machine disk formats. The format is passed to qemu-nbd
    // explicitly so it never has to probe (and trust) the file contents.
    {"qcow2", Backend::Nbd, "qcow2", false},
    {"qcow", Backend::Nbd, "qcow", false},
    {"qed", Backend::Nbd, "qed", false},
    {"vmdk", Backend::Nbd, "vmdk", false},
    {"vdi", Backend::Nbd, "vdi", false},
    {"vhd", Backend::Nbd, "vpc", false},
    {"vhdx", Backend::Nbd, "vhdx", false},
    {"dmg", Backend::Nbd, "dmg", true},
};

const FormatInfo *formatForPath(const QString &pathOrName)
{
    const QString suffix = QFileInfo(pathOrName).suffix().toLower();
    if (suffix.isEmpty())
        return nullptr;
    for (const FormatInfo &format : kFormats) {
        if (suffix == QLatin1String(format.suffix))
            return &format;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

QString findTool(const QString &name)
{
    QString path = QStandardPaths::findExecutable(name);
    if (path.isEmpty()) {
        path = QStandardPaths::findExecutable(name, {QStringLiteral("/usr/sbin"), QStringLiteral("/usr/bin"),
                                                    QStringLiteral("/sbin"), QStringLiteral("/bin")});
    }
    return path;
}

QString readSmallFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.read(8192));
}

QString decodeMountField(QString value)
{
    return value.replace(QStringLiteral("\\040"), QStringLiteral(" "))
                .replace(QStringLiteral("\\011"), QStringLiteral("\t"))
                .replace(QStringLiteral("\\012"), QStringLiteral("\n"))
                .replace(QStringLiteral("\\134"), QStringLiteral("\\"));
}

// Mount points of one block device according to the live mount table.
QStringList mountPointsForDevice(const QString &device)
{
    QStringList result;
    QFile mounts(QStringLiteral("/proc/self/mounts"));
    if (!mounts.open(QIODevice::ReadOnly | QIODevice::Text))
        return result;
    const QStringList lines = QString::fromUtf8(mounts.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QStringList fields = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (fields.size() < 2)
            continue;
        if (decodeMountField(fields.at(0)) == device)
            result.push_back(QDir::cleanPath(decodeMountField(fields.at(1))));
    }
    return result;
}

bool isVisibleMountedPath(const QString &path)
{
    const QString root = QDir::cleanPath(path);
    if (root.isEmpty() || root == QLatin1String("/"))
        return false;
    const QString home = QDir::homePath();
    return root.startsWith(QStringLiteral("/mnt/"))
        || root.startsWith(QStringLiteral("/media/"))
        || root.startsWith(QStringLiteral("/run/media/"))
        || (!home.isEmpty() && home != QLatin1String("/") && root.startsWith(home + QLatin1Char('/')));
}

bool isSystemImagePath(const QString &path)
{
    // Snap, Flatpak and container layers are loop mounts too, but they are
    // implementation details of the system, not images the user opened.
    static const char *const prefixes[] = {
        "/var/lib/snapd/", "/snap/", "/var/lib/flatpak/", "/usr/",
        "/var/lib/docker/", "/var/lib/containers/"
    };
    for (const char *prefix : prefixes) {
        if (path.startsWith(QLatin1String(prefix)))
            return true;
    }
    return false;
}

bool isMountableFileSystem(const QString &fsType)
{
    if (fsType.isEmpty())
        return false;
    static const QSet<QString> unsupported = {
        QStringLiteral("swap"), QStringLiteral("crypto_luks"), QStringLiteral("lvm2_member"),
        QStringLiteral("linux_raid_member"), QStringLiteral("zfs_member"), QStringLiteral("bitlocker"),
        QStringLiteral("isw_raid_member"), QStringLiteral("ddf_raid_member"), QStringLiteral("stratis")
    };
    return !unsupported.contains(fsType.toLower());
}

bool jsonBool(const QJsonValue &value)
{
    if (value.isBool())
        return value.toBool();
    if (value.isDouble())
        return value.toInt() != 0;
    const QString text = value.toString().trimmed().toLower();
    return text == QLatin1String("1") || text == QLatin1String("true") || text == QLatin1String("yes");
}

struct BlockNode {
    QString name;
    QString path;
    QString fsType;
    QString label;
    QString type;
    QStringList mountPoints;
    qint64 size = 0;
    bool readOnly = false;
};

void flattenBlockTree(const QJsonObject &object, QVector<BlockNode> &out)
{
    BlockNode node;
    node.name = object.value(QStringLiteral("name")).toString().trimmed();
    node.path = object.value(QStringLiteral("path")).toString().trimmed();
    node.fsType = object.value(QStringLiteral("fstype")).toString().trimmed();
    node.label = object.value(QStringLiteral("label")).toString().trimmed();
    node.type = object.value(QStringLiteral("type")).toString().trimmed();
    node.readOnly = jsonBool(object.value(QStringLiteral("ro")));
    const QJsonValue size = object.value(QStringLiteral("size"));
    node.size = size.isString() ? size.toString().toLongLong() : static_cast<qint64>(size.toDouble(0));

    const auto addMountPoint = [&node](const QString &value) {
        const QString clean = value.trimmed();
        if (!clean.isEmpty() && !node.mountPoints.contains(clean))
            node.mountPoints.push_back(clean);
    };
    const QJsonValue points = object.value(QStringLiteral("mountpoints"));
    if (points.isArray()) {
        for (const QJsonValue &point : points.toArray())
            addMountPoint(point.toString());
    }
    addMountPoint(object.value(QStringLiteral("mountpoint")).toString());

    out.push_back(node);
    for (const QJsonValue &child : object.value(QStringLiteral("children")).toArray())
        flattenBlockTree(child.toObject(), out);
}

QString stripDeletedSuffix(QString path)
{
    path = path.trimmed();
    const QString suffix = QStringLiteral(" (deleted)");
    if (path.endsWith(suffix))
        path.chop(suffix.size());
    return path;
}

QString backingFileOfLoop(const QString &name)
{
    return stripDeletedSuffix(readSmallFile(QStringLiteral("/sys/block/%1/loop/backing_file").arg(name)));
}

QString backingFileOfNbd(const QString &name, const QString &devicePath,
                         const QHash<QString, QString> &nbdFiles)
{
    const QString known = nbdFiles.value(devicePath);
    if (!known.isEmpty())
        return known;

    // An NBD device that was attached by someone else: the qemu-nbd process
    // that serves it is listed in sysfs, and the image is its last argument.
    const QString pid = readSmallFile(QStringLiteral("/sys/block/%1/pid").arg(name)).trimmed();
    if (pid.isEmpty())
        return {};
    QFile cmdline(QStringLiteral("/proc/%1/cmdline").arg(pid));
    if (!cmdline.open(QIODevice::ReadOnly))
        return {};
    const QList<QByteArray> parts = cmdline.readAll().split('\0');
    QStringList arguments;
    for (const QByteArray &part : parts) {
        if (!part.isEmpty())
            arguments.push_back(QString::fromLocal8Bit(part));
    }
    if (arguments.size() < 2 || !QFileInfo(arguments.first()).fileName().startsWith(QLatin1String("qemu-nbd")))
        return {};
    const QString last = arguments.last();
    return last.startsWith(QLatin1Char('/')) ? last : QString();
}

} // namespace

// ---------------------------------------------------------------------------
// Job state kept alive across the asynchronous process chain
// ---------------------------------------------------------------------------

struct DiskImageManager::MountJob {
    QString path;
    QString name;
    QString udisksctl;
    QString device;                 // /dev/loopN or /dev/nbdN
    bool nbd = false;
    int attempts = 0;
    QStringList queue;              // file systems still to be mounted
    QStringList mounted;            // mounted by this job
    QStringList alreadyMounted;     // found mounted before the job mounted anything
    QString lastError;
    bool sawUnsupported = false;
};

struct DiskImageManager::UnmountJob {
    QString path;
    QString name;
    QString udisksctl;
    QStringList all;                // every file system of the image
    QStringList devices;            // file systems still to be unmounted
    QStringList parents;            // loop/nbd devices still to be detached
    bool nbd = false;
};

// ---------------------------------------------------------------------------
// Model
// ---------------------------------------------------------------------------

DiskImageManager::DiskImageManager(QObject *parent)
    : QAbstractListModel(parent)
{
    m_refreshTimer.setSingleShot(true);
    connect(&m_refreshTimer, &QTimer::timeout, this, &DiskImageManager::refresh);
    connect(&m_watcher, &QFutureWatcher<QVector<Entry>>::finished, this, [this] {
        applyEntries(m_watcher.result());
        if (m_refreshQueued) {
            m_refreshQueued = false;
            refresh();
        }
    });
    refresh();
}

DiskImageManager::~DiskImageManager() = default;

int DiskImageManager::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant DiskImageManager::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};

    const Entry &e = m_entries.at(index.row());
    const qint64 used = qMax<qint64>(0, e.total - e.free);
    switch (role) {
    case NameRole:
        if (e.siblings > 1) {
            const QString part = e.label.isEmpty() ? QFileInfo(e.device).fileName() : e.label;
            return e.imageName + QStringLiteral(" · ") + part;
        }
        return e.imageName;
    case ImageNameRole: return e.imageName;
    case ImagePathRole: return e.imagePath;
    case RootPathRole: return e.rootPath;
    case FileSystemRole: return e.fileSystem;
    case TotalBytesRole: return e.total;
    case FreeBytesRole: return e.free;
    case UsedBytesRole: return used;
    case UsedRatioRole: return e.total > 0 ? static_cast<double>(used) / e.total : 0.0;
    case ReadOnlyRole: return e.readOnly;
    case DeviceRole: return e.device;
    case BusyRole: return m_busy.contains(e.imagePath);
    case StableIdRole: return e.stableId;
    default: return {};
    }
}

QHash<int, QByteArray> DiskImageManager::roleNames() const
{
    return {
        {NameRole, "name"}, {ImageNameRole, "imageName"}, {ImagePathRole, "imagePath"},
        {RootPathRole, "rootPath"}, {FileSystemRole, "fileSystem"},
        {TotalBytesRole, "totalBytes"}, {FreeBytesRole, "freeBytes"}, {UsedBytesRole, "usedBytes"},
        {UsedRatioRole, "usedRatio"}, {ReadOnlyRole, "readOnly"}, {DeviceRole, "device"},
        {BusyRole, "busy"}, {StableIdRole, "stableId"}
    };
}

QStringList DiskImageManager::mountedPaths() const
{
    QStringList result;
    for (const Entry &entry : m_entries) {
        if (!result.contains(entry.imagePath))
            result.push_back(entry.imagePath);
    }
    return result;
}

QStringList DiskImageManager::busyPaths() const
{
    return QStringList(m_busy.constBegin(), m_busy.constEnd());
}

bool DiskImageManager::isSupportedImage(const QString &pathOrName) const
{
    return formatForPath(pathOrName) != nullptr;
}

bool DiskImageManager::canMount(const QString &pathOrName) const
{
    const FormatInfo *format = formatForPath(pathOrName);
    if (!format)
        return false;
    if (format->backend == Backend::Loop)
        return !findTool(QStringLiteral("udisksctl")).isEmpty();
    return !findTool(QStringLiteral("qemu-nbd")).isEmpty()
        && !findTool(QStringLiteral("pkexec")).isEmpty()
        && !findTool(QStringLiteral("udisksctl")).isEmpty();
}

bool DiskImageManager::isMounted(const QString &pathOrUrl) const
{
    const QString path = normalizedPath(pathOrUrl);
    for (const Entry &entry : m_entries) {
        if (entry.imagePath == path)
            return true;
    }
    return false;
}

bool DiskImageManager::isBusy(const QString &pathOrUrl) const
{
    return m_busy.contains(normalizedPath(pathOrUrl));
}

QString DiskImageManager::mountPointFor(const QString &pathOrUrl) const
{
    const QString path = normalizedPath(pathOrUrl);
    for (const Entry &entry : m_entries) {
        if (entry.imagePath == path)
            return entry.rootPath;
    }
    return {};
}

QString DiskImageManager::normalizedPath(const QString &pathOrUrl)
{
    QString path = pathOrUrl.trimmed();
    if (path.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive))
        path = QUrl(path).toLocalFile();
    if (path.isEmpty())
        return {};
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? QDir::cleanPath(info.absoluteFilePath()) : canonical;
}

// ---------------------------------------------------------------------------
// Discovery
// ---------------------------------------------------------------------------

QVector<DiskImageManager::Entry> DiskImageManager::discoverEntries(const QHash<QString, QString> &nbdFiles)
{
    QVector<Entry> result;

    QProcess lsblk;
    lsblk.start(QStringLiteral("lsblk"), {
        QStringLiteral("--json"), QStringLiteral("--bytes"), QStringLiteral("--output"),
        QStringLiteral("NAME,PATH,FSTYPE,LABEL,MOUNTPOINT,MOUNTPOINTS,SIZE,RO,TYPE")
    });
    if (!lsblk.waitForStarted(1500))
        return result;
    if (!lsblk.waitForFinished(3500)) {
        lsblk.kill();
        lsblk.waitForFinished(500);
        return result;
    }

    const QJsonArray devices = QJsonDocument::fromJson(lsblk.readAllStandardOutput())
                                   .object().value(QStringLiteral("blockdevices")).toArray();
    for (const QJsonValue &value : devices) {
        const QJsonObject top = value.toObject();
        const QString type = top.value(QStringLiteral("type")).toString().trimmed();
        const QString name = top.value(QStringLiteral("name")).toString().trimmed();
        const QString devicePath = top.value(QStringLiteral("path")).toString().trimmed();
        const bool isLoop = type == QLatin1String("loop");
        const bool isNbd = !isLoop && name.startsWith(QLatin1String("nbd"));
        if (!isLoop && !isNbd)
            continue;

        const QString backing = isLoop ? backingFileOfLoop(name)
                                       : backingFileOfNbd(name, devicePath, nbdFiles);
        if (backing.isEmpty())
            continue;
        const QString canonical = QFileInfo(backing).canonicalFilePath();
        const QString imagePath = canonical.isEmpty() ? backing : canonical;
        if (isSystemImagePath(imagePath))
            continue;

        QVector<BlockNode> nodes;
        flattenBlockTree(top, nodes);
        for (const BlockNode &node : std::as_const(nodes)) {
            if (!isMountableFileSystem(node.fsType))
                continue;
            QString root;
            for (const QString &mountPoint : node.mountPoints) {
                if (isVisibleMountedPath(mountPoint)) {
                    root = QDir::cleanPath(mountPoint);
                    break;
                }
            }
            if (root.isEmpty())
                continue;

            Entry entry;
            entry.imagePath = imagePath;
            entry.imageName = QFileInfo(imagePath).fileName();
            entry.device = node.path;
            entry.parentDevice = devicePath;
            entry.rootPath = root;
            entry.fileSystem = node.fsType;
            entry.label = node.label;
            entry.total = node.size;
            entry.readOnly = node.readOnly;
            entry.nbd = isNbd;
            entry.stableId = QStringLiteral("image:") + imagePath + QLatin1Char('|') + node.path;

            const QStorageInfo storage(root);
            if (storage.isValid() && storage.isReady()) {
                entry.total = qMax<qint64>(entry.total, storage.bytesTotal());
                entry.free = qMax<qint64>(0, storage.bytesAvailable());
                entry.readOnly = entry.readOnly || storage.isReadOnly();
            }
            result.push_back(entry);
        }
    }

    QHash<QString, int> perImage;
    for (const Entry &entry : std::as_const(result))
        perImage[entry.imagePath] += 1;
    for (Entry &entry : result)
        entry.siblings = perImage.value(entry.imagePath, 1);

    std::sort(result.begin(), result.end(), [](const Entry &a, const Entry &b) {
        const int byName = a.imageName.compare(b.imageName, Qt::CaseInsensitive);
        if (byName != 0)
            return byName < 0;
        if (a.imagePath != b.imagePath)
            return a.imagePath < b.imagePath;
        return a.device < b.device;
    });
    return result;
}

void DiskImageManager::refresh()
{
    if (m_watcher.isRunning()) {
        m_refreshQueued = true;
        return;
    }
    const QHash<QString, QString> nbdFiles = m_nbdFiles;
    m_watcher.setFuture(QtConcurrent::run([nbdFiles] {
        return DiskImageManager::discoverEntries(nbdFiles);
    }));
}

void DiskImageManager::queueRefresh(int delayMs)
{
    m_refreshTimer.start(qMax(0, delayMs));
}

void DiskImageManager::applyEntries(QVector<Entry> entries)
{
    const QStringList previousImages = mountedPaths();

    bool sameStructure = entries.size() == m_entries.size();
    for (int i = 0; sameStructure && i < entries.size(); ++i)
        sameStructure = entries.at(i).stableId == m_entries.at(i).stableId;

    if (sameStructure) {
        // Same images and file systems: only update what changed (usually the
        // free space) so the views keep their delegates and hover state.
        for (int i = 0; i < entries.size(); ++i) {
            const Entry &a = m_entries.at(i);
            const Entry &b = entries.at(i);
            if (a.rootPath == b.rootPath && a.total == b.total && a.free == b.free
                    && a.readOnly == b.readOnly && a.fileSystem == b.fileSystem
                    && a.label == b.label && a.siblings == b.siblings)
                continue;
            m_entries[i] = b;
            emit dataChanged(index(i), index(i));
        }
    } else {
        beginResetModel();
        m_entries = std::move(entries);
        endResetModel();
        emit countChanged();
    }

    if (previousImages != mountedPaths() || !sameStructure)
        emit imagesChanged();
}

void DiskImageManager::removeEntriesFor(const QString &path)
{
    bool found = false;
    for (const Entry &entry : std::as_const(m_entries)) {
        if (entry.imagePath == path) {
            found = true;
            break;
        }
    }
    if (!found)
        return;
    beginResetModel();
    m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(),
                                   [&path](const Entry &entry) { return entry.imagePath == path; }),
                    m_entries.end());
    endResetModel();
    emit countChanged();
    emit imagesChanged();
}

void DiskImageManager::setBusy(const QString &path, bool busy)
{
    if (busy == m_busy.contains(path))
        return;
    if (busy)
        m_busy.insert(path);
    else
        m_busy.remove(path);
    for (int row = 0; row < m_entries.size(); ++row) {
        if (m_entries.at(row).imagePath == path)
            emit dataChanged(index(row), index(row), {BusyRole});
    }
    emit busyChanged();
}

void DiskImageManager::finishOperation(const QString &path, const QString &name, bool success,
                                       bool mountedNow, const QString &detail, const QString &mountPoint)
{
    if (success && !mountedNow)
        removeEntriesFor(path);
    setBusy(path, false);
    emit operationFinished(success, mountedNow, name, detail, mountPoint);
    queueRefresh(success ? 60 : 120);
}

// ---------------------------------------------------------------------------
// Process helper
// ---------------------------------------------------------------------------

QString DiskImageManager::describeFailure(const ProcessResult &result)
{
    if (!result.started)
        return QStringLiteral("The helper program could not be started");
    QString detail = result.err.trimmed();
    if (detail.isEmpty())
        detail = result.out.trimmed();
    detail.replace(QLatin1Char('\n'), QStringLiteral(" · "));
    if (detail.size() > 260)
        detail = detail.left(257) + QStringLiteral("…");
    return detail;
}

void DiskImageManager::runProcess(const QString &program, const QStringList &arguments,
                                  int timeoutMs, ProcessCallback done)
{
    auto *process = new QProcess(this);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    // Output is parsed in places; keep it independent of the desktop language.
    environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    environment.insert(QStringLiteral("LANGUAGE"), QStringLiteral("C"));
    process->setProcessEnvironment(environment);

    auto completed = std::make_shared<bool>(false);
    const auto complete = [process, completed, done = std::move(done)](bool started, int code,
                                                                       QProcess::ExitStatus status) {
        if (*completed)
            return;
        *completed = true;
        ProcessResult result;
        result.started = started;
        result.normalExit = status == QProcess::NormalExit;
        result.exitCode = code;
        result.out = QString::fromLocal8Bit(process->readAllStandardOutput());
        result.err = QString::fromLocal8Bit(process->readAllStandardError());
        process->deleteLater();
        done(result);
    };

    connect(process, &QProcess::errorOccurred, this, [complete](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            complete(false, -1, QProcess::CrashExit);
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [complete](int code, QProcess::ExitStatus status) { complete(true, code, status); });

    if (timeoutMs > 0) {
        QTimer::singleShot(timeoutMs, process, [process] {
            if (process->state() != QProcess::NotRunning)
                process->kill();
        });
    }
    process->start(program, arguments);
}

// ---------------------------------------------------------------------------
// Mounting
// ---------------------------------------------------------------------------

void DiskImageManager::mountImage(const QString &pathOrUrl)
{
    const QString path = normalizedPath(pathOrUrl);
    if (path.isEmpty() || m_busy.contains(path))
        return;
    const QString name = QFileInfo(path).fileName();

    const FormatInfo *format = formatForPath(path);
    if (!format) {
        emit operationFinished(false, true, name, QStringLiteral("This file type is not a supported disk image"), {});
        return;
    }
    if (!QFileInfo(path).isFile()) {
        emit operationFinished(false, true, name, QStringLiteral("The image file was not found"), {});
        return;
    }
    if (isMounted(path)) {
        emit operationFinished(true, true, name, QString(), mountPointFor(path));
        return;
    }

    setBusy(path, true);
    emit operationStarted(true, name);
    if (format->backend == Backend::Loop)
        mountViaLoop(path, name, format->readOnly);
    else
        mountViaNbd(path, name, QString::fromLatin1(format->qemuFormat), format->readOnly);
}

void DiskImageManager::mountViaLoop(const QString &path, const QString &name, bool readOnlyFormat)
{
    const QString udisksctl = findTool(QStringLiteral("udisksctl"));
    if (udisksctl.isEmpty()) {
        finishOperation(path, name, false, true, QStringLiteral("udisksctl (UDisks2) is not installed"), {});
        return;
    }

    auto job = std::make_shared<MountJob>();
    job->path = path;
    job->name = name;
    job->udisksctl = udisksctl;

    QStringList arguments{QStringLiteral("loop-setup"), QStringLiteral("--file"), path};
    if (readOnlyFormat || !QFileInfo(path).isWritable())
        arguments << QStringLiteral("--read-only");

    runProcess(udisksctl, arguments, 60000, [this, job](const ProcessResult &result) {
        if (!result.ok()) {
            finishOperation(job->path, job->name, false, true, describeFailure(result), {});
            return;
        }
        // "Mapped file /path/x.iso as /dev/loop3." Use the last match: the
        // image path itself could contain something that looks like a device.
        static const QRegularExpression loopDevice(QStringLiteral("(/dev/loop\\d+)"));
        QString device;
        QRegularExpressionMatchIterator it = loopDevice.globalMatch(result.out);
        while (it.hasNext())
            device = it.next().captured(1);
        if (device.isEmpty()) {
            finishOperation(job->path, job->name, false, true,
                            QStringLiteral("Unexpected output from udisksctl"), {});
            return;
        }
        job->device = device;
        settleAndProbe(job);
    });
}

void DiskImageManager::mountViaNbd(const QString &path, const QString &name,
                                   const QString &qemuFormat, bool readOnlyFormat)
{
    const QString pkexec = findTool(QStringLiteral("pkexec"));
    const QString qemuNbd = findTool(QStringLiteral("qemu-nbd"));
    const QString udisksctl = findTool(QStringLiteral("udisksctl"));
    if (pkexec.isEmpty() || qemuNbd.isEmpty()) {
        finishOperation(path, name, false, true,
                        QStringLiteral("qemu-nbd and pkexec are required for this image format"), {});
        return;
    }
    if (udisksctl.isEmpty()) {
        finishOperation(path, name, false, true, QStringLiteral("udisksctl (UDisks2) is not installed"), {});
        return;
    }

    auto job = std::make_shared<MountJob>();
    job->path = path;
    job->name = name;
    job->udisksctl = udisksctl;
    job->nbd = true;
    const bool readOnly = readOnlyFormat || !QFileInfo(path).isWritable();

    const auto connectDevice = [this, job, pkexec, qemuNbd, qemuFormat, readOnly]() {
        QString device;
        for (int i = 0; i < 32 && device.isEmpty(); ++i) {
            const QString node = QStringLiteral("/sys/block/nbd%1").arg(i);
            const QString candidate = QStringLiteral("/dev/nbd%1").arg(i);
            // A connected NBD device has a "pid" file; a free one does not.
            if (QFileInfo::exists(node) && !QFileInfo::exists(node + QStringLiteral("/pid"))
                    && !m_nbdFiles.contains(candidate))
                device = candidate;
        }
        if (device.isEmpty()) {
            finishOperation(job->path, job->name, false, true,
                            QStringLiteral("No free NBD device is available"), {});
            return;
        }
        job->device = device;
        m_nbdFiles.insert(device, job->path);

        QStringList arguments{qemuNbd, QStringLiteral("--connect=") + device,
                              QStringLiteral("--format=") + qemuFormat};
        if (readOnly)
            arguments << QStringLiteral("--read-only");
        arguments << job->path;

        runProcess(pkexec, arguments, 180000, [this, job, pkexec](const ProcessResult &result) {
            if (!result.ok()) {
                m_nbdFiles.remove(job->device);
                finishOperation(job->path, job->name, false, true, describeFailure(result), {});
                return;
            }
            // With max_part=0 the kernel never scans for partitions on its own.
            const QString maxPart = readSmallFile(QStringLiteral("/sys/module/nbd/parameters/max_part")).trimmed();
            const QString partx = findTool(QStringLiteral("partx"));
            if (maxPart == QLatin1String("0") && !partx.isEmpty()) {
                runProcess(pkexec, {partx, QStringLiteral("-a"), job->device}, 60000,
                           [this, job](const ProcessResult &) { settleAndProbe(job); });
            } else {
                settleAndProbe(job);
            }
        });
    };

    if (QFileInfo::exists(QStringLiteral("/sys/module/nbd"))) {
        connectDevice();
        return;
    }
    const QString modprobe = findTool(QStringLiteral("modprobe"));
    if (modprobe.isEmpty()) {
        finishOperation(path, name, false, true,
                        QStringLiteral("The nbd kernel module is not loaded and modprobe was not found"), {});
        return;
    }
    runProcess(pkexec, {modprobe, QStringLiteral("nbd"), QStringLiteral("max_part=16")}, 180000,
               [this, job, connectDevice](const ProcessResult &result) {
        if (!result.ok()) {
            finishOperation(job->path, job->name, false, true, describeFailure(result), {});
            return;
        }
        connectDevice();
    });
}

void DiskImageManager::settleAndProbe(const std::shared_ptr<MountJob> &job)
{
    // Partition nodes appear a moment after the device is attached; let udev
    // finish creating them before looking for file systems.
    const QString udevadm = findTool(QStringLiteral("udevadm"));
    if (udevadm.isEmpty()) {
        probeAndMount(job);
        return;
    }
    runProcess(udevadm, {QStringLiteral("settle"), QStringLiteral("--timeout=5")}, 8000,
               [this, job](const ProcessResult &) { probeAndMount(job); });
}

void DiskImageManager::probeAndMount(const std::shared_ptr<MountJob> &job)
{
    runProcess(QStringLiteral("lsblk"),
               {QStringLiteral("--json"), QStringLiteral("--output"),
                QStringLiteral("NAME,PATH,FSTYPE,MOUNTPOINT,MOUNTPOINTS,TYPE"), job->device},
               5000, [this, job](const ProcessResult &result) {
        QVector<BlockNode> nodes;
        if (result.ok()) {
            const QJsonArray devices = QJsonDocument::fromJson(result.out.toUtf8())
                                           .object().value(QStringLiteral("blockdevices")).toArray();
            for (const QJsonValue &device : devices)
                flattenBlockTree(device.toObject(), nodes);
        }

        QStringList candidates;
        QStringList mounted;
        bool unsupported = false;
        for (const BlockNode &node : std::as_const(nodes)) {
            if (node.fsType.isEmpty())
                continue;
            if (!isMountableFileSystem(node.fsType)) {
                unsupported = true;
                continue;
            }
            if (node.mountPoints.isEmpty())
                candidates.push_back(node.path);
            else
                mounted.push_back(node.path);
        }
        job->sawUnsupported = job->sawUnsupported || unsupported;

        if (candidates.isEmpty() && mounted.isEmpty()) {
            // Partition devices may still be appearing; look again for a few seconds.
            if (++job->attempts < 15) {
                QTimer::singleShot(200, this, [this, job] { probeAndMount(job); });
                return;
            }
            job->lastError = job->sawUnsupported
                ? QStringLiteral("The image only contains encrypted, swap, LVM or RAID volumes, which cannot be mounted")
                : QStringLiteral("No mountable file system was found in this image");
            abortMount(job);
            return;
        }
        job->queue = candidates;
        job->alreadyMounted = mounted;
        mountNext(job);
    });
}

void DiskImageManager::mountNext(const std::shared_ptr<MountJob> &job)
{
    if (job->queue.isEmpty()) {
        completeMount(job);
        return;
    }
    const QString device = job->queue.takeFirst();
    runProcess(job->udisksctl, {QStringLiteral("mount"), QStringLiteral("--block-device"), device}, 120000,
               [this, job, device](const ProcessResult &result) {
        if (result.ok() || !mountPointsForDevice(device).isEmpty())
            job->mounted.push_back(device);
        else
            job->lastError = describeFailure(result);
        mountNext(job);
    });
}

void DiskImageManager::completeMount(const std::shared_ptr<MountJob> &job)
{
    if (job->mounted.isEmpty() && job->alreadyMounted.isEmpty()) {
        abortMount(job);
        return;
    }
    QString mountPoint;
    const QStringList devices = job->alreadyMounted + job->mounted;
    for (const QString &device : devices) {
        for (const QString &point : mountPointsForDevice(device)) {
            if (isVisibleMountedPath(point)) {
                mountPoint = point;
                break;
            }
        }
        if (!mountPoint.isEmpty())
            break;
    }
    finishOperation(job->path, job->name, true, true, QString(), mountPoint);
}

void DiskImageManager::abortMount(const std::shared_ptr<MountJob> &job)
{
    const QString detail = job->lastError.isEmpty()
        ? QStringLiteral("The image could not be mounted") : job->lastError;
    // Do not leave an unused loop/NBD device behind.
    detachDevice(job->device, job->nbd, [this, job, detail] {
        finishOperation(job->path, job->name, false, true, detail, {});
    });
}

void DiskImageManager::detachDevice(const QString &device, bool nbd, std::function<void()> done)
{
    if (device.isEmpty()) {
        done();
        return;
    }
    if (nbd) {
        const QString pkexec = findTool(QStringLiteral("pkexec"));
        const QString qemuNbd = findTool(QStringLiteral("qemu-nbd"));
        if (pkexec.isEmpty() || qemuNbd.isEmpty()) {
            m_nbdFiles.remove(device);
            done();
            return;
        }
        runProcess(pkexec, {qemuNbd, QStringLiteral("--disconnect"), device}, 180000,
                   [this, device, done](const ProcessResult &) {
            m_nbdFiles.remove(device);
            done();
        });
        return;
    }
    const QString udisksctl = findTool(QStringLiteral("udisksctl"));
    if (udisksctl.isEmpty()) {
        done();
        return;
    }
    // Loop devices created by UDisks are released automatically after the last
    // unmount, so a failure here is normal and not an error.
    runProcess(udisksctl, {QStringLiteral("loop-delete"), QStringLiteral("--block-device"), device}, 30000,
               [done](const ProcessResult &) { done(); });
}

// ---------------------------------------------------------------------------
// Unmounting
// ---------------------------------------------------------------------------

void DiskImageManager::unmountImage(const QString &pathOrUrl)
{
    const QString path = normalizedPath(pathOrUrl);
    if (path.isEmpty() || m_busy.contains(path))
        return;
    const QString name = QFileInfo(path).fileName();

    auto job = std::make_shared<UnmountJob>();
    job->path = path;
    job->name = name;
    for (const Entry &entry : std::as_const(m_entries)) {
        if (entry.imagePath != path)
            continue;
        if (!job->all.contains(entry.device))
            job->all.push_back(entry.device);
        if (!job->parents.contains(entry.parentDevice))
            job->parents.push_back(entry.parentDevice);
        job->nbd = job->nbd || entry.nbd;
    }
    if (job->all.isEmpty()) {
        emit operationFinished(true, false, name, QString(), QString());
        return;
    }
    job->devices = job->all;
    job->udisksctl = findTool(QStringLiteral("udisksctl"));
    if (job->udisksctl.isEmpty()) {
        emit operationFinished(false, false, name, QStringLiteral("udisksctl (UDisks2) is not installed"), {});
        return;
    }

    setBusy(path, true);
    emit operationStarted(false, name);
    unmountNext(job);
}

void DiskImageManager::unmountNext(const std::shared_ptr<UnmountJob> &job)
{
    if (job->devices.isEmpty()) {
        detachNext(job);
        return;
    }
    const QString device = job->devices.takeFirst();
    runProcess(job->udisksctl, {QStringLiteral("unmount"), QStringLiteral("--block-device"), device}, 120000,
               [this, job, device](const ProcessResult &result) {
        // A busy file system keeps the whole image attached.
        if (!result.ok() && !mountPointsForDevice(device).isEmpty()) {
            finishOperation(job->path, job->name, false, false, describeFailure(result), {});
            return;
        }
        unmountNext(job);
    });
}

void DiskImageManager::detachNext(const std::shared_ptr<UnmountJob> &job)
{
    if (job->parents.isEmpty()) {
        // Give UDisks a moment to release the device, then verify.
        QTimer::singleShot(120, this, [this, job] {
            for (const QString &device : std::as_const(job->all)) {
                if (!mountPointsForDevice(device).isEmpty()) {
                    finishOperation(job->path, job->name, false, false,
                                    QStringLiteral("The image is still mounted"), {});
                    return;
                }
            }
            finishOperation(job->path, job->name, true, false, QString(), QString());
        });
        return;
    }
    const QString parent = job->parents.takeFirst();
    detachDevice(parent, job->nbd, [this, job] { detachNext(job); });
}
