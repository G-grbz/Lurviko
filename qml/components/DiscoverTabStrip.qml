import QtQuick
import Lurviko.App

// Tab view selector for a Discover section: wrapping chips with icon,
// title and an optional count. The selected chip uses the accent colours.
Flow {
    id: strip
    // [{key, title, icon, badge}]
    property var tabs: []
    property string current: ""
    signal selected(string key)

    spacing: 8

    Repeater {
        model: strip.tabs
        delegate: Rectangle {
            id: chip
            required property var modelData
            readonly property bool active: strip.current === modelData.key
            objectName: "discoverTab_" + modelData.key
            height: 38
            width: chipRow.implicitWidth + 24
            radius: 12
            color: active ? AppTheme.accentSoft
                 : chipMouse.containsMouse ? AppTheme.cardHover : AppTheme.cardSurface
            border.width: 1
            border.color: active ? AppTheme.accentBorder : AppTheme.border
            Row {
                id: chipRow
                anchors.centerIn: parent
                spacing: 8
                CrispIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 22; height: 22
                    source: chip.modelData.icon || ""
                }
                GText {
                    anchors.verticalCenter: parent.verticalCenter
                    text: chip.modelData.title
                    baseColor: chip.active ? AppTheme.accent : AppTheme.text
                    basePixelSize: 12
                    baseFontWeight: chip.active ? Font.DemiBold : Font.Normal
                }
                GText {
                    visible: text.length > 0
                    anchors.verticalCenter: parent.verticalCenter
                    text: chip.modelData.badge !== undefined ? String(chip.modelData.badge) : ""
                    baseColor: AppTheme.textMuted
                    basePixelSize: 10
                }
            }
            MouseArea {
                id: chipMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: strip.selected(chip.modelData.key)
            }
        }
    }
}
