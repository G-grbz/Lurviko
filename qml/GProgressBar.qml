import QtQuick
import QtQuick.Controls
import Lurviko.App

ProgressBar {
    id: control
    // Media players can supply a fixed palette; ordinary controls follow AppTheme.
    property var colorTheme: AppTheme

    implicitWidth: 180
    implicitHeight: 8

    background: Rectangle {
        implicitWidth: 180
        implicitHeight: 6
        radius: height / 2
        color: control.colorTheme.surfacePressed
    }

    contentItem: Item {
        implicitWidth: 180
        implicitHeight: 6
        clip: true

        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            radius: height / 2
            color: control.colorTheme.accent

            Behavior on width {
                NumberAnimation { duration: AppTheme.motionNormal; easing.type: Easing.OutCubic }
            }
        }
    }
}
