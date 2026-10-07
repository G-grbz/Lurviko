#include "googledrivemanager.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QStandardPaths>
#include <QUuid>
#include <QUrlQuery>
#include <utility>


namespace {
QString safeCloudName(QString name, const QString &fallback)
{
    name = name.trimmed();
    const QString bad = QStringLiteral("/\\:*?\"<>|");
    for (const QChar ch : bad)
        name.replace(ch, QLatin1Char('_'));
    if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral(".."))
        name = fallback;
    return name;
}

QString uniqueCloudPath(const QString &parentPath, const QString &requestedName, bool directory)
{
    const QString safe = safeCloudName(requestedName, directory ? QStringLiteral("Folder") : QStringLiteral("File"));
    QDir parent(parentPath);
    QString candidate = parent.filePath(safe);
    if (!QFileInfo::exists(candidate))
        return candidate;

    QFileInfo info(safe);
    const QString suffix = directory ? QString() : info.completeSuffix();
    QString base = directory ? safe : info.completeBaseName();
    if (base.isEmpty())
        base = safe;
    int n = 1;
    do {
        const QString fileName = suffix.isEmpty()
            ? QStringLiteral("%1 (%2)").arg(base).arg(n++)
            : QStringLiteral("%1 (%2).%3").arg(base).arg(n++).arg(suffix);
        candidate = parent.filePath(fileName);
    } while (QFileInfo::exists(candidate));
    return candidate;
}

QString uniqueZipPath(const QString &destinationFolder, const QString &baseName)
{
    const QString safe = safeCloudName(baseName, QStringLiteral("Google Drive Folder"));
    QDir dir(destinationFolder);
    QString path = dir.filePath(safe + QStringLiteral(".zip"));
    int n = 1;
    while (QFileInfo::exists(path))
        path = dir.filePath(QStringLiteral("%1 (%2).zip").arg(safe).arg(n++));
    return path;
}

QString googleExportMime(const QString &mimeType, QString *extension)
{
    if (mimeType == QStringLiteral("application/vnd.google-apps.document")) {
        if (extension) *extension = QStringLiteral(".docx");
        return QStringLiteral("application/vnd.openxmlformats-officedocument.wordprocessingml.document");
    }
    if (mimeType == QStringLiteral("application/vnd.google-apps.spreadsheet")) {
        if (extension) *extension = QStringLiteral(".xlsx");
        return QStringLiteral("application/vnd.openxmlformats-officedocument.spreadsheetml.sheet");
    }
    if (mimeType == QStringLiteral("application/vnd.google-apps.presentation")) {
        if (extension) *extension = QStringLiteral(".pptx");
        return QStringLiteral("application/vnd.openxmlformats-officedocument.presentationml.presentation");
    }
    if (mimeType == QStringLiteral("application/vnd.google-apps.drawing")) {
        if (extension) *extension = QStringLiteral(".pdf");
        return QStringLiteral("application/pdf");
    }
    return {};
}
}

GoogleDriveManager::GoogleDriveManager(QObject *parent)
    : QObject(parent)
{
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString &path) {
        scheduleSync(path);
        QTimer::singleShot(250, this, [this, path]() { ensureWatch(path); });
        QTimer::singleShot(1000, this, [this, path]() { ensureWatch(path); });
    });
}

GoogleDriveManager::~GoogleDriveManager()
{
    for (TrackedFile *tracked : std::as_const(m_trackedFiles))
        delete tracked;
    m_trackedFiles.clear();
}

void GoogleDriveManager::setAccessToken(const QString &token)
{
    if (m_accessToken == token)
        return;
    m_accessToken = token;
    emit accessTokenChanged();
}

QString GoogleDriveManager::fileIdFromUrl(const QString &itemUrl) const
{
    const QUrl url(itemUrl);
    if (url.scheme() != QStringLiteral("gdrive"))
        return {};
    if (url.host() != QStringLiteral("file") && url.host() != QStringLiteral("folder"))
        return {};
    QString id = url.path();
    if (id.startsWith('/'))
        id.remove(0, 1);
    return id;
}

QString GoogleDriveManager::folderIdFromUrl(const QString &folderUrl) const
{
    const QUrl url(folderUrl);
    if (url.scheme() != QStringLiteral("gdrive"))
        return {};
    if (url.host().isEmpty() || url.host() == QStringLiteral("root"))
        return QStringLiteral("root");
    if (url.host() != QStringLiteral("folder"))
        return {};
    QString id = url.path();
    if (id.startsWith('/'))
        id.remove(0, 1);
    return id;
}

QString GoogleDriveManager::normalizeLocalPath(const QString &pathOrUrl) const
{
    const QUrl url(pathOrUrl);
    if (url.isLocalFile())
        return url.toLocalFile();
    if (pathOrUrl.startsWith(QStringLiteral("file://")))
        return QUrl(pathOrUrl).toLocalFile();
    return pathOrUrl;
}

QNetworkRequest GoogleDriveManager::request(const QUrl &url) const
{
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + m_accessToken.toUtf8());
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("g-File/0.5.10"));
    return request;
}

QString GoogleDriveManager::apiError(const QByteArray &payload, const QString &fallback) const
{
    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (doc.isObject()) {
        const QJsonObject errorObj = doc.object().value(QStringLiteral("error")).toObject();
        const QString message = errorObj.value(QStringLiteral("message")).toString();
        if (!message.isEmpty())
            return message;
    }
    return fallback;
}

void GoogleDriveManager::fail(const QString &message)
{
    clearTransfer();
    emit operationFinished(false, message);
}

void GoogleDriveManager::setTransfer(const QString &label, const QString &detail, qint64 done, qint64 total, bool active)
{
    m_transferActive = active;
    m_transferLabel = label;
    m_transferDetail = detail;
    m_transferDone = qMax<qint64>(0, done);
    m_transferTotal = qMax<qint64>(0, total);
    m_transferPercent = m_transferTotal > 0
        ? qBound(0, static_cast<int>((m_transferDone * 100) / m_transferTotal), 100)
        : 0;
    emit transferChanged();
}

void GoogleDriveManager::clearTransfer()
{
    m_transferActive = false;
    m_transferPercent = 0;
    m_transferDone = 0;
    m_transferTotal = 0;
    m_transferLabel.clear();
    m_transferDetail.clear();
    emit transferChanged();
}

void GoogleDriveManager::wireProgress(QNetworkReply *reply, const QString &label, const QString &detail, bool upload)
{
    if (upload) {
        connect(reply, &QNetworkReply::uploadProgress, this, [this, label, detail](qint64 sent, qint64 total) {
            setTransfer(label, detail, sent, total);
        });
    } else {
        connect(reply, &QNetworkReply::downloadProgress, this, [this, label, detail](qint64 received, qint64 total) {
            setTransfer(label, detail, received, total);
        });
    }
}

void GoogleDriveManager::uploadFile(const QString &localPathOrUrl, const QString &destinationFolderUrl)
{
    uploadPaths(QStringList{localPathOrUrl}, destinationFolderUrl);
}

void GoogleDriveManager::uploadPaths(const QStringList &localPathsOrUrls, const QString &destinationFolderUrl)
{
    if (m_accessToken.isEmpty()) {
        fail(QStringLiteral("Google Drive is not connected."));
        return;
    }
    const QString parentId = folderIdFromUrl(destinationFolderUrl);
    if (parentId.isEmpty()) {
        fail(QStringLiteral("Invalid Google Drive destination folder."));
        return;
    }

    QStringList paths;
    for (const QString &value : localPathsOrUrls) {
        const QString path = normalizeLocalPath(value);
        if (QFileInfo::exists(path))
            paths.push_back(path);
    }
    if (paths.isEmpty()) {
        fail(QStringLiteral("No valid local files or folders to upload."));
        return;
    }
    startUploadQueue(paths, parentId);
}

void GoogleDriveManager::startUploadQueue(const QStringList &paths, const QString &destinationParentId)
{
    if (m_uploadQueueRunning) {
        fail(QStringLiteral("Another Google Drive upload is already running."));
        return;
    }
    m_uploadQueue.clear();
    m_uploadCompleted = 0;
    m_uploadDiscovered = paths.size();
    for (const QString &path : paths)
        m_uploadQueue.enqueue({path, destinationParentId});
    m_uploadQueueRunning = true;
    processNextUploadTask();
}

void GoogleDriveManager::processNextUploadTask()
{
    if (m_uploadQueue.isEmpty()) {
        m_uploadQueueRunning = false;
        clearTransfer();
        emit operationFinished(true, QStringLiteral("Upload completed."));
        emit refreshRequested();
        return;
    }

    const UploadTask task = m_uploadQueue.dequeue();
    const QFileInfo info(task.localPath);
    if (!info.exists()) {
        ++m_uploadCompleted;
        processNextUploadTask();
        return;
    }
    if (info.isDir())
        createRemoteFolderForUpload(task);
    else
        uploadSingleFile(task);
}

void GoogleDriveManager::createRemoteFolderForUpload(const UploadTask &task)
{
    const QFileInfo info(task.localPath);
    setTransfer(QStringLiteral("Uploading folder"), info.fileName(), m_uploadCompleted, qMax(1, m_uploadDiscovered));

    QUrl url(QStringLiteral("https://www.googleapis.com/drive/v3/files"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("supportsAllDrives"), QStringLiteral("true"));
    query.addQueryItem(QStringLiteral("fields"), QStringLiteral("id,name"));
    url.setQuery(query);

    QJsonObject metadata;
    metadata.insert(QStringLiteral("name"), info.fileName());
    metadata.insert(QStringLiteral("mimeType"), QStringLiteral("application/vnd.google-apps.folder"));
    metadata.insert(QStringLiteral("parents"), QJsonArray{task.destinationParentId});

    QNetworkRequest req = request(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json; charset=UTF-8"));
    QNetworkReply *reply = m_network.post(req, QJsonDocument(metadata).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, task]() {
        const QByteArray payload = reply->readAll();
        const auto error = reply->error();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString fallback = reply->errorString();
        reply->deleteLater();
        if (error != QNetworkReply::NoError || status >= 400) {
            m_uploadQueue.clear();
            m_uploadQueueRunning = false;
            fail(apiError(payload, fallback));
            return;
        }

        const QString folderId = QJsonDocument::fromJson(payload).object().value(QStringLiteral("id")).toString();
        if (folderId.isEmpty()) {
            m_uploadQueue.clear();
            m_uploadQueueRunning = false;
            fail(QStringLiteral("Google Drive did not return the created folder id."));
            return;
        }

        const QDir dir(task.localPath);
        const QFileInfoList children = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name);
        m_uploadDiscovered += children.size();
        for (const QFileInfo &child : children)
            m_uploadQueue.enqueue({child.absoluteFilePath(), folderId});
        ++m_uploadCompleted;
        processNextUploadTask();
    });
}

void GoogleDriveManager::uploadSingleFile(const UploadTask &task)
{
    const QFileInfo info(task.localPath);
    const QString mimeType = QMimeDatabase().mimeTypeForFile(info).name();
    setTransfer(QStringLiteral("Uploading"), info.fileName(), 0, info.size());

    QUrl url(QStringLiteral("https://www.googleapis.com/upload/drive/v3/files"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("uploadType"), QStringLiteral("resumable"));
    query.addQueryItem(QStringLiteral("supportsAllDrives"), QStringLiteral("true"));
    query.addQueryItem(QStringLiteral("fields"), QStringLiteral("id,name,size"));
    url.setQuery(query);

    QJsonObject metadata;
    metadata.insert(QStringLiteral("name"), info.fileName());
    metadata.insert(QStringLiteral("parents"), QJsonArray{task.destinationParentId});

    QNetworkRequest req = request(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json; charset=UTF-8"));
    req.setRawHeader("X-Upload-Content-Type", mimeType.toUtf8());
    req.setRawHeader("X-Upload-Content-Length", QByteArray::number(info.size()));

    QNetworkReply *initReply = m_network.post(req, QJsonDocument(metadata).toJson(QJsonDocument::Compact));
    connect(initReply, &QNetworkReply::finished, this, [this, initReply, task, mimeType]() {
        const QByteArray payload = initReply->readAll();
        const auto error = initReply->error();
        const int status = initReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QUrl sessionUrl = initReply->header(QNetworkRequest::LocationHeader).toUrl();
        const QString fallback = initReply->errorString();
        initReply->deleteLater();

        if (error != QNetworkReply::NoError || status >= 400 || !sessionUrl.isValid()) {
            m_uploadQueue.clear();
            m_uploadQueueRunning = false;
            fail(apiError(payload, fallback));
            return;
        }

        auto *file = new QFile(task.localPath);
        if (!file->open(QIODevice::ReadOnly)) {
            m_uploadQueue.clear();
            m_uploadQueueRunning = false;
            const QString message = file->errorString();
            delete file;
            fail(message);
            return;
        }

        QNetworkRequest uploadReq(sessionUrl);
        uploadReq.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + m_accessToken.toUtf8());
        uploadReq.setHeader(QNetworkRequest::ContentTypeHeader, mimeType);
        uploadReq.setHeader(QNetworkRequest::ContentLengthHeader, file->size());
        if (file->size() > 0) {
            uploadReq.setRawHeader("Content-Range",
                QStringLiteral("bytes 0-%1/%2").arg(file->size() - 1).arg(file->size()).toUtf8());
        }
        QNetworkReply *uploadReply = m_network.put(uploadReq, file);
        file->setParent(uploadReply);
        wireProgress(uploadReply, QStringLiteral("Uploading"), QFileInfo(task.localPath).fileName(), true);
        connect(uploadReply, &QNetworkReply::finished, this, [this, uploadReply]() {
            const QByteArray body = uploadReply->readAll();
            const auto uploadError = uploadReply->error();
            const int status = uploadReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QString fallback = uploadReply->errorString();
            uploadReply->deleteLater();
            if (uploadError != QNetworkReply::NoError || status >= 400) {
                m_uploadQueue.clear();
                m_uploadQueueRunning = false;
                fail(apiError(body, fallback));
                return;
            }
            ++m_uploadCompleted;
            processNextUploadTask();
        });
    });
}

void GoogleDriveManager::moveItem(const QString &itemUrl, const QString &destinationFolderUrl)
{
    const QString id = fileIdFromUrl(itemUrl);
    const QString destinationId = folderIdFromUrl(destinationFolderUrl);
    if (id.isEmpty() || destinationId.isEmpty()) {
        fail(QStringLiteral("Invalid Google Drive move source or destination."));
        return;
    }

    QUrl metadataUrl(QStringLiteral("https://www.googleapis.com/drive/v3/files/%1").arg(id));
    QUrlQuery metadataQuery;
    metadataQuery.addQueryItem(QStringLiteral("fields"), QStringLiteral("id,parents,name"));
    metadataQuery.addQueryItem(QStringLiteral("supportsAllDrives"), QStringLiteral("true"));
    metadataUrl.setQuery(metadataQuery);
    setTransfer(QStringLiteral("Moving"), QStringLiteral("Google Drive item"), 0, 0);

    QNetworkReply *metaReply = m_network.get(request(metadataUrl));
    connect(metaReply, &QNetworkReply::finished, this, [this, metaReply, id, destinationId]() {
        const QByteArray payload = metaReply->readAll();
        const auto error = metaReply->error();
        const int status = metaReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString fallback = metaReply->errorString();
        metaReply->deleteLater();
        if (error != QNetworkReply::NoError || status >= 400) {
            fail(apiError(payload, fallback));
            return;
        }
        const QJsonObject obj = QJsonDocument::fromJson(payload).object();
        const QJsonArray parentsArray = obj.value(QStringLiteral("parents")).toArray();
        QStringList parents;
        for (const QJsonValue &v : parentsArray)
            parents.push_back(v.toString());
        if (parents.contains(destinationId)) {
            clearTransfer();
            emit operationFinished(true, QStringLiteral("Item is already in that Google Drive folder."));
            return;
        }

        QUrl patchUrl(QStringLiteral("https://www.googleapis.com/drive/v3/files/%1").arg(id));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("addParents"), destinationId);
        if (!parents.isEmpty())
            query.addQueryItem(QStringLiteral("removeParents"), parents.join(','));
        query.addQueryItem(QStringLiteral("supportsAllDrives"), QStringLiteral("true"));
        query.addQueryItem(QStringLiteral("fields"), QStringLiteral("id,name,parents"));
        patchUrl.setQuery(query);
        QNetworkReply *reply = m_network.sendCustomRequest(request(patchUrl), QByteArrayLiteral("PATCH"), QByteArray());
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            const QByteArray body = reply->readAll();
            const auto err = reply->error();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QString fallback = reply->errorString();
            reply->deleteLater();
            clearTransfer();
            if (err != QNetworkReply::NoError || status >= 400) {
                emit operationFinished(false, apiError(body, fallback));
                return;
            }
            emit operationFinished(true, QStringLiteral("Moved in Google Drive."));
            emit refreshRequested();
        });
    });
}

void GoogleDriveManager::copyItem(const QString &itemUrl, const QString &destinationFolderUrl)
{
    const QString id = fileIdFromUrl(itemUrl);
    const QString destinationId = folderIdFromUrl(destinationFolderUrl);
    if (id.isEmpty() || destinationId.isEmpty()) {
        fail(QStringLiteral("Invalid Google Drive copy source or destination."));
        return;
    }
    if (m_copyQueueRunning) {
        fail(QStringLiteral("Another Google Drive copy is already running."));
        return;
    }

    m_copyQueue.clear();
    m_copyCompleted = 0;
    m_copyDiscovered = 1;
    m_copyQueue.enqueue({id, destinationId});
    m_copyQueueRunning = true;
    processNextCopyTask();
}

void GoogleDriveManager::processNextCopyTask()
{
    if (m_copyQueue.isEmpty()) {
        m_copyQueueRunning = false;
        clearTransfer();
        emit operationFinished(true, QStringLiteral("Copied in Google Drive."));
        emit refreshRequested();
        return;
    }
    const CopyTask task = m_copyQueue.dequeue();
    setTransfer(QStringLiteral("Copying"), QStringLiteral("Google Drive"), m_copyCompleted, qMax(1, m_copyDiscovered));
    fetchAndCopyTask(task);
}

void GoogleDriveManager::fetchAndCopyTask(const CopyTask &task)
{
    QUrl url(QStringLiteral("https://www.googleapis.com/drive/v3/files/%1").arg(task.sourceId));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("fields"), QStringLiteral("id,name,mimeType"));
    query.addQueryItem(QStringLiteral("supportsAllDrives"), QStringLiteral("true"));
    url.setQuery(query);
    QNetworkReply *reply = m_network.get(request(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, task]() {
        const QByteArray payload = reply->readAll();
        const auto error = reply->error();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString fallback = reply->errorString();
        reply->deleteLater();
        if (error != QNetworkReply::NoError || status >= 400) {
            m_copyQueue.clear();
            m_copyQueueRunning = false;
            fail(apiError(payload, fallback));
            return;
        }
        const QJsonObject obj = QJsonDocument::fromJson(payload).object();
        const QString mime = obj.value(QStringLiteral("mimeType")).toString();
        const QString name = obj.value(QStringLiteral("name")).toString();
        if (mime == QStringLiteral("application/vnd.google-apps.folder")) {
            createCopiedFolder(task, name);
            return;
        }

        QUrl copyUrl(QStringLiteral("https://www.googleapis.com/drive/v3/files/%1/copy").arg(task.sourceId));
        QUrlQuery copyQuery;
        copyQuery.addQueryItem(QStringLiteral("supportsAllDrives"), QStringLiteral("true"));
        copyQuery.addQueryItem(QStringLiteral("fields"), QStringLiteral("id,name"));
        copyUrl.setQuery(copyQuery);
        QJsonObject body;
        body.insert(QStringLiteral("parents"), QJsonArray{task.destinationParentId});
        QNetworkRequest req = request(copyUrl);
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json; charset=UTF-8"));
        QNetworkReply *copyReply = m_network.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
        connect(copyReply, &QNetworkReply::finished, this, [this, copyReply]() {
            const QByteArray body = copyReply->readAll();
            const auto err = copyReply->error();
            const int status = copyReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QString fallback = copyReply->errorString();
            copyReply->deleteLater();
            if (err != QNetworkReply::NoError || status >= 400) {
                m_copyQueue.clear();
                m_copyQueueRunning = false;
                fail(apiError(body, fallback));
                return;
            }
            ++m_copyCompleted;
            processNextCopyTask();
        });
    });
}

void GoogleDriveManager::createCopiedFolder(const CopyTask &task, const QString &name)
{
    QUrl url(QStringLiteral("https://www.googleapis.com/drive/v3/files"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("supportsAllDrives"), QStringLiteral("true"));
    query.addQueryItem(QStringLiteral("fields"), QStringLiteral("id,name"));
    url.setQuery(query);
    QJsonObject metadata;
    metadata.insert(QStringLiteral("name"), name);
    metadata.insert(QStringLiteral("mimeType"), QStringLiteral("application/vnd.google-apps.folder"));
    metadata.insert(QStringLiteral("parents"), QJsonArray{task.destinationParentId});
    QNetworkRequest req = request(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json; charset=UTF-8"));
    QNetworkReply *reply = m_network.post(req, QJsonDocument(metadata).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, task]() {
        const QByteArray payload = reply->readAll();
        const auto error = reply->error();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString fallback = reply->errorString();
        reply->deleteLater();
        if (error != QNetworkReply::NoError || status >= 400) {
            m_copyQueue.clear();
            m_copyQueueRunning = false;
            fail(apiError(payload, fallback));
            return;
        }
        const QString newFolderId = QJsonDocument::fromJson(payload).object().value(QStringLiteral("id")).toString();
        if (newFolderId.isEmpty()) {
            m_copyQueue.clear();
            m_copyQueueRunning = false;
            fail(QStringLiteral("Google Drive did not return the copied folder id."));
            return;
        }
        ++m_copyCompleted;
        listChildrenForCopy(task.sourceId, newFolderId);
    });
}

void GoogleDriveManager::listChildrenForCopy(const QString &sourceFolderId, const QString &destinationFolderId, const QString &pageToken)
{
    QUrl url(QStringLiteral("https://www.googleapis.com/drive/v3/files"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("q"), QStringLiteral("'%1' in parents and trashed = false").arg(sourceFolderId));
    query.addQueryItem(QStringLiteral("fields"), QStringLiteral("nextPageToken,files(id)"));
    query.addQueryItem(QStringLiteral("pageSize"), QStringLiteral("1000"));
    query.addQueryItem(QStringLiteral("supportsAllDrives"), QStringLiteral("true"));
    query.addQueryItem(QStringLiteral("includeItemsFromAllDrives"), QStringLiteral("true"));
    if (!pageToken.isEmpty())
        query.addQueryItem(QStringLiteral("pageToken"), pageToken);
    url.setQuery(query);
    QNetworkReply *reply = m_network.get(request(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, sourceFolderId, destinationFolderId]() {
        const QByteArray payload = reply->readAll();
        const auto error = reply->error();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString fallback = reply->errorString();
        reply->deleteLater();
        if (error != QNetworkReply::NoError || status >= 400) {
            m_copyQueue.clear();
            m_copyQueueRunning = false;
            fail(apiError(payload, fallback));
            return;
        }
        const QJsonObject root = QJsonDocument::fromJson(payload).object();
        const QJsonArray files = root.value(QStringLiteral("files")).toArray();
        m_copyDiscovered += files.size();
        for (const QJsonValue &value : files) {
            const QString childId = value.toObject().value(QStringLiteral("id")).toString();
            if (!childId.isEmpty())
                m_copyQueue.enqueue({childId, destinationFolderId});
        }
        const QString nextPageToken = root.value(QStringLiteral("nextPageToken")).toString();
        if (!nextPageToken.isEmpty()) {
            listChildrenForCopy(sourceFolderId, destinationFolderId, nextPageToken);
            return;
        }
        processNextCopyTask();
    });
}

void GoogleDriveManager::downloadAndOpen(const QString &itemUrl, const QString &name, const QString &mimeType)
{
    const QString id = fileIdFromUrl(itemUrl);
    if (id.isEmpty()) {
        fail(QStringLiteral("Invalid Google Drive file."));
        return;
    }
    if (mimeType.startsWith(QStringLiteral("application/vnd.google-apps."))) {
        QUrl webUrl(QStringLiteral("https://drive.google.com/open"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("id"), id);
        webUrl.setQuery(query);
        QDesktopServices::openUrl(webUrl);
        return;
    }

    // If this file is already being edited locally, reopen the watched copy instead
    // of downloading over possible unsaved local changes.
    for (TrackedFile *tracked : std::as_const(m_trackedFiles)) {
        if (tracked && tracked->fileId == id && QFileInfo::exists(tracked->localPath)) {
            ensureWatch(tracked->localPath);
            QDesktopServices::openUrl(QUrl::fromLocalFile(tracked->localPath));
            emit operationFinished(true, QStringLiteral("Opened the existing synced local copy."));
            return;
        }
    }

    const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                           + QStringLiteral("/google-drive-edit");
    QDir().mkpath(cacheDir);
    QString safeName = name;
    safeName.replace('/', '_');
    if (safeName.isEmpty())
        safeName = id;
    const QString destination = QDir(cacheDir).filePath(id + QStringLiteral("-") + safeName);

    QUrl url(QStringLiteral("https://www.googleapis.com/drive/v3/files/%1").arg(id));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("alt"), QStringLiteral("media"));
    query.addQueryItem(QStringLiteral("supportsAllDrives"), QStringLiteral("true"));
    url.setQuery(query);
    QNetworkReply *reply = m_network.get(request(url));
    auto *file = new QFile(destination, reply);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QString message = file->errorString();
        reply->abort();
        reply->deleteLater();
        fail(message);
        return;
    }
    setTransfer(QStringLiteral("Downloading"), name, 0, 0);
    wireProgress(reply, QStringLiteral("Downloading"), name, false);
    connect(reply, &QIODevice::readyRead, this, [reply, file]() { file->write(reply->readAll()); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, file, destination, id, mimeType, name]() {
        file->write(reply->readAll());
        file->close();
        const auto error = reply->error();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray payload = error == QNetworkReply::NoError ? QByteArray() : reply->readAll();
        const QString fallback = reply->errorString();
        reply->deleteLater();
        clearTransfer();
        if (error != QNetworkReply::NoError || status >= 400) {
            QFile::remove(destination);
            emit operationFinished(false, apiError(payload, fallback));
            return;
        }
        trackEditedFile(destination, id, mimeType, name);
        QDesktopServices::openUrl(QUrl::fromLocalFile(destination));
        emit operationFinished(true, QStringLiteral("Downloaded. Changes will be synced back to Google Drive."));
    });
}

void GoogleDriveManager::downloadToFolder(const QString &itemUrl, const QString &name, const QString &mimeType, const QString &destinationFolderOrUrl)
{
    const QString id = fileIdFromUrl(itemUrl);
    QString destinationFolder = normalizeLocalPath(destinationFolderOrUrl);
    if (id.isEmpty() || destinationFolder.isEmpty() || !QFileInfo(destinationFolder).isDir()) {
        fail(QStringLiteral("Invalid Google Drive file or download folder."));
        return;
    }
    if (mimeType.startsWith(QStringLiteral("application/vnd.google-apps."))) {
        fail(QStringLiteral("Google-native documents are not downloaded as binary files yet."));
        return;
    }
    QString safeName = name;
    safeName.replace('/', '_');
    QString destination = QDir(destinationFolder).filePath(safeName);
    if (QFileInfo::exists(destination)) {
        const QFileInfo fi(destination);
        int n = 1;
        do {
            const QString suffix = fi.completeSuffix();
            const QString base = fi.completeBaseName();
            const QString candidate = suffix.isEmpty()
                ? QStringLiteral("%1 (%2)").arg(base).arg(n++)
                : QStringLiteral("%1 (%2).%3").arg(base).arg(n++).arg(suffix);
            destination = QDir(destinationFolder).filePath(candidate);
        } while (QFileInfo::exists(destination));
    }

    QUrl url(QStringLiteral("https://www.googleapis.com/drive/v3/files/%1").arg(id));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("alt"), QStringLiteral("media"));
    query.addQueryItem(QStringLiteral("supportsAllDrives"), QStringLiteral("true"));
    url.setQuery(query);
    QNetworkReply *reply = m_network.get(request(url));
    auto *file = new QFile(destination, reply);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QString message = file->errorString();
        reply->abort();
        reply->deleteLater();
        fail(message);
        return;
    }
    setTransfer(QStringLiteral("Downloading"), name, 0, 0);
    wireProgress(reply, QStringLiteral("Downloading"), name, false);
    connect(reply, &QIODevice::readyRead, this, [reply, file]() { file->write(reply->readAll()); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, file, destination]() {
        file->write(reply->readAll());
        file->close();
        const auto error = reply->error();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString fallback = reply->errorString();
        reply->deleteLater();
        clearTransfer();
        if (error != QNetworkReply::NoError || status >= 400) {
            QFile::remove(destination);
            emit operationFinished(false, fallback);
            return;
        }
        emit operationFinished(true, QStringLiteral("Downloaded to %1").arg(destination));
    });
}


void GoogleDriveManager::downloadFolderAsZip(const QString &itemUrl, const QString &name, const QString &destinationFolderOrUrl)
{
    const QUrl cloudUrl(itemUrl);
    const QString id = fileIdFromUrl(itemUrl);
    const QString destinationFolder = normalizeLocalPath(destinationFolderOrUrl);
    if (m_folderDownloadRunning) {
        fail(QStringLiteral("Another Google Drive folder download is already running."));
        return;
    }
    if (cloudUrl.host() != QStringLiteral("folder") || id.isEmpty() || m_accessToken.isEmpty()
            || destinationFolder.isEmpty() || !QFileInfo(destinationFolder).isDir()) {
        fail(QStringLiteral("Invalid Google Drive folder or download directory."));
        return;
    }

    m_folderDownloadRootName = safeCloudName(name, QStringLiteral("Google Drive Folder"));
    m_folderDownloadTempBase = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
        + QStringLiteral("/cloud-folder-zips/gdrive-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString rootPath = QDir(m_folderDownloadTempBase).filePath(m_folderDownloadRootName);
    if (!QDir().mkpath(rootPath)) {
        folderDownloadFail(QStringLiteral("Could not create temporary Google Drive folder."));
        return;
    }

    m_folderDownloadArchivePath = uniqueZipPath(destinationFolder, m_folderDownloadRootName);
    m_folderDownloadQueue.clear();
    m_folderDownloadCompleted = 0;
    m_folderDownloadDiscovered = 1;
    m_folderDownloadSkipped = 0;
    m_folderDownloadRunning = true;
    m_folderDownloadQueue.enqueue({id, rootPath, m_folderDownloadRootName,
                                   QStringLiteral("application/vnd.google-apps.folder"), QString(), true});
    setTransfer(QStringLiteral("Preparing ZIP"), m_folderDownloadRootName, 0, 1);
    processNextFolderDownloadTask();
}

void GoogleDriveManager::processNextFolderDownloadTask()
{
    if (!m_folderDownloadRunning)
        return;
    if (m_folderDownloadQueue.isEmpty()) {
        finishFolderDownloadArchive();
        return;
    }

    const FolderDownloadTask task = m_folderDownloadQueue.dequeue();
    setTransfer(QStringLiteral("Downloading Google Drive folder"), task.name,
                m_folderDownloadCompleted, qMax(1, m_folderDownloadDiscovered));
    if (task.isDir) {
        QDir().mkpath(task.localPath);
        listFolderDownloadChildren(task);
    } else {
        downloadFolderFile(task);
    }
}

void GoogleDriveManager::listFolderDownloadChildren(const FolderDownloadTask &task, const QString &pageToken)
{
    if (!m_folderDownloadRunning)
        return;

    QUrl url(QStringLiteral("https://www.googleapis.com/drive/v3/files"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("q"), QStringLiteral("'%1' in parents and trashed = false").arg(task.itemId));
    query.addQueryItem(QStringLiteral("pageSize"), QStringLiteral("200"));
    query.addQueryItem(QStringLiteral("supportsAllDrives"), QStringLiteral("true"));
    query.addQueryItem(QStringLiteral("includeItemsFromAllDrives"), QStringLiteral("true"));
    query.addQueryItem(QStringLiteral("fields"), QStringLiteral("nextPageToken,files(id,name,mimeType,size)"));
    if (!pageToken.isEmpty())
        query.addQueryItem(QStringLiteral("pageToken"), pageToken);
    url.setQuery(query);

    QNetworkReply *reply = m_network.get(request(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, task]() {
        const QByteArray payload = reply->readAll();
        const auto error = reply->error();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString fallback = reply->errorString();
        reply->deleteLater();
        if (!m_folderDownloadRunning)
            return;
        if (error != QNetworkReply::NoError || status >= 400) {
            folderDownloadFail(apiError(payload, fallback));
            return;
        }

        const QJsonObject root = QJsonDocument::fromJson(payload).object();
        const QJsonArray files = root.value(QStringLiteral("files")).toArray();
        for (const QJsonValue &value : files) {
            const QJsonObject obj = value.toObject();
            const QString childId = obj.value(QStringLiteral("id")).toString();
            const QString mimeType = obj.value(QStringLiteral("mimeType")).toString();
            if (childId.isEmpty() || mimeType.isEmpty())
                continue;

            const bool isDir = mimeType == QStringLiteral("application/vnd.google-apps.folder");
            QString childName = safeCloudName(obj.value(QStringLiteral("name")).toString(), childId);
            QString exportMime;
            if (!isDir && mimeType.startsWith(QStringLiteral("application/vnd.google-apps."))) {
                QString extension;
                exportMime = googleExportMime(mimeType, &extension);
                if (exportMime.isEmpty()) {
                    ++m_folderDownloadSkipped;
                    continue;
                }
                if (!extension.isEmpty() && !childName.endsWith(extension, Qt::CaseInsensitive))
                    childName += extension;
            }

            const QString childPath = uniqueCloudPath(task.localPath, childName, isDir);
            if (isDir) {
                if (!QDir().mkpath(childPath)) {
                    folderDownloadFail(QStringLiteral("Could not create folder: %1").arg(childName));
                    return;
                }
            } else {
                QFile reserve(childPath);
                if (!reserve.open(QIODevice::WriteOnly)) {
                    folderDownloadFail(reserve.errorString());
                    return;
                }
                reserve.close();
            }
            m_folderDownloadQueue.enqueue({childId, childPath, childName, mimeType, exportMime, isDir});
            ++m_folderDownloadDiscovered;
        }

        const QString nextPageToken = root.value(QStringLiteral("nextPageToken")).toString();
        if (!nextPageToken.isEmpty()) {
            listFolderDownloadChildren(task, nextPageToken);
            return;
        }

        ++m_folderDownloadCompleted;
        processNextFolderDownloadTask();
    });
}

void GoogleDriveManager::downloadFolderFile(const FolderDownloadTask &task)
{
    QUrl url;
    QUrlQuery query;
    if (!task.exportMimeType.isEmpty()) {
        url = QUrl(QStringLiteral("https://www.googleapis.com/drive/v3/files/%1/export").arg(task.itemId));
        query.addQueryItem(QStringLiteral("mimeType"), task.exportMimeType);
    } else {
        url = QUrl(QStringLiteral("https://www.googleapis.com/drive/v3/files/%1").arg(task.itemId));
        query.addQueryItem(QStringLiteral("alt"), QStringLiteral("media"));
        query.addQueryItem(QStringLiteral("supportsAllDrives"), QStringLiteral("true"));
    }
    url.setQuery(query);

    QNetworkReply *reply = m_network.get(request(url));
    auto *file = new QFile(task.localPath, reply);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QString message = file->errorString();
        reply->abort();
        reply->deleteLater();
        folderDownloadFail(message);
        return;
    }
    connect(reply, &QIODevice::readyRead, this, [reply, file]() { file->write(reply->readAll()); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, file, task]() {
        file->write(reply->readAll());
        file->close();
        const auto error = reply->error();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString fallback = reply->errorString();
        reply->deleteLater();
        if (!m_folderDownloadRunning)
            return;
        if (error != QNetworkReply::NoError || status >= 400) {
            folderDownloadFail(fallback);
            return;
        }
        ++m_folderDownloadCompleted;
        processNextFolderDownloadTask();
    });
}

void GoogleDriveManager::finishFolderDownloadArchive()
{
    if (!m_folderDownloadRunning)
        return;
    const QString bsdtar = QStandardPaths::findExecutable(QStringLiteral("bsdtar"));
    if (bsdtar.isEmpty()) {
        folderDownloadFail(QStringLiteral("bsdtar was not found. Install the libarchive package to create ZIP files."));
        return;
    }

    setTransfer(QStringLiteral("Creating ZIP"), m_folderDownloadRootName,
                m_folderDownloadDiscovered, qMax(1, m_folderDownloadDiscovered));
    auto *process = new QProcess(this);
    const QString archivePath = m_folderDownloadArchivePath;
    const QString tempBase = m_folderDownloadTempBase;
    const QString rootName = m_folderDownloadRootName;
    const int skipped = m_folderDownloadSkipped;
    const QStringList args{QStringLiteral("-a"), QStringLiteral("-cf"), archivePath,
                           QStringLiteral("-C"), tempBase, rootName};

    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && m_folderDownloadRunning) {
            const QString message = process->errorString();
            process->deleteLater();
            folderDownloadFail(message);
        }
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, process, archivePath, skipped](int exitCode, QProcess::ExitStatus status) {
        const QString stderrText = QString::fromUtf8(process->readAllStandardError()).trimmed();
        process->deleteLater();
        if (!m_folderDownloadRunning)
            return;
        if (status != QProcess::NormalExit || exitCode != 0) {
            folderDownloadFail(stderrText.isEmpty() ? QStringLiteral("Google Drive folder could not be converted to ZIP.") : stderrText);
            return;
        }

        QDir(m_folderDownloadTempBase).removeRecursively();
        m_folderDownloadQueue.clear();
        m_folderDownloadRunning = false;
        m_folderDownloadCompleted = 0;
        m_folderDownloadDiscovered = 0;
        m_folderDownloadSkipped = 0;
        m_folderDownloadTempBase.clear();
        m_folderDownloadRootName.clear();
        m_folderDownloadArchivePath.clear();
        clearTransfer();
        QString message = QStringLiteral("Folder downloaded as ZIP: %1").arg(archivePath);
        if (skipped > 0)
            message += QStringLiteral(" (%1 unsupported Google-native item(s) skipped)").arg(skipped);
        emit operationFinished(true, message);
    });
    process->start(bsdtar, args);
}

void GoogleDriveManager::folderDownloadFail(const QString &message)
{
    const QString archivePath = m_folderDownloadArchivePath;
    const QString tempBase = m_folderDownloadTempBase;
    m_folderDownloadQueue.clear();
    m_folderDownloadRunning = false;
    m_folderDownloadCompleted = 0;
    m_folderDownloadDiscovered = 0;
    m_folderDownloadSkipped = 0;
    m_folderDownloadTempBase.clear();
    m_folderDownloadRootName.clear();
    m_folderDownloadArchivePath.clear();
    if (!tempBase.isEmpty())
        QDir(tempBase).removeRecursively();
    if (!archivePath.isEmpty())
        QFile::remove(archivePath);
    clearTransfer();
    if (!message.isEmpty())
        emit operationFinished(false, message);
}

void GoogleDriveManager::trackEditedFile(const QString &localPath, const QString &fileId, const QString &mimeType, const QString &name)
{
    TrackedFile *tracked = m_trackedFiles.value(localPath, nullptr);
    if (!tracked) {
        tracked = new TrackedFile;
        tracked->localPath = localPath;
        tracked->timer = new QTimer(this);
        tracked->timer->setSingleShot(true);
        tracked->timer->setInterval(1200);
        connect(tracked->timer, &QTimer::timeout, this, [this, localPath]() { syncTrackedFile(localPath); });
        m_trackedFiles.insert(localPath, tracked);
    }
    tracked->fileId = fileId;
    tracked->mimeType = mimeType;
    tracked->name = name;
    ensureWatch(localPath);
}

void GoogleDriveManager::ensureWatch(const QString &localPath)
{
    if (!m_trackedFiles.contains(localPath) || !QFileInfo::exists(localPath))
        return;
    if (!m_watcher.files().contains(localPath))
        m_watcher.addPath(localPath);
}

void GoogleDriveManager::scheduleSync(const QString &localPath)
{
    TrackedFile *tracked = m_trackedFiles.value(localPath, nullptr);
    if (!tracked)
        return;
    if (tracked->syncing) {
        tracked->pending = true;
        return;
    }
    tracked->timer->start();
}

void GoogleDriveManager::syncTrackedFile(const QString &localPath)
{
    TrackedFile *tracked = m_trackedFiles.value(localPath, nullptr);
    if (!tracked || tracked->syncing || !QFileInfo::exists(localPath) || m_accessToken.isEmpty())
        return;

    auto *file = new QFile(localPath);
    if (!file->open(QIODevice::ReadOnly)) {
        emit operationFinished(false, file->errorString());
        delete file;
        return;
    }
    tracked->syncing = true;
    tracked->pending = false;

    QUrl url(QStringLiteral("https://www.googleapis.com/upload/drive/v3/files/%1").arg(tracked->fileId));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("uploadType"), QStringLiteral("media"));
    query.addQueryItem(QStringLiteral("supportsAllDrives"), QStringLiteral("true"));
    query.addQueryItem(QStringLiteral("fields"), QStringLiteral("id,name,size,modifiedTime"));
    url.setQuery(query);
    QNetworkRequest req = request(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, tracked->mimeType.isEmpty() ? QStringLiteral("application/octet-stream") : tracked->mimeType);
    req.setHeader(QNetworkRequest::ContentLengthHeader, file->size());
    QNetworkReply *reply = m_network.sendCustomRequest(req, QByteArrayLiteral("PATCH"), file);
    file->setParent(reply);
    setTransfer(QStringLiteral("Syncing changes"), tracked->name, 0, file->size());
    wireProgress(reply, QStringLiteral("Syncing changes"), tracked->name, true);
    connect(reply, &QNetworkReply::finished, this, [this, reply, localPath]() {
        const QByteArray payload = reply->readAll();
        const auto error = reply->error();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString fallback = reply->errorString();
        reply->deleteLater();
        TrackedFile *tracked = m_trackedFiles.value(localPath, nullptr);
        if (tracked)
            tracked->syncing = false;
        clearTransfer();
        ensureWatch(localPath);
        if (error != QNetworkReply::NoError || status >= 400) {
            emit operationFinished(false, apiError(payload, fallback));
        } else {
            emit synced(localPath);
            emit operationFinished(true, QStringLiteral("Changes synced to Google Drive."));
            emit refreshRequested();
        }
        if (tracked && tracked->pending) {
            tracked->pending = false;
            tracked->timer->start();
        }
    });
}
