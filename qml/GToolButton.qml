import QtQuick
import QtQuick.Controls
import GFile.App

ToolButton {
    id: control
    // Media players can supply a fixed palette; ordinary controls follow AppTheme.
    property var colorTheme: AppTheme
    property bool primary: false
    property bool danger: false
    property bool quiet: false

    // ToolButton defaults to TextBesideIcon even when text is empty. Several
    // compact player controls therefore kept the full text-button padding and
    // left only a few pixels for the icon. Empty text is the reliable signal
    // that these controls are icon-only in G-File.
    readonly property bool iconOnlyLayout: text.length === 0
    readonly property bool compactGlyphLayout: !iconOnlyLayout && text.length <= 2

    implicitWidth: iconOnlyLayout
                   ? AppTheme.toolButtonSize
                   : Math.max(AppTheme.toolButtonSize, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: AppTheme.toolButtonSize
    leftPadding: iconOnlyLayout || compactGlyphLayout ? 4 : 14
    rightPadding: iconOnlyLayout || compactGlyphLayout ? 4 : 14
    topPadding: iconOnlyLayout || compactGlyphLayout ? 4 : 10
    bottomPadding: iconOnlyLayout || compactGlyphLayout ? 4 : 10
    spacing: 8
    icon.width: 16
    icon.height: 16
    font.pixelSize: AppTheme.fontSizeBody
    font.weight: Font.DemiBold
    hoverEnabled: true
    scale: down ? 0.97 : 1.0
    Behavior on scale { NumberAnimation { duration: AppTheme.motionFast; easing.type: Easing.OutCubic } }

    palette.buttonText: {
        if (!enabled) return control.colorTheme.textFaint
        if (primary) return "white"
        if (danger && !flat) return control.colorTheme.danger
        return control.colorTheme.text
    }

    HoverHandler { cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }

    background: Rectangle {
        radius: AppTheme.buttonRadiusCompact
        border.width: (flat || control.quiet || control.iconOnlyLayout) ? 0 : 1
        border.color: {
            if (!control.enabled) return control.colorTheme.border
            if (control.primary) return control.down ? control.colorTheme.accentHover : control.colorTheme.accent
            if (control.danger) return control.checked || control.down ? control.colorTheme.danger : control.colorTheme.borderStrong
            if (control.checked) return control.colorTheme.accentBorder
            return control.hovered ? control.colorTheme.accentBorder : control.colorTheme.border
        }
        color: {
            if (control.flat || control.quiet || control.iconOnlyLayout) {
                if (control.down || control.checked) return control.colorTheme.accentSoft
                if (control.hovered) return control.colorTheme.surfaceHover
                return "transparent"
            }
            if (!control.enabled) return control.colorTheme.surfaceRaised
            if (control.primary) return control.down ? control.colorTheme.accentHover : control.colorTheme.accent
            if (control.danger) {
                if (control.down || control.checked) return control.colorTheme.dark ? "#5A2630" : "#FDECEE"
                if (control.hovered) return control.colorTheme.dark ? "#432129" : "#FFF4F5"
                return control.colorTheme.surfaceRaised
            }
            if (control.down) return control.colorTheme.accentSoft
            if (control.checked) return control.colorTheme.accentSoft
            if (control.hovered) return control.colorTheme.surfaceHover
            return control.colorTheme.surfaceRaised
        }
        Behavior on color { ColorAnimation { duration: AppTheme.motionFast } }
        Behavior on border.color { ColorAnimation { duration: AppTheme.motionFast } }
    }
}
