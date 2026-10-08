import QtQuick
import QtQuick.Controls
import Lurviko.App

CheckBox {
    id: control

    spacing: 9
    hoverEnabled: true
    font.pixelSize: AppTheme.fontSizeBody
    implicitHeight: Math.max(AppTheme.controlHeightCompact, indicator.implicitHeight)

    indicator: Rectangle {
        implicitWidth: 20
        implicitHeight: 20
        x: control.leftPadding
        y: parent.height / 2 - height / 2
        radius: 6
        color: control.checked ? AppTheme.accent
                               : (control.hovered ? AppTheme.surfaceHover : AppTheme.surfaceRaised)
        border.width: 1
        border.color: control.checked ? AppTheme.accent
                                      : (control.activeFocus ? AppTheme.accent : AppTheme.borderStrong)

        Text {
            anchors.centerIn: parent
            text: "✓"
            visible: control.checked
            color: "white"
            font.pixelSize: 12
            font.bold: true
        }

        Behavior on color {
            ColorAnimation { duration: AppTheme.motionFast }
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
