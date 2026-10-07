#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QSet>
#include <QStringList>
#include <QUrl>
#include <QVector>
#include <QVariantMap>
#include <QVariant>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QPointer>
#include <QQueue>
#include <QHash>
#include <QSharedPointer>
#include <QMutex>
#include <QWaitCondition>
#include <KIO/ListJob>

#include <atomic>
#include <memory>

class ContentIndexModel;
class QProcess;

class DirectoryModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString location READ location WRITE setLocation NOTIFY locationChanged)
    Q_PROPERTY(ContentIndexModel *contentIndexSource READ contentIndexSource WRITE setContentIndexSource NOTIFY contentIndexSourceChanged)
    Q_PROPERTY(QString parentLocation READ parentLocation NOTIFY locationChanged)
    Q_PROPERTY(QString displayLocation READ displayLocation NOTIFY locationChanged)
    Q_PROPERTY(bool remote READ remote NOTIFY locationChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)
    Q_PROPERTY(bool showHidden READ showHidden WRITE setShowHidden NOTIFY showHiddenChanged)
    Q_PROPERTY(QString hiddenPlacement READ hiddenPlacement WRITE setHiddenPlacement NOTIFY hiddenPlacementChanged)
    Q_PROPERTY(QString sortMode READ sortMode WRITE setSortMode NOTIFY sortModeChanged)
    Q_PROPERTY(bool sortAscending READ sortAscending WRITE setSortAscending NOTIFY sortAscendingChanged)
    Q_PROPERTY(QString googleAccessToken READ googleAccessToken WRITE setGoogleAccessToken NOTIFY googleAccessTokenChanged)
    Q_PROPERTY(QString oneDriveAccessToken READ oneDriveAccessToken WRITE setOneDriveAccessToken NOTIFY oneDriveAccessTokenChanged)
    Q_PROPERTY(bool searchActive READ searchActive NOTIFY searchChanged)
    Q_PROPERTY(QString searchQuery READ searchQuery NOTIFY searchChanged)
    Q_PROPERTY(bool searchEverywhere READ searchEverywhere NOTIFY searchChanged)
    Q_PROPERTY(QString searchSessionId READ searchSessionId NOTIFY searchChanged)
    Q_PROPERTY(qint64 storageTotalBytes READ storageTotalBytes NOTIFY storageInfoChanged)
    Q_PROPERTY(qint64 storageAvailableBytes READ storageAvailableBytes NOTIFY storageInfoChanged)
    Q_PROPERTY(bool heavyIoBusy READ heavyIoBusy WRITE setHeavyIoBusy NOTIFY heavyIoBusyChanged)
    Q_PROPERTY(bool folderPreviewsEnabled READ folderPreviewsEnabled WRITE setFolderPreviewsEnabled NOTIFY folderPreviewsEnabledChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        UrlRole,
        LocalPathRole,
        IsDirRole,
        SizeRole,
        ModifiedRole,
        CreatedRole,
        SuffixRole,
        HiddenRole,
        MimeTypeRole,
        SystemIconNameRole,
        VideoInfoRole,
        ChildCountRole,
        FolderPreviewPathsRole,
        LinkTypeRole,
        LinkTargetRole
    };

    explicit DirectoryModel(QObject *parent = nullptr);
    ~DirectoryModel() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString location() const { return m_location.toString(); }
    ContentIndexModel *contentIndexSource() const { return m_contentIndexSource; }
    QString parentLocation() const;
    QString displayLocation() const;
    bool remote() const { return !m_location.isLocalFile(); }
    bool loading() const { return m_loading; }
    QString errorString() const { return m_errorString; }
    bool showHidden() const { return m_showHidden; }
    QString hiddenPlacement() const { return m_hiddenPlacement; }
    QString sortMode() const { return m_sortMode; }
    bool sortAscending() const { return m_sortAscending; }
    QString googleAccessToken() const { return m_googleAccessToken; }
    QString oneDriveAccessToken() const { return m_oneDriveAccessToken; }
    bool searchActive() const { return !m_searchQuery.isEmpty(); }
    QString searchQuery() const { return m_searchQuery; }
    bool searchEverywhere() const { return m_searchEverywhere; }
    QString searchSessionId() const { return m_activeSearchSessionId; }
    qint64 storageTotalBytes() const { return m_storageTotalBytes; }
    qint64 storageAvailableBytes() const { return m_storageAvailableBytes; }
    bool heavyIoBusy() const { return m_heavyIoBusy; }
    bool folderPreviewsEnabled() const { return m_folderPreviewsEnabled; }

    void setLocation(const QString &location);
    void setContentIndexSource(ContentIndexModel *source);
    void setShowHidden(bool value);
    void setHiddenPlacement(const QString &value);
    void setSortMode(const QString &value);
    void setSortAscending(bool value);
    void setGoogleAccessToken(const QString &value);
    void setOneDriveAccessToken(const QString &value);
    void setHeavyIoBusy(bool value);
    void setFolderPreviewsEnabled(bool value);

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void requestRefresh();
    Q_INVOKABLE QString formatBytes(qint64 bytes) const;
    Q_INVOKABLE QVariantMap itemAt(int row) const;
    Q_INVOKABLE QVariantList itemsForUrls(const QStringList &urls) const;
    Q_INVOKABLE QVariantList allItemUrls() const;
    Q_INVOKABLE int indexOfUrl(const QString &itemUrl) const;
    Q_INVOKABLE QVariantList pathCompletions(const QString &input) const;
    Q_INVOKABLE void requestVideoMetadata(int row);
    Q_INVOKABLE void googleCreateFolder(const QString &name);
    Q_INVOKABLE void googleRenameItem(const QString &itemUrl, const QString &newName);
    Q_INVOKABLE void googleTrashItem(const QString &itemUrl);
    Q_INVOKABLE void googleDeleteItem(const QString &itemUrl);
    Q_INVOKABLE void googleUploadFile(const QString &localPathOrUrl);
    Q_INVOKABLE void googleOpenItem(const QString &itemUrl);
    Q_INVOKABLE void oneDriveCreateFolder(const QString &name);
    Q_INVOKABLE void oneDriveRenameItem(const QString &itemUrl, const QString &newName);
    Q_INVOKABLE void oneDriveTrashItem(const QString &itemUrl);
    Q_INVOKABLE void oneDriveDeleteItem(const QString &itemUrl);
    Q_INVOKABLE void search(const QString &query, bool everywhere = false);
    Q_INVOKABLE void clearSearch();
    Q_INVOKABLE void cancelSearchForInput();
    Q_INVOKABLE QString detachSearchSession();
    Q_INVOKABLE bool restoreSearchSession(const QString &sessionId);
    Q_INVOKABLE void releaseSearchSession(const QString &sessionId);

signals:
    void refreshCompleted();
    void locationChanged();
    void contentIndexSourceChanged();
    void countChanged();
    void loadingChanged();
    void errorStringChanged();
    void showHiddenChanged();
    void hiddenPlacementChanged();
    void sortModeChanged();
    void sortAscendingChanged();
    void googleAccessTokenChanged();
    void oneDriveAccessTokenChanged();
    void googleOperationFinished(bool success, const QString &message);
    void oneDriveOperationFinished(bool success, const QString &message);
    void refreshAboutToStart(bool preserveView);
    void searchChanged();
    void storageInfoChanged();
    void heavyIoBusyChanged();
    void folderPreviewsEnabledChanged();

private:
    struct Entry {
        QString name;
        QUrl url;
        QString localPath;
        bool isDir = false;
        qint64 size = 0;
        QDateTime modified;
        QDateTime created;
        QString suffix;
        bool hidden = false;
        QString mimeType;
        QString systemIconName;
        QString videoInfo;
        int childCount = -1;
        QStringList folderPreviewPaths;
        QString linkType;
        QString linkTarget;
    };

    // Only this bounded mailbox crosses the worker/UI boundary. Session
    // caches and the visible model remain owned by the UI thread.
    struct SearchStream {
        QMutex mutex;
        QWaitCondition spaceAvailable;
        QQueue<QVector<Entry>> batches;
        int frontOffset = 0;
        int pendingCount = 0;
        bool finished = false;
        QString error;
    };

    struct SearchSession {
        QString id;
        QUrl originLocation;
        QString query;
        bool everywhere = false;
        bool complete = false;
        QVector<Entry> items;
        QSet<QString> seenUrls;
        QPointer<KIO::ListJob> job;
        std::shared_ptr<std::atomic_bool> canceled;
        std::shared_ptr<SearchStream> stream;
    };

    struct LocalLoadResult {
        QVector<Entry> items;
        QString absolutePath;
        QString error;
        qint64 storageTotalBytes = 0;
        qint64 storageAvailableBytes = 0;
    };

    void refreshInternal(bool preserveView);
    void setLoading(bool value);
    void setErrorString(const QString &value);
    void loadLocal(bool incremental);
    void loadIndexedCategory(bool incremental = false);
    void sortEntriesVector(QVector<Entry> &entries) const;
    void reconcileLocalEntries(QVector<Entry> target);
    void loadKio();
    void loadTrashLocal(const QString &localPath, const QUrl &trashLocation, int generation);
    QString resolveTrashLocalPath(const QUrl &trashUrl) const;
    void rememberTrashLocalEntry(const Entry &entry);
    void cancelKioListing();
    void appendKioEntries(QVector<Entry> entries);
    void loadHomeTrashPreview(int generation);
    void loadGoogleDrive();
    void loadOneDrive();
    void loadSearch(bool incremental = false);
    QSharedPointer<SearchSession> createSearchSession(const QString &query, bool everywhere);
    void appendSearchBatch(const QString &sessionId, QVector<Entry> batch);
    void queueSearchBatch(const QString &sessionId, QVector<Entry> batch);
    void drainSearchResults();
    void finishSearchSession(const QString &sessionId, const QString &error = QString());
    void cancelSearchSession(const QString &sessionId, bool remove);
    void requestGoogleDrivePage(const QString &folderId, const QString &pageToken, int generation);
    void requestGoogleDriveFolderMetadata(const QString &folderId, int generation);
    void requestOneDrivePage(const QString &folderId, const QUrl &nextUrl, int generation);
    void requestOneDriveFolderMetadata(const QString &folderId, int generation);
    void finishGoogleOperation(QNetworkReply *reply, const QString &successMessage, bool refreshAfter = true);
    void startGoogleResumableUpload(const QString &localPath);
    QString googleFileIdFromUrl(const QString &itemUrl) const;
    const Entry *entryForUrl(const QString &itemUrl) const;
    QNetworkRequest googleRequest(const QUrl &url) const;
    QString googleApiError(const QByteArray &payload, const QString &fallback) const;
    QString oneDriveFileIdFromUrl(const QString &itemUrl) const;
    QNetworkRequest oneDriveRequest(const QUrl &url) const;
    QString oneDriveApiError(const QByteArray &payload, const QString &fallback) const;
    void finishOneDriveOperation(QNetworkReply *reply, const QString &successMessage, bool refreshAfter = true);
    void sortEntries();
    void watchLocalDirectory(const QString &path);
    void watchLocalFiles(const QVector<Entry> &entries);
    void installLocalFileWatchBatch();
    void refreshLocalFileMetadata();
    void updateStorageInfo();
    void scheduleVideoMetadata();
    void startNextVideoProbe(int generation);
    void applyVideoMetadata(const QString &localPath, const QString &videoInfo, int generation);
    static QUrl normalizeLocation(const QString &location);

    QUrl m_location;
    QPointer<ContentIndexModel> m_contentIndexSource;
    QVector<Entry> m_items;
    bool m_loading = false;
    QString m_errorString;
    bool m_showHidden = false;
    QString m_hiddenPlacement = QStringLiteral("normal");
    QString m_sortMode = QStringLiteral("name");
    bool m_sortAscending = true;
    QString m_normalSortMode = QStringLiteral("name");
    bool m_normalSortAscending = true;
    QString m_categorySortMode = QStringLiteral("date");
    bool m_categorySortAscending = false;
    QString m_googleAccessToken;
    QString m_oneDriveAccessToken;
    QString m_googleFolderId = QStringLiteral("root");
    QString m_googleParentId;
    QString m_googleFolderName = QStringLiteral("Google Drive");
    int m_googleGeneration = 0;
    QString m_oneDriveFolderId = QStringLiteral("root");
    QString m_oneDriveParentId;
    QString m_oneDriveFolderName = QStringLiteral("OneDrive");
    QString m_oneDriveDriveId;
    int m_oneDriveGeneration = 0;
    QString m_searchQuery;
    bool m_searchEverywhere = false;
    int m_searchGeneration = 0;
    QString m_activeSearchSessionId;
    quint64 m_searchSessionCounter = 0;
    QHash<QString, QSharedPointer<SearchSession>> m_searchSessions;
    QTimer m_searchResultTimer;
    int m_localGeneration = 0;
    int m_kioGeneration = 0;
    QPointer<KIO::ListJob> m_kioJob;
    QHash<QString, QString> m_trashLocalPaths;
    QPointer<KIO::ListJob> m_searchJob;
    QNetworkAccessManager m_network;
    QFileSystemWatcher m_localWatcher;
    QTimer m_localRefreshTimer;
    QTimer m_localMetadataTimer;
    QTimer m_localWatchInstallTimer;
    QStringList m_localWatchQueue;
    QSet<QString> m_pendingLocalMetadataPaths;
    qint64 m_storageTotalBytes = 0;
    qint64 m_storageAvailableBytes = 0;
    bool m_heavyIoBusy = false;
    bool m_folderPreviewsEnabled = true;
    bool m_refreshDeferredByHeavyIo = false;
    QQueue<QString> m_videoProbeQueue;
    QSet<QString> m_videoProbePending;
    QPointer<QProcess> m_activeVideoProbe;
    bool m_videoProbeAvailable = false;
    int m_videoProbeGeneration = 0;
};
