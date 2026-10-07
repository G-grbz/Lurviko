import QtQuick
import QtQuick.Layouts
import GFile.App

ColumnLayout {
    id: row
    property string icon: "■"
    property string title: "Files"
    property string valueText: ""
    property real ratio: 0.5
    property color accentColor: "#8aa4ff"
    spacing: 7

    RowLayout {
        Layout.fillWidth: true
        Rectangle {
            width: 42; height: 42; radius: 9; color: AppTheme.accentSoft
            Text { anchors.centerIn: parent; text: row.icon; color: row.accentColor; font.pixelSize: 18 }
        }
        Text { text: row.title; color: AppTheme.textMuted; font.pixelSize: 13; Layout.fillWidth: true }
        Text { text: row.valueText; color: AppTheme.text; font.pixelSize: 12; font.bold: true }
    }
    Rectangle {
        Layout.fillWidth: true; Layout.preferredHeight: 7; radius: 4; color: AppTheme.surfaceHover
        Rectangle { width: parent.width * Math.max(0, Math.min(1, row.ratio)); height: parent.height; radius: 4; color: row.accentColor }
    }
}
