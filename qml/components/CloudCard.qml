import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lurviko.App

Rectangle {
    id: card
    property string language: "en"
    property string cloudKey: ""
    property string provider: "Cloud"
    property string iconSource: ""
    property string iconName: "cloud.svg"
    property color accentColor: "#2997e8"
    property string statusText: ""
    property string actionText: ""
    property string settingsToolTip: qsTr("Settings")
    readonly property int effectiveIconPixels: Math.max(24, Math.round(AppTheme.effectiveHomeIconSize * 0.96))
    property bool cloudReorder: true
    property bool dragging: reorderDrag.active
    property bool suppressClick: false
    readonly property bool stackedLayout: effectiveIconPixels >= 72
    // Compact tile: icon, name and status stacked; settings in the corner.
    property bool compact: false
    implicitWidth: compact ? 152 : 280
    implicitHeight: compact ? 116 : stackedLayout
            ? Math.max(124, effectiveIconPixels + 54)
            : Math.max(76, effectiveIconPixels + 30)
    signal actionRequested()
    signal settingsRequested()
    signal editRequested(string key, string title, string icon)
    signal reorderRequested(string fromKey, string toKey)

    radius: AppTheme.cardRadius
    color: dropTarget.containsDrag ? AppTheme.accentSoft : (mouse.containsMouse || dragging ? AppTheme.cardHover : AppTheme.cardSurface)
    border.color: dropTarget.containsDrag ? AppTheme.accent : (mouse.containsMouse ? AppTheme.accentBorder : AppTheme.border)
    border.width: dropTarget.containsDrag ? 2 : 1
    scale: dragging ? 0.97 : 1.0
    opacity: dragging ? 0.72 : 1.0


    Rectangle {
        z: 3
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 5
        width: 4
        height: Math.min(44, parent.height - 24)
        radius: 4
        color: card.accentColor
        opacity: mouse.containsMouse ? 1.0 : 0.72
        Behavior on opacity { NumberAnimation { duration: AppTheme.motionNormal } }
    }

    Drag.dragType: Drag.Automatic
    Drag.active: reorderDrag.active
    Drag.source: card
    Drag.supportedActions: Qt.MoveAction
    Drag.proposedAction: Qt.MoveAction

    Timer { id: dragClickReset; interval: 90; onTriggered: card.suppressClick = false }

    DropArea {
        id: dropTarget
        anchors.fill: parent
        onDropped: function(drop) {
            if (drop.source && drop.source.cloudReorder
                    && drop.source.cloudKey.length > 0
                    && drop.source.cloudKey !== card.cloudKey) {
                card.reorderRequested(drop.source.cloudKey, card.cloudKey)
                drop.acceptProposedAction()
            }
        }
    }

    MouseArea {
        id: mouse
        z: 1
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: function(event) {
            if (card.suppressClick) {
                event.accepted = true
                return
            }
            if (event.button === Qt.RightButton)
                cloudContextMenu.popup(event.x, event.y)
            else
                card.actionRequested()
        }
    }

    GMenu {
        id: cloudContextMenu
        GMenuItem {
            text: card.language === "tr" ? "Düzenle" : "Edit"
            onTriggered: card.editRequested(card.cloudKey, card.provider, card.iconName)
        }
    }

    DragHandler {
        id: reorderDrag
        target: null
        acceptedButtons: Qt.LeftButton
        onActiveChanged: {
            if (!active) {
                card.suppressClick = true
                dragClickReset.restart()
            }
        }
    }

    RowLayout {
        z: 2
        visible: !card.stackedLayout && !card.compact
        anchors.fill: parent
        anchors.margins: 14
        spacing: 11
        Item {
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: card.effectiveIconPixels
            Layout.preferredHeight: card.effectiveIconPixels
            CrispIcon {
                anchors.fill: parent
                source: AppTheme.useSystemIcons
                        ? AppTheme.systemIconAtSize(card.iconSource, card.effectiveIconPixels)
                        : card.iconSource
            }
        }
        ColumnLayout {
            Layout.fillWidth: true; Layout.alignment: Qt.AlignVCenter; spacing: 2
            GText { objectName: "cloudCardTitle"; typographyRole: "body"; text: card.provider; baseColor: AppTheme.text; basePixelSize: 13; baseFontWeight: Font.Normal; elide: Text.ElideRight; Layout.fillWidth: true }
            GText { text: card.statusText; baseColor: AppTheme.textMuted; basePixelSize: 10; elide: Text.ElideRight; Layout.fillWidth: true }
        }
        CloudSettingsButton { }
        GText { text: "›"; baseColor: card.accentColor; basePixelSize: 22; Layout.alignment: Qt.AlignVCenter }
    }

    Column {
        z: 2
        visible: card.compact
        anchors.centerIn: parent
        spacing: 6
        CrispIcon {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 40; height: 40
            source: AppTheme.useSystemIcons ? AppTheme.systemIconAtSize(card.iconSource, 40) : card.iconSource
            scale: mouse.containsMouse ? 1.06 : 1.0
            Behavior on scale { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
        }
        GText {
            objectName: "cloudCardCompactTitle"
            width: card.width - 20
            horizontalAlignment: Text.AlignHCenter
            text: card.provider
            baseColor: AppTheme.text
            basePixelSize: 12
            elide: Text.ElideRight
        }
        GText {
            width: card.width - 20
            horizontalAlignment: Text.AlignHCenter
            text: card.statusText
            baseColor: AppTheme.textMuted
            basePixelSize: 9
            elide: Text.ElideRight
        }
    }
    CloudSettingsButton {
        visible: card.compact
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 4
        implicitWidth: 26
        implicitHeight: 26
    }

    ColumnLayout {
        z: 2
        visible: card.stackedLayout && !card.compact
        anchors.fill: parent
        anchors.margins: 14
        spacing: 6

        Item {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: card.effectiveIconPixels
            Layout.preferredHeight: card.effectiveIconPixels
            CrispIcon {
                anchors.fill: parent
                source: AppTheme.useSystemIcons
                        ? AppTheme.systemIconAtSize(card.iconSource, card.effectiveIconPixels)
                        : card.iconSource
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                GText { objectName: "cloudCardTitle"; typographyRole: "body"; text: card.provider; baseColor: AppTheme.text; basePixelSize: 13; baseFontWeight: Font.Normal; elide: Text.ElideRight; Layout.fillWidth: true }
                GText { text: card.statusText; baseColor: AppTheme.textMuted; basePixelSize: 10; elide: Text.ElideRight; Layout.fillWidth: true }
            }
            CloudSettingsButton { }
            GText { text: "›"; baseColor: card.accentColor; basePixelSize: 22; Layout.alignment: Qt.AlignVCenter }
        }
    }

    component CloudSettingsButton: GToolButton {
        id: cloudSettingsControl
        z: 6
        HoverHandler { cursorShape: Qt.PointingHandCursor }
        implicitWidth: 30
        implicitHeight: 30
        text: ""
        onClicked: card.settingsRequested()
        background: Rectangle {
            radius: 9
            color: cloudSettingsControl.hovered ? AppTheme.surfaceActive : "transparent"
        }
        contentItem: Item {
            CrispIcon {
                anchors.centerIn: parent
                width: 18; height: 18
                source: AppTheme.icon("settings.svg", cloudSettingsControl.hovered ? AppTheme.text : AppTheme.textMuted)
            }
        }
        GToolTip { text: card.settingsToolTip }
    }
}
