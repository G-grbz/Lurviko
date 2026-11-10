#include "bundlediconprovider.h"

#include <QColor>
#include <QImageReader>
#include <QPainter>
#include <QRegularExpression>
#include <QUrl>
#include <QCache>
#include <QMutex>
#include <QMutexLocker>

BundledIconProvider::BundledIconProvider()
    : QQuickImageProvider(Image, ForceAsynchronousImageLoading)
{
}

namespace {

// Rendered icons are small and few, but Qt's own cache of unreferenced
// pixmaps is only a couple of MiB: views that recycle delegates while
// scrolling asked for the same artwork again and again, and each request
// parsed and rasterised its SVG from scratch. Costs are KiB.
struct RenderedIconCache {
    explicit RenderedIconCache(int maxKiB) : images(maxKiB) {}
    QMutex mutex;
    QCache<QString, QImage> images;

    QImage find(const QString &key)
    {
        QMutexLocker lock(&mutex);
        const QImage *hit = images.object(key);
        return hit ? *hit : QImage();
    }
    void store(const QString &key, const QImage &image)
    {
        QMutexLocker lock(&mutex);
        images.insert(key, new QImage(image), qMax<qsizetype>(1, (image.sizeInBytes() + 1023) / 1024));
    }
};

QString sizedKey(const QString &id, const QSize &extent)
{
    return id + QLatin1Char('#') + QString::number(extent.width())
        + QLatin1Char('x') + QString::number(extent.height());
}

} // namespace

QImage BundledIconProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    // Controls' IconImage percent-encodes the separator as well as the color;
    // ordinary Image may pass it literally. Accept both URL representations.
    const QString decoded = QUrl::fromPercentEncoding(id.toUtf8());
    const QString name = decoded.section(QLatin1Char('|'), 0, 0);
    static const QRegularExpression safeName(QStringLiteral("^[a-z0-9-]+\\.svg$"));
    const QColor foreground(decoded.section(QLatin1Char('|'), 1, 1));
    if (!safeName.match(name).hasMatch() || !foreground.isValid())
        return {};
    const QSize extent(qBound(1, requestedSize.width() > 0 ? requestedSize.width() : 24, 512),
                       qBound(1, requestedSize.height() > 0 ? requestedSize.height() : 24, 512));
    static RenderedIconCache cache(8 * 1024);
    const QString key = sizedKey(name + QLatin1Char('|') + foreground.name(QColor::HexArgb), extent);
    QImage result = cache.find(key);
    if (result.isNull()) {
        const QString resource = QStringLiteral(":/qt/qml/Lurviko/App/assets/icons/") + name;
        QImageReader reader(resource);
        const QSize original = reader.size();
        reader.setScaledSize(original.isValid() ? original.scaled(extent, Qt::KeepAspectRatio) : extent);
        result = reader.read().convertToFormat(QImage::Format_ARGB32_Premultiplied);
        if (result.isNull())
            return {};
        QPainter painter(&result);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(result.rect(), foreground);
        painter.end();
        cache.store(key, result);
    }
    if (size) *size = result.size();
    return result;
}

ArtworkIconProvider::ArtworkIconProvider()
    : QQuickImageProvider(Image, ForceAsynchronousImageLoading)
{
}

QImage ArtworkIconProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    const QString name = QUrl::fromPercentEncoding(id.toUtf8());
    static const QRegularExpression safeName(QStringLiteral("^[A-Za-z0-9._+-]+\\.(?:svg|png)$"));
    if (!safeName.match(name).hasMatch() || name.startsWith(QLatin1Char('.')))
        return {};
    const QString resource = QStringLiteral(":/qt/qml/Lurviko/App/assets/icons/") + name;
    // An Image without sourceSize keeps the artwork's own size, as it did
    // when it loaded the resource directly.
    QSize extent = requestedSize;
    if (extent.width() <= 0 || extent.height() <= 0) {
        const QSize natural = QImageReader(resource).size();
        if (!natural.isValid())
            return {};
        extent = requestedSize.width() > 0 ? natural.scaled(requestedSize.width(), INT_MAX, Qt::KeepAspectRatio)
               : requestedSize.height() > 0 ? natural.scaled(INT_MAX, requestedSize.height(), Qt::KeepAspectRatio)
               : natural;
    }
    extent = QSize(qBound(1, extent.width(), 1024), qBound(1, extent.height(), 1024));
    static RenderedIconCache cache(24 * 1024);
    const QString key = sizedKey(name, extent);
    QImage result = cache.find(key);
    if (result.isNull()) {
        QImageReader reader(resource);
        const QSize original = reader.size();
        reader.setScaledSize(original.isValid() ? original.scaled(extent, Qt::KeepAspectRatio) : extent);
        result = reader.read().convertToFormat(QImage::Format_ARGB32_Premultiplied);
        if (result.isNull())
            return {};
        cache.store(key, result);
    }
    if (size) *size = result.size();
    return result;
}

FolderIconProvider::FolderIconProvider() : QQuickImageProvider(Image) {}

QImage FolderIconProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    static const QRegularExpression safeName(QStringLiteral(
        "^(?:folder|(?:blue|green|orange|pink|purple|red|turquoise|yellow)-(?:folder|documents|download|music|picture|video))\\.svg$"));
    if (!safeName.match(id).hasMatch()) return {};
    const QSize extent(qBound(1, requestedSize.width() > 0 ? requestedSize.width() : 128, 512),
                       qBound(1, requestedSize.height() > 0 ? requestedSize.height() : 128, 512));
    // Layouts briefly expose one-pixel geometry; avoid decoding throwaway art.
    if (qMax(extent.width(), extent.height()) <= 1) {
        QImage empty(1, 1, QImage::Format_ARGB32_Premultiplied);
        empty.fill(Qt::transparent);
        if (size) *size = empty.size();
        return empty;
    }
    struct Cache {
        QMutex mutex;
        QCache<QString, QImage> rasters{8 * 1024};
        QCache<QString, QImage> scaled{16 * 1024};
    };
    static Cache cache;
    QMutexLocker lock(&cache.mutex);
    const QString key = id + QLatin1Char('|') + QString::number(extent.width())
        + QLatin1Char('x') + QString::number(extent.height());
    if (const QImage *hit = cache.scaled.object(key)) {
        if (size) *size = hit->size();
        return *hit;
    }
    const int bucket = ((qMax(extent.width(), extent.height()) + 63) / 64) * 64;
    const QString resource = QStringLiteral(":/qt/qml/Lurviko/App/assets/folder-raster/")
        + id + QLatin1Char('/') + QString::number(bucket) + QStringLiteral(".png");
    QImage raster;
    if (const QImage *hit = cache.rasters.object(resource)) raster = *hit;
    else {
        raster = QImage(resource).convertToFormat(QImage::Format_ARGB32_Premultiplied);
        if (raster.isNull()) return {};
        cache.rasters.insert(resource, new QImage(raster), (raster.sizeInBytes() + 1023) / 1024);
    }
    const QImage result = raster.scaled(extent, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    cache.scaled.insert(key, new QImage(result), qMax<qsizetype>(1, (result.sizeInBytes() + 1023) / 1024));
    if (size) *size = result.size();
    return result;
}
