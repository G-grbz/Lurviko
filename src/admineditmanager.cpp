#include "admineditmanager.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <KIO/FileCopyJob>
#include <KIO/Job>
#include <KIO/OpenUrlJob>
#include <KJob>

AdminEditManager::AdminEditManager(QObject *parent)
    : QObject(parent)
    , m_tempDir(QDir::tempPath() + QStringLiteral("/g-file-admin-XXXXXX"))
{
    if (m_tempDir.isValid()) {
        QFile::setPermissions(m_tempDir.path(),
                              QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }

    m_syncTimer.setSingleShot(true);
    m_syncTimer.setInterval(700);
    m_pollTimer.setInterval(900);

    connect(&m_syncTimer, &QTimer::timeout, this, &AdminEditManager::syncDirtyFiles);
    connect(&m_pollTimer, &QTimer::timeout, this, &AdminEditManager::scanTrackedFiles);
    m_pollTimer.start();

    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this,
            [this](const QString &path) {
        // Editors such as Kate commonly save atomically (write temporary file,
        // rename over the original). QFileSystemWatcher then drops the file
        // watch because the inode changed. Rearm it after the replacement and
        // inspect the actual contents.
        QTimer::singleShot(0, this, [this, path]() {
            rearmWatch(path);
            markDirty(path);
        });
    });

    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this,
            [this](const QString &directory) {
        const auto paths = m_adminUrls.keys();
        for (const QString &path : paths) {
            if (QFileInfo(path).absolutePath() != directory)
                continue;
            QTimer::singleShot(40, this, [this, path]() {
                rearmWatch(path);
                markDirty(path);
            });
        }
    });
}

AdminEditManager::Fingerprint AdminEditManager::fingerprintFor(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile())
        return {};

    QCryptographicHash hash(QCryptographicHash::Sha256);
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        while (!file.atEnd())
            hash.addData(file.read(256 * 1024));
    }

    return {info.lastModified().toMSecsSinceEpoch(), info.size(), hash.result()};
}

QString AdminEditManager::safeFileName(const QUrl &url)
{
    QString name = QFileInfo(url.path()).fileName();
    if (name.isEmpty())
        name = QStringLiteral("admin-file");
    name.replace('/', '_');
    name.replace('\\', '_');
    return name;
}

QString AdminEditManager::localPathFor(const QUrl &adminUrl) const
{
    const QByteArray hash = QCryptographicHash::hash(adminUrl.toEncoded(), QCryptographicHash::Sha256)
                                .toHex().left(12);
    return QDir(m_tempDir.path()).filePath(QString::fromLatin1(hash) + QStringLiteral("-") + safeFileName(adminUrl));
}

void AdminEditManager::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void AdminEditManager::openUrl(const QString &urlString)
{
    const QUrl url = QUrl::fromUserInput(urlString);
    if (!url.isValid()) {
        emit error(QStringLiteral("Invalid URL"));
        return;
    }

    if (url.scheme().compare(QStringLiteral("admin"), Qt::CaseInsensitive) != 0) {
        auto *openJob = new KIO::OpenUrlJob(url, this);
        openJob->setRunExecutables(true);
        connect(openJob, &KJob::result, this, [this, openJob]() {
            if (openJob->error())
                emit error(openJob->errorString());
            // Normal user-owned files are not administrator edit sessions.
            // Do not emit opened() here; BrowsePage uses that signal for the
            // privileged temporary-edit toast only.
        });
        openJob->start();
        return;
    }

    if (!m_tempDir.isValid()) {
        emit error(QStringLiteral("Could not create a secure temporary directory"));
        return;
    }

    const QString localPath = localPathFor(url);
    const QUrl localUrl = QUrl::fromLocalFile(localPath);

    setBusy(true);
    auto *job = KIO::file_copy(url, localUrl, -1,
                               KIO::HideProgressInfo | KIO::Overwrite);

    connect(job, &KJob::result, this, [this, job, localPath, localUrl, url]() {
        setBusy(false);
        if (job->error()) {
            emit error(job->errorString());
            return;
        }

        QFile::setPermissions(localPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        trackFile(localPath, url);

        auto *openJob = new KIO::OpenUrlJob(localUrl, this);
        openJob->setRunExecutables(false);
        connect(openJob, &KJob::result, this, [this, openJob]() {
            if (openJob->error())
                emit error(openJob->errorString());
            else
                emit opened();
        });
        openJob->start();
    });
}

void AdminEditManager::trackFile(const QString &localPath, const QUrl &adminUrl)
{
    m_adminUrls.insert(localPath, adminUrl);
    m_lastSynced.insert(localPath, fingerprintFor(localPath));

    rearmWatch(localPath);
}

void AdminEditManager::rearmWatch(const QString &localPath)
{
    if (!QFileInfo::exists(localPath))
        return;

    if (!m_watcher.files().contains(localPath))
        m_watcher.addPath(localPath);

    const QString directory = QFileInfo(localPath).absolutePath();
    if (!m_watcher.directories().contains(directory))
        m_watcher.addPath(directory);
}

void AdminEditManager::scanTrackedFiles()
{
    const auto paths = m_adminUrls.keys();
    for (const QString &path : paths) {
        rearmWatch(path);
        markDirty(path);
    }
}

void AdminEditManager::markDirty(const QString &localPath)
{
    if (!m_adminUrls.contains(localPath) || !QFileInfo::exists(localPath))
        return;

    const Fingerprint current = fingerprintFor(localPath);
    if (current == m_lastSynced.value(localPath))
        return;

    m_dirty.insert(localPath);
    m_syncTimer.start();
}

void AdminEditManager::syncDirtyFiles()
{
    const auto dirty = m_dirty.values();
    m_dirty.clear();
    for (const QString &path : dirty)
        syncOne(path);
}

void AdminEditManager::syncOne(const QString &localPath)
{
    if (!m_adminUrls.contains(localPath) || m_syncing.contains(localPath) || !QFileInfo::exists(localPath))
        return;

    const Fingerprint current = fingerprintFor(localPath);
    if (current == m_lastSynced.value(localPath))
        return;

    m_syncing.insert(localPath);
    const QUrl source = QUrl::fromLocalFile(localPath);
    const QUrl destination = m_adminUrls.value(localPath);

    auto *job = KIO::file_copy(source, destination, -1,
                               KIO::HideProgressInfo | KIO::Overwrite);

    connect(job, &KJob::result, this, [this, job, localPath, current]() {
        m_syncing.remove(localPath);
        if (job->error()) {
            emit error(job->errorString());
            // Preserve the dirty state so a subsequent change/save can retry.
            m_dirty.insert(localPath);
            return;
        }

        m_lastSynced.insert(localPath, current);
        rearmWatch(localPath);
        emit saved();

        // If the editor wrote again while the KIO job was running, schedule another pass.
        if (fingerprintFor(localPath) != m_lastSynced.value(localPath)) {
            m_dirty.insert(localPath);
            m_syncTimer.start();
        }
    });
}
