#include "thumbnailcache.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <cstdio>
#include <ctime>
#include <memory>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace ThumbnailCache {

namespace {

struct Bucket {
    const char *name;
    int size;
};
// freedesktop.org thumbnail sizes, smallest first.
constexpr Bucket buckets[] = {{"normal", 128}, {"large", 256}, {"x-large", 512}, {"xx-large", 1024}};

bool isUnder(const QString &path, QString directory)
{
    if (directory.isEmpty())
        return false;
    directory = QDir::cleanPath(directory);
    return path == directory || path.startsWith(directory + QLatin1Char('/'));
}

QByteArray fileUri(const QString &absolutePath)
{
    return QUrl::fromLocalFile(absolutePath).toEncoded();
}

QString modificationSeconds(const QFileInfo &info)
{
    return QString::number(info.lastModified().toSecsSinceEpoch());
}

bool statPath(const QString &path, struct stat &st)
{
    return ::lstat(QFile::encodeName(path).constData(), &st) == 0;
}

} // namespace

QString privateDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/thumbnails");
}

QString sharedDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/thumbnails");
}

bool isInsideRepository(const QString &path)
{
    if (path.isEmpty())
        return false;
    const QString clean = QDir::cleanPath(path);
    return isUnder(clean, privateDirectory()) || isUnder(clean, sharedDirectory());
}

bool usesSharedRepository(const QString &kind, const QString &suffix, const QString &absolutePath)
{
    const bool standard = kind == QLatin1String("pdf")
        // OpenRaster previews come from the embedded mergedimage, which is
        // not what other file managers would store for it.
        || (kind == QLatin1String("images") && suffix.compare(QLatin1String("ora"), Qt::CaseInsensitive) != 0);
    if (!standard)
        return false;
    // Decrypted vault files live in memory-backed runtime directories; their
    // previews must never land in a repository other applications read.
    const QString clean = QDir::cleanPath(absolutePath);
    return !isUnder(clean, QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation))
        && !isUnder(clean, QStringLiteral("/dev/shm"));
}

QString sharedFilePath(const QString &absolutePath, const QString &bucket)
{
    const QByteArray hash = QCryptographicHash::hash(fileUri(absolutePath), QCryptographicHash::Md5).toHex();
    return sharedDirectory() + QLatin1Char('/') + bucket + QLatin1Char('/')
        + QString::fromLatin1(hash) + QStringLiteral(".png");
}

QImage loadShared(const QFileInfo &info, const QSize &target)
{
    const QString path = info.absoluteFilePath();
    const QString uri = QString::fromLatin1(fileUri(path));
    const QString mtime = modificationSeconds(info);
    const int needed = qMax(target.width(), target.height());
    for (const Bucket &bucket : buckets) {
        // A smaller bucket would be upscaled into a blurry preview.
        if (bucket.size < needed)
            continue;
        const QString file = sharedFilePath(path, QString::fromLatin1(bucket.name));
        if (!QFileInfo::exists(file))
            continue;
        QImageReader reader(file, "png");
        QImage image = reader.read();
        if (image.isNull())
            continue;
        // Thumb::MTime is mandatory; an entry without it, or for an older
        // version of the file, is stale.
        if (image.text(QStringLiteral("Thumb::MTime")) != mtime)
            continue;
        const QString recorded = image.text(QStringLiteral("Thumb::URI"));
        if (!recorded.isEmpty() && QUrl(recorded).toLocalFile() != path)
            continue;
        return image;
    }
    return {};
}

bool saveShared(const QFileInfo &info, const QImage &source, const QString &mimeType)
{
    if (source.isNull())
        return false;
    const QString path = info.absoluteFilePath();
    const QString directory = sharedDirectory() + QStringLiteral("/x-large");
    if (!QFileInfo::exists(directory)) {
        if (!QDir().mkpath(directory))
            return false;
        // The specification asks for private (0700) repository directories.
        const auto owner = QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner;
        QFile::setPermissions(sharedDirectory(), owner);
        QFile::setPermissions(directory, owner);
    }

    QImage image = source;
    if (image.width() > 512 || image.height() > 512)
        image = image.scaled(QSize(512, 512), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    image.setText(QStringLiteral("Thumb::URI"), QString::fromLatin1(fileUri(path)));
    image.setText(QStringLiteral("Thumb::MTime"), modificationSeconds(info));
    image.setText(QStringLiteral("Thumb::Size"), QString::number(info.size()));
    if (!mimeType.isEmpty() && mimeType != QLatin1String("application/octet-stream"))
        image.setText(QStringLiteral("Thumb::Mimetype"), mimeType);
    image.setText(QStringLiteral("Software"), QStringLiteral("Lurviko"));

    // QTemporaryFile creates the file with 0600, as the specification asks.
    // Writing beside the target and renaming keeps readers from ever seeing a
    // half-written PNG.
    QTemporaryFile temporary(directory + QStringLiteral("/.lurviko-XXXXXX.png"));
    if (!temporary.open())
        return false;
    QImageWriter writer(&temporary, "png");
    if (!writer.write(image))
        return false;
    temporary.close();
    const QString target = sharedFilePath(path);
    if (::rename(QFile::encodeName(temporary.fileName()).constData(),
                 QFile::encodeName(target).constData()) != 0)
        return false;
    temporary.setAutoRemove(false);
    return true;
}

void markUsed(const QString &privateFile)
{
    struct stat st {};
    const QByteArray encoded = QFile::encodeName(privateFile);
    if (::stat(encoded.constData(), &st) != 0)
        return;
    // Weekly granularity is plenty for a 90-day expiry and keeps scrolling
    // through a cached folder from writing inode metadata on every visit.
    if (::time(nullptr) - st.st_mtim.tv_sec < 7 * 24 * 3600)
        return;
    ::utimensat(AT_FDCWD, encoded.constData(), nullptr, 0);
}

MaintenanceResult maintain(const QString &directory, qint64 capBytes, int maxAgeDays,
                           qint64 nowSecs, const std::atomic_bool *stop)
{
    MaintenanceResult result;
    const auto stopped = [stop] { return stop && stop->load(std::memory_order_relaxed); };
    const qint64 maxAgeHours = qint64(qMax(1, maxAgeDays)) * 24;
    // Bytes per age in hours; the second pass deletes everything at or past
    // the cutoff age. Two passes keep memory constant for 100 000s of files.
    std::vector<qint64> bytesByAge(size_t(maxAgeHours) + 1, 0);
    const auto ageHours = [nowSecs](const struct stat &st) {
        return qMax<qint64>(0, (nowSecs - qint64(st.st_mtim.tv_sec)) / 3600);
    };
    const auto remove = [&result](const QString &file, const struct stat &st) {
        if (::unlink(QFile::encodeName(file).constData()) == 0) {
            ++result.removedFiles;
            result.removedBytes += st.st_size;
            return true;
        }
        return false;
    };

    qint64 total = 0;
    {
        QDirIterator it(directory, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
        while (it.hasNext()) {
            if (stopped())
                return result;
            const QString file = it.next();
            struct stat st {};
            if (!statPath(file, st) || !S_ISREG(st.st_mode))
                continue;
            const qint64 age = ageHours(st);
            const bool leftover = it.fileName().startsWith(QLatin1String("render-")) && age >= 1;
            if (leftover || age > maxAgeHours) {
                remove(file, st);
                continue;
            }
            bytesByAge[size_t(age)] += st.st_size;
            total += st.st_size;
        }
    }

    qint64 cutoff = -1;
    if (capBytes > 0 && total > capBytes) {
        const qint64 goal = capBytes / 10 * 8;
        qint64 kept = 0;
        for (qint64 age = 0; age <= maxAgeHours; ++age) {
            kept += bytesByAge[size_t(age)];
            if (kept > goal) {
                // Keep at least the most recent hour, so the folder being
                // browsed right now does not lose its previews.
                cutoff = qMax<qint64>(1, age);
                break;
            }
        }
    }

    QDirIterator it(directory, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
    while (it.hasNext()) {
        if (stopped())
            return result;
        const QString file = it.next();
        struct stat st {};
        if (!statPath(file, st) || !S_ISREG(st.st_mode))
            continue;
        if (cutoff >= 0 && ageHours(st) >= cutoff && remove(file, st))
            continue;
        ++result.keptFiles;
        result.keptBytes += st.st_size;
    }
    return result;
}

void scheduleMaintenance()
{
    static std::atomic_bool scheduled{false};
    if (scheduled.exchange(true) || !QCoreApplication::instance())
        return;
    // Well after the first window is up and its folder has been previewed.
    QTimer::singleShot(45 * 1000, QCoreApplication::instance(), [] {
        const QString stamp = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
            + QStringLiteral("/thumbnail-maintenance.stamp");
        const QFileInfo stampInfo(stamp);
        if (stampInfo.exists()
                && stampInfo.lastModified().secsTo(QDateTime::currentDateTime()) < 24 * 3600)
            return;

        const QString directory = privateDirectory();
        auto stop = std::make_shared<std::atomic_bool>(false);
        QThread *thread = QThread::create([directory, stamp, stop] {
#ifdef SYS_ioprio_set
            // Idle I/O class: unlinking thousands of files must not compete
            // with the folder the user is looking at.
            constexpr int whoThread = 1, idleClass = 3, classShift = 13;
            ::syscall(SYS_ioprio_set, whoThread, 0, idleClass << classShift);
#endif
            maintain(directory, defaultCapBytes, defaultMaxAgeDays,
                     QDateTime::currentSecsSinceEpoch(), stop.get());
            if (stop->load())
                return;
            QDir().mkpath(QFileInfo(stamp).absolutePath());
            QFile file(stamp);
            if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
                file.close();
        });
        QObject::connect(qApp, &QCoreApplication::aboutToQuit, thread, [thread, stop] {
            stop->store(true);
            thread->wait();
        });
        QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
        thread->start(QThread::IdlePriority);
    });
}

} // namespace ThumbnailCache
