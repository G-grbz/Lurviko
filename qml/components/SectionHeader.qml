import QtQuick
import QtQuick.Layouts
import GFile.App

RowLayout {
    property string title: ""
    property string actionText: ""
    signal actionTriggered()

    Text {
        text: parent.title
        color: AppTheme.text
        font.pixelSize: 19
        font.weight: Font.Bold
        Layout.fillWidth: true
    }
    Text {
        visible: parent.actionText.length > 0
        text: parent.actionText
        color: AppTheme.accent
        font.pixelSize: 11
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: parent.parent.actionTriggered() }
    }
}
