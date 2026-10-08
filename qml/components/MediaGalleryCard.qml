import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import Lurviko.App

Rectangle {
    id: card
    property var itemData: null
    property string imageSource: ""
    property bool videoMode: false
    property bool artworkMode: false
    property bool featured: false
    property bool compact: false
    property bool singleClickOpen: false
    property bool selected: false
    property real textScale: 1.0
    required property var lang
    signal activated()
    signal pressed(int modifiers)
    signal browseRequested(string location)
    signal previewRequested(var item, var anchor)
    signal previewDismissed(var anchor)

    radius: featured ? 18 : 13
    color: card.selected ? AppTheme.accentSoft : AppTheme.surface
    border.width: card.selected ? 2 : 1
    border.color: card.selected ? AppTheme.accent : (mouse.containsMouse ? AppTheme.accentBorder : AppTheme.border)
    clip: true
    opacity: itemData ? 1 : 0

    Behavior on border.color { ColorAnimation { duration: 120 } }
    onItemDataChanged: previewDismissed(card)
    Component.onDestruction: previewDismissed(card)

    HoverHandler { cursorShape: Qt.PointingHandCursor }

    // Keep media and metadata in separate layout regions.  The old card
    // overlaid the title on top of PreserveAspectFit images, which became
    // especially visible for portrait/square media with letterboxing.
    readonly property int metadataHeight: card.compact ? 0 : Math.max(40, Math.round(44 * card.textScale))

    Item {
        id: roundedContent
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: metadataPanel.visible ? metadataPanel.top : parent.bottom
        anchors.margins: card.videoMode ? 0 : 1
        clip: true
        // StackLayout also constructs this component while the media page is
        // hidden. Allocate the off-screen mask texture only for a real card.
        layer.enabled: card.itemData !== null && card.imageSource.length > 0
        layer.smooth: true
        layer.samples: 4
        layer.effect: MultiEffect {
            maskEnabled: true
            maskSource: roundedMask
        }

        Image {
            id: preview
            anchors.fill: parent
            anchors.margins: (card.videoMode || card.compact) ? 0 : 6
            source: card.imageSource
            // ThumbnailProvider already caps cached previews at 512 px.
            // Keeping the requested decode size stable lets slider movement
            // reuse the same texture instead of decoding again for every px.
            sourceSize.width: 512
            sourceSize.height: 512
            fillMode: (card.videoMode || card.artworkMode) ? Image.PreserveAspectCrop : Image.PreserveAspectFit
            asynchronous: true
            cache: true
        }

        Rectangle {
            visible: card.videoMode
            anchors.centerIn: parent
            width: card.compact ? 34 : 46
            height: width
            radius: width / 2
            color: "#B8000000"
            border.color: "#A0FFFFFF"
            CrispIcon {
                anchors.centerIn: parent
                width: Math.round((card.compact ? 15 : 20) * Math.min(1.12, card.textScale))
                height: width
                source: "qrc:/qt/qml/Lurviko/App/assets/icons/viewer-play.svg"
            }
        }
    }

    Item {
        id: metadataPanel
        visible: !card.compact
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        height: card.metadataHeight

        Column {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            Text {
                width: parent.width
                text: card.itemData ? card.itemData.name : ""
                color: AppTheme.text
                font.pixelSize: Math.round(11 * card.textScale)
                font.weight: Font.DemiBold
                elide: Text.ElideMiddle
                maximumLineCount: 1
            }
            Text {
                width: parent.width
                text: card.itemData ? card.formatSize(card.itemData.size) : ""
                color: AppTheme.textMuted
                font.pixelSize: Math.round(8 * card.textScale)
                elide: Text.ElideRight
                maximumLineCount: 1
            }
        }
    }

    Rectangle {
        id: roundedMask
        width: roundedContent.width
        height: roundedContent.height
        radius: Math.max(0, card.radius - (card.videoMode ? 0 : 1))
        color: "white"
        antialiasing: true
        visible: false
        layer.enabled: roundedContent.layer.enabled
        layer.samples: 4
    }

    function formatSize(bytes) {
        const n = Number(bytes || 0)
        if (n < 1024 * 1024) return (n / 1024).toFixed(0) + " KiB"
        if (n < 1024 * 1024 * 1024) return (n / (1024 * 1024)).toFixed(1) + " MiB"
        return (n / (1024 * 1024 * 1024)).toFixed(1) + " GiB"
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: Qt.PointingHandCursor
        onEntered: if (card.itemData) card.previewRequested(card.itemData, card)
        onPositionChanged: if (!pressed && card.itemData) card.previewRequested(card.itemData, card)
        onExited: card.previewDismissed(card)
        onPressed: card.previewDismissed(card)
        onClicked: function(event) {
            if (!card.itemData)
                return
            if (event.button === Qt.RightButton) {
                menu.popup()
                return
            }
            if (event.button === Qt.LeftButton) {
                card.pressed(event.modifiers)
                if (card.singleClickOpen)
                    card.activated()
            }
        }
        onDoubleClicked: function(event) {
            if (!card.itemData || card.singleClickOpen)
                return
            if (event.button === Qt.LeftButton)
                card.activated()
        }
    }

    GMenu {
        id: menu
        GMenuItem {
            text: card.lang.language === "tr" ? "İçeren klasörü aç" : "Open containing folder"
            onTriggered: card.browseRequested(card.itemData.parentPath)
        }
        GMenuItem {
            text: card.lang.language === "tr" ? "Harici uygulamada aç" : "Open externally"
            onTriggered: Qt.openUrlExternally(card.itemData.url)
        }
    }
}
