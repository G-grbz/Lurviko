#include "onedrivemanager.h"

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
#include <utility>

namespace {
constexpr qint64 OneDriveChunkSize = 10LL * 1024LL * 1024LL; // 32 * 320 KiB

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
    const QString safe = safeCloudName(baseName, QStringLiteral("OneDrive Folder"));
    QDir dir(destinationFolder);
    QString path = dir.filePath(safe + QStringLiteral(".zip"));
    int n = 1;
    while (QFileInfo::exists(path))
        path = dir.filePath(QStringLiteral("%1 (%2).zip").arg(safe).arg(n++));
    return path;
}
}

OneDriveManager::OneDriveManager(QObject *parent) : QObject(parent)
{
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString &path) {
        scheduleSync(path);
        QTimer::singleShot(250, this, [this, path]() { ensureWatch(path); });
        QTimer::singleShot(1000, this, [this, path]() { ensureWatch(path); });
    });
}

OneDriveManager::~OneDriveManager()
{
    for (TrackedFile *tracked : std::as_const(m_trackedFiles))
        delete tracked;
    m_trackedFiles.clear();
}

void OneDriveManager::setAccessToken(const QString &token)
{
    if (m_accessToken == token)
        return;
    m_accessToken = token;
    emit accessTokenChanged();
}

QString OneDriveManager::fileIdFromUrl(const QString &itemUrl) const
{
    const QUrl url(itemUrl);
    if (url.scheme() != QStringLiteral("onedrive") || (url.host() != QStringLiteral("file") && url.host() != QStringLiteral("folder")))
        return {};
    QString id = url.path();
    if (id.startsWith('/')) id.remove(0, 1);
    return QUrl::fromPercentEncoding(id.toUtf8());
}

QString OneDriveManager::folderIdFromUrl(const QString &folderUrl) const
{
    const QUrl url(folderUrl);
    if (url.scheme() != QStringLiteral("onedrive")) return {};
    if (url.host().isEmpty() || url.host() == QStringLiteral("root")) return QStringLiteral("root");
    if (url.host() != QStringLiteral("folder")) return {};
    QString id = url.path();
    if (id.startsWith('/')) id.remove(0, 1);
    return QUrl::fromPercentEncoding(id.toUtf8());
}

QString OneDriveManager::normalizeLocalPath(const QString &pathOrUrl) const
{
    const QUrl url(pathOrUrl);
    if (url.isLocalFile()) return url.toLocalFile();
    if (pathOrUrl.startsWith(QStringLiteral("file://"))) return QUrl(pathOrUrl).toLocalFile();
    return pathOrUrl;
}

QString OneDriveManager::encodedName(const QString &name) const
{
    return QString::fromLatin1(QUrl::toPercentEncoding(name));
}

QNetworkRequest OneDriveManager::request(const QUrl &url) const
{
    QNetworkRequest req(url);
    req.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + m_accessToken.toUtf8());
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("g-File/0.5.10"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    return req;
}

QNetworkRequest OneDriveManager::downloadRequest(const QUrl &url) const
{
    // Microsoft Graph returns a short-lived, preauthenticated OneDrive CDN URL.
    // Do not forward the Graph Bearer token to that host.
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("g-File/0.5.10"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    return req;
}

void OneDriveManager::resolveDownloadUrl(const QString &fileId, std::function<void(QUrl)> callback)
{
    if (fileId.isEmpty() || m_accessToken.isEmpty()) {
        fail(QStringLiteral("OneDrive oturumu geçerli değil."));
        callback({});
        return;
    }

    // Ask Microsoft Graph for /content but deliberately do NOT follow the
    // redirect here. Graph answers with a short-lived preauthenticated URL in
    // Location. The final OneDrive CDN request must not inherit our Bearer
    // token, otherwise some hosts challenge it with HTTP authentication.
    const QString encodedId = QString::fromLatin1(QUrl::toPercentEncoding(fileId));
    const QUrl contentUrl(QStringLiteral("https://graph.microsoft.com/v1.0/me/drive/items/%1/content")
                          .arg(encodedId));
    QNetworkRequest req = request(contentUrl);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);

    auto *reply = m_network.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, callback = std::move(callback)]() mutable {
        const QByteArray payload = reply->readAll();
        const auto error = reply->error();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString fallback = reply->errorString();

        QUrl redirectUrl = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
        if (redirectUrl.isEmpty()) {
            const QByteArray location = reply->rawHeader("Location");
            if (!location.isEmpty())
                redirectUrl = QUrl(QString::fromUtf8(location));
        }
        if (redirectUrl.isRelative())
            redirectUrl = reply->url().resolved(redirectUrl);

        reply->deleteLater();

        if (status >= 300 && status < 400 && redirectUrl.isValid() && !redirectUrl.isEmpty()) {
            callback(redirectUrl);
            return;
        }

        if (error != QNetworkReply::NoError || status >= 400) {
            fail(apiError(payload, fallback));
            callback({});
            return;
        }

        fail(QStringLiteral("OneDrive indirme yönlendirmesi alınamadı."));
        callback({});
    });
}

QString OneDriveManager::apiError(const QByteArray &payload, const QString &fallback) const
{
    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (doc.isObject()) {
        const QJsonObject error = doc.object().value(QStringLiteral("error")).toObject();
        const QString message = error.value(QStringLiteral("message")).toString();
        if (!message.isEmpty()) return message;
    }
    return fallback;
}

void OneDriveManager::fail(const QString &message)
{
    clearTransfer();
    emit operationFinished(false, message);
}

void OneDriveManager::setTransfer(const QString &label, const QString &detail, qint64 done, qint64 total, bool active)
{
    m_transferActive = active;
    m_transferLabel = label;
    m_transferDetail = detail;
    m_transferDone = qMax<qint64>(0, done);
    m_transferTotal = qMax<qint64>(0, total);
    m_transferPercent = m_transferTotal > 0 ? qBound(0, int((m_transferDone * 100) / m_transferTotal), 100) : 0;
    emit transferChanged();
}

void OneDriveManager::clearTransfer()
{
    m_transferActive = false;
    m_transferPercent = 0;
    m_transferDone = 0;
    m_transferTotal = 0;
    m_transferLabel.clear();
    m_transferDetail.clear();
    emit transferChanged();
}

void OneDriveManager::wireProgress(QNetworkReply *reply, const QString &label, const QString &detail, bool upload, qint64 base, qint64 grandTotal)
{
    auto update = [this, label, detail, base, grandTotal](qint64 done, qint64 total) {
        const qint64 finalTotal = grandTotal >= 0 ? grandTotal : total;
        setTransfer(label, detail, base + done, finalTotal);
    };
    if (upload)
        connect(reply, &QNetworkReply::uploadProgress, this, update);
    else
        connect(reply, &QNetworkReply::downloadProgress, this, update);
}

void OneDriveManager::resolveFolderId(const QString &folderUrl, std::function<void(QString)> callback)
{
    const QString id = folderIdFromUrl(folderUrl);
    if (id.isEmpty()) { callback({}); return; }
    if (id != QStringLiteral("root")) { callback(id); return; }
    if (m_accessToken.isEmpty()) { callback({}); return; }

    auto *reply = m_network.get(request(QUrl(QStringLiteral("https://graph.microsoft.com/v1.0/me/drive/root?$select=id"))));
    connect(reply, &QNetworkReply::finished, this, [this, reply, callback = std::move(callback)]() mutable {
        const QByteArray payload = reply->readAll();
        const auto error = reply->error();
        const QString fallback = reply->errorString();
        reply->deleteLater();
        if (error != QNetworkReply::NoError) {
            emit operationFinished(false, apiError(payload, fallback));
            callback({});
            return;
        }
        callback(QJsonDocument::fromJson(payload).object().value(QStringLiteral("id")).toString());
    });
}

void OneDriveManager::uploadFile(const QString &localPathOrUrl, const QString &destinationFolderUrl)
{
    uploadPaths(QStringList{localPathOrUrl}, destinationFolderUrl);
}

void OneDriveManager::uploadPaths(const QStringList &localPathsOrUrls, const QString &destinationFolderUrl)
{
    if (m_accessToken.isEmpty()) { fail(QStringLiteral("OneDrive bağlı değil.")); return; }
    QStringList paths;
    for (const QString &value : localPathsOrUrls) {
        const QString path = normalizeLocalPath(value);
        if (QFileInfo::exists(path)) paths.push_back(path);
    }
    if (paths.isEmpty()) { fail(QStringLiteral("Yüklenecek geçerli dosya veya klasör yok.")); return; }

    resolveFolderId(destinationFolderUrl, [this, paths](const QString &parentId) {
        if (parentId.isEmpty()) return;
        startUploadQueue(paths, parentId);
    });
}

void OneDriveManager::startUploadQueue(const QStringList &paths, const QString &destinationParentId)
{
    if (m_uploadQueueRunning) { fail(QStringLiteral("Başka bir OneDrive yüklemesi sürüyor.")); return; }
    m_uploadQueue.clear();
    for (const QString &path : paths) m_uploadQueue.enqueue({path, destinationParentId});
    m_uploadQueueRunning = true;
    m_uploadCompleted = 0;
    m_uploadDiscovered = m_uploadQueue.size();
    processNextUploadTask();
}

void OneDriveManager::processNextUploadTask()
{
    if (m_uploadQueue.isEmpty()) {
        m_uploadQueueRunning = false;
        clearTransfer();
        emit operationFinished(true, QStringLiteral("OneDrive yüklemesi tamamlandı."));
        emit refreshRequested();
        return;
    }
    const UploadTask task = m_uploadQueue.dequeue();
    const QFileInfo info(task.localPath);
    if (!info.exists()) { ++m_uploadCompleted; processNextUploadTask(); return; }
    setTransfer(QStringLiteral("OneDrive'a yükleniyor"), info.fileName(), m_uploadCompleted, qMax(1, m_uploadDiscovered));
    if (info.isDir()) createRemoteFolderForUpload(task); else uploadSingleFile(task);
}

void OneDriveManager::createRemoteFolderForUpload(const UploadTask &task)
{
    const QFileInfo info(task.localPath);
    QUrl url(QStringLiteral("https://graph.microsoft.com/v1.0/me/drive/items/%1/children").arg(QString::fromLatin1(QUrl::toPercentEncoding(task.destinationParentId))));
    QJsonObject body;
    body.insert(QStringLiteral("name"), info.fileName());
    body.insert(QStringLiteral("folder"), QJsonObject{});
    body.insert(QStringLiteral("@microsoft.graph.conflictBehavior"), QStringLiteral("rename"));
    QNetworkRequest req = request(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    auto *reply = m_network.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, task]() {
        const QByteArray payload = reply->readAll();
        const auto error = reply->error();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString fallback = reply->errorString();
        reply->deleteLater();
        if (error != QNetworkReply::NoError || status >= 400) { m_uploadQueueRunning = false; fail(apiError(payload, fallback)); return; }
        const QString remoteId = QJsonDocument::fromJson(payload).object().value(QStringLiteral("id")).toString();
        if (remoteId.isEmpty()) { m_uploadQueueRunning = false; fail(QStringLiteral("OneDrive klasör kimliği alınamadı.")); return; }
        QDir dir(task.localPath);
        const QFileInfoList children = dir.entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries, QDir::Name | QDir::DirsFirst);
        for (const QFileInfo &child : children) {
            m_uploadQueue.enqueue({child.absoluteFilePath(), remoteId});
            ++m_uploadDiscovered;
        }
        ++m_uploadCompleted;
        processNextUploadTask();
    });
}

void OneDriveManager::uploadSingleFile(const UploadTask &task)
{
    const QFileInfo info(task.localPath);
    const QString parent = QString::fromLatin1(QUrl::toPercentEncoding(task.destinationParentId));
    const QString name = encodedName(info.fileName());
    if (info.size() == 0) {
        QNetworkRequest emptyReq = request(QUrl(QStringLiteral("https://graph.microsoft.com/v1.0/me/drive/items/%1:/%2:/content").arg(parent, name)));
        emptyReq.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/octet-stream"));
        auto *emptyReply = m_network.put(emptyReq, QByteArray());
        connect(emptyReply, &QNetworkReply::finished, this, [this, emptyReply]() {
            const QByteArray payload = emptyReply->readAll();
            const auto error = emptyReply->error();
            const int status = emptyReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QString fallback = emptyReply->errorString();
            emptyReply->deleteLater();
            if (error != QNetworkReply::NoError || status >= 400) { m_uploadQueueRunning = false; fail(apiError(payload, fallback)); return; }
            ++m_uploadCompleted;
            processNextUploadTask();
        });
        return;
    }
    const QUrl sessionEndpoint(QStringLiteral("https://graph.microsoft.com/v1.0/me/drive/items/%1:/%2:/createUploadSession").arg(parent, name));
    QJsonObject item;
    item.insert(QStringLiteral("@microsoft.graph.conflictBehavior"), QStringLiteral("rename"));
    item.insert(QStringLiteral("name"), info.fileName());
    QJsonObject body; body.insert(QStringLiteral("item"), item);
    QNetworkRequest req = request(sessionEndpoint);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    auto *reply = m_network.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, task, info]() {
        const QByteArray payload = reply->readAll();
        const auto error = reply->error();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString fallback = reply->errorString();
        reply->deleteLater();
        if (error != QNetworkReply::NoError || status >= 400) { m_uploadQueueRunning = false; fail(apiError(payload, fallback)); return; }
        const QUrl uploadUrl(QJsonDocument::fromJson(payload).object().value(QStringLiteral("uploadUrl")).toString());
        if (!uploadUrl.isValid()) { m_uploadQueueRunning = false; fail(QStringLiteral("OneDrive upload session URL alınamadı.")); return; }
        auto *file = new QFile(task.localPath, this);
        if (!file->open(QIODevice::ReadOnly)) { const QString e=file->errorString(); delete file; m_uploadQueueRunning=false; fail(e); return; }
        uploadChunk(file, uploadUrl, info.fileName(), 0, file->size());
    });
}

void OneDriveManager::uploadChunk(QFile *file, const QUrl &uploadUrl, const QString &name, qint64 offset, qint64 total)
{
    if (!file || !file->isOpen()) { m_uploadQueueRunning=false; fail(QStringLiteral("Yükleme dosyası açılamadı.")); return; }
    if (offset >= total) {
        file->close(); file->deleteLater(); ++m_uploadCompleted; processNextUploadTask(); return;
    }
    file->seek(offset);
    const QByteArray chunk = file->read(qMin(OneDriveChunkSize, total - offset));
    if (chunk.isEmpty() && total > 0) { const QString e=file->errorString(); file->close(); file->deleteLater(); m_uploadQueueRunning=false; fail(e); return; }
    QNetworkRequest req(uploadUrl);
    req.setHeader(QNetworkRequest::ContentLengthHeader, chunk.size());
    req.setRawHeader("Content-Range", QStringLiteral("bytes %1-%2/%3").arg(offset).arg(offset + chunk.size() - 1).arg(total).toUtf8());
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    auto *reply = m_network.put(req, chunk);
    wireProgress(reply, QStringLiteral("OneDrive'a yükleniyor"), name, true, offset, total);
    connect(reply, &QNetworkReply::finished, this, [this, reply, file, uploadUrl, name, offset, total, chunkSize=chunk.size()]() {
        const QByteArray payload = reply->readAll();
        const auto error = reply->error();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString fallback = reply->errorString();
        reply->deleteLater();
        if (error != QNetworkReply::NoError || status >= 400) { file->close(); file->deleteLater(); m_uploadQueueRunning=false; fail(apiError(payload, fallback)); return; }
        const qint64 next = offset + chunkSize;
        if (status == 200 || status == 201 || next >= total) {
            file->close(); file->deleteLater(); ++m_uploadCompleted; processNextUploadTask();
        } else {
            uploadChunk(file, uploadUrl, name, next, total);
        }
    });
}

void OneDriveManager::moveItem(const QString &itemUrl, const QString &destinationFolderUrl)
{
    const QString id = fileIdFromUrl(itemUrl);
    if (id.isEmpty() || m_accessToken.isEmpty()) { fail(QStringLiteral("Geçersiz OneDrive öğesi.")); return; }
    resolveFolderId(destinationFolderUrl, [this, id](const QString &parentId) {
        if (parentId.isEmpty()) return;
        QJsonObject parent; parent.insert(QStringLiteral("id"), parentId);
        QJsonObject body; body.insert(QStringLiteral("parentReference"), parent);
        QNetworkRequest req = request(QUrl(QStringLiteral("https://graph.microsoft.com/v1.0/me/drive/items/%1").arg(QString::fromLatin1(QUrl::toPercentEncoding(id)))));
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        auto *reply = m_network.sendCustomRequest(req, QByteArrayLiteral("PATCH"), QJsonDocument(body).toJson(QJsonDocument::Compact));
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            const QByteArray p=reply->readAll(); const auto e=reply->error(); const int st=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(); const QString f=reply->errorString(); reply->deleteLater();
            if (e!=QNetworkReply::NoError || st>=400) { fail(apiError(p,f)); return; }
            emit operationFinished(true, QStringLiteral("OneDrive öğesi taşındı.")); emit refreshRequested();
        });
    });
}

void OneDriveManager::copyItem(const QString &itemUrl, const QString &destinationFolderUrl)
{
    const QString id = fileIdFromUrl(itemUrl);
    if (id.isEmpty() || m_accessToken.isEmpty()) { fail(QStringLiteral("Geçersiz OneDrive öğesi.")); return; }
    resolveFolderId(destinationFolderUrl, [this, id](const QString &parentId) {
        if (parentId.isEmpty()) return;
        QJsonObject parent; parent.insert(QStringLiteral("id"), parentId);
        QJsonObject body; body.insert(QStringLiteral("parentReference"), parent);
        QNetworkRequest req = request(QUrl(QStringLiteral("https://graph.microsoft.com/v1.0/me/drive/items/%1/copy").arg(QString::fromLatin1(QUrl::toPercentEncoding(id)))));
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        auto *reply = m_network.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            const QByteArray p=reply->readAll(); const auto e=reply->error(); const int st=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(); const QString f=reply->errorString(); reply->deleteLater();
            if (e!=QNetworkReply::NoError || st>=400) { fail(apiError(p,f)); return; }
            emit operationFinished(true, QStringLiteral("OneDrive kopyalama işlemi başlatıldı.")); emit refreshRequested(); QTimer::singleShot(1500, this, [this]() { emit refreshRequested(); }); QTimer::singleShot(4000, this, [this]() { emit refreshRequested(); });
        });
    });
}

void OneDriveManager::downloadAndOpen(const QString &itemUrl, const QString &name, const QString &mimeType)
{
    const QString id = fileIdFromUrl(itemUrl);
    if (id.isEmpty()) { fail(QStringLiteral("Geçersiz OneDrive dosyası.")); return; }
    for (TrackedFile *tracked : std::as_const(m_trackedFiles)) {
        if (tracked && tracked->fileId == id && QFileInfo::exists(tracked->localPath)) {
            ensureWatch(tracked->localPath);
            QDesktopServices::openUrl(QUrl::fromLocalFile(tracked->localPath));
            emit operationFinished(true, QStringLiteral("Senkronize yerel kopya açıldı."));
            return;
        }
    }

    const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/onedrive-edit");
    QDir().mkpath(cacheDir);
    QString safeName = name;
    safeName.replace('/', '_');
    if (safeName.isEmpty()) safeName = id;
    const QString destination = QDir(cacheDir).filePath(id + QStringLiteral("-") + safeName);

    resolveDownloadUrl(id, [this, destination, id, mimeType, name](const QUrl &downloadUrl) {
        if (!downloadUrl.isValid() || downloadUrl.isEmpty()) return;

        QNetworkReply *reply = m_network.get(downloadRequest(downloadUrl));
        auto *file = new QFile(destination, reply);
        if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            const QString e = file->errorString();
            reply->abort();
            reply->deleteLater();
            fail(e);
            return;
        }

        setTransfer(QStringLiteral("İndiriliyor"), name, 0, 0);
        wireProgress(reply, QStringLiteral("İndiriliyor"), name, false);
        connect(reply, &QIODevice::readyRead, this, [reply, file]() { file->write(reply->readAll()); });
        connect(reply, &QNetworkReply::finished, this, [this, reply, file, destination, id, mimeType, name]() {
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
            trackEditedFile(destination, id, mimeType, name);
            QDesktopServices::openUrl(QUrl::fromLocalFile(destination));
            emit operationFinished(true, QStringLiteral("İndirildi. Değişiklikler OneDrive'a geri senkronlanacak."));
        });
    });
}

void OneDriveManager::downloadToFolder(const QString &itemUrl, const QString &name, const QString &, const QString &destinationFolderOrUrl)
{
    const QString id = fileIdFromUrl(itemUrl);
    const QString folder = normalizeLocalPath(destinationFolderOrUrl);
    if (id.isEmpty() || !QFileInfo(folder).isDir()) {
        fail(QStringLiteral("Geçersiz OneDrive dosyası veya indirme klasörü."));
        return;
    }

    QString safe = name;
    safe.replace('/', '_');
    if (safe.isEmpty()) safe = id;
    QString destination = QDir(folder).filePath(safe);
    int n = 1;
    while (QFileInfo::exists(destination)) {
        QFileInfo fi(destination);
        const QString suffix = fi.completeSuffix();
        destination = QDir(folder).filePath(suffix.isEmpty()
            ? QStringLiteral("%1 (%2)").arg(fi.completeBaseName()).arg(n++)
            : QStringLiteral("%1 (%2).%3").arg(fi.completeBaseName()).arg(n++).arg(suffix));
    }

    resolveDownloadUrl(id, [this, destination, name](const QUrl &downloadUrl) {
        if (!downloadUrl.isValid() || downloadUrl.isEmpty()) return;

        QNetworkReply *reply = m_network.get(downloadRequest(downloadUrl));
        auto *file = new QFile(destination, reply);
        if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            const QString e = file->errorString();
            reply->abort();
            reply->deleteLater();
            fail(e);
            return;
        }

        setTransfer(QStringLiteral("İndiriliyor"), name, 0, 0);
        wireProgress(reply, QStringLiteral("İndiriliyor"), name, false);
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
            emit operationFinished(true, QStringLiteral("Dosya indirildi: %1").arg(destination));
        });
    });
}


void OneDriveManager::downloadFolderAsZip(const QString &itemUrl, const QString &name, const QString &destinationFolderOrUrl)
{
    const QUrl cloudUrl(itemUrl);
    const QString id = fileIdFromUrl(itemUrl);
    const QString destinationFolder = normalizeLocalPath(destinationFolderOrUrl);
    if (m_folderDownloadRunning) {
        fail(QStringLiteral("Başka bir OneDrive klasör indirmesi sürüyor."));
        return;
    }
    if (cloudUrl.host() != QStringLiteral("folder") || id.isEmpty() || m_accessToken.isEmpty()
            || destinationFolder.isEmpty() || !QFileInfo(destinationFolder).isDir()) {
        fail(QStringLiteral("Geçersiz OneDrive klasörü veya indirme dizini."));
        return;
    }

    m_folderDownloadRootName = safeCloudName(name, QStringLiteral("OneDrive Folder"));
    m_folderDownloadTempBase = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
        + QStringLiteral("/cloud-folder-zips/onedrive-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString rootPath = QDir(m_folderDownloadTempBase).filePath(m_folderDownloadRootName);
    if (!QDir().mkpath(rootPath)) {
        folderDownloadFail(QStringLiteral("Geçici OneDrive klasörü oluşturulamadı."));
        return;
    }

    m_folderDownloadArchivePath = uniqueZipPath(destinationFolder, m_folderDownloadRootName);
    m_folderDownloadQueue.clear();
    m_folderDownloadCompleted = 0;
    m_folderDownloadDiscovered = 1;
    m_folderDownloadRunning = true;
    m_folderDownloadQueue.enqueue({id, rootPath, m_folderDownloadRootName, true});
    setTransfer(QStringLiteral("ZIP hazırlanıyor"), m_folderDownloadRootName, 0, 1);
    processNextFolderDownloadTask();
}

void OneDriveManager::processNextFolderDownloadTask()
{
    if (!m_folderDownloadRunning)
        return;
    if (m_folderDownloadQueue.isEmpty()) {
        finishFolderDownloadArchive();
        return;
    }

    const FolderDownloadTask task = m_folderDownloadQueue.dequeue();
    setTransfer(QStringLiteral("OneDrive klasörü indiriliyor"), task.name,
                m_folderDownloadCompleted, qMax(1, m_folderDownloadDiscovered));
    if (task.isDir) {
        QDir().mkpath(task.localPath);
        listFolderDownloadChildren(task);
    } else {
        downloadFolderFile(task);
    }
}

void OneDriveManager::listFolderDownloadChildren(const FolderDownloadTask &task, const QUrl &nextUrl)
{
    if (!m_folderDownloadRunning)
        return;

    QUrl url = nextUrl;
    if (!url.isValid() || url.isEmpty()) {
        const QString encodedId = QString::fromLatin1(QUrl::toPercentEncoding(task.itemId));
        url = QUrl(QStringLiteral("https://graph.microsoft.com/v1.0/me/drive/items/%1/children?$top=200&$select=id,name,file,folder,package")
                   .arg(encodedId));
    }

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
        const QJsonArray values = root.value(QStringLiteral("value")).toArray();
        for (const QJsonValue &value : values) {
            const QJsonObject obj = value.toObject();
            const QString childId = obj.value(QStringLiteral("id")).toString();
            if (childId.isEmpty())
                continue;
            const bool isDir = obj.contains(QStringLiteral("folder")) || obj.contains(QStringLiteral("package"));
            const QString childName = safeCloudName(obj.value(QStringLiteral("name")).toString(), childId);
            const QString childPath = uniqueCloudPath(task.localPath, childName, isDir);
            if (isDir) {
                if (!QDir().mkpath(childPath)) {
                    folderDownloadFail(QStringLiteral("Klasör oluşturulamadı: %1").arg(childName));
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
            m_folderDownloadQueue.enqueue({childId, childPath, childName, isDir});
            ++m_folderDownloadDiscovered;
        }

        const QUrl next(root.value(QStringLiteral("@odata.nextLink")).toString());
        if (next.isValid() && !next.isEmpty()) {
            listFolderDownloadChildren(task, next);
            return;
        }

        ++m_folderDownloadCompleted;
        processNextFolderDownloadTask();
    });
}

void OneDriveManager::downloadFolderFile(const FolderDownloadTask &task)
{
    resolveDownloadUrl(task.itemId, [this, task](const QUrl &downloadUrl) {
        if (!m_folderDownloadRunning)
            return;
        if (!downloadUrl.isValid() || downloadUrl.isEmpty()) {
            // resolveDownloadUrl already reported the Graph error.
            folderDownloadFail(QString(), true);
            return;
        }

        QNetworkReply *reply = m_network.get(downloadRequest(downloadUrl));
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
    });
}

void OneDriveManager::finishFolderDownloadArchive()
{
    if (!m_folderDownloadRunning)
        return;
    const QString bsdtar = QStandardPaths::findExecutable(QStringLiteral("bsdtar"));
    if (bsdtar.isEmpty()) {
        folderDownloadFail(QStringLiteral("bsdtar bulunamadı. ZIP oluşturmak için libarchive paketini kurun."));
        return;
    }

    setTransfer(QStringLiteral("ZIP oluşturuluyor"), m_folderDownloadRootName,
                m_folderDownloadDiscovered, qMax(1, m_folderDownloadDiscovered));
    auto *process = new QProcess(this);
    const QString archivePath = m_folderDownloadArchivePath;
    const QString tempBase = m_folderDownloadTempBase;
    const QString rootName = m_folderDownloadRootName;
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
            [this, process, archivePath](int exitCode, QProcess::ExitStatus status) {
        const QString stderrText = QString::fromUtf8(process->readAllStandardError()).trimmed();
        process->deleteLater();
        if (!m_folderDownloadRunning)
            return;
        if (status != QProcess::NormalExit || exitCode != 0) {
            folderDownloadFail(stderrText.isEmpty() ? QStringLiteral("OneDrive klasörü ZIP'e dönüştürülemedi.") : stderrText);
            return;
        }

        QDir(m_folderDownloadTempBase).removeRecursively();
        m_folderDownloadQueue.clear();
        m_folderDownloadRunning = false;
        m_folderDownloadCompleted = 0;
        m_folderDownloadDiscovered = 0;
        m_folderDownloadTempBase.clear();
        m_folderDownloadRootName.clear();
        m_folderDownloadArchivePath.clear();
        clearTransfer();
        emit operationFinished(true, QStringLiteral("Klasör ZIP olarak indirildi: %1").arg(archivePath));
    });
    process->start(bsdtar, args);
}

void OneDriveManager::folderDownloadFail(const QString &message, bool alreadyEmitted)
{
    const QString archivePath = m_folderDownloadArchivePath;
    const QString tempBase = m_folderDownloadTempBase;
    m_folderDownloadQueue.clear();
    m_folderDownloadRunning = false;
    m_folderDownloadCompleted = 0;
    m_folderDownloadDiscovered = 0;
    m_folderDownloadTempBase.clear();
    m_folderDownloadRootName.clear();
    m_folderDownloadArchivePath.clear();
    if (!tempBase.isEmpty())
        QDir(tempBase).removeRecursively();
    if (!archivePath.isEmpty())
        QFile::remove(archivePath);
    clearTransfer();
    if (!alreadyEmitted && !message.isEmpty())
        emit operationFinished(false, message);
}

void OneDriveManager::trackEditedFile(const QString &localPath, const QString &fileId, const QString &mimeType, const QString &name)
{
    TrackedFile *tracked=m_trackedFiles.value(localPath,nullptr);
    if(!tracked){tracked=new TrackedFile;tracked->localPath=localPath;tracked->timer=new QTimer(this);tracked->timer->setSingleShot(true);tracked->timer->setInterval(1200);connect(tracked->timer,&QTimer::timeout,this,[this,localPath](){syncTrackedFile(localPath);});m_trackedFiles.insert(localPath,tracked);}
    tracked->fileId=fileId;tracked->mimeType=mimeType;tracked->name=name;ensureWatch(localPath);
}

void OneDriveManager::ensureWatch(const QString &localPath)
{
    if(!m_trackedFiles.contains(localPath)||!QFileInfo::exists(localPath))return;if(!m_watcher.files().contains(localPath))m_watcher.addPath(localPath);
}

void OneDriveManager::scheduleSync(const QString &localPath)
{
    TrackedFile *tracked=m_trackedFiles.value(localPath,nullptr);if(!tracked)return;if(tracked->syncing){tracked->pending=true;return;}tracked->timer->start();
}

void OneDriveManager::syncTrackedFile(const QString &localPath)
{
    TrackedFile *tracked=m_trackedFiles.value(localPath,nullptr);if(!tracked||tracked->syncing||!QFileInfo::exists(localPath)||m_accessToken.isEmpty())return;
    auto *file=new QFile(localPath);if(!file->open(QIODevice::ReadOnly)){emit operationFinished(false,file->errorString());delete file;return;}tracked->syncing=true;tracked->pending=false;
    QNetworkRequest req=request(QUrl(QStringLiteral("https://graph.microsoft.com/v1.0/me/drive/items/%1/content").arg(QString::fromLatin1(QUrl::toPercentEncoding(tracked->fileId)))));req.setHeader(QNetworkRequest::ContentTypeHeader,tracked->mimeType.isEmpty()?QStringLiteral("application/octet-stream"):tracked->mimeType);req.setHeader(QNetworkRequest::ContentLengthHeader,file->size());
    auto *reply=m_network.put(req,file);file->setParent(reply);setTransfer(QStringLiteral("Değişiklikler senkronlanıyor"),tracked->name,0,file->size());wireProgress(reply,QStringLiteral("Değişiklikler senkronlanıyor"),tracked->name,true);
    connect(reply,&QNetworkReply::finished,this,[this,reply,localPath](){const QByteArray p=reply->readAll();const auto e=reply->error();const int st=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();const QString f=reply->errorString();reply->deleteLater();TrackedFile *tracked=m_trackedFiles.value(localPath,nullptr);if(tracked)tracked->syncing=false;clearTransfer();ensureWatch(localPath);if(e!=QNetworkReply::NoError||st>=400)emit operationFinished(false,apiError(p,f));else{emit synced(localPath);emit operationFinished(true,QStringLiteral("Değişiklikler OneDrive'a senkronlandı."));emit refreshRequested();}if(tracked&&tracked->pending){tracked->pending=false;tracked->timer->start();}});
}
