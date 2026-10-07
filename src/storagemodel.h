#pragma once

#include <QAbstractListModel>
#include <QFutureWatcher>
#include <QHash>
#include <QProcess>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

class StorageModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(qint64 totalBytes READ totalBytes NOTIFY totalsChanged)
    Q_PROPERTY(qint64 usedBytes READ usedBytes NOTIFY totalsChanged)
    Q_PROPERTY(qint64 freeBytes READ freeBytes NOTIFY totalsChanged)
    Q_PROPERTY(QVariantList favoriteItems READ favoriteItems NOTIFY favoritesChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        RootPathRole,
        FileSystemRole,
        TotalBytesRole,
        FreeBytesRole,
        UsedBytesRole,
        UsedRatioRole,
        ReadOnlyRole,
        ReadyRole,
        MountedRole,
        ConfiguredRole,
        SourceRole,
        DeviceRole,
        RemovableRole,
        CanMountToggleRole,
        BusyRole,
        StableIdRole,
        IconRole
    };

    explicit StorageModel(QObject *parent = nullptr);
    ~StorageModel() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    qint64 totalBytes() const { return m_totalBytes; }
    qint64 usedBytes() const { return m_usedBytes; }
    qint64 freeBytes() const { return m_freeBytes; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void toggleMount(const QString &device);
    Q_INVOKABLE bool moveDisk(int from, int to);
    Q_INVOKABLE bool updateDiskAppearance(const QString &stableId, const QString &title, const QString &icon);
    Q_INVOKABLE void setReorderActive(bool active);
    Q_INVOKABLE QString formatBytes(qint64 bytes) const;
    Q_INVOKABLE bool isFavorite(const QString &rootPath) const;
    Q_INVOKABLE void setFavorite(const QString &rootPath, bool favorite);
    Q_INVOKABLE void toggleFavorite(const QString &rootPath);
    QVariantList favoriteItems() const;

signals:
    void totalsChanged();
    void countChanged();
    void favoritesChanged();
    void mountOperationFinished(bool success, bool mountedNow, const QString &detail);

private:
    struct Entry {
        QString name;
        QString rootPath;
        QString fileSystem;
        QString source;
        QString device;
        qint64 total = 0;
        qint64 free = 0;
        bool readOnly = false;
        bool ready = false;
        bool mounted = false;
        bool configured = false;
        bool removable = false;
        bool canMountToggle = false;
        bool busy = false;
        QString stableId;
    };
    struct NetworkCapacity {
        QString rootPath;
        qint64 total = 0;
        qint64 free = 0;
        bool readOnly = false;
        bool ready = false;
    };

    static QVector<Entry> discoverLiveEntries();
    static QString prettyNameForPath(const QString &path, const QString &displayName);
    void upsertEntry(const Entry &entry);
    void refreshNetworkCapacities();
    void recalculateTotals();
    void startDeviceMonitor();
    void queueRefresh(int delayMs = 180);
    int rowForDevice(const QString &device) const;
    void saveDiskOrder();

    QVector<Entry> m_entries;
    qint64 m_totalBytes = 0;
    qint64 m_usedBytes = 0;
    qint64 m_freeBytes = 0;
    QSet<QString> m_favoritePaths;
    QSet<QString> m_busyDevices;
    QStringList m_diskOrder;
    QHash<QString, QString> m_customDiskTitles;
    QHash<QString, QString> m_customDiskIcons;
    QFutureWatcher<QVector<Entry>> m_refreshWatcher;
    QFutureWatcher<QVector<NetworkCapacity>> m_networkWatcher;
    QProcess m_udevMonitor;
    QTimer m_pollTimer;
    QTimer m_refreshDebounce;
    bool m_refreshQueued = false;
    bool m_networkRefreshQueued = false;
    bool m_reorderActive = false;
    bool m_shuttingDown = false;
};
