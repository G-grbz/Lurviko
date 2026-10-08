import QtQuick
import QtQuick.Controls
import Lurviko.App

GButton {
    id: control

    implicitHeight: 40
    implicitWidth: Math.max(88, modalLabel.implicitWidth + leftPadding + rightPadding)
    leftPadding: 16
    rightPadding: 16

    background: Rectangle {
        radius: 12
        border.width: control.flat ? 0 : 1
        border.color: {
            if (!control.enabled) return AppTheme.border
            if (control.primary) return control.down ? AppTheme.accentHover : AppTheme.accent
            if (control.danger) return control.down ? AppTheme.danger : AppTheme.borderStrong
            return control.hovered ? AppTheme.accentBorder : AppTheme.border
        }
        color: {
            if (control.flat) {
                if (control.down || control.checked) return AppTheme.accentSoft
                if (control.hovered) return Qt.rgba(AppTheme.accent.r, AppTheme.accent.g, AppTheme.accent.b, AppTheme.dark ? 0.16 : 0.10)
                return "transparent"
            }
            if (!control.enabled) return AppTheme.surfaceRaised
            if (control.primary) return control.down ? AppTheme.accentHover : AppTheme.accent
            if (control.danger) {
                if (control.down || control.checked) return AppTheme.dark ? "#5A2630" : "#FDECEE"
                if (control.hovered) return AppTheme.dark ? "#432129" : "#FFF4F5"
                return AppTheme.surfaceRaised
            }
            if (control.down || control.checked) return AppTheme.accentSoft
            if (control.hovered) return AppTheme.surfaceHover
            return AppTheme.surfaceRaised
        }
        Behavior on color { ColorAnimation { duration: 100 } }
        Behavior on border.color { ColorAnimation { duration: 100 } }
    }

    contentItem: Text {
        id: modalLabel
        text: control.text
        color: control.enabled ? (control.primary ? "white" : (control.danger ? AppTheme.danger : AppTheme.text)) : AppTheme.textFaint
        font.pixelSize: 12
        font.weight: Font.DemiBold
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
