import QtQuick
import QtQuick.Window

Image {
    id: icon

    // Render vector and themed icons at the exact physical size used by the
    // screen. Oversampling them 3–4x and then enabling mipmaps softened thin
    // strokes, reduced color saturation and looked uneven at small sizes.
    readonly property real effectiveDpr: Math.max(1.0, Screen.devicePixelRatio)

    // QML layouts can briefly expose invalid/very large geometry while icon
    // mode is being switched. Never let that transient geometry turn into a
    // gigantic SVG decode request (Qt's image reader will otherwise retry the
    // failing request as bindings settle and can burn CPU).
    function safeSourceExtent(value) {
        var pixels = Number(value) * effectiveDpr
        if (!isFinite(pixels) || pixels <= 0)
            return 1
        return Math.max(1, Math.min(512, Math.round(pixels)))
    }

    sourceSize.width: safeSourceExtent(width)
    sourceSize.height: safeSourceExtent(height)
    fillMode: Image.PreserveAspectFit
    smooth: true
    mipmap: false
    cache: true
    asynchronous: true
    retainWhileLoading: true
}
