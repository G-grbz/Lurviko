#include "storagemodel.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <QSettings>
#include <QStorageInfo>
#include <QStandardPaths>
#include <QTextStream>
#include <QtConcurrent>
#include <algorithm>
#include <functional>
#include <utility>

namespace {
static const QSet<QString> kIgnoredFs = {
    QStringLiteral("proc"), QStringLiteral("sysfs"), QStringLiteral("devtmpfs"),
    QStringLiteral("devpts"), QStringLiteral("tmpfs"), QStringLiteral("overlay"),
    QStringLiteral("squashfs"), QStringLiteral("cgroup"), QStringLiteral("cgroup2"),
    QStringLiteral("mqueue"), QStringLiteral("debugfs"), QStringLiteral("securityfs"),
    QStringLiteral("pstore"), QStringLiteral("autofs"), QStringLiteral("tracefs"),
    QStringLiteral("hugetlbfs"), QStringLiteral("fusectl"), QStringLiteral("rpc_pipefs"),
    QStringLiteral("configfs"), QStringLiteral("binfmt_misc"), QStringLiteral("efivarfs"),
    QStringLiteral("ramfs"), QStringLiteral("nsfs"), QStringLiteral("fuse.portal"),
    QStringLiteral("fuse.gvfsd-fuse"), QStringLiteral("swap")
};

QString decodeMountField(QString value)
{
    return value.replace(QStringLiteral("\\040"), QStringLiteral(" "))
                .replace(QStringLiteral("\\011"), QStringLiteral("\t"))
                .replace(QStringLiteral("\\012"), QStringLiteral("\n"))
                .replace(QStringLiteral("\\134"), QStringLiteral("\\"));
}

bool isNetworkStorageFilesystem(const QString &type)
{
    return type == QStringLiteral("cifs") || type == QStringLiteral("smb3")
        || type == QStringLiteral("nfs") || type == QStringLiteral("nfs4")
        || type == QStringLiteral("fuse.sshfs");
}

bool isVisibleMountedPath(const QString &rootPath, const QString &fileSystem)
{
    const QString root = QDir::cleanPath(rootPath);
    if (root == QStringLiteral("/"))
        return true;
    if (kIgnoredFs.contains(fileSystem))
        return false;
    if (root.startsWith(QStringLiteral("/boot")) || root == QStringLiteral("/tmp")
            || root.startsWith(QStringLiteral("/proc")) || root.startsWith(QStringLiteral("/sys"))
            || root.startsWith(QStringLiteral("/dev")))
        return false;
    if (root.startsWith(QStringLiteral("/run/")) && !root.startsWith(QStringLiteral("/run/media/")))
        return false;
    return root.startsWith(QStringLiteral("/mnt/"))
        || root.startsWith(QStringLiteral("/media/"))
        || root.startsWith(QStringLiteral("/run/media/"))
        || root == QStringLiteral("/home");
}

bool jsonBool(const QJsonValue &value)
{
    if (value.isBool())
        return value.toBool();
    if (value.isDouble())
        return value.toInt() != 0;
    const QString text = value.toString().trimmed().toLower();
    return text == QStringLiteral("1") || text == QStringLiteral("true") || text == QStringLiteral("yes");
}

QString firstMountPoint(const QJsonObject &object)
{
    const QJsonValue points = object.value(QStringLiteral("mountpoints"));
    if (points.isArray()) {
        for (const QJsonValue &value : points.toArray()) {
            const QString path = value.toString().trimmed();
            if (!path.isEmpty())
                return QDir::cleanPath(path);
        }
    }
    const QString single = object.value(QStringLiteral("mountpoint")).toString().trimmed();
    return single.isEmpty() ? QString() : QDir::cleanPath(single);
}

QHash<QString, QString> configuredMountPoints()
{
    QHash<QString, QString> result;
    QFile file(QStringLiteral("/etc/fstab"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return result;

    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const QStringList parts = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (parts.size() < 3)
            continue;
        const QString source = decodeMountField(parts.at(0));
        const QString target = QDir::cleanPath(decodeMountField(parts.at(1)));
        const QString fsType = parts.at(2).trimmed();
        if (target.isEmpty() || kIgnoredFs.contains(fsType))
            continue;
        result.insert(source, target);
        if (source.startsWith(QStringLiteral("UUID="), Qt::CaseInsensitive))
            result.insert(QStringLiteral("uuid:") + source.mid(5).toLower(), target);
    }
    return result;
}

QString configuredPathFor(const QHash<QString, QString> &mounts, const QString &device, const QString &uuid)
{
    if (mounts.contains(device))
        return mounts.value(device);
    if (!uuid.isEmpty()) {
        const QString key = QStringLiteral("uuid:") + uuid.toLower();
        if (mounts.contains(key))
            return mounts.value(key);
    }
    return {};
}

struct ConfiguredNetworkMount {
    QString source;
    QString target;
    QString fileSystem;
};

QVector<ConfiguredNetworkMount> configuredNetworkMounts()
{
    QVector<ConfiguredNetworkMount> result;
    QFile file(QStringLiteral("/etc/fstab"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return result;

    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const QStringList parts = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (parts.size() < 3)
            continue;
        const QString source = decodeMountField(parts.at(0));
        const QString target = QDir::cleanPath(decodeMountField(parts.at(1)));
        const QString fsType = parts.at(2).trimmed().toLower();
        if (!isNetworkStorageFilesystem(fsType) || target.isEmpty())
            continue;
        result.push_back({source, target, fsType});
    }
    return result;
}

QString stableIdForBlock(const QString &uuid, const QString &device)
{
    if (!uuid.trimmed().isEmpty())
        return QStringLiteral("uuid:") + uuid.trimmed().toLower();
    return QStringLiteral("block:") + device;
}

QString stableIdForNetwork(const QString &source, const QString &target)
{
    return QStringLiteral("network:") + source + QLatin1Char('|') + QDir::cleanPath(target);
}

QString cleanedProcessDetail(const QByteArray &stderrData, const QByteArray &stdoutData)
{
    QString detail = QString::fromLocal8Bit(stderrData).trimmed();
    if (detail.isEmpty())
        detail = QString::fromLocal8Bit(stdoutData).trimmed();
    detail.replace(QLatin1Char('\n'), QStringLiteral(" · "));
    if (detail.size() > 260)
        detail = detail.left(257) + QStringLiteral("…");
    return detail;
}

struct MountTableSnapshot {
    QSet<QString> sources;
    QSet<QString> targets;
};

MountTableSnapshot readCurrentMountTable()
{
    MountTableSnapshot snapshot;
    QFile mounts(QStringLiteral("/proc/self/mounts"));
    if (!mounts.open(QIODevice::ReadOnly | QIODevice::Text))
        return snapshot;

    const QStringList lines = QString::fromUtf8(mounts.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QStringList fields = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (fields.size() < 2)
            continue;
        snapshot.sources.insert(decodeMountField(fields.at(0)).trimmed());
        snapshot.targets.insert(QDir::cleanPath(decodeMountField(fields.at(1)).trimmed()));
    }
    return snapshot;
}

bool entryMatchesMountedState(const QString &rootPathValue, const QString &device,
                              const QString &source, bool mountedExpected)
{
    const MountTableSnapshot snapshot = readCurrentMountTable();
    bool mountedNow = false;
    const QString rootPath = QDir::cleanPath(rootPathValue);
    if (!rootPath.isEmpty() && rootPath != device && snapshot.targets.contains(rootPath))
        mountedNow = true;
    if (!mountedNow && !device.isEmpty() && snapshot.sources.contains(device))
        mountedNow = true;
    if (!mountedNow && !source.isEmpty() && snapshot.sources.contains(source))
        mountedNow = true;
    return mountedNow == mountedExpected;
}
}

StorageModel::StorageModel(QObject *parent)
    : QAbstractListModel(parent)
{
    QSettings settings(QStringLiteral("Lurviko"), QStringLiteral("Lurviko"));
    const QStringList saved = settings.value(QStringLiteral("navigation/favoriteDisks")).toStringList();
    for (const QString &path : saved) {
        const QString clean = QDir::cleanPath(path);
        if (!clean.isEmpty())
            m_favoritePaths.insert(clean);
    }

    m_diskOrder = settings.value(QStringLiteral("navigation/diskOrder")).toStringList();
    m_diskOrder.removeDuplicates();
    const QVariantMap customTitles = settings.value(QStringLiteral("navigation/diskCustomTitles")).toMap();
    const QVariantMap customIcons = settings.value(QStringLiteral("navigation/diskCustomIcons")).toMap();
    for (auto it = customTitles.constBegin(); it != customTitles.constEnd(); ++it)
        m_customDiskTitles.insert(it.key(), it.value().toString());
    for (auto it = customIcons.constBegin(); it != customIcons.constEnd(); ++it)
        m_customDiskIcons.insert(it.key(), it.value().toString());

    m_refreshDebounce.setSingleShot(true);
    connect(&m_refreshDebounce, &QTimer::timeout, this, &StorageModel::refresh);

    connect(&m_refreshWatcher, &QFutureWatcher<QVector<Entry>>::finished, this, [this] {
        const QVector<Entry> entries = m_refreshWatcher.result();
        // Never reset a QML Repeater model while a card is the active drag
        // source. Destroying/reindexing delegates mid-drag can leave Qt Quick
        // with a stale drag source and has caused a native crash. Discard this
        // snapshot and run one fresh scan as soon as the drag is finished.
        if (m_reorderActive) {
            m_refreshQueued = true;
            return;
        }
        QHash<QString, QPair<qint64, qint64>> previousNetworkCapacity;
        for (const Entry &entry : std::as_const(m_entries)) {
            if (entry.mounted && entry.total > 0 && isNetworkStorageFilesystem(entry.fileSystem))
                previousNetworkCapacity.insert(entry.rootPath, {entry.total, entry.free});
        }

        beginResetModel();
        m_entries.clear();
        for (Entry entry : entries) {
            entry.busy = m_busyDevices.contains(entry.device);
            if (entry.mounted && entry.total == 0 && isNetworkStorageFilesystem(entry.fileSystem)
                    && previousNetworkCapacity.contains(entry.rootPath)) {
                const auto capacity = previousNetworkCapacity.value(entry.rootPath);
                entry.total = capacity.first;
                entry.free = capacity.second;
            }
            upsertEntry(entry);
        }
        std::sort(m_entries.begin(), m_entries.end(), [](const Entry &a, const Entry &b) {
            const bool aRoot = a.rootPath == QStringLiteral("/");
            const bool bRoot = b.rootPath == QStringLiteral("/");
            if (aRoot != bRoot)
                return aRoot;
            if (a.removable != b.removable)
                return a.removable < b.removable;
            if (a.mounted != b.mounted)
                return a.mounted > b.mounted;
            return a.name.localeAwareCompare(b.name) < 0;
        });

        QHash<QString, int> savedRanks;
        for (int i = 0; i < m_diskOrder.size(); ++i)
            savedRanks.insert(m_diskOrder.at(i), i);
        std::stable_sort(m_entries.begin(), m_entries.end(), [&](const Entry &a, const Entry &b) {
            const int ar = savedRanks.value(a.stableId, -1);
            const int br = savedRanks.value(b.stableId, -1);
            if (ar >= 0 && br >= 0)
                return ar < br;
            if (ar >= 0)
                return true;
            if (br >= 0)
                return false;
            return false;
        });

        bool orderChanged = false;
        for (const Entry &entry : std::as_const(m_entries)) {
            if (!entry.stableId.isEmpty() && !m_diskOrder.contains(entry.stableId)) {
                m_diskOrder.push_back(entry.stableId);
                orderChanged = true;
            }
        }
        if (orderChanged)
            saveDiskOrder();
        recalculateTotals();
        endResetModel();
        emit countChanged();
        emit totalsChanged();
        emit favoritesChanged();

        if (m_refreshQueued) {
            m_refreshQueued = false;
            refresh();
        } else {
            refreshNetworkCapacities();
        }
    });

    connect(&m_networkWatcher, &QFutureWatcher<QVector<NetworkCapacity>>::finished, this, [this] {
        bool changed = false;
        for (const NetworkCapacity &capacity : m_networkWatcher.result()) {
            if (!capacity.ready || capacity.total <= 0)
                continue;
            for (int row = 0; row < m_entries.size(); ++row) {
                Entry &entry = m_entries[row];
                if (entry.rootPath != capacity.rootPath || !entry.mounted
                        || !isNetworkStorageFilesystem(entry.fileSystem))
                    continue;
                if (entry.total == capacity.total && entry.free == capacity.free
                        && entry.readOnly == capacity.readOnly)
                    break;
                entry.total = capacity.total;
                entry.free = capacity.free;
                entry.readOnly = capacity.readOnly;
                entry.ready = true;
                emit dataChanged(index(row), index(row),
                                 {TotalBytesRole, FreeBytesRole, UsedBytesRole, UsedRatioRole,
                                  ReadOnlyRole, ReadyRole});
                changed = true;
                break;
            }
        }
        if (changed) {
            recalculateTotals();
            emit totalsChanged();
            emit favoritesChanged();
        }
        if (m_networkRefreshQueued) {
            m_networkRefreshQueued = false;
            refreshNetworkCapacities();
        }
    });

    m_pollTimer.setInterval(5000);
    m_pollTimer.setSingleShot(false);
    connect(&m_pollTimer, &QTimer::timeout, this, &StorageModel::refresh);
    m_pollTimer.start();

    startDeviceMonitor();
    refresh();
}

StorageModel::~StorageModel()
{
    m_shuttingDown = true;
    m_pollTimer.stop();
    m_refreshDebounce.stop();

    // udisksctl monitor is intentionally long-lived. Stop it explicitly
    // before QObject destruction so QProcess is never destroyed while the
    // helper is still running. Disconnect first so the finished handler does
    // not schedule a restart during application shutdown.
    disconnect(&m_udevMonitor, nullptr, this, nullptr);
    if (m_udevMonitor.state() != QProcess::NotRunning) {
        m_udevMonitor.terminate();
        if (!m_udevMonitor.waitForFinished(180)) {
            m_udevMonitor.kill();
            m_udevMonitor.waitForFinished(180);
        }
    }
}

int StorageModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant StorageModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};

    const Entry &e = m_entries.at(index.row());
    const qint64 used = e.mounted ? qMax<qint64>(0, e.total - e.free) : 0;
    switch (role) {
    case NameRole: return m_customDiskTitles.value(e.stableId, e.name);
    case RootPathRole: return e.rootPath;
    case FileSystemRole: return e.fileSystem;
    case TotalBytesRole: return e.total;
    case FreeBytesRole: return e.free;
    case UsedBytesRole: return used;
    case UsedRatioRole: return e.mounted && e.total > 0 ? static_cast<double>(used) / e.total : 0.0;
    case ReadOnlyRole: return e.readOnly;
    case ReadyRole: return e.ready;
    case MountedRole: return e.mounted;
    case ConfiguredRole: return e.configured;
    case SourceRole: return e.source;
    case DeviceRole: return e.device;
    case RemovableRole: return e.removable;
    case CanMountToggleRole: return e.canMountToggle;
    case BusyRole: return e.busy;
    case StableIdRole: return e.stableId;
    case IconRole: return m_customDiskIcons.value(e.stableId, QStringLiteral("drive.svg"));
    default: return {};
    }
}

QHash<int, QByteArray> StorageModel::roleNames() const
{
    return {
        {NameRole, "name"}, {RootPathRole, "rootPath"}, {FileSystemRole, "fileSystem"},
        {TotalBytesRole, "totalBytes"}, {FreeBytesRole, "freeBytes"}, {UsedBytesRole, "usedBytes"},
        {UsedRatioRole, "usedRatio"}, {ReadOnlyRole, "readOnly"}, {ReadyRole, "ready"},
        {MountedRole, "mounted"}, {ConfiguredRole, "configured"}, {SourceRole, "source"},
        {DeviceRole, "device"}, {RemovableRole, "removable"},
        {CanMountToggleRole, "canMountToggle"}, {BusyRole, "busy"},
        {StableIdRole, "stableId"}, {IconRole, "icon"}
    };
}

QVector<StorageModel::Entry> StorageModel::discoverLiveEntries()
{
    QVector<Entry> entries;
    const QHash<QString, QString> fstabMounts = configuredMountPoints();

    QProcess lsblk;
    lsblk.start(QStringLiteral("lsblk"), {
        QStringLiteral("--json"), QStringLiteral("--bytes"),
        QStringLiteral("--output"),
        QStringLiteral("NAME,PATH,FSTYPE,LABEL,UUID,MOUNTPOINT,MOUNTPOINTS,SIZE,RO,TYPE,RM,HOTPLUG,TRAN,MODEL,VENDOR")
    });
    if (lsblk.waitForStarted(1500) && lsblk.waitForFinished(3500)) {
        const QJsonDocument document = QJsonDocument::fromJson(lsblk.readAllStandardOutput());
        const QJsonArray devices = document.object().value(QStringLiteral("blockdevices")).toArray();

        const auto candidateScore = [](const Entry &entry) -> qint64 {
            // One card represents one physical disk. Prefer the live root
            // filesystem, then an fstab-known filesystem, then another mounted
            // filesystem, and finally the largest usable partition. This keeps
            // EFI/recovery partitions from appearing as separate disks.
            constexpr qint64 rootBonus = 6000000000000000000LL;
            constexpr qint64 configuredBonus = 4000000000000000000LL;
            constexpr qint64 mountedBonus = 2000000000000000000LL;
            const qint64 sizePart = qMin<qint64>(entry.total, 900000000000000000LL);
            if (entry.rootPath == QStringLiteral("/"))
                return rootBonus + sizePart;
            if (entry.configured)
                return configuredBonus + sizePart;
            if (entry.mounted)
                return mountedBonus + sizePart;
            return sizePart;
        };

        std::function<void(const QJsonObject &, const QString &, const QString &, QVector<Entry> &)> collect;
        collect = [&](const QJsonObject &object, const QString &parentModel,
                      const QString &parentVendor, QVector<Entry> &candidates) {
            const QString type = object.value(QStringLiteral("type")).toString().trimmed();
            const QString device = object.value(QStringLiteral("path")).toString().trimmed();
            const QString fsType = object.value(QStringLiteral("fstype")).toString().trimmed();
            const QString uuid = object.value(QStringLiteral("uuid")).toString().trimmed();
            const QString label = object.value(QStringLiteral("label")).toString().trimmed();
            QString model = object.value(QStringLiteral("model")).toString().trimmed();
            QString vendor = object.value(QStringLiteral("vendor")).toString().trimmed();
            if (model.isEmpty()) model = parentModel;
            if (vendor.isEmpty()) vendor = parentVendor;

            const QString configuredPath = configuredPathFor(fstabMounts, device, uuid);
            const QString mountedPath = firstMountPoint(object);
            const bool hasFileSystem = !fsType.isEmpty() && !kIgnoredFs.contains(fsType);
            const bool supportedType = type != QStringLiteral("loop") && type != QStringLiteral("rom");
            const QString effectivePath = !mountedPath.isEmpty() ? mountedPath : configuredPath;
            const bool hiddenSystemPath = effectivePath.startsWith(QStringLiteral("/boot"));

            if (hasFileSystem && supportedType && !device.isEmpty() && !hiddenSystemPath) {
                Entry entry;
                entry.device = device;
                entry.source = device;
                entry.fileSystem = fsType;
                entry.configured = !configuredPath.isEmpty();
                entry.mounted = !mountedPath.isEmpty();
                entry.ready = entry.mounted;
                entry.rootPath = entry.mounted ? mountedPath
                                               : (!configuredPath.isEmpty() ? configuredPath : device);
                entry.readOnly = jsonBool(object.value(QStringLiteral("ro")));
                entry.removable = jsonBool(object.value(QStringLiteral("rm")))
                    || jsonBool(object.value(QStringLiteral("hotplug")))
                    || object.value(QStringLiteral("tran")).toString().compare(QStringLiteral("usb"), Qt::CaseInsensitive) == 0;
                entry.canMountToggle = entry.rootPath != QStringLiteral("/")
                    && !entry.rootPath.startsWith(QStringLiteral("/boot"));
                entry.total = static_cast<qint64>(object.value(QStringLiteral("size")).toDouble(0));
                entry.stableId = stableIdForBlock(uuid, device);

                QString displayName = label;
                if (displayName.isEmpty() && entry.rootPath == QStringLiteral("/"))
                    displayName = QStringLiteral("Root");
                if (displayName.isEmpty() && !model.isEmpty())
                    displayName = (vendor + QLatin1Char(' ') + model).trimmed();
                entry.name = prettyNameForPath(entry.rootPath, displayName);

                if (entry.mounted) {
                    const QStorageInfo storage(entry.rootPath);
                    if (storage.isValid()) {
                        const QString storageName = storage.displayName().trimmed();
                        if (label.isEmpty() && !storageName.isEmpty())
                            entry.name = prettyNameForPath(entry.rootPath, storageName);
                        entry.total = qMax<qint64>(entry.total, storage.bytesTotal());
                        entry.free = qMax<qint64>(0, storage.bytesAvailable());
                        entry.readOnly = storage.isReadOnly();
                        entry.ready = storage.isReady();
                    }
                }
                candidates.push_back(entry);
            }

            for (const QJsonValue &child : object.value(QStringLiteral("children")).toArray())
                collect(child.toObject(), model, vendor, candidates);
        };

        for (const QJsonValue &value : devices) {
            QVector<Entry> candidates;
            const QJsonObject top = value.toObject();
            collect(top, QString(), QString(), candidates);
            if (candidates.isEmpty())
                continue;

            auto best = std::max_element(candidates.begin(), candidates.end(), [&](const Entry &a, const Entry &b) {
                return candidateScore(a) < candidateScore(b);
            });
            if (best != candidates.end())
                entries.push_back(*best);
        }
    }

    // fstab is allowed to keep NETWORK cards around while disconnected, so a
    // CIFS/NFS share can be mounted again from the same card. Local block
    // devices are intentionally different: fstab never creates a card unless
    // the physical device is actually present in lsblk above.
    const QVector<ConfiguredNetworkMount> configuredNetworks = configuredNetworkMounts();
    QHash<QString, ConfiguredNetworkMount> configuredByTarget;
    for (const ConfiguredNetworkMount &mount : configuredNetworks) {
        configuredByTarget.insert(QDir::cleanPath(mount.target), mount);
        Entry entry;
        entry.rootPath = QDir::cleanPath(mount.target);
        entry.source = mount.source;
        entry.stableId = stableIdForNetwork(mount.source, mount.target);
        entry.device = entry.stableId; // action token; not a /dev path
        entry.name = prettyNameForPath(entry.rootPath, QString());
        entry.fileSystem = mount.fileSystem;
        entry.mounted = false;
        entry.ready = false;
        entry.configured = true;
        entry.removable = false;
        entry.canMountToggle = true;
        entries.push_back(entry);
    }

    QFile mounts(QStringLiteral("/proc/self/mounts"));
    if (mounts.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QStringList lines = QString::fromUtf8(mounts.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            const QStringList fields = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (fields.size() < 4)
                continue;
            const QString fsType = fields.at(2).toLower();
            if (!isNetworkStorageFilesystem(fsType))
                continue;
            const QString rootPath = QDir::cleanPath(decodeMountField(fields.at(1)));
            if (!isVisibleMountedPath(rootPath, fsType))
                continue;
            const QString source = decodeMountField(fields.at(0));
            const bool configured = configuredByTarget.contains(rootPath);
            const ConfiguredNetworkMount known = configured ? configuredByTarget.value(rootPath)
                                                            : ConfiguredNetworkMount{source, rootPath, fsType};
            Entry entry;
            entry.rootPath = rootPath;
            entry.source = source;
            entry.stableId = stableIdForNetwork(known.source, rootPath);
            entry.device = entry.stableId;
            entry.name = prettyNameForPath(rootPath, QString());
            entry.fileSystem = fsType;
            entry.readOnly = fields.at(3).split(QLatin1Char(',')).contains(QStringLiteral("ro"));
            entry.ready = true;
            entry.mounted = true;
            entry.configured = configured;
            entry.removable = false;
            entry.canMountToggle = configured;
            entries.push_back(entry);
        }
    }

    return entries;
}

void StorageModel::refresh()
{
    if (m_reorderActive) {
        m_refreshQueued = true;
        return;
    }
    if (m_refreshWatcher.isRunning()) {
        m_refreshQueued = true;
        return;
    }
    m_refreshWatcher.setFuture(QtConcurrent::run([] { return discoverLiveEntries(); }));
}

void StorageModel::startDeviceMonitor()
{
    connect(&m_udevMonitor, &QProcess::readyReadStandardOutput, this, [this] {
        m_udevMonitor.readAllStandardOutput();
        queueRefresh();
    });
    connect(&m_udevMonitor, &QProcess::readyReadStandardError, this, [this] {
        m_udevMonitor.readAllStandardError();
    });
    connect(&m_udevMonitor, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
        QTimer::singleShot(1500, this, [this] {
            if (!m_shuttingDown && m_udevMonitor.state() == QProcess::NotRunning) {
                m_udevMonitor.start(QStringLiteral("udisksctl"), {QStringLiteral("monitor")});
            }
        });
    });
    // UDisks emits both hot-plug and filesystem mount-point changes, which
    // makes this behave like a desktop file manager instead of relying on
    // /etc/fstab snapshots. The timer above is only a conservative fallback.
    if (!m_shuttingDown)
        m_udevMonitor.start(QStringLiteral("udisksctl"), {QStringLiteral("monitor")});
}

void StorageModel::queueRefresh(int delayMs)
{
    m_refreshDebounce.start(qMax(0, delayMs));
}

int StorageModel::rowForDevice(const QString &device) const
{
    for (int row = 0; row < m_entries.size(); ++row) {
        if (m_entries.at(row).device == device)
            return row;
    }
    return -1;
}

void StorageModel::toggleMount(const QString &device)
{
    const int row = rowForDevice(device);
    if (row < 0)
        return;
    const Entry entry = m_entries.at(row);
    if (!entry.canMountToggle || device.isEmpty() || m_busyDevices.contains(device))
        return;

    const bool targetMounted = !entry.mounted;
    const bool configuredMount = entry.configured;
    m_busyDevices.insert(device);
    m_entries[row].busy = true;
    emit dataChanged(index(row), index(row), {BusyRole});

    auto *process = new QProcess(this);
    if (configuredMount) {
        const QString tool = targetMounted ? QStringLiteral("/usr/bin/mount")
                                           : QStringLiteral("/usr/bin/umount");
        const QString pkexec = QStandardPaths::findExecutable(QStringLiteral("pkexec"));
        if (!pkexec.isEmpty()) {
            process->setProgram(pkexec);
            process->setArguments({tool, entry.rootPath});
        } else {
            process->setProgram(tool);
            process->setArguments({entry.rootPath});
        }
    } else {
        process->setProgram(QStringLiteral("udisksctl"));
        process->setArguments({targetMounted ? QStringLiteral("mount") : QStringLiteral("unmount"),
                               QStringLiteral("--block-device"), entry.device});
    }

    connect(process, &QProcess::errorOccurred, this,
            [this, process, device, targetMounted](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        m_busyDevices.remove(device);
        const int currentRow = rowForDevice(device);
        if (currentRow >= 0) {
            m_entries[currentRow].busy = false;
            emit dataChanged(index(currentRow), index(currentRow), {BusyRole});
        }
        emit mountOperationFinished(false, targetMounted,
                                    QStringLiteral("Mount helper could not be started"));
        process->deleteLater();
    });

    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, process, device, targetMounted](int exitCode, QProcess::ExitStatus status) {
        const bool commandSucceeded = status == QProcess::NormalExit && exitCode == 0;
        const QString detail = cleanedProcessDetail(process->readAllStandardError(),
                                                    process->readAllStandardOutput());
        m_busyDevices.remove(device);
        const int currentRow = rowForDevice(device);
        if (currentRow >= 0) {
            m_entries[currentRow].busy = false;
            emit dataChanged(index(currentRow), index(currentRow), {BusyRole});
        }

        const auto finishToggle = [this, device, targetMounted, commandSucceeded, detail] {
            bool success = commandSucceeded;
            if (!success) {
                const int latestRow = rowForDevice(device);
                if (latestRow >= 0) {
                    const Entry &latestEntry = m_entries.at(latestRow);
                    success = entryMatchesMountedState(latestEntry.rootPath, latestEntry.device,
                                                       latestEntry.source, targetMounted);
                }
            }
            emit mountOperationFinished(success, targetMounted, detail);
            queueRefresh(success ? 220 : 80);
        };

        if (commandSucceeded)
            finishToggle();
        else
            QTimer::singleShot(140, this, finishToggle);
        process->deleteLater();
    });

    process->start();
}

void StorageModel::setReorderActive(bool active)
{
    if (m_reorderActive == active)
        return;
    m_reorderActive = active;

    // Defer the pending refresh to the next event-loop turn so the QML drag
    // can fully tear down before any delegates are reset/recreated.
    if (!m_reorderActive && m_refreshQueued) {
        m_refreshQueued = false;
        queueRefresh(0);
    }
}

bool StorageModel::moveDisk(int from, int to)
{
    if (from < 0 || to < 0 || from >= m_entries.size() || to >= m_entries.size() || from == to)
        return false;

    const int destination = to > from ? to + 1 : to;
    beginMoveRows(QModelIndex(), from, from, QModelIndex(), destination);
    m_entries.move(from, to);
    endMoveRows();
    saveDiskOrder();
    return true;
}

bool StorageModel::updateDiskAppearance(const QString &stableId, const QString &title, const QString &icon)
{
    const QString key = stableId.trimmed();
    const QString cleanTitle = title.trimmed();
    const QString cleanIcon = icon.trimmed();
    if (key.isEmpty() || cleanTitle.isEmpty() || cleanIcon.isEmpty())
        return false;

    int row = -1;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).stableId == key) { row = i; break; }
    }
    if (row < 0)
        return false;

    m_customDiskTitles.insert(key, cleanTitle);
    m_customDiskIcons.insert(key, cleanIcon);
    QVariantMap titles;
    QVariantMap icons;
    for (auto it = m_customDiskTitles.constBegin(); it != m_customDiskTitles.constEnd(); ++it)
        titles.insert(it.key(), it.value());
    for (auto it = m_customDiskIcons.constBegin(); it != m_customDiskIcons.constEnd(); ++it)
        icons.insert(it.key(), it.value());
    QSettings settings(QStringLiteral("Lurviko"), QStringLiteral("Lurviko"));
    settings.setValue(QStringLiteral("navigation/diskCustomTitles"), titles);
    settings.setValue(QStringLiteral("navigation/diskCustomIcons"), icons);
    emit dataChanged(index(row), index(row), {NameRole, IconRole});
    emit favoritesChanged();
    return true;
}

void StorageModel::saveDiskOrder()
{
    QStringList nextOrder;
    nextOrder.reserve(m_entries.size() + m_diskOrder.size());
    for (const Entry &entry : std::as_const(m_entries)) {
        if (!entry.stableId.isEmpty() && !nextOrder.contains(entry.stableId))
            nextOrder.push_back(entry.stableId);
    }
    for (const QString &stableId : std::as_const(m_diskOrder)) {
        if (!stableId.isEmpty() && !nextOrder.contains(stableId))
            nextOrder.push_back(stableId);
    }
    m_diskOrder = nextOrder;
    QSettings settings(QStringLiteral("Lurviko"), QStringLiteral("Lurviko"));
    settings.setValue(QStringLiteral("navigation/diskOrder"), m_diskOrder);
}

void StorageModel::refreshNetworkCapacities()
{
    if (m_networkWatcher.isRunning()) {
        m_networkRefreshQueued = true;
        return;
    }
    QStringList paths;
    for (const Entry &entry : std::as_const(m_entries)) {
        if (entry.mounted && isNetworkStorageFilesystem(entry.fileSystem))
            paths.push_back(entry.rootPath);
    }
    if (paths.isEmpty())
        return;

    m_networkWatcher.setFuture(QtConcurrent::run([paths] {
        QVector<NetworkCapacity> capacities;
        for (const QString &path : paths) {
            const QStorageInfo storage(path);
            NetworkCapacity capacity;
            capacity.rootPath = path;
            capacity.ready = storage.isValid() && storage.isReady();
            if (capacity.ready) {
                capacity.total = qMax<qint64>(0, storage.bytesTotal());
                capacity.free = qMax<qint64>(0, storage.bytesAvailable());
                capacity.readOnly = storage.isReadOnly();
            }
            capacities.push_back(capacity);
        }
        return capacities;
    }));
}

void StorageModel::recalculateTotals()
{
    m_totalBytes = m_usedBytes = m_freeBytes = 0;
    for (const Entry &entry : std::as_const(m_entries)) {
        if (!entry.mounted || entry.total <= 0)
            continue;
        m_totalBytes += entry.total;
        m_freeBytes += entry.free;
        m_usedBytes += qMax<qint64>(0, entry.total - entry.free);
    }
}

bool StorageModel::isFavorite(const QString &rootPath) const
{
    return m_favoritePaths.contains(QDir::cleanPath(rootPath));
}

void StorageModel::setFavorite(const QString &rootPath, bool favorite)
{
    const QString clean = QDir::cleanPath(rootPath);
    if (clean.isEmpty() || clean.startsWith(QStringLiteral("/dev/")))
        return;
    const bool already = m_favoritePaths.contains(clean);
    if (already == favorite)
        return;
    if (favorite)
        m_favoritePaths.insert(clean);
    else
        m_favoritePaths.remove(clean);

    QSettings settings(QStringLiteral("Lurviko"), QStringLiteral("Lurviko"));
    QStringList saved = m_favoritePaths.values();
    std::sort(saved.begin(), saved.end(), [](const QString &a, const QString &b) {
        return a.toLower() < b.toLower();
    });
    settings.setValue(QStringLiteral("navigation/favoriteDisks"), saved);
    emit favoritesChanged();
}

void StorageModel::toggleFavorite(const QString &rootPath)
{
    setFavorite(rootPath, !isFavorite(rootPath));
}

QVariantList StorageModel::favoriteItems() const
{
    QVariantList result;
    for (const Entry &e : m_entries) {
        if (!isFavorite(e.rootPath))
            continue;
        QVariantMap item;
        item.insert(QStringLiteral("name"), m_customDiskTitles.value(e.stableId, e.name));
        item.insert(QStringLiteral("icon"), m_customDiskIcons.value(e.stableId, QStringLiteral("drive.svg")));
        item.insert(QStringLiteral("rootPath"), e.rootPath);
        item.insert(QStringLiteral("mounted"), e.mounted);
        item.insert(QStringLiteral("fileSystem"), e.fileSystem);
        item.insert(QStringLiteral("totalBytes"), e.total);
        item.insert(QStringLiteral("freeBytes"), e.free);
        item.insert(QStringLiteral("usedRatio"), e.mounted && e.total > 0
                    ? static_cast<double>(qMax<qint64>(0, e.total - e.free)) / e.total : 0.0);
        result.push_back(item);
    }
    return result;
}

QString StorageModel::formatBytes(qint64 bytes) const
{
    if (bytes <= 0)
        return QStringLiteral("0 B");
    static const char *units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 5) {
        value /= 1024.0;
        ++unit;
    }
    const int decimals = unit >= 3 ? 1 : (unit == 0 ? 0 : 1);
    return QLocale().toString(value, 'f', decimals) + " " + units[unit];
}

QString StorageModel::prettyNameForPath(const QString &path, const QString &displayName)
{
    if (!displayName.isEmpty() && displayName != path)
        return displayName;
    if (path == QStringLiteral("/"))
        return QStringLiteral("Root");
    QFileInfo fi(path);
    const QString base = fi.fileName();
    if (!base.isEmpty())
        return base;
    return path;
}

void StorageModel::upsertEntry(const Entry &entry)
{
    for (Entry &existing : m_entries) {
        const bool sameDevice = !entry.device.isEmpty() && !existing.device.isEmpty()
            && existing.device == entry.device;
        const bool samePath = !entry.rootPath.isEmpty() && existing.rootPath == entry.rootPath;
        if (!sameDevice && !samePath)
            continue;

        if (!entry.name.isEmpty()) existing.name = entry.name;
        if (!entry.rootPath.isEmpty()) existing.rootPath = entry.rootPath;
        if (!entry.fileSystem.isEmpty()) existing.fileSystem = entry.fileSystem;
        if (!entry.source.isEmpty()) existing.source = entry.source;
        if (!entry.device.isEmpty()) existing.device = entry.device;
        if (!entry.stableId.isEmpty()) existing.stableId = entry.stableId;
        existing.total = qMax(existing.total, entry.total);
        if (entry.mounted) existing.free = entry.free;
        existing.readOnly = entry.readOnly;
        existing.ready = existing.ready || entry.ready;
        existing.mounted = existing.mounted || entry.mounted;
        existing.configured = existing.configured || entry.configured;
        existing.removable = existing.removable || entry.removable;
        existing.canMountToggle = existing.canMountToggle || entry.canMountToggle;
        existing.busy = existing.busy || entry.busy;
        return;
    }
    m_entries.push_back(entry);
}
