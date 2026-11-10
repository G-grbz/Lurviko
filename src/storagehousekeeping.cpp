#include "storagehousekeeping.h"
#include "thumbnailprovider.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QStandardPaths>
#include <QtConcurrent>

namespace {

qint64 directoryBytes(const QString &path, int *files)
{
    qint64 total = 0;
    QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo info = it.fileInfo();
        if (info.isSymLink())
            continue;
        total += info.size();
        if (files)
            ++*files;
    }
    return total;
}

StorageHousekeeping::Usage measure(bool clearPreviews)
{
    StorageHousekeeping::Usage usage;
    const QString previews = ThumbnailProvider::cacheDirectory();
    if (clearPreviews) {
        QDir directory(previews);
        const QStringList entries = directory.entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
        for (const QString &name : entries)
            directory.remove(name);
        ThumbnailProvider::clearMemoryCache();
    }
    usage.previewBytes = directoryBytes(previews, &usage.previewFiles);

    const QString trash = StorageHousekeeping::trashFilesDirectory();
    const QDir trashDirectory(trash);
    usage.trashItems = int(trashDirectory.entryList(QDir::AllEntries | QDir::Hidden | QDir::System
                                                    | QDir::NoDotAndDotDot).size());
    usage.trashBytes = directoryBytes(trash, nullptr);
    return usage;
}

} // namespace

StorageHousekeeping::StorageHousekeeping(QObject *parent)
    : QObject(parent)
{
    connect(&m_watcher, &QFutureWatcher<Usage>::finished, this, [this]() {
        m_usage = m_watcher.result();
        m_measured = true;
        m_busy = false;
        emit changed();
        emit busyChanged();
        if (m_pending) {
            const bool clear = m_pendingClear;
            m_pending = false;
            m_pendingClear = false;
            start(clear);
        }
    });
}

StorageHousekeeping::~StorageHousekeeping()
{
    m_watcher.waitForFinished();
}

QString StorageHousekeeping::trashFilesDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Trash/files");
}

void StorageHousekeeping::start(bool clearPreviews)
{
    if (m_busy) {
        m_pending = true;
        m_pendingClear = m_pendingClear || clearPreviews;
        return;
    }
    m_busy = true;
    emit busyChanged();
    m_watcher.setFuture(QtConcurrent::run([clearPreviews]() { return measure(clearPreviews); }));
}

void StorageHousekeeping::refresh()
{
    start(false);
}

void StorageHousekeeping::clearPreviewCache()
{
    start(true);
}
