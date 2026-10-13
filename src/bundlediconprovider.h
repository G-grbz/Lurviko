#pragma once

#include <QQuickImageProvider>

// Monochrome UI artwork is rendered in the application's foreground color.
// Colored file/category artwork continues to use its original resource.
class BundledIconProvider final : public QQuickImageProvider
{
public:
    BundledIconProvider();
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};

// Cached original-color rasters; never parse SVG on the UI/thumbnail queue.
class FolderIconProvider final : public QQuickImageProvider
{
public:
    FolderIconProvider();
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};
