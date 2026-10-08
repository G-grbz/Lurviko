import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

Rectangle {
    id: root
    property string language: "en"
    property int categoryIndex: -1
    property string categoryKey: ""
    property string title: ""
    property string iconName: "file.svg"
    property int itemCount: 0
    property string itemLabel: "öğe"
    property int systemIconPixels: AppTheme.systemHomeIconSize
    readonly property int iconPixels: Math.round(AppTheme.effectiveHomeIconSize)
    property bool categoryReorder: true
    property bool editable: true
    property bool dragging: reorderDrag.active
    property bool suppressClick: false
    property string secondaryText: ""
    property bool actionVisible: false
    property string actionText: ""
    property string actionIcon: ""
    property string actionToolTip: ""
    signal openRequested(string key, string title, string iconName)
    signal editRequested(string key, string title, string iconName)
    signal reorderRequested(int from, int to)
    signal actionRequested()

    implicitWidth: 280
    implicitHeight: Math.max(74, root.iconPixels + 28)
    radius: AppTheme.cardRadius
    clip: true
    color: dropTarget.containsDrag ? AppTheme.accentSoft : (mouse.containsMouse || dragging ? AppTheme.cardHover : AppTheme.cardSurface)
    border.color: dropTarget.containsDrag ? AppTheme.accent : (mouse.containsMouse ? AppTheme.accentBorder : AppTheme.border)
    border.width: dropTarget.containsDrag ? 2 : 1
    scale: dragging ? 0.97 : 1.0
    opacity: dragging ? 0.72 : 1.0

    Drag.dragType: Drag.Automatic
    Drag.active: reorderDrag.active
    Drag.source: root
    Drag.supportedActions: Qt.MoveAction
    Drag.proposedAction: Qt.MoveAction

    DropArea {
        id: dropTarget
        anchors.fill: parent
        onDropped: function(drop) {
            if (drop.source && drop.source.categoryReorder
                    && drop.source.categoryIndex !== root.categoryIndex) {
                root.reorderRequested(drop.source.categoryIndex, root.categoryIndex)
                drop.acceptProposedAction()
            }
        }
    }

    Timer { id: dragClickReset; interval: 90; onTriggered: root.suppressClick = false }

    Behavior on color { ColorAnimation { duration: 120 } }
    Behavior on border.color { ColorAnimation { duration: 120 } }

    Item {
        id: centeredContent
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        height: Math.max(root.iconPixels,
                         labelsColumn.implicitHeight,
                         arrowText.implicitHeight)

        Item {
            id: iconStage
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: root.iconPixels
            height: root.iconPixels

            CrispIcon {
                id: categoryIconVisual
                anchors.fill: parent
                source: AppTheme.useSystemIcons
                        ? AppTheme.systemIconAtSize(AppTheme.discoveryIcon(root.iconName, AppTheme.categoryDefaultIcon(root.categoryKey)), root.iconPixels)
                        : AppTheme.discoveryIcon(root.iconName, AppTheme.categoryDefaultIcon(root.categoryKey))
                scale: mouse.containsMouse ? 1.045 : 1.0
                transformOrigin: Item.Center
                Behavior on scale {
                    NumberAnimation { duration: 145; easing.type: Easing.OutCubic }
                }
            }
        }

        Column {
            id: labelsColumn
            anchors.left: iconStage.right
            anchors.right: arrowText.left
            anchors.leftMargin: 12
            anchors.rightMargin: root.actionVisible ? 28 : 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 3

            Text {
                width: parent.width
                text: root.title
                color: AppTheme.text
                font.pixelSize: 13
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Text {
                text: root.secondaryText.length > 0 ? root.secondaryText : (root.itemCount + " " + root.itemLabel)
                color: AppTheme.textMuted
                font.pixelSize: 10
            }
        }

        Text {
            id: arrowText
            visible: !root.actionVisible
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: "›"
            color: mouse.containsMouse ? AppTheme.accent : AppTheme.textFaint
            font.pixelSize: 24
            font.weight: Font.Light
        }
    }

    GToolButton {
        id: cornerAction
        z: 8
        visible: root.actionVisible
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.rightMargin: 14
        implicitWidth: 32
        implicitHeight: 32
        hoverEnabled: true
        onClicked: root.actionRequested()
        background: Rectangle {
            radius: 10
            color: cornerAction.hovered ? AppTheme.accentSoft : AppTheme.surface
            border.width: 1
            border.color: cornerAction.hovered ? AppTheme.accentBorder : AppTheme.border
        }
        contentItem: Item {
            CrispIcon {
                anchors.centerIn: parent
                width: 17
                height: 17
                visible: root.actionIcon.length > 0
                source: AppTheme.icon(root.actionIcon)
            }
            Text {
                anchors.fill: parent
                visible: root.actionIcon.length === 0
                text: root.actionText
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font.pixelSize: 14
                color: AppTheme.text
            }
        }
        GToolTip { text: root.actionToolTip }
    }

    MouseArea {
        id: mouse
        z: 2
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: function(event) {
            if (root.suppressClick) {
                event.accepted = true
                return
            }
            if (event.button === Qt.RightButton) {
                if (root.editable) categoryMenu.popup(event.x, event.y)
            } else
                root.openRequested(root.categoryKey, root.title, root.iconName)
        }
    }

    GMenu {
        id: categoryMenu
        GMenuItem {
            visible: root.editable
            text: root.language === "tr" ? "Düzenle" : "Edit"
            onTriggered: root.editRequested(root.categoryKey, root.title, root.iconName)
        }
    }

    DragHandler {
        id: reorderDrag
        target: null
        acceptedButtons: Qt.LeftButton
        onActiveChanged: {
            if (!active) {
                root.suppressClick = true
                dragClickReset.restart()
            }
        }
    }
}
