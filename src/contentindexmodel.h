#pragma once

#include <QAbstractListModel>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QStringList>
#include <QHash>
#include <QSet>
#include <QTimer>
#include <QThreadPool>
#include <QVariantList>
#include <functional>
#include <atomic>
#include <memory>

class ContentIndexModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(bool indexing READ indexing NOTIFY indexingChanged)
    Q_PROPERTY(bool scanProgressVisible READ scanProgressVisible NOTIFY scanProgressVisibilityChanged)
    Q_PROPERTY(int totalIndexed READ totalIndexed NOTIFY indexChanged)
    Q_PROPERTY(int scanProcessedDirectories READ scanProcessedDirectories NOTIFY scanProgressChanged)
    Q_PROPERTY(int scanPendingDirectories READ scanPendingDirectories NOTIFY scanProgressChanged)
    Q_PROPERTY(int scanVisitedEntries READ scanVisitedEntries NOTIFY scanProgressChanged)
    Q_PROPERTY(int scanMatchedFiles READ scanMatchedFiles NOTIFY scanProgressChanged)
    Q_PROPERTY(int scanProgressPercent READ scanProgressPercent NOTIFY scanProgressChanged)
    Q_PROPERTY(int categoryInteractionRevision READ categoryInteractionRevision NOTIFY categoryInteractionRevisionChanged)

public:
    enum Roles {
        KeyRole = Qt::UserRole + 1,
        TitleTrRole,
        TitleEnRole,
        IconRole,
        CountRole,
        VisibleRole,
        IndexHiddenRole
    };

    explicit ContentIndexModel(QObject *parent = nullptr);
    ~ContentIndexModel() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool indexing() const { return m_indexing; }
    bool scanProgressVisible() const { return m_scanProgressVisible; }
    int totalIndexed() const { return m_files.size(); }
    int scanProcessedDirectories() const { return m_scanProcessedDirectories; }
    int scanPendingDirectories() const { return m_scanPendingDirectories; }
    int scanVisitedEntries() const { return m_scanVisitedEntries; }
    int scanMatchedFiles() const { return m_scanMatchedFiles; }
    int scanProgressPercent() const;
    int categoryInteractionRevision() const { return m_categoryInteractionRevision; }

    Q_INVOKABLE QVariantList filesForCategory(const QString &key) const;
    Q_INVOKABLE int countForCategory(const QString &key) const;
    Q_INVOKABLE qint64 bytesForCategory(const QString &key) const;
    Q_INVOKABLE qint64 homeStorageTotalBytes() const;
    Q_INVOKABLE qint64 homeStorageAvailableBytes() const;
    Q_INVOKABLE QString titleForCategory(const QString &key, const QString &language) const;
    Q_INVOKABLE void moveCategory(int from, int to);
    Q_INVOKABLE bool updateCategoryAppearance(const QString &key, const QString &title, const QString &icon);
    Q_INVOKABLE void setCategoryVisible(const QString &key, bool visible);
    Q_INVOKABLE void setCategoryIndexHidden(const QString &key, bool enabled);
    Q_INVOKABLE bool categoryIndexHidden(const QString &key) const;
    Q_INVOKABLE bool categorySingleClickOpen(const QString &key) const;
    Q_INVOKABLE void setCategorySingleClickOpen(const QString &key, bool enabled);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void refreshCategory(const QString &category);
    Q_INVOKABLE QStringList mediaRoots(const QString &category) const;
    Q_INVOKABLE QString defaultMediaRoot() const;
    Q_INVOKABLE bool setMediaRoots(const QString &category, const QVariantList &roots);
    Q_INVOKABLE QStringList ignoreRules(const QString &category) const;
    Q_INVOKABLE QStringList defaultIgnoreRules(const QString &category) const;
    Q_INVOKABLE bool setCategoryIndexSettings(const QString &category, const QVariantList &roots,
                                               bool indexHidden, const QVariantList &ignoreRules);

signals:
    void indexingChanged();
    void scanProgressVisibilityChanged();
    void indexChanged();
    void categoryFilesChanged(const QStringList &categories);
    void scanProgressChanged();
    void mediaRootsChanged(const QString &category);
    void categoryIndexSettingsChanged(const QString &category);
    void categoryInteractionRevisionChanged();

private:
    struct Category {
        QString key;
        QString titleTr;
        QString titleEn;
        QString icon;
        QString customTitle;
        QString customIcon;
        bool visible = true;
        bool indexHidden = false;
        bool singleClickOpen = false;
    };

    struct IndexedFile {
        QString path;
        QString category;
        qint64 size = 0;
        qint64 modifiedMs = 0;
    };

    struct ScanResult {
        QHash<QString, IndexedFile> files;
        QHash<QString, int> counts;
        QStringList directories;
    };

    static QString categoryForPath(const QString &path);
    using ProgressCallback = std::function<void(int, int, int, int, const QHash<QString, int> &)>;
    static ScanResult scanTree(const QString &rootPath, const QSet<QString> &hiddenCategories,
                               const QHash<QString, QStringList> &categoryRoots,
                               const QHash<QString, QStringList> &ignoreRules,
                               const QSet<QString> &categoryFilter = {},
                               const ProgressCallback &progress = {},
                               const std::shared_ptr<std::atomic_bool> &cancel = {},
                               bool background = false);
    static bool shouldSkipDirectory(const QString &path);
    QSet<QString> hiddenCategories() const;

    void loadCategorySettings();
    void saveCategorySettings() const;
    void loadMediaRootSettings();
    void saveMediaRootSettings() const;
    void loadIgnoreRuleSettings();
    void saveIgnoreRuleSettings() const;
    QString mediaRootsSignature() const;
    bool isKnownCategory(const QString &category) const;
    static bool pathWithinRoot(const QString &path, const QString &root);
    bool pathAllowedForMediaCategory(const QString &path, const QString &category) const;
    void loadIndex();
    void startIndexLoad();
    static ScanResult loadIndexFile(const QString &path, const QString &homePath, const QString &mediaRootsSignature);
    void saveIndex() const;
    void startFullScan(bool showProgress = false);
    void startCategoryScan(const QString &category, bool showProgress = true);
    void startPendingScan();
    void applyFullScan(const ScanResult &result);
    void applyCategoryScan(const QString &category, const ScanResult &result);
    void applySubtreeScan(const QString &rootPath, const ScanResult &result);
    void rebuildWatchers(const QStringList &directories);
    void addWatchers(const QStringList &directories);
    void queueWatchers(const QStringList &directories);
    void installWatcherBatch();
    void setIndexing(bool value);
    void setScanProgressVisible(bool visible);
    void notifyCountsChanged(const QSet<QString> &changedCategories = {});
    void probeMediaRootAvailability();
    QString indexFilePath() const;

    QVector<Category> m_categories;
    QHash<QString, QStringList> m_mediaRoots;
    QHash<QString, QStringList> m_ignoreRules;
    QHash<QString, IndexedFile> m_files;
    QHash<QString, int> m_counts;
    QHash<QString, int> m_liveCounts;
    QFileSystemWatcher m_watcher;
    QTimer m_changeDebounce;
    QTimer m_saveDebounce;
    QTimer m_watchInstallTimer;
    QTimer m_mediaRootProbeTimer;
    QStringList m_watchQueue;
    QSet<QString> m_queuedWatchers;
    QSet<QString> m_pendingRoots;
    QSet<QString> m_pendingCategoryRefreshes;
    QHash<QString, bool> m_mediaRootAvailability;
    bool m_indexing = false;
    bool m_scanProgressVisible = false;
    int m_scanProcessedDirectories = 0;
    int m_scanPendingDirectories = 0;
    int m_scanVisitedEntries = 0;
    int m_scanMatchedFiles = 0;
    quint64 m_scanGeneration = 0;
    bool m_scanRunning = false;
    bool m_fullRescanRequested = false;
    bool m_fullRescanShowProgress = false;
    QString m_activeSubtree;
    QString m_activeCategory;
    // Process-lifetime pools are intentionally not destroyed with the model:
    // shutdown must never wait for background indexing/JSON work. Workers only
    // capture value snapshots or QPointer, so letting the OS tear them down at
    // process exit is safe.
    QThreadPool *m_scanPool = nullptr;
    QThreadPool *m_ioPool = nullptr;
    QFutureWatcher<ScanResult> m_loadWatcher;
    QFutureWatcher<ScanResult> m_scanWatcher;
    std::shared_ptr<std::atomic_bool> m_scanCancel;
    QFutureWatcher<void> m_saveWatcher;
    bool m_savePending = false;
    int m_categoryInteractionRevision = 0;
};
