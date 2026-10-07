#pragma once

#include <QObject>
#include <QFileSystemWatcher>
#include <QHash>
#include <QNetworkAccessManager>
#include <QQueue>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <functional>

class QFile;
class QNetworkReply;
class QNetworkRequest;

class OneDriveManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString accessToken READ accessToken WRITE setAccessToken NOTIFY accessTokenChanged)
    Q_PROPERTY(bool transferActive READ transferActive NOTIFY transferChanged)
    Q_PROPERTY(int transferPercent READ transferPercent NOTIFY transferChanged)
    Q_PROPERTY(qint64 transferDone READ transferDone NOTIFY transferChanged)
    Q_PROPERTY(qint64 transferTotal READ transferTotal NOTIFY transferChanged)
    Q_PROPERTY(QString transferLabel READ transferLabel NOTIFY transferChanged)
    Q_PROPERTY(QString transferDetail READ transferDetail NOTIFY transferChanged)

public:
    explicit OneDriveManager(QObject *parent = nullptr);
    ~OneDriveManager() override;

    QString accessToken() const { return m_accessToken; }
    bool transferActive() const { return m_transferActive; }
    int transferPercent() const { return m_transferPercent; }
    qint64 transferDone() const { return m_transferDone; }
    qint64 transferTotal() const { return m_transferTotal; }
    QString transferLabel() const { return m_transferLabel; }
    QString transferDetail() const { return m_transferDetail; }

    void setAccessToken(const QString &token);

    Q_INVOKABLE void uploadFile(const QString &localPathOrUrl, const QString &destinationFolderUrl);
    Q_INVOKABLE void uploadPaths(const QStringList &localPathsOrUrls, const QString &destinationFolderUrl);
    Q_INVOKABLE void moveItem(const QString &itemUrl, const QString &destinationFolderUrl);
    Q_INVOKABLE void copyItem(const QString &itemUrl, const QString &destinationFolderUrl);
    Q_INVOKABLE void downloadAndOpen(const QString &itemUrl, const QString &name, const QString &mimeType);
    Q_INVOKABLE void downloadToFolder(const QString &itemUrl, const QString &name, const QString &mimeType, const QString &destinationFolderOrUrl);
    Q_INVOKABLE void downloadFolderAsZip(const QString &itemUrl, const QString &name, const QString &destinationFolderOrUrl);

signals:
    void accessTokenChanged();
    void transferChanged();
    void operationFinished(bool success, const QString &message);
    void refreshRequested();
    void synced(const QString &localPath);

private:
    struct UploadTask { QString localPath; QString destinationParentId; };
    struct FolderDownloadTask {
        QString itemId;
        QString localPath;
        QString name;
        bool isDir = false;
    };
    struct TrackedFile {
        QString fileId;
        QString localPath;
        QString mimeType;
        QString name;
        QTimer *timer = nullptr;
        bool syncing = false;
        bool pending = false;
    };

    QString fileIdFromUrl(const QString &itemUrl) const;
    QString folderIdFromUrl(const QString &folderUrl) const;
    QString normalizeLocalPath(const QString &pathOrUrl) const;
    QString encodedName(const QString &name) const;
    QNetworkRequest request(const QUrl &url) const;
    QNetworkRequest downloadRequest(const QUrl &url) const;
    void resolveDownloadUrl(const QString &fileId, std::function<void(QUrl)> callback);
    QString apiError(const QByteArray &payload, const QString &fallback) const;
    void fail(const QString &message);
    void setTransfer(const QString &label, const QString &detail, qint64 done, qint64 total, bool active = true);
    void clearTransfer();
    void wireProgress(QNetworkReply *reply, const QString &label, const QString &detail, bool upload, qint64 base = 0, qint64 grandTotal = -1);

    void resolveFolderId(const QString &folderUrl, std::function<void(QString)> callback);
    void startUploadQueue(const QStringList &paths, const QString &destinationParentId);
    void processNextUploadTask();
    void createRemoteFolderForUpload(const UploadTask &task);
    void uploadSingleFile(const UploadTask &task);
    void uploadChunk(QFile *file, const QUrl &uploadUrl, const QString &name, qint64 offset, qint64 total);

    void processNextFolderDownloadTask();
    void listFolderDownloadChildren(const FolderDownloadTask &task, const QUrl &nextUrl = QUrl());
    void downloadFolderFile(const FolderDownloadTask &task);
    void finishFolderDownloadArchive();
    void folderDownloadFail(const QString &message, bool alreadyEmitted = false);

    void trackEditedFile(const QString &localPath, const QString &fileId, const QString &mimeType, const QString &name);
    void scheduleSync(const QString &localPath);
    void syncTrackedFile(const QString &localPath);
    void ensureWatch(const QString &localPath);

    QString m_accessToken;
    QNetworkAccessManager m_network;
    QFileSystemWatcher m_watcher;

    bool m_transferActive = false;
    int m_transferPercent = 0;
    qint64 m_transferDone = 0;
    qint64 m_transferTotal = 0;
    QString m_transferLabel;
    QString m_transferDetail;

    QQueue<UploadTask> m_uploadQueue;
    bool m_uploadQueueRunning = false;
    int m_uploadCompleted = 0;
    int m_uploadDiscovered = 0;
    QHash<QString, TrackedFile *> m_trackedFiles;

    QQueue<FolderDownloadTask> m_folderDownloadQueue;
    bool m_folderDownloadRunning = false;
    int m_folderDownloadCompleted = 0;
    int m_folderDownloadDiscovered = 0;
    QString m_folderDownloadTempBase;
    QString m_folderDownloadRootName;
    QString m_folderDownloadArchivePath;
};
