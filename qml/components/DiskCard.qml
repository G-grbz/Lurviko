import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lurviko.App

Rectangle {
    id: card
    property string language: "en"
    property int diskIndex: -1
    property bool diskReorder: true
    property bool dragging: reorderDrag.active
    property bool suppressClick: false
    property int pendingReorderTo: -1
    property string title: "Disk"
    property string stableId: ""
    property string iconName: "drive.svg"
    property string subtitle: ""
    property string rootPath: "/"
    property string device: ""
    property string usedText: ""
    property string totalText: ""
    property real ratio: 0
    property bool mounted: false
    property bool favorite: false
    readonly property bool favoriteAvailable: card.rootPath.length > 0 && !card.rootPath.startsWith("/dev/")
    property bool canMountToggle: false
    property bool mountBusy: false
    property string mountActionText: ""
    property string mountedStatusText: ""
    property int systemIconPixels: AppTheme.systemHomeIconSize
    readonly property int effectiveIconPixels: Math.max(24, Math.round(AppTheme.effectiveHomeIconSize * 1.05))
    // Large icons need their own row; otherwise the text/badges get squeezed out.
    readonly property bool stackedLayout: effectiveIconPixels >= 72
    readonly property int responsiveHeight: stackedLayout
            ? Math.max(176, effectiveIconPixels + 116)
            : Math.max(138, effectiveIconPixels + 84)
    signal openRequested(string path)
    signal openNewTabRequested(string path)
    signal favoriteToggleRequested(string path, string title)
    signal editRequested(string stableId, string title, string iconName)
    signal mountToggleRequested(string device)
    signal reorderRequested(int from, int to)
    signal reorderDragStateChanged(bool active)

    radius: AppTheme.cardRadius
    color: dropTarget.containsDrag ? AppTheme.accentSoft : (mouse.containsMouse || dragging ? AppTheme.cardHover : AppTheme.cardSurface)
    border.color: dropTarget.containsDrag ? AppTheme.accent : (mouse.containsMouse ? AppTheme.accentBorder : AppTheme.border)
    border.width: dropTarget.containsDrag ? 2 : 1
    scale: dragging ? 0.97 : 1.0
    opacity: dragging ? 0.72 : 1.0

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
            if (drop.source && drop.source.diskReorder
                    && drop.source.diskIndex !== card.diskIndex) {
                // Queue the destination on the SOURCE card. The actual model
                // move happens after DragHandler becomes inactive; mutating a
                // Repeater model from inside the drop event can invalidate the
                // active drag source.
                drop.source.pendingReorderTo = card.diskIndex
                drop.acceptProposedAction()
            }
        }
    }

    MouseArea {
        id: mouse
        z: 1
        anchors.fill: parent
        anchors.rightMargin: 38
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
        onClicked: function(event) {
            if (card.suppressClick) {
                event.accepted = true
                return
            }
            if (event.button === Qt.RightButton) {
                diskContextMenu.popup(event.x, event.y)
                return
            }
            if (!card.mounted && card.canMountToggle) {
                card.mountToggleRequested(card.device)
                return
            }
            if (event.button === Qt.MiddleButton)
                card.openNewTabRequested(card.rootPath)
            else
                card.openRequested(card.rootPath)
        }
    }

    GMenu {
        id: diskContextMenu
        GMenuItem {
            text: card.language === "tr" ? "Düzenle" : "Edit"
            onTriggered: card.editRequested(card.stableId, card.title, card.iconName)
        }
    }

    DragHandler {
        id: reorderDrag
        target: null
        acceptedButtons: Qt.LeftButton
        onActiveChanged: {
            if (active) {
                card.pendingReorderTo = -1
                card.reorderDragStateChanged(true)
                return
            }

            card.suppressClick = true
            dragClickReset.restart()
            if (card.pendingReorderTo >= 0 && card.pendingReorderTo !== card.diskIndex) {
                var destination = card.pendingReorderTo
                card.pendingReorderTo = -1
                card.reorderRequested(card.diskIndex, destination)
            } else {
                card.pendingReorderTo = -1
            }
            card.reorderDragStateChanged(false)
        }
    }

    Item {
        id: headerArea
        z: 2
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: capacityRow.top
        anchors.leftMargin: 18
        anchors.rightMargin: 18
        anchors.topMargin: 14
        anchors.bottomMargin: 10

        ColumnLayout {
            id: horizontalHeader
            visible: !card.stackedLayout
            anchors.fill: parent
            spacing: 8

            Text {
                text: card.title
                color: AppTheme.text
                font.pixelSize: 15
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 12

                Item {
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: card.effectiveIconPixels
                    Layout.preferredHeight: card.effectiveIconPixels
                    CrispIcon {
                        anchors.fill: parent
                        source: AppTheme.useSystemIcons
                                ? AppTheme.systemIconAtSize(AppTheme.discoveryIcon(card.iconName, "drive.svg"), card.effectiveIconPixels)
                                : AppTheme.discoveryIcon(card.iconName, "drive.svg")
                    }
                }

                Text {
                    text: card.subtitle
                    color: AppTheme.textMuted
                    font.pixelSize: 11
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                }

                DiskFavoriteButton { }
                DiskMountBadge { }
            }
        }

        ColumnLayout {
            id: stackedHeader
            visible: card.stackedLayout
            anchors.fill: parent
            spacing: 7

            Text {
                text: card.title
                color: AppTheme.text
                font.pixelSize: 15
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
                Layout.fillWidth: true
            }

            Item {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: card.effectiveIconPixels
                Layout.preferredHeight: card.effectiveIconPixels
                CrispIcon {
                    anchors.fill: parent
                    source: AppTheme.useSystemIcons
                            ? AppTheme.systemIconAtSize(AppTheme.discoveryIcon(card.iconName, "drive.svg"), card.effectiveIconPixels)
                            : AppTheme.discoveryIcon(card.iconName, "drive.svg")
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    text: card.subtitle
                    color: AppTheme.textMuted
                    font.pixelSize: 11
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                }

                DiskFavoriteButton { }
                DiskMountBadge { }
            }
        }
    }

    component DiskFavoriteButton: Item {
        id: favoriteControl
        visible: card.favoriteAvailable
        readonly property bool hovered: favoriteMouse.containsMouse
        Layout.preferredWidth: visible ? 30 : 0
        Layout.preferredHeight: visible ? 30 : 0
        Layout.alignment: Qt.AlignVCenter
        z: 5

        Rectangle {
            anchors.fill: parent
            radius: 9
            color: favoriteControl.hovered ? AppTheme.surfaceRaised : "transparent"
        }
        CrispIcon {
            anchors.centerIn: parent
            source: AppTheme.icon("favorite.svg")
            width: 18
            height: 18
            opacity: card.favorite ? 1.0 : 0.35
        }
        MouseArea {
            id: favoriteMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: card.favoriteToggleRequested(card.rootPath, card.title)
        }
        GToolTip { text: card.favorite ? qsTr("Favorilerden çıkar") : qsTr("Favorilere ekle") }
    }

    component DiskMountBadge: Rectangle {
        id: mountBadge
        Layout.alignment: Qt.AlignVCenter
        z: 6
        radius: 10
        implicitWidth: Math.max(54, mountBadgeRow.implicitWidth + 16)
        implicitHeight: 26
        color: {
            if (!card.canMountToggle)
                return card.mounted ? (AppTheme.dark ? "#17382B" : "#EAF8F1") : AppTheme.surfaceRaised
            if (mountBadgeMouse.containsMouse)
                return card.mounted ? (AppTheme.dark ? "#4A2B2F" : "#FFF0F0") : AppTheme.accentSoft
            return card.mounted ? (AppTheme.dark ? "#2A2738" : "#F7F4FF") : AppTheme.surfaceRaised
        }
        border.width: 1
        border.color: {
            if (!card.canMountToggle)
                return card.mounted ? AppTheme.success : AppTheme.border
            if (card.mountBusy)
                return AppTheme.accentBorder
            return card.mounted ? (mountBadgeMouse.containsMouse ? AppTheme.danger : AppTheme.borderStrong)
                                : (mountBadgeMouse.containsMouse ? AppTheme.accent : AppTheme.borderStrong)
        }

        RowLayout {
            id: mountBadgeRow
            anchors.centerIn: parent
            spacing: 5
            GBusyIndicator {
                visible: card.mountBusy
                running: visible
                Layout.preferredWidth: 13
                Layout.preferredHeight: 13
            }
            Text {
                text: card.mountActionText.length ? card.mountActionText : card.mountedStatusText
                color: {
                    if (card.mountBusy) return AppTheme.textMuted
                    if (!card.canMountToggle) return card.mounted ? AppTheme.success : AppTheme.textMuted
                    return card.mounted ? (mountBadgeMouse.containsMouse ? AppTheme.danger : AppTheme.textMuted)
                                        : (mountBadgeMouse.containsMouse ? AppTheme.accent : AppTheme.textMuted)
                }
                font.pixelSize: 9
                font.weight: Font.DemiBold
            }
        }

        MouseArea {
            id: mountBadgeMouse
            anchors.fill: parent
            enabled: card.canMountToggle && !card.mountBusy
            hoverEnabled: true
            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: card.mountToggleRequested(card.device)
        }
    }

    Rectangle {
        id: usageTrack
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: 18
        anchors.rightMargin: 18
        anchors.bottomMargin: 12
        height: 7
        radius: 4
        color: AppTheme.surfaceRaised

        Rectangle {
            width: parent.width * Math.min(1, Math.max(0, card.ratio))
            height: parent.height
            radius: parent.radius
            color: card.ratio > 0.9 ? AppTheme.danger : AppTheme.accent
        }
    }

    RowLayout {
        id: capacityRow
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: usageTrack.top
        anchors.leftMargin: 18
        anchors.rightMargin: 18
        anchors.bottomMargin: 7
        spacing: 8

        Text { text: card.usedText; color: AppTheme.textMuted; font.pixelSize: 11 }
        Item { Layout.fillWidth: true }
        Text { text: card.totalText; color: AppTheme.textMuted; font.pixelSize: 11 }
    }
}
