#include "sambashareindex.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>

namespace {

struct Loaded {
    QSet<QString> paths;
    QString directory;
    bool directoryReadable = false;
};

QString cleanLocal(const QString &value)
{
    const QUrl url(value);
    const QString path = url.isLocalFile() ? url.toLocalFile() : value;
    if (!QDir::isAbsolutePath(path))
        return {};
    return QDir::cleanPath(path);
}

QString runOnce(const QString &program, const QStringList &args)
{
    if (program.isEmpty())
        return {};
    QProcess process;
    process.start(program, args);
    process.closeWriteChannel();
    if (!process.waitForFinished(5000)) {
        process.kill();
        process.waitForFinished(1000);
        return {};
    }
    return QString::fromUtf8(process.readAllStandardOutput().left(1024 * 1024));
}

void addPath(QSet<QString> &paths, const QString &value)
{
    const QString path = cleanLocal(value.trimmed());
    if (path.isEmpty())
        return;
    paths.insert(path);
    // Views may list the folder through a symlinked parent or by its real
    // path; both spellings get the badge.
    const QString canonical = QFileInfo(path).canonicalFilePath();
    if (!canonical.isEmpty())
        paths.insert(canonical);
}

Loaded load(QString directory, bool resolve)
{
    Loaded result;
    if (resolve && directory.isEmpty()) {
        const QString fallback = QStringLiteral("/var/lib/samba/usershares");
        if (QFileInfo(fallback).isDir()) {
            directory = fallback;
        } else {
            const QString configured = runOnce(QStandardPaths::findExecutable(QStringLiteral("testparm")),
                                               {"-s", "--parameter-name", "usershare path"}).trimmed();
            if (QDir::isAbsolutePath(configured))
                directory = QDir::cleanPath(configured);
        }
    }
    result.directory = directory;
    const QDir dir(directory);
    if (!directory.isEmpty() && dir.exists() && dir.isReadable()) {
        result.directoryReadable = true;
        const auto entries = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name);
        int count = 0;
        for (const QFileInfo &entry : entries) {
            if (++count > 4096)
                break;
            if (entry.size() > 64 * 1024 || entry.isSymLink())
                continue;
            QFile file(entry.filePath());
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
                continue;
            for (const QString &line : QString::fromUtf8(file.read(64 * 1024)).split('\n')) {
                if (line.startsWith(QLatin1String("path=")))
                    addPath(result.paths, line.mid(5));
            }
        }
        return result;
    }
    // Not readable (or unknown): ask Samba itself.
    const QString output = runOnce(QStandardPaths::findExecutable(QStringLiteral("net")),
                                   {"--long", "--", "usershare", "info"});
    for (const QString &raw : output.split('\n')) {
        const QString line = raw.trimmed();
        if (line.startsWith(QLatin1String("path=")))
            addPath(result.paths, line.mid(5));
    }
    return result;
}

} // namespace

SambaShareIndex *SambaShareIndex::instance()
{
    static SambaShareIndex *index = new SambaShareIndex;
    return index;
}

SambaShareIndex::SambaShareIndex(QObject *parent)
    : QObject(parent)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(250);
    connect(&m_debounce, &QTimer::timeout, this, &SambaShareIndex::refresh);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_debounce, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_debounce, qOverload<>(&QTimer::start));
    // Nothing is read until a view asks; the first isShared() starts it.
}

void SambaShareIndex::setDirectoryForTest(const QString &directory)
{
    m_forTest = true;
    m_directory = directory;
    m_directoryResolved = true;
    refresh();
}

QStringList SambaShareIndex::sharedPaths() const
{
    QStringList list(m_paths.cbegin(), m_paths.cend());
    list.sort();
    return list;
}

bool SambaShareIndex::isShared(const QString &pathOrUrl) const
{
    if (!m_directoryResolved && !m_loading)
        const_cast<SambaShareIndex *>(this)->refresh();
    if (m_paths.isEmpty())
        return false;
    const QString path = cleanLocal(pathOrUrl);
    return !path.isEmpty() && m_paths.contains(path);
}

void SambaShareIndex::refresh()
{
    if (m_loading) {
        m_reloadPending = true;
        return;
    }
    m_loading = true;
    const quint64 generation = ++m_generation;
    const bool resolve = !m_directoryResolved && !m_forTest;
    auto *watcher = new QFutureWatcher<Loaded>(this);
    connect(watcher, &QFutureWatcher<Loaded>::finished, this, [this, watcher, generation] {
        const Loaded loaded = watcher->result();
        watcher->deleteLater();
        m_loading = false;
        if (generation == m_generation) {
            m_directory = loaded.directory;
            m_directoryResolved = true;
            if (loaded.directoryReadable)
                watchDirectory();
            if (loaded.paths != m_paths) {
                m_paths = loaded.paths;
                ++m_revision;
                emit changed();
            }
        }
        if (m_reloadPending) {
            m_reloadPending = false;
            refresh();
        }
    });
    watcher->setFuture(QtConcurrent::run(load, m_directory, resolve));
}

void SambaShareIndex::watchDirectory()
{
    if (m_directory.isEmpty() || m_watcher.directories().contains(m_directory))
        return;
    m_watcher.addPath(m_directory);
}
