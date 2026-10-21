import QtQuick
import QtQuick.Effects
import Lurviko.App

Rectangle {
    id: surface
    property bool nativeTheme: AppTheme.useQtTheme
    property color nativeColor: AppTheme.background
    // Embedded overlays need their own window base beneath the native panel
    // tint. Otherwise replacing the captured scene also removes that base,
    // making icons behind the panel much more prominent than intended.
    property color nativeBaseColor: AppTheme.background
    property color customColor: AppTheme.surface
    property Item sourceItem: safeSource(surface.Window.window ? surface.Window.window.contentItem : null)
    readonly property rect captureRect: {
        if (!sourceItem) return Qt.rect(0, 0, 1, 1)
        // mapToItem() alone does not create binding dependencies on ancestor
        // transforms. Watch both paths, including popup animation/reparenting.
        const revision = geometryRevision(surface) + geometryRevision(sourceItem)
        if (!Number.isFinite(revision)) return Qt.rect(0, 0, 1, 1)
        return surface.mapToItem(sourceItem, Qt.rect(0, 0, surface.width, surface.height))
    }
    readonly property real sceneOpacity: {
        let result = 1
        for (let item = surface; item; item = item.parent) result *= item.opacity
        return result
    }
    readonly property bool blurActive: nativeTheme && visible && nativeColor.a < 1
        && sourceItem !== null && safeSource(sourceItem) !== null
        && sourceItem.Window.window === surface.Window.window
        && GraphicsInfo.api !== GraphicsInfo.Software

    // ApplicationWindow's content is a sibling of its popup overlay. A plain
    // Window, or an inline hover, may instead contain this surface: never
    // capture an ancestor, which would create a recursive texture dependency.
    function safeSource(candidate) {
        for (let item = surface; item; item = item.parent) {
            if (item === candidate) return null
        }
        return candidate
    }
    function geometryRevision(item) {
        let result = 0
        for (; item; item = item.parent)
            result += item.x + item.y + item.width + item.height
                    + item.scale + item.rotation + item.transformOrigin
        return result
    }

    radius: 20
    color: nativeTheme ? (blurActive ? "transparent" : AppTheme.tint(nativeColor, 1)) : customColor
    border.width: 1
    border.color: AppTheme.borderStrong
    antialiasing: true

    Loader {
        objectName: "surfaceBlurLoader"
        anchors.fill: parent
        active: surface.blurActive
        sourceComponent: Item {
            Item {
                id: compositedCapture
                anchors.fill: parent
                visible: false
                layer.enabled: true
                layer.smooth: true
                layer.textureSize: Qt.size(Math.ceil(surface.width), Math.ceil(surface.height))

                // Keep the view's original alpha. The final pass applies the
                // panel base and tint over this blurred scene, in that order.
                ShaderEffectSource {
                    id: capture
                    anchors.fill: parent
                    sourceItem: surface.sourceItem
                    sourceRect: surface.captureRect
                    // Keep both intermediate textures bounded to this surface;
                    // hiding it destroys the loader and releases the textures.
                    textureSize: Qt.size(Math.ceil(surface.width), Math.ceil(surface.height))
                    live: true
                    hideSource: false
                    recursive: false
                }
            }
            MultiEffect {
                id: blurredCapture
                anchors.fill: parent
                visible: false
                layer.enabled: true
                layer.textureSize: Qt.size(Math.ceil(surface.width), Math.ceil(surface.height))
                source: compositedCapture
                blurEnabled: true
                blur: 0.7
                blurMax: 32
                autoPaddingEnabled: false
            }
            ShaderEffect {
                anchors.fill: parent
                property var blurredSource: blurredCapture
                property var originalSource: compositedCapture
                property vector2d surfaceSize: Qt.vector2d(width, height)
                property real cornerRadius: surface.radius
                property color nativeTint: surface.nativeColor
                property color nativeBase: surface.nativeBaseColor
                // During an opacity transition use ordinary source-over so
                // the fade never erases the scene beneath the popup.
                blending: surface.sceneOpacity < 0.999
                fragmentShader: "qrc:/qt/qml/Lurviko/App/qml/shaders/overlay.frag.qsb"
            }
            Rectangle {
                anchors.fill: parent
                radius: surface.radius
                color: "transparent"
                border.width: surface.border.width
                border.color: surface.border.color
                antialiasing: true
            }
        }
    }
}
