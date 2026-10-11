import QtQuick
import QtQuick.Effects

Item {
    id: backdrop
    property Item sourceItem: null
    property color backgroundColor: "black"
    property real backgroundOpacity: 0.68
    property real cornerRadius: 8
    property real blurRadius: 0
    property bool effectsEnabled: true
    readonly property real tintOpacity: Math.max(0, Math.min(1, backgroundOpacity * backgroundColor.a))
    readonly property bool blurActive: effectsEnabled && visible && sourceItem !== null
                                      && backgroundOpacity > 0 && tintOpacity < 1 && blurRadius > 0
                                      && GraphicsInfo.api !== GraphicsInfo.Software

    // Only capture the media/preview scene, never an ancestor containing us.
    // The blur follows opacity too: 100% transparency removes the entire patch.
    Loader {
        id: blurLoader
        anchors.fill: parent
        active: backdrop.blurActive
        opacity: backdrop.backgroundOpacity
        sourceComponent: Item {
            ShaderEffectSource {
                id: capture
                anchors.fill: parent
                sourceItem: backdrop.sourceItem
                sourceRect: {
                    const point = backdrop.mapToItem(backdrop.sourceItem, 0, 0)
                    return Qt.rect(point.x, point.y, Math.max(1, backdrop.width), Math.max(1, backdrop.height))
                }
                live: true
                recursive: false
                hideSource: false
                visible: false
            }
            Rectangle {
                id: mask
                anchors.fill: parent
                radius: backdrop.cornerRadius
                color: "white"
                visible: false
                antialiasing: true
                layer.enabled: true
            }
            MultiEffect {
                anchors.fill: parent
                source: capture
                blurEnabled: true
                blur: Math.min(1, backdrop.blurRadius / 48)
                blurMax: 48
                autoPaddingEnabled: false
                maskEnabled: true
                maskSource: mask
            }
        }
    }
    Rectangle {
        anchors.fill: parent
        radius: backdrop.cornerRadius
        antialiasing: true
        color: Qt.rgba(backdrop.backgroundColor.r, backdrop.backgroundColor.g,
                       backdrop.backgroundColor.b, backdrop.tintOpacity)
    }
}
