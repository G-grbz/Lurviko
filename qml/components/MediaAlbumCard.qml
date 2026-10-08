import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import Lurviko.App

Rectangle {
    id: card
    required property var album
    required property var lang
    property bool selected: false
    property var hoveredCover: null
    signal activated()
    signal previewRequested(var item, var anchor)
    signal previewDismissed(var anchor)

    radius: 14
    color: selected ? AppTheme.accentSoft : AppTheme.surface
    border.color: selected ? AppTheme.accent : (mouse.containsMouse ? AppTheme.accentBorder : AppTheme.border)
    border.width: 1
    Component.onDestruction: clearPreview()
    onAlbumChanged: clearPreview()

    function clearPreview() {
        if (hoveredCover) previewDismissed(hoveredCover)
        hoveredCover = null
    }
    function updatePreview(x, y) {
        if (x < covers.x || x >= covers.x + covers.width
                || y < covers.y || y >= covers.y + covers.height) {
            clearPreview()
            return
        }
        const index = Math.min(album.covers.length - 1,
                               Math.floor((x - covers.x) / ((covers.width + 3) / album.covers.length)))
        const cover = coverRepeater.itemAt(index)
        if (!cover) return
        if (hoveredCover && hoveredCover !== cover) previewDismissed(hoveredCover)
        hoveredCover = cover
        previewRequested(cover.modelData.item, cover)
    }

    Item {
        id: covers
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 6
        height: 90
        layer.enabled: true
        layer.smooth: true
        layer.effect: MultiEffect { maskEnabled: true; maskSource: coverMask }
        Row {
            anchors.fill: parent
            spacing: 3
            Repeater {
                id: coverRepeater
                model: card.album.covers
                delegate: Item {
                    id: cover
                    objectName: "galleryAlbumCover"
                    required property var modelData
                    width: (covers.width - (card.album.covers.length - 1) * 3) / card.album.covers.length
                    height: covers.height
                    Rectangle { anchors.fill: parent; color: AppTheme.surfaceRaised }
                    Image {
                        anchors.fill: parent
                        source: modelData.thumbnailSource
                        sourceSize: Qt.size(512, 512)
                        asynchronous: true
                        cache: true
                        fillMode: Image.PreserveAspectCrop
                    }
                }
            }
        }
    }
    Rectangle {
        id: coverMask
        width: covers.width; height: covers.height
        radius: 9; color: "white"; visible: false
        antialiasing: true; layer.enabled: true
    }
    Column {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: covers.bottom
        anchors.margins: 10
        spacing: 4
        Text {
            width: parent.width
            text: card.album.title
            elide: Text.ElideMiddle
            font.pixelSize: 12
            font.weight: Font.DemiBold
            color: AppTheme.text
        }
        Text {
            width: parent.width
            text: card.album.count + (card.lang.language === "tr" ? " görsel · " : " images · ")
                  + Qt.formatDate(new Date(card.album.latestMs), "dd MMM yyyy")
            elide: Text.ElideRight
            font.pixelSize: 10
            color: AppTheme.textMuted
        }
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onEntered: card.updatePreview(mouseX, mouseY)
        onPositionChanged: function(event) { if (!pressed) card.updatePreview(event.x, event.y) }
        onExited: card.clearPreview()
        onPressed: card.clearPreview()
        onClicked: card.activated()
    }
}
