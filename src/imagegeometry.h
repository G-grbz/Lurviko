#pragma once

#include <QObject>
#include <QSize>
#include <QString>

// Reads an image's pixel size from its header, without decoding it.
//
// Qt Quick's Image treats sourceSize as a box to *cover* when fillMode is
// PreserveAspectFit, and it also scales up: a 600×400 file with a
// 1200×1200 sourceSize is decoded at 1800×1200. Views that bound their
// decode size therefore need the real size to stay at or below it.
class ImageGeometry : public QObject
{
    Q_OBJECT
public:
    explicit ImageGeometry(QObject *parent = nullptr);

    // Size after the EXIF orientation is applied; invalid for non-local
    // URLs, unreadable files and scalable formats (SVG, PDF).
    Q_INVOKABLE QSize pixelSize(const QString &urlOrPath) const;

    // sourceSize that decodes the picture with its long side at about
    // longSide device pixels and never above its real size. The box is
    // square on the image's short side, so the cover rule of
    // PreserveAspectFit yields exactly that scale in either orientation.
    // Returns an invalid size when the native size should be used.
    Q_INVOKABLE QSize boundedSourceSize(const QString &urlOrPath, int longSide) const;
};
