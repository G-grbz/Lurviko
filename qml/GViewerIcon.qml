import QtQuick
import QtQuick.Effects
import QtQuick.Window
import GFile.App

Item {
    id: root
    property url source
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
        id: sourceImage
        anchors.fill: parent
        source: root.source
        sourceSize.width: root.safeExtent(root.width)
        sourceSize.height: root.safeExtent(root.height)
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: false
        cache: true
        asynchronous: true
        visible: false
    }

    MultiEffect {
        anchors.fill: parent
        source: sourceImage
        colorization: 1.0
        colorizationColor: root.tintColor
        opacity: root.iconOpacity
        autoPaddingEnabled: false
    }
}
