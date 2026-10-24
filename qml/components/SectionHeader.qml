import QtQuick
import QtQuick.Layouts
import Lurviko.App

RowLayout {
    property string title: ""
    property string actionText: ""
    signal actionTriggered()

    GText {
        text: parent.title
        baseColor: AppTheme.text
        basePixelSize: 19
        baseFontWeight: Font.Bold
        Layout.fillWidth: true
    }
    GText {
        visible: parent.actionText.length > 0
        text: parent.actionText
        baseColor: AppTheme.accent
        basePixelSize: 11
        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: parent.parent.actionTriggered() }
    }
}
