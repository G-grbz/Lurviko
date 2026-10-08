#pragma once

#include <QQuickAsyncImageProvider>
#include <QQuickImageProvider>

class ThumbnailProvider : public QQuickAsyncImageProvider
{
public:
    // Shared with MPRIS: publish the same persisted artwork as file previews.
    static QString cachedFilePath(const QString &path);
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;
};

class SystemIconProvider : public QQuickAsyncImageProvider
{
public:
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;
};
