#include "bundlediconprovider.h"

#include <QColor>
#include <QFile>
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
    const QString resource = QStringLiteral(":/qt/qml/Lurviko/App/assets/icons/") + name;
    QFile artwork(resource);
    const bool fontAwesome = artwork.open(QIODevice::ReadOnly)
        && artwork.read(4096).contains("Font Awesome");
    QImageReader reader(resource);
    const QSize extent(qBound(1, requestedSize.width() > 0 ? requestedSize.width() : 24, 512),
                       qBound(1, requestedSize.height() > 0 ? requestedSize.height() : 24, 512));
    const QSize original = reader.size();
    // SVG canvases have different built-in padding. Give up/history/search
    // the same visible extent as back/forward, without editing the artwork.
    qreal canvasScale = 1.0;
    if (fontAwesome) {
        if (name == QStringLiteral("nav-up.svg")) canvasScale = 0.8;
        else if (name == QStringLiteral("history.svg")) canvasScale = 0.75;
        else if (name == QStringLiteral("search.svg")) canvasScale = 0.625;
    }
    const bool normalizeNavigation = canvasScale < 1.0;
    const QSize renderExtent = normalizeNavigation
        ? QSize(qMax(1, qRound(extent.width() * canvasScale)), qMax(1, qRound(extent.height() * canvasScale)))
        : extent;
    reader.setScaledSize(original.isValid() ? original.scaled(renderExtent, Qt::KeepAspectRatio) : renderExtent);
    QImage result = reader.read().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (result.isNull())
        return {};
    if (normalizeNavigation) {
        QImage padded(extent, QImage::Format_ARGB32_Premultiplied);
        padded.fill(Qt::transparent);
        QPainter canvas(&padded);
        canvas.drawImage((extent.width() - result.width()) / 2,
                         (extent.height() - result.height()) / 2, result);
        canvas.end();
        result = std::move(padded);
    }
    if (fontAwesome) {
        // These glyphs are filled outlines, not SVG strokes. A fractional
        // erosion of the alpha silhouette gently reduces their visual weight;
        // no blur, larger texture or SVG mutation is involved. Keep solid
        // interiors intact and apply this only to bundled Font Awesome UI art.
        const QImage source = result;
        const qreal amount = qBound(qreal(0.15), qMax(extent.width(), extent.height()) / 96.0, qreal(0.45));
        for (int y = 0; y < result.height(); ++y) {
            auto *target = reinterpret_cast<QRgb *>(result.scanLine(y));
            const auto *row = reinterpret_cast<const QRgb *>(source.constScanLine(y));
            const auto *above = y > 0 ? reinterpret_cast<const QRgb *>(source.constScanLine(y - 1)) : nullptr;
            const auto *below = y + 1 < source.height() ? reinterpret_cast<const QRgb *>(source.constScanLine(y + 1)) : nullptr;
            for (int x = 0; x < result.width(); ++x) {
                const int alpha = qAlpha(row[x]);
                int neighbor = qMin(alpha, qMin(above ? qAlpha(above[x]) : 0, below ? qAlpha(below[x]) : 0));
                neighbor = qMin(neighbor, x > 0 ? qAlpha(row[x - 1]) : 0);
                neighbor = qMin(neighbor, x + 1 < source.width() ? qAlpha(row[x + 1]) : 0);
                target[x] = qRgba(0, 0, 0, alpha - qRound((alpha - neighbor) * amount));
            }
        }
    }
    QPainter painter(&result);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(result.rect(), foreground);
    painter.end();
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
