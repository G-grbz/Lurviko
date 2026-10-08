import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lurviko.App

Item {
    id: root
    property string title: ""
    property int count: 0
    property var lang: null
    property real textScale: 1.0

    implicitHeight: 48

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        height: 42
        radius: 12
        color: AppTheme.surfaceRaised
        border.width: 1
        border.color: AppTheme.border

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 10

            Rectangle {
                Layout.preferredWidth: 4
                Layout.preferredHeight: 22
                radius: 2
                color: AppTheme.accent
            }

            Text {
                text: root.title
                color: AppTheme.text
                font.pixelSize: Math.round(14 * root.textScale)
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
            }

            Rectangle {
                Layout.preferredWidth: countLabel.implicitWidth + 16
                Layout.preferredHeight: 25
                radius: 8
                color: AppTheme.accentSoft
                border.width: 1
                border.color: AppTheme.accentBorder

                Text {
                    id: countLabel
                    anchors.centerIn: parent
                    text: root.count + " " + (root.lang && root.lang.language === "tr" ? "öğe" : "items")
                    color: AppTheme.accent
                    font.pixelSize: Math.round(10 * root.textScale)
                    font.weight: Font.DemiBold
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: AppTheme.border
                opacity: 0.78
            }
        }
    }
}
