import QtQuick
import QtQuick.Effects
import Lurviko.App

Item {
    id: glass

    // The source must be the media-only layer, never an ancestor containing
    // this glass item, otherwise the capture would feed back into itself.
    property Item sourceItem: null
    property real radius: AppTheme.radiusMedium
    property color tintColor: AppTheme.viewerGlass
    property color borderColor: AppTheme.viewerGlassBorder
    property real borderWidth: 1
    property real blurAmount: 0.58
    property int blurMax: 40
    property bool blurEnabled: false

    Rectangle {
        id: maskShape
        anchors.fill: parent
        radius: glass.radius
        visible: false
    }

    // Kept as an opt-in path for future Qt/RHI fixes.  It is disabled by
    // default because VideoOutput cannot be captured reliably on every
    // Wayland backend.  With opaque viewer tokens the panel remains correct
    // even when blur is unavailable.
    ShaderEffectSource {
        id: backdropCapture
        anchors.fill: parent
        visible: false
        // Even a hidden, non-live capture retains its source's window. Do
        // not attach an unused capture to a player that moves to fullscreen.
        sourceItem: glass.blurEnabled ? glass.sourceItem : null
        live: glass.blurEnabled
        hideSource: false
        recursive: false
        sourceRect: {
            if (!glass.sourceItem || glass.width <= 0 || glass.height <= 0)
                return Qt.rect(0, 0, 1, 1)
            const p = glass.mapToItem(glass.sourceItem, 0, 0)
            return Qt.rect(p.x, p.y, glass.width, glass.height)
        }
    }

    MultiEffect {
        anchors.fill: parent
        visible: glass.blurEnabled && glass.sourceItem !== null
        source: backdropCapture
        blurEnabled: glass.blurEnabled
        blur: glass.blurAmount
        blurMax: glass.blurMax
        autoPaddingEnabled: false
        maskEnabled: true
        maskSource: maskShape
    }

    Rectangle {
        anchors.fill: parent
        radius: glass.radius
        color: glass.tintColor
        border.width: glass.borderWidth
        border.color: glass.borderColor
        antialiasing: true
    }
}
