import QtQuick
import Lurviko.App

// Pill-shaped segmented control used next to Discover section titles.
Rectangle {
    id: switcher
    // [{id, label}]
    property var modes: []
    property string current: ""
    signal selected(string mode)

    implicitHeight: 32
    implicitWidth: segments.implicitWidth + 8
    radius: height / 2
    color: AppTheme.dark ? Qt.rgba(1, 1, 1, 0.05) : Qt.rgba(0.05, 0.08, 0.19, 0.045)
    border.width: 1
    border.color: AppTheme.border
    Accessible.role: Accessible.PageTabList

    Row {
        id: segments
        anchors.centerIn: parent
        spacing: 2
        Repeater {
            model: switcher.modes
            delegate: Rectangle {
                id: segment
                required property var modelData
                readonly property bool active: switcher.current === modelData.id
                objectName: "viewMode_" + modelData.id
                height: switcher.height - 6
                width: segmentLabel.implicitWidth + 26
                radius: height / 2
                color: active ? (AppTheme.dark ? AppTheme.accentSoft : AppTheme.surface)
                     : segmentMouse.containsMouse ? AppTheme.surfaceHover
                     : Qt.rgba(AppTheme.surfaceHover.r, AppTheme.surfaceHover.g, AppTheme.surfaceHover.b, 0)
                border.width: active ? 1 : 0
                border.color: AppTheme.dark ? AppTheme.accentBorder : Qt.rgba(0.05, 0.08, 0.19, 0.09)
                Accessible.role: Accessible.PageTab
                Accessible.name: modelData.label
                Accessible.checked: active
                GText {
                    id: segmentLabel
                    anchors.centerIn: parent
                    text: segment.modelData.label
                    baseColor: segment.active ? AppTheme.accent : AppTheme.textMuted
                    basePixelSize: 11
                    baseFontWeight: segment.active ? Font.DemiBold : Font.Medium
                }
                MouseArea {
                    id: segmentMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: switcher.selected(segment.modelData.id)
                }
            }
        }
    }
}
