import QtQuick
import QtQuick.Window
import Lurviko.App

// Viewer glyph. Sources come from AppTheme.icon(name, colour), whose bundled
// icon provider already renders the glyph in that colour, so no colourising
// shader is needed (the old MultiEffect pass drew nothing on some renderers
// and cost an effect per button).
Item {
    id: root
    property url source
    // Kept for API compatibility: pass the same colour to AppTheme.icon().
    property color tintColor: AppTheme.viewerText
    property real iconOpacity: 1.0
    readonly property real effectiveDpr: Math.max(1.0, Screen.devicePixelRatio)

    function safeExtent(value) {
        const pixels = Number(value) * effectiveDpr
        if (!isFinite(pixels) || pixels <= 0)
            return 1
        return Math.max(1, Math.min(512, Math.round(pixels)))
    }

    Image {
        anchors.fill: parent
        source: root.source
        sourceSize.width: root.safeExtent(root.width)
        sourceSize.height: root.safeExtent(root.height)
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: false
        cache: true
        asynchronous: true
        opacity: root.iconOpacity
    }
}
