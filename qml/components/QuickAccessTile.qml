import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lurviko.App

Rectangle {
    id: tile
    property string language: "en"
    property int shortcutIndex: -1
    property bool fixedShortcut: false
    property string iconSource: AppTheme.icon("folder.svg")
    property string title: ""
    property string path: ""
    property int systemIconPixels: AppTheme.systemHomeIconSize
    readonly property int effectiveIconPixels: AppTheme.effectiveHomeIconSize
    property bool quickAccessReorder: true
    property var recentLocations: []
    property bool dragging: reorderDrag.active
    property bool suppressClick: false
    // Second line under the path (e.g. "1.204 öğe · 3,2 GB"); empty hides it.
    property string infoText: ""
    readonly property bool hovered: mouse.containsMouse || actionsHover.hovered
    // Compact dock layout: icon over the title. Drag to reorder, the context
    // menu and the Last Location menu keep working as in the full card.
    property bool compact: false
    function openRecentMenu(anchor) {
        tile.pruneRecentRequested()
        // Let the model notification update the Instantiator before
        // opening so removed entries never flash in the popup.
        Qt.callLater(function() {
            const point = anchor.mapToItem(tile, 0, anchor.height + 6)
            recentMenu.x = tile.compact ? point.x
                         : Math.max(6, Math.min(tile.width - recentMenu.implicitWidth - 6, point.x + anchor.width - recentMenu.implicitWidth))
            recentMenu.y = point.y
            recentMenu.open()
        })
    }
    // Opaque even with a translucent Qt window (same rule as the disk cards).
    readonly property bool glassSurface: AppTheme.useQtTheme && AppTheme.translucentWindow

    // file:///home/user/x -> ~/x ; other locations stay as they are.
    function prettyLocation(location) {
        let value = tile.displayLocation(location)
        const home = tile.displayLocation(String(StandardPaths.writableLocation(StandardPaths.HomeLocation))).replace(/\/+$/, "")
        if (home.length > 1 && (value === home || value.indexOf(home + "/") === 0))
            value = "~" + value.substring(home.length)
        return value
    }

    function displayLocation(location) {
        const value = String(location || "")
        if (value.indexOf("file://") === 0) {
            try {
                return decodeURIComponent(value.replace(/^file:\/\//, ""))
            } catch (error) {
                return value.replace(/^file:\/\//, "")
            }
        }
        return value
    }
    signal openRequested(string path)
    signal openNewTabRequested(string path)
    signal editRequested(int index, string title, string path)
    signal removeRequested(int index)
    signal reorderRequested(int from, int to)
    signal externalDropRequested(string url, string title, string icon)
    signal pruneRecentRequested()

    implicitHeight: compact ? 104 : Math.max(74, tile.effectiveIconPixels + 28)
    implicitWidth: compact ? 108 : 280

    radius: AppTheme.cardRadius
    color: dropTarget.containsDrag
           ? (tile.glassSurface ? AppTheme.mix(AppTheme.surface, AppTheme.accent, 0.14) : AppTheme.accentSoft)
           : (tile.hovered || dragging
              ? (tile.glassSurface ? AppTheme.mix(AppTheme.surface, AppTheme.accent, 0.05) : AppTheme.cardHover)
              : (tile.glassSurface ? AppTheme.surface : AppTheme.cardSurface))
    border.color: dropTarget.containsDrag ? AppTheme.accent : (tile.hovered ? AppTheme.accentBorder : AppTheme.border)
    border.width: dropTarget.containsDrag ? 2 : 1
    scale: dragging ? 0.97 : (mouse.pressed ? 0.98 : 1)
    opacity: dragging ? 0.72 : 1
    Behavior on color { ColorAnimation { duration: 100 } }
    Behavior on scale { NumberAnimation { duration: 90 } }

    Drag.dragType: Drag.Automatic
    Drag.active: reorderDrag.active
    Drag.source: tile
    Drag.supportedActions: Qt.MoveAction
    Drag.proposedAction: Qt.MoveAction

    Timer { id: dragClickReset; interval: 90; onTriggered: tile.suppressClick = false }

    DropArea {
        id: dropTarget
        anchors.fill: parent
        enabled: true
        onDropped: function(drop) {
            if (drop.source && drop.source.quickAccessReorder
                    && drop.source.shortcutIndex !== tile.shortcutIndex) {
                tile.reorderRequested(drop.source.shortcutIndex, tile.shortcutIndex)
                drop.acceptProposedAction()
            } else if (drop.source && drop.source.dragUrl) {
                tile.externalDropRequested(drop.source.dragUrl,
                                           drop.source.title || "",
                                           drop.source.dragIcon || "folder.svg")
                drop.acceptProposedAction()
            }
        }
    }

    RowLayout {
        z: 2
        visible: !tile.compact
        anchors.fill: parent
        anchors.margins: 13
        spacing: 11

        Item {
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: tile.effectiveIconPixels
            Layout.preferredHeight: tile.effectiveIconPixels
            CrispIcon {
                id: quickIconVisual
                objectName: "quickAccessIconVisual"
                anchors.fill: parent
                source: AppTheme.useSystemIcons
                        ? AppTheme.systemIconAtSize(tile.iconSource, tile.effectiveIconPixels)
                        : tile.iconSource
                visible: status === Image.Ready || status === Image.Loading
            }
            CrispIcon {
                objectName: "quickAccessIconFallback"
                anchors.fill: parent
                visible: quickIconVisual.status === Image.Error || quickIconVisual.status === Image.Empty
                source: visible ? (tile.fixedShortcut ? AppTheme.icon("last-location.svg")
                        : AppTheme.useSystemIcons
                        ? AppTheme.systemIconAtSize(AppTheme.systemIcon("folder"), tile.effectiveIconPixels)
                        : AppTheme.icon("folder.svg")) : ""
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            Layout.rightMargin: hoverActionsOverlay.visible ? hoverActionsOverlay.width - 8 : 0
            spacing: 3
            GText {
                objectName: "quickAccessTitle"
                typographyRole: "body"
                Layout.fillWidth: true
                text: tile.title
                baseColor: AppTheme.text
                basePixelSize: 12
                baseFontWeight: Font.Normal
                elide: Text.ElideRight
            }
            GText {
                objectName: "quickAccessPath"
                Layout.fillWidth: true
                text: tile.prettyLocation(tile.path)
                baseColor: AppTheme.textMuted
                basePixelSize: 10
                elide: Text.ElideMiddle
            }
            GText {
                objectName: "quickAccessInfo"
                visible: tile.infoText.length > 0
                Layout.fillWidth: true
                text: tile.infoText
                baseColor: AppTheme.textFaint
                basePixelSize: 9
                elide: Text.ElideRight
            }
        }

        Rectangle {
            id: recentBadge
            visible: tile.fixedShortcut
            Layout.preferredWidth: recentBadgeRow.implicitWidth + 18
            Layout.preferredHeight: 24
            radius: 12
            color: recentBadgeMouse.containsMouse ? AppTheme.accent
                                                  : AppTheme.tint(AppTheme.accent, AppTheme.dark ? 0.22 : 0.12)
            border.width: 1
            border.color: recentBadgeMouse.containsMouse ? AppTheme.accent
                                                         : AppTheme.tint(AppTheme.accent, AppTheme.dark ? 0.55 : 0.40)
            Behavior on color { ColorAnimation { duration: 90 } }

            Row {
                id: recentBadgeRow
                anchors.centerIn: parent
                spacing: 5
                CrispIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 12; height: 12
                    source: AppTheme.icon("history.svg")
                }
                GText {
                    anchors.verticalCenter: parent.verticalCenter
                    text: tile.language === "tr" ? "Son 10" : "Last 10"
                    baseColor: recentBadgeMouse.containsMouse ? "white" : AppTheme.accent
                    basePixelSize: 10
                    baseFontWeight: Font.DemiBold
                }
                GText {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "▾"
                    baseColor: recentBadgeMouse.containsMouse ? "white" : AppTheme.accent
                    basePixelSize: 9
                }
            }

            MouseArea {
                id: recentBadgeMouse
                anchors.fill: parent
                z: 8
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                acceptedButtons: Qt.LeftButton
                onPressed: function(mouse) { mouse.accepted = true }
                onReleased: function(mouse) { mouse.accepted = true }
                onClicked: function(mouse) {
                    mouse.accepted = true
                    tile.openRecentMenu(recentBadge)
                }
            }
        }

    }

    // Quick actions float over the right edge while hovering; the text column
    // reserves their width meanwhile, so nothing moves, it just elides sooner.
    // (The Last Location card has its own "Last 10" menu instead.)
    // ---- Compact layout -----------------------------------------------------
    Column {
        z: 2
        visible: tile.compact
        anchors.centerIn: parent
        spacing: 8
        CrispIcon {
            objectName: "quickAccessCompactIcon"
            anchors.horizontalCenter: parent.horizontalCenter
            width: 44; height: 44
            source: tile.fixedShortcut && !tile.path.length ? AppTheme.icon("last-location.svg")
                    : AppTheme.useSystemIcons ? AppTheme.systemIconAtSize(tile.iconSource, 44) : tile.iconSource
            scale: tile.hovered ? 1.06 : 1.0
            Behavior on scale { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
        }
        GText {
            width: tile.width - 14
            horizontalAlignment: Text.AlignHCenter
            text: tile.title
            baseColor: AppTheme.text
            basePixelSize: 11
            elide: Text.ElideRight
        }
    }
    // Last Location keeps its "last 10" menu as a corner button.
    Rectangle {
        id: compactRecentButton
        objectName: "quickAccessCompactRecent"
        z: 8
        visible: tile.compact && tile.fixedShortcut
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 6
        width: 24; height: 24
        radius: 12
        color: compactRecentMouse.containsMouse ? AppTheme.accent
                                                : AppTheme.tint(AppTheme.accent, AppTheme.dark ? 0.22 : 0.12)
        border.width: 1
        border.color: AppTheme.tint(AppTheme.accent, AppTheme.dark ? 0.55 : 0.40)
        CrispIcon {
            anchors.centerIn: parent
            width: 13; height: 13
            source: AppTheme.icon("history.svg", compactRecentMouse.containsMouse ? "#ffffff" : AppTheme.accent)
        }
        MouseArea {
            id: compactRecentMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: function(mouse) {
                mouse.accepted = true
                tile.openRecentMenu(compactRecentButton)
            }
        }
        GToolTip {
            visible: compactRecentMouse.containsMouse
            text: tile.language === "tr" ? "Son 10 konum" : "Last 10 locations"
        }
    }
    GToolTip {
        visible: tile.compact && mouse.containsMouse && !compactRecentMouse.containsMouse && tile.path.length > 0
        text: tile.prettyLocation(tile.path)
    }

    Item {
        id: hoverActionsOverlay
        z: 5
        visible: tile.hovered && !tile.dragging && !tile.fixedShortcut && !tile.compact
        anchors.right: parent.right
        anchors.rightMargin: 1
        anchors.verticalCenter: parent.verticalCenter
        width: hoverActions.width + 14
        height: parent.height - 2
        HoverHandler { id: actionsHover }
        Row {
            id: hoverActions
            objectName: "quickAccessActions"
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            TileAction {
                iconName: "menu-open.svg"
                tip: tile.language === "tr" ? "Yeni sekmede aç" : "Open in new tab"
                onClicked: tile.openNewTabRequested(tile.path)
            }
            TileAction {
                iconName: "edit.svg"
                tip: tile.language === "tr" ? "Düzenle" : "Edit"
                onClicked: tile.editRequested(tile.shortcutIndex, tile.title, tile.path)
            }
            TileAction {
                iconName: "close-ui.svg"
                tip: tile.language === "tr" ? "Kaldır" : "Remove"
                danger: true
                onClicked: tile.removeRequested(tile.shortcutIndex)
            }
        }
    }

    MouseArea {
        id: mouse
        z: 1
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
        onClicked: function(event) {
            if (tile.suppressClick) {
                event.accepted = true
                return
            }
            if (event.button === Qt.MiddleButton)
                tile.openNewTabRequested(tile.path)
            else if (event.button === Qt.RightButton)
                menu.popup(event.x, event.y)
            else if (event.button === Qt.LeftButton)
                tile.openRequested(tile.path)
        }
    }

    DragHandler {
        id: reorderDrag
        target: null
        enabled: true
        acceptedButtons: Qt.LeftButton
        onActiveChanged: {
            if (!active) {
                tile.suppressClick = true
                dragClickReset.restart()
            }
        }
    }

    GMenu {
        id: recentMenu
        implicitWidth: 430
        maximumPopupHeight: 440

        GMenuItem {
            enabled: false
            text: tile.language === "tr" ? "Son 10 Konum" : "Last 10 locations"
        }

        Instantiator {
            model: tile.recentLocations
            delegate: GMenuItem {
                required property int index
                required property var modelData
                text: (index + 1) + ".  " + String(modelData.title || "")
                      + "   ·   " + tile.displayLocation(modelData.location)

                onMiddleClicked: {
                    const targetLocation = String(modelData.location || "")
                    recentMenu.close()
                    if (targetLocation.length > 0)
                        tile.openNewTabRequested(targetLocation)
                }

                onTriggered: {
                    const targetLocation = String(modelData.location || "")
                    recentMenu.close()
                    if (targetLocation.length > 0)
                        tile.openRequested(targetLocation)
                }
            }
            onObjectAdded: function(index, object) { recentMenu.insertItem(index + 1, object) }
            onObjectRemoved: function(index, object) { recentMenu.removeItem(object) }
        }
    }

    GMenu {
        id: menu
        GMenuItem {
            text: tile.language === "tr" ? "Düzenle" : "Edit"
            onTriggered: tile.editRequested(tile.shortcutIndex, tile.title, tile.path)
        }
        GMenuItem {
            visible: !tile.fixedShortcut
            text: tile.language === "tr" ? "Kaldır" : "Remove"
            onTriggered: tile.removeRequested(tile.shortcutIndex)
        }
    }

    component TileAction: Rectangle {
        id: action
        property string iconName: ""
        property string tip: ""
        property bool danger: false
        signal clicked()
        width: 26; height: 26
        radius: 8
        z: 6
        color: actionMouse.containsMouse
               ? AppTheme.tint(action.danger ? AppTheme.danger : AppTheme.accent, AppTheme.dark ? 0.22 : 0.12)
               : "transparent"
        CrispIcon {
            anchors.centerIn: parent
            width: 15; height: 15
            source: AppTheme.icon(action.iconName, actionMouse.containsMouse
                                  ? (action.danger ? AppTheme.danger : AppTheme.accent) : AppTheme.textMuted)
        }
        MouseArea {
            id: actionMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: action.clicked()
        }
        GToolTip { visible: actionMouse.containsMouse; text: action.tip }
    }
}
