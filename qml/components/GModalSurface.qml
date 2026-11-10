import QtQuick
import Lurviko.App

// Shared surface of every centered modal: a soft layered shadow, a lightly
// translucent body that lets the blurred backdrop show through, a faint
// accent glow behind the header area and a 1 px inner highlight.
Item {
    id: surface
    property color accentColor: AppTheme.accent
    property alias sourceItem: nativeSurface.sourceItem
    property real radius: 26
    readonly property color bodyColor: AppTheme.dark
        ? Qt.rgba(AppTheme.surface.r, AppTheme.surface.g, AppTheme.surface.b, 0.94)
        : Qt.rgba(1, 1, 1, 0.96)

    GOverlaySurface {
        id: nativeSurface
        objectName: "nativeOverlaySurface"
        anchors.fill: parent
        radius: surface.radius
        visible: AppTheme.useQtTheme
    }

    // ---- Shadow and body ---------------------------------------------------
    // Soft shadow from stacked, widening rounded layers. Drawn directly every
    // frame, so it always follows the popup's size; MultiEffect kept its first
    // size after a resize and left a stale second layer behind the dialog.
    readonly property color shadowTint: AppTheme.dark ? "#000000" : "#0D1530"
    readonly property real shadowStrength: AppTheme.dark ? 0.55 : 0.20
    Repeater {
        model: AppTheme.useQtTheme ? 0 : 8
        delegate: Rectangle {
            required property int index
            readonly property real spread: 3 + index * 4
            x: -spread
            y: -spread + 8 + index * 2.5
            width: surface.width + spread * 2
            height: surface.height + spread * 2
            radius: surface.radius + spread
            color: Qt.rgba(surface.shadowTint.r, surface.shadowTint.g, surface.shadowTint.b,
                           surface.shadowStrength / 8 * (1 - index / 10))
            antialiasing: true
        }
    }
    // Hairline contact edge under the body.
    Rectangle {
        anchors.fill: parent
        anchors.margins: -1
        anchors.topMargin: 0
        radius: surface.radius + 1
        visible: !AppTheme.useQtTheme
        color: AppTheme.dark ? Qt.rgba(0, 0, 0, 0.45) : Qt.rgba(0.05, 0.08, 0.19, 0.07)
    }
    Rectangle {
        id: body
        objectName: "modalSurfaceBody"
        anchors.fill: parent
        radius: surface.radius
        color: surface.bodyColor
        antialiasing: true
        visible: !AppTheme.useQtTheme
    }
    Rectangle {
        anchors.fill: parent
        radius: surface.radius
        visible: !AppTheme.useQtTheme
        color: "transparent"
        border.width: 1
        border.color: AppTheme.dark ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(0.05, 0.08, 0.19, 0.10)
        antialiasing: true
    }
    // Accent glow behind the header; fades out within the top third.
    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: surface.radius - 1
        visible: !AppTheme.useQtTheme
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(surface.accentColor.r, surface.accentColor.g, surface.accentColor.b, AppTheme.dark ? 0.13 : 0.075) }
            GradientStop { position: 0.16; color: Qt.rgba(surface.accentColor.r, surface.accentColor.g, surface.accentColor.b, AppTheme.dark ? 0.04 : 0.02) }
            GradientStop { position: 0.34; color: "transparent" }
        }
    }
    // Inner highlight: a light top edge that reads as a raised glass rim.
    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: surface.radius - 1
        color: "transparent"
        visible: !AppTheme.useQtTheme
        border.width: 1
        border.color: AppTheme.dark ? Qt.rgba(1, 1, 1, 0.06) : Qt.rgba(1, 1, 1, 0.85)
    }
}
