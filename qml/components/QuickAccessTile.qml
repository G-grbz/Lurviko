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

    implicitHeight: Math.max(74, tile.effectiveIconPixels + 28)

    radius: AppTheme.cardRadius
    color: dropTarget.containsDrag ? AppTheme.accentSoft : (mouse.containsMouse || dragging ? AppTheme.cardHover : AppTheme.cardSurface)
    border.color: dropTarget.containsDrag ? AppTheme.accent : (mouse.containsMouse ? AppTheme.accentBorder : AppTheme.border)
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
        anchors.fill: parent
        anchors.margins: 13
        spacing: 11

        Item {
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: tile.effectiveIconPixels
            Layout.preferredHeight: tile.effectiveIconPixels
            CrispIcon {
                id: quickIconVisual
                anchors.fill: parent
                source: AppTheme.useSystemIcons
                        ? AppTheme.systemIconAtSize(tile.iconSource, tile.effectiveIconPixels)
                        : tile.iconSource
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: 3
            Text {
                Layout.fillWidth: true
                text: tile.title
                color: AppTheme.text
                font.pixelSize: 12
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                text: tile.path
                color: AppTheme.textFaint
                font.pixelSize: 10
                elide: Text.ElideMiddle
            }
        }

        Rectangle {
            id: recentBadge
            visible: tile.fixedShortcut
            Layout.preferredWidth: 54
            Layout.preferredHeight: 22
            radius: 8
            color: recentBadgeMouse.containsMouse ? AppTheme.accent : AppTheme.accentSoft
            Behavior on color { ColorAnimation { duration: 90 } }

            Text {
                anchors.centerIn: parent
                text: tile.language === "tr" ? "son" : "recent"
                color: recentBadgeMouse.containsMouse ? "white" : AppTheme.accent
                font.pixelSize: 9
                font.bold: true
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
                    tile.pruneRecentRequested()
                    // Let the model notification update the Instantiator before
                    // opening so removed entries never flash in the popup.
                    Qt.callLater(function() {
                        const point = recentBadge.mapToItem(tile, 0, recentBadge.height + 6)
                        recentMenu.x = Math.max(6, Math.min(tile.width - recentMenu.implicitWidth - 6, point.x + recentBadge.width - recentMenu.implicitWidth))
                        recentMenu.y = point.y
                        recentMenu.open()
                    })
                }
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
}
