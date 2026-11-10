import QtQuick
import QtQuick.Effects
import Lurviko.App

// Backdrop behind modal popups: the application is blurred and slightly
// desaturated, then a soft vertical shade keeps the modal in focus. The
// capture runs only while this item exists (the modal is open) and at half
// resolution, so it stays cheap. Software rendering falls back to a dim.
Item {
    id: backdrop
    readonly property Item sourceItem: safeSource(Window.window ? Window.window.contentItem : null)
    readonly property bool blurActive: visible && sourceItem !== null
        && GraphicsInfo.api !== GraphicsInfo.Software && width > 0 && height > 0

    // The overlay is a sibling of the window content in ApplicationWindow.
    // Never capture an ancestor, which would feed the texture back into itself.
    function safeSource(candidate) {
        for (let item = backdrop; item; item = item.parent) {
            if (item === candidate) return null
        }
        return candidate
    }

    ShaderEffectSource {
        id: capture
        anchors.fill: parent
        visible: false
        sourceItem: backdrop.blurActive ? backdrop.sourceItem : null
        sourceRect: backdrop.sourceItem
            ? backdrop.mapToItem(backdrop.sourceItem, Qt.rect(0, 0, backdrop.width, backdrop.height))
            : Qt.rect(0, 0, 1, 1)
        textureSize: Qt.size(Math.max(1, Math.ceil(backdrop.width / 2)), Math.max(1, Math.ceil(backdrop.height / 2)))
        live: true
        hideSource: false
        recursive: false
    }

    MultiEffect {
        objectName: "modalBackdropBlur"
        anchors.fill: parent
        visible: backdrop.blurActive
        source: capture
        blurEnabled: true
        blur: 1.0
        blurMax: 40
        saturation: -0.18
        autoPaddingEnabled: false
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop {
                position: 0.0
                color: backdrop.blurActive ? (AppTheme.dark ? "#70060A12" : "#3A0F1728")
                                           : (AppTheme.dark ? "#A0060A12" : "#730F1728")
            }
            GradientStop {
                position: 1.0
                color: backdrop.blurActive ? (AppTheme.dark ? "#A0060A12" : "#5A0F1728")
                                           : (AppTheme.dark ? "#B4060A12" : "#850F1728")
            }
        }
    }
}
