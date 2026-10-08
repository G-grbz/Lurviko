import QtQuick
import QtQuick.Controls
import Lurviko.App

BusyIndicator {
    id: control

    implicitWidth: 32
    implicitHeight: 32

    contentItem: Item {
        id: spinner
        opacity: control.running ? 1.0 : 0.0

        Behavior on opacity {
            NumberAnimation { duration: AppTheme.motionFast }
        }

        Item {
            id: rotor
            anchors.fill: parent

            Repeater {
                model: 8

                Rectangle {
                    required property int index
                    readonly property real angle: index * Math.PI / 4
                    readonly property real dotSize: Math.max(2, Math.min(rotor.width, rotor.height) * 0.11)
                    width: dotSize
                    height: dotSize
                    radius: dotSize / 2
                    x: rotor.width / 2 + Math.cos(angle) * rotor.width * 0.34 - width / 2
                    y: rotor.height / 2 + Math.sin(angle) * rotor.height * 0.34 - height / 2
                    color: AppTheme.accent
                    opacity: 0.28 + index * 0.09
                }
            }

            RotationAnimator on rotation {
                from: 0
                to: 360
                duration: 760
                loops: Animation.Infinite
                running: control.running && control.visible
            }
        }
    }
}
