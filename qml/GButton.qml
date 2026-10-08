import QtQuick
import QtQuick.Controls
import Lurviko.App

Button {
    id: control
    // Media players can supply a fixed palette; ordinary controls follow AppTheme.
    property var colorTheme: AppTheme
    property bool primary: highlighted
    property bool danger: false
    property bool quiet: false

    // A large number of Lurviko toolbar actions intentionally have no text and
    // provide their icon either via icon.source or a custom contentItem. Treat
    // those as icon-only even when the call site did not explicitly set
    // display: AbstractButton.IconOnly. Otherwise the normal 16 px horizontal
    // padding can squeeze a 38 px toolbar button down to a ~6 px icon.
    readonly property bool iconOnlyLayout: text.length === 0

    implicitHeight: iconOnlyLayout
                    ? AppTheme.toolButtonSize
                    : Math.max(AppTheme.buttonHeight, implicitContentHeight + topPadding + bottomPadding)
    implicitWidth: iconOnlyLayout
                   ? AppTheme.toolButtonSize
                   : Math.max(90, implicitContentWidth + leftPadding + rightPadding)
    leftPadding: iconOnlyLayout ? 8 : 16
    rightPadding: iconOnlyLayout ? 8 : 16
    topPadding: iconOnlyLayout ? 8 : 10
    bottomPadding: iconOnlyLayout ? 8 : 10
    spacing: 8
    font.pixelSize: AppTheme.fontSizeBody
    font.weight: Font.DemiBold
    hoverEnabled: true
    scale: down ? 0.97 : 1.0
    Behavior on scale { NumberAnimation { duration: AppTheme.motionFast; easing.type: Easing.OutCubic } }
    icon.width: 16
    icon.height: 16

    palette.buttonText: {
        if (!enabled) return control.colorTheme.textFaint
        if (primary) return "white"
        if (danger && !flat) return control.colorTheme.danger
        return control.colorTheme.text
    }

    HoverHandler { cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }

    background: Rectangle {
        radius: AppTheme.buttonRadius
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
                return Qt.rgba(control.colorTheme.surfaceHover.r,
                               control.colorTheme.surfaceHover.g,
                               control.colorTheme.surfaceHover.b, 0)
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
