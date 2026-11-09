#include "folderstats.h"

#include "directorysizeindex.h"

#include <QDir>
#include <QUrl>

FolderStats::FolderStats(QObject* parent)
    : QObject(parent)
{
    connect(DirectorySizeIndex::instance(), &DirectorySizeIndex::snapshotChanged, this,
            [this](const QString& changed) {
                if (!m_path.isEmpty() && changed == m_path)
                    reload();
            });
}

void FolderStats::setPath(const QString& value)
{
    const QUrl url(value);
    QString local = url.isLocalFile() ? url.toLocalFile() : (value.startsWith(QLatin1Char('/')) ? value : QString());
    if (!local.isEmpty())
        local = QDir::cleanPath(local);
    // Only folders inside the home directory: the startup warmup has already
    // indexed them, so this is a cached read. Shortcuts to other disks or to
    // remote locations would otherwise start a fresh scan just for a label.
    const QString home = QDir::cleanPath(QDir::homePath());
    if (!(local == home || local.startsWith(home + QLatin1Char('/'))))
        local.clear();
    if (local == m_path)
        return;
    m_path = local;
    emit pathChanged();
    if (!m_path.isEmpty())
        DirectorySizeIndex::instance()->request(m_path);
    reload();
}

void FolderStats::reload()
{
    const DirectorySizeSnapshot snapshot = m_path.isEmpty()
        ? DirectorySizeSnapshot{} : DirectorySizeIndex::instance()->snapshot(m_path);
    const bool available = snapshot.available;
    const int files = available ? snapshot.totals.files : 0;
    const int folders = available ? snapshot.totals.folders : 0;
    const qint64 bytes = available ? snapshot.totals.bytes : 0;
    if (available == m_available && files == m_files && folders == m_folders && bytes == m_bytes)
        return;
    m_available = available;
    m_files = files;
    m_folders = folders;
    m_bytes = bytes;
    emit statsChanged();
}
