#pragma once

#include <QFile>
#include <QString>
#include <sys/stat.h>

// SVGs often have identical sizes/timestamps (icon packs, preserved copies).
// Include the actual file and nanosecond change time so swapping their names
// invalidates both Qt's pixmap cache and the provider's disk/memory caches.
inline QString svgPreviewIdentity(const struct stat &metadata)
{
    return QStringLiteral("svg3-%1-%2-%3-%4-%5-%6-%7")
        .arg(qulonglong(metadata.st_dev)).arg(qulonglong(metadata.st_ino))
        .arg(qlonglong(metadata.st_size))
        .arg(qlonglong(metadata.st_mtim.tv_sec)).arg(qlonglong(metadata.st_mtim.tv_nsec))
        .arg(qlonglong(metadata.st_ctim.tv_sec)).arg(qlonglong(metadata.st_ctim.tv_nsec));
}

inline QString svgPreviewIdentity(const QString &path)
{
    struct stat metadata {};
    return ::stat(QFile::encodeName(path).constData(), &metadata) == 0
        ? svgPreviewIdentity(metadata) : QString();
}
