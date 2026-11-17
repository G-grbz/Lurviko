#include "imagegeometry.h"

#include <QImageIOHandler>
#include <QImageReader>
#include <QUrl>

#include <algorithm>
#include <cmath>

ImageGeometry::ImageGeometry(QObject *parent)
    : QObject(parent)
{
}

QSize ImageGeometry::pixelSize(const QString &urlOrPath) const
{
    if (urlOrPath.isEmpty())
        return {};
    QString path = urlOrPath;
    if (urlOrPath.contains(QLatin1String("://"))) {
        const QUrl url(urlOrPath);
        if (!url.isLocalFile())
            return {};
        path = url.toLocalFile();
    }
    if (path.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive)
            || path.endsWith(QLatin1String(".svgz"), Qt::CaseInsensitive)
            || path.endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive))
        return {};

    QImageReader reader(path);
    QSize size = reader.size();
    if (!size.isValid())
        return {};
    if (reader.transformation() & QImageIOHandler::TransformationRotate90)
        size.transpose();
    return size;
}

QSize ImageGeometry::boundedSourceSize(const QString &urlOrPath, int longSide) const
{
    const QSize native = pixelSize(urlOrPath);
    if (!native.isValid() || longSide <= 0)
        return {};
    const int nativeLong = std::max(native.width(), native.height());
    const int nativeShort = std::min(native.width(), native.height());
    if (nativeLong <= longSide)
        return {};
    const qreal scale = qreal(longSide) / qreal(nativeLong);
    const int side = std::max(1, int(std::lround(nativeShort * scale)));
    return QSize(side, side);
}
