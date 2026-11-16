#pragma once

#include <QImage>
#include <QString>

// Preview generators for formats that need their own parsers: Android
// packages, Windows executables, desktop launchers, Blender/3MF models and
// FictionBook/Mobipocket e-books. Each one only reads the file, never runs
// code from it, and returns a null image when the file has nothing to show.
// All functions are safe to call from the thumbnail worker threads.
namespace ThumbnailExtractors {

// Call once on the GUI thread so desktop launchers resolve names from the
// same icon theme as the rest of the application.
void captureIconTheme();

// APK launcher icon, including adaptive icons whose layers are bitmaps or
// vector drawables. XAPK/APKS/APKM bundles use their icon or base split.
QImage androidPackageIcon(const QString &path);
// The first icon group of a PE executable or library (EXE, DLL…).
QImage windowsExecutableIcon(const QString &path);
// The Icon= key of a .desktop file, resolved through the icon theme.
QImage desktopEntryIcon(const QString &path);
// The thumbnail Blender embeds in .blend files (plain, gzip or zstd).
QImage blenderThumbnail(const QString &path);
// The package thumbnail of a 3MF model or slicer project.
QImage threeMfThumbnail(const QString &path);
// FictionBook (.fb2, .fbz, .fb2.zip) cover page.
QImage fictionBookCover(const QString &path);
// Mobipocket/Kindle (.mobi, .azw, .azw3, .prc) cover record.
QImage mobipocketCover(const QString &path);

} // namespace ThumbnailExtractors
