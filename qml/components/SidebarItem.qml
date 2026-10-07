import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GFile.App

Rectangle {
    id: control

    property string title: ""
    property string subtitle: ""
    property string iconSource: ""
    property bool active: false
    property bool danger: false
    property bool removable: false
    property bool editable: false
    property bool propertiesEnabled: false
    property string editText: "Düzenle"
    property string propertiesText: "Özellikler"
    property string removeText: "Kaldır"
    property string dragUrl: ""
    property string dragIcon: "folder.svg"
    property bool dragging: dragHandler.active
    property real iconScale: 1.0
    property string capacityText: ""
    property bool showUsageBar: false
    property real usageRatio: 0
    readonly property int iconSize: AppTheme.effectiveSidebarIconSize

    signal clicked()
    signal middleClicked()
    signal removeRequested()
    signal editRequested()
    signal propertiesRequested()

    implicitHeight: capacityText.length > 0
                    ? Math.max(72, iconSize + 38)
                    : Math.max(subtitle.length > 0 ? 48 : 42, iconSize + 18)
    radius: 14
    color: active ? AppTheme.accentSoft
                  : (itemMouse.containsMouse || dragging ? AppTheme.surfaceHover : "transparent")
    border.color: active ? AppTheme.accentBorder : "transparent"
    border.width: active ? 1 : 0
    scale: dragging ? 0.97 : 1

    Behavior on scale { NumberAnimation { duration: AppTheme.motionFast } }

    Drag.dragType: Drag.Automatic
    Drag.active: dragHandler.active
    Drag.source: control
    Drag.supportedActions: Qt.CopyAction
    Drag.proposedAction: Qt.CopyAction
    Drag.mimeData: ({ "text/uri-list": dragUrl })

    Rectangle {
        visible: control.active
        anchors.left: parent.left
        anchors.leftMargin: 3
        anchors.verticalCenter: parent.verticalCenter
        width: 4
        height: Math.min(28, parent.height - 12)
        radius: 4
        color: control.danger ? AppTheme.danger : AppTheme.accent
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 13
        anchors.rightMargin: 8
        spacing: 10

        Rectangle {
            Layout.preferredWidth: Math.max(34, control.iconSize + 12)
            Layout.preferredHeight: Math.max(34, control.iconSize + 12)
            radius: 11
            color: control.active ? Qt.rgba(AppTheme.accent.r, AppTheme.accent.g, AppTheme.accent.b, AppTheme.dark ? 0.24 : 0.12)
                                  : (itemMouse.containsMouse ? AppTheme.surfaceRaised : "transparent")
            border.width: control.active ? 1 : 0
            border.color: AppTheme.accentBorder

            CrispIcon {
                anchors.centerIn: parent
                source: AppTheme.useSystemIcons ? AppTheme.systemIconAtSize(control.iconSource, control.iconSize) : control.iconSource
                width: Math.max(10, Math.round(control.iconSize * control.iconScale))
                height: width
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1

            Text {
                Layout.fillWidth: true
                text: control.title
                color: control.danger ? AppTheme.danger : AppTheme.text
                font.pixelSize: 12
                font.weight: control.active ? Font.Bold : Font.Medium
                elide: Text.ElideRight
            }

            Text {
                visible: control.subtitle.length > 0
                Layout.fillWidth: true
                text: control.subtitle
                color: AppTheme.textFaint
                font.pixelSize: 10
                elide: Text.ElideMiddle
            }

            Text {
                visible: control.capacityText.length > 0
                Layout.fillWidth: true
                text: control.capacityText
                color: AppTheme.textMuted
                font.pixelSize: 10
                elide: Text.ElideRight
            }

            Rectangle {
                visible: control.showUsageBar
                Layout.fillWidth: true
                Layout.preferredHeight: visible ? 5 : 0
                radius: 3
                color: AppTheme.surfaceRaised
                Rectangle {
                    width: parent.width * Math.max(0, Math.min(1, control.usageRatio))
                    height: parent.height
                    radius: parent.radius
                    color: control.usageRatio > 0.9 ? AppTheme.danger : AppTheme.accent
                }
            }
        }

    }

    MouseArea {
        id: itemMouse
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.rightMargin: 0
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: function(mouse) {
            if (mouse.button === Qt.MiddleButton) {
                control.middleClicked()
            } else if (mouse.button === Qt.RightButton) {
                if (control.editable || control.removable || control.propertiesEnabled)
                    contextMenu.popup(mouse.x, mouse.y)
            } else {
                control.clicked()
            }
        }
    }

    GMenu {
        id: contextMenu
        GMenuItem {
            visible: control.editable
            text: control.editText
            onTriggered: control.editRequested()
        }
        GMenuItem {
            visible: control.propertiesEnabled
            text: control.propertiesText
            onTriggered: control.propertiesRequested()
        }
        GMenuItem {
            visible: control.removable
            text: control.removeText
            onTriggered: control.removeRequested()
        }
    }

    DragHandler {
        id: dragHandler
        target: null
        enabled: control.dragUrl.length > 0
        acceptedButtons: Qt.LeftButton
    }
}
