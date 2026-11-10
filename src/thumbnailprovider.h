#pragma once

#include <QQuickAsyncImageProvider>
#include <QQuickImageProvider>

class ThumbnailProvider : public QQuickAsyncImageProvider
{
public:
    // Shared with MPRIS: publish the same persisted artwork as file previews.
    static QString cachedFilePath(const QString &path);
    // Directory holding the persisted previews (PNG and ".miss" markers).
    static QString cacheDirectory();
    // Drops the in-memory previews, e.g. after the disk cache was cleared.
    static void clearMemoryCache();
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;
};

class SystemIconProvider : public QQuickAsyncImageProvider
{
public:
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;
};
