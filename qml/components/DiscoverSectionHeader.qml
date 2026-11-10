import QtQuick
import QtQuick.Layouts
import Lurviko.App

// One header style for every Discover section:
// Title · count badge · subtitle ……… [view switcher] · actions
RowLayout {
    id: header
    property string title
    property string subtitle
    property string countText
    property var modes: []
    property string viewMode: ""
    property alias switcherName: switcher.objectName
    signal viewModeRequested(string mode)
    default property alias actions: actionRow.data

    Layout.fillWidth: true
    Layout.preferredHeight: 36
    spacing: 10

    GText {
        text: header.title
        baseColor: AppTheme.text
        basePixelSize: 17
        baseFontWeight: Font.DemiBold
        Layout.alignment: Qt.AlignVCenter
    }
    Rectangle {
        visible: header.countText.length > 0
        Layout.alignment: Qt.AlignVCenter
        implicitWidth: countLabel.implicitWidth + 16
        implicitHeight: 20
        radius: 10
        color: AppTheme.accentSoft
        border.width: 1
        border.color: AppTheme.accentBorder
        GText {
            id: countLabel
            anchors.centerIn: parent
            text: header.countText
            baseColor: AppTheme.accent
            basePixelSize: 10
            baseFontWeight: Font.DemiBold
        }
    }
    GText {
        text: header.subtitle
        visible: text.length > 0
        baseColor: AppTheme.textMuted
        basePixelSize: 10
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        Layout.alignment: Qt.AlignVCenter
        elide: Text.ElideRight
    }
    Item { visible: header.subtitle.length === 0; Layout.fillWidth: true }

    ViewModeSwitcher {
        id: switcher
        visible: header.modes.length > 1
        Layout.alignment: Qt.AlignVCenter
        modes: header.modes
        current: header.viewMode
        onSelected: function(mode) { header.viewModeRequested(mode) }
    }
    RowLayout {
        id: actionRow
        Layout.alignment: Qt.AlignVCenter
        spacing: 8
    }
}
