import QtQuick
import QtQuick.Window

Image {
    id: icon

    // Render vector and themed icons at the exact physical size used by the
    // screen. Oversampling them 3–4x and then enabling mipmaps softened thin
    // strokes, reduced color saturation and looked uneven at small sizes.
    readonly property real effectiveDpr: Math.max(1.0, Screen.devicePixelRatio)

    // Layouts briefly expose zero/one-pixel geometry before their first polish.
    // QtSvg can round filtered artwork's intermediate bounds to 0x0 at that
    // size (notably drive.svg on Qt 6.12). Keep a small usable decode canvas;
    // Image still displays it at the exact item size. Also cap oversized input.
    function safeSourceExtent(value) {
        const sourceName = String(icon.source)
        // Providers already bound their rendering and folderart deliberately
        // returns a transparent pixel until the layout has usable geometry.
        const minimum = sourceName.indexOf("image://") !== 0 && /\.svgz?$/i.test(sourceName) ? 16 : 1
        var pixels = Number(value) * effectiveDpr
        if (!isFinite(pixels) || pixels <= 0)
            return minimum
        return Math.max(minimum, Math.min(512, Math.round(pixels)))
    }

    sourceSize.width: safeSourceExtent(width)
    sourceSize.height: safeSourceExtent(height)
    fillMode: Image.PreserveAspectFit
    smooth: true
    mipmap: false
    cache: true
    // Baked/cached folder pixels arrive in this frame, without waiting behind
    // media thumbnails. All artwork that still needs decoding remains async.
    asynchronous: String(source).indexOf("image://folderart/") !== 0
    retainWhileLoading: true
}
