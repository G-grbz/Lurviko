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
