import QtQuick
import QtQuick.Controls
import GFile.App

Slider {
    id: control
    // Media players can supply a fixed palette; ordinary controls follow AppTheme.
    property var colorTheme: AppTheme

    implicitWidth: 180
    implicitHeight: 30
    leftPadding: 4
    rightPadding: 4
    topPadding: 4
    bottomPadding: 4
    hoverEnabled: true

    background: Rectangle {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.availableWidth
        height: 4
        radius: 2
        color: control.colorTheme.surfacePressed

        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            radius: parent.radius
            color: control.enabled ? control.colorTheme.accent : control.colorTheme.textFaint
        }
    }

    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.pressed ? 18 : 16
        height: width
        radius: width / 2
        color: control.enabled ? control.colorTheme.accent : control.colorTheme.textFaint
        border.width: 3
        border.color: control.colorTheme.surface

        Behavior on width {
            NumberAnimation { duration: AppTheme.motionFast; easing.type: Easing.OutCubic }
        }
    }
}
