#pragma once

#include <QImage>
#include <QSize>
#include <QString>

#include <atomic>

class QFileInfo;

// On-disk preview storage.
//
// Raster images and PDFs use the freedesktop.org shared thumbnail repository
// ($XDG_CACHE_HOME/thumbnails), so previews made by Dolphin, Gwenview,
// Nautilus… are reused and Lurviko's are reused by them. Everything rendered in
// Lurviko's own style (video frames, covers, text pages, launchers, packages,
// SVGs keyed by content identity…) stays in the private repository, which is
// size-capped and pruned by scheduleMaintenance().
namespace ThumbnailCache {

// Lurviko's own previews: PNG files and ".miss" markers.
QString privateDirectory();
// The shared repository root (normal/, large/, x-large/, xx-large/, fail/).
QString sharedDirectory();

// True for a path inside either repository. Previewing those files would write
// new thumbnails into the very folder being shown, which the directory watcher
// reports as a change, which re-lists the folder and requests more previews.
// The thumbnail specification forbids it as well.
bool isInsideRepository(const QString &path);

// Whether this file's preview belongs in the shared repository: a plain
// rendering any file manager would produce, for a file that is not in a
// temporary or runtime (vault) location.
bool usesSharedRepository(const QString &kind, const QString &suffix, const QString &absolutePath);

// A valid shared thumbnail at least as large as the requested size, or null.
// Entries whose Thumb::MTime no longer matches the file are ignored.
QImage loadShared(const QFileInfo &info, const QSize &target);
// Stores an (at most 512 px) preview as an x-large shared thumbnail with the
// metadata the specification requires, atomically and with 0600 permissions.
bool saveShared(const QFileInfo &info, const QImage &image, const QString &mimeType);
// Path the shared x-large thumbnail of a file is stored at.
QString sharedFilePath(const QString &absolutePath, const QString &bucket = QStringLiteral("x-large"));

// Refreshes a private entry's modification time when it is used, so pruning
// removes what has not been shown for a long time rather than what is old.
void markUsed(const QString &privateFile);

struct MaintenanceResult {
    qint64 removedFiles = 0;
    qint64 removedBytes = 0;
    qint64 keptFiles = 0;
    qint64 keptBytes = 0;
};
// Removes private entries unused for maxAgeDays, leftover render files, and
// then the least recently used entries until the total is at most 80 % of
// capBytes. Runs in two streaming passes, so memory use does not grow with
// the number of entries.
MaintenanceResult maintain(const QString &directory, qint64 capBytes, int maxAgeDays,
                           qint64 nowSecs, const std::atomic_bool *stop = nullptr);

constexpr qint64 defaultCapBytes = 2LL * 1024 * 1024 * 1024;
constexpr int defaultMaxAgeDays = 90;

// Prunes the private repository on an idle background thread, at most once a
// day, shortly after startup. Safe to call more than once.
void scheduleMaintenance();

} // namespace ThumbnailCache
