import QtQuick
import QtQuick.Controls
import GFile.App

Switch {
    id: control

    spacing: 10
    hoverEnabled: true
    font.pixelSize: AppTheme.fontSizeBody
    implicitHeight: Math.max(AppTheme.controlHeightCompact, indicator.implicitHeight)

    indicator: Item {
        implicitWidth: 42
        implicitHeight: 24
        x: control.leftPadding
        y: parent.height / 2 - height / 2

        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: control.checked ? AppTheme.accent
                                   : (control.hovered ? AppTheme.surfacePressed : AppTheme.surfaceHover)
            border.width: control.checked ? 0 : 1
            border.color: AppTheme.borderStrong

            Behavior on color {
                ColorAnimation { duration: AppTheme.motionNormal }
            }
        }

        Rectangle {
            width: 18
            height: 18
            radius: 9
            y: 3
            x: control.checked ? parent.width - width - 3 : 3
            color: control.checked ? "white" : AppTheme.textMuted

            Behavior on x {
                NumberAnimation { duration: AppTheme.motionNormal; easing.type: Easing.OutCubic }
            }
        }
    }

    contentItem: Text {
        leftPadding: control.indicator.width + control.spacing
        text: control.text
        font: control.font
        color: control.enabled ? AppTheme.text : AppTheme.textFaint
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
