#include "bundlediconprovider.h"

#include <QColor>
#include <QImageReader>
#include <QPainter>
#include <QRegularExpression>
#include <QUrl>

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
    QImageReader reader(QStringLiteral(":/qt/qml/Lurviko/App/assets/icons/") + name);
    const QSize extent(qBound(1, requestedSize.width() > 0 ? requestedSize.width() : 24, 512),
                       qBound(1, requestedSize.height() > 0 ? requestedSize.height() : 24, 512));
    const QSize original = reader.size();
    reader.setScaledSize(original.isValid() ? original.scaled(extent, Qt::KeepAspectRatio) : extent);
    QImage result = reader.read().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (result.isNull())
        return {};
    QPainter painter(&result);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(result.rect(), foreground);
    painter.end();
    if (size) *size = result.size();
    return result;
}
