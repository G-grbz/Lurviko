import QtQuick
import QtQuick.Controls
import Lurviko.App
import Lurviko.Backend

Rectangle {
    id: card
    // Opens with the type's preferred application, like the file browser;
    // Qt.openUrlExternally asked for an application every time.
    OpenWithModel { id: cardOpenWith }
    property var itemData: null
    property string imageSource: ""
    property bool videoMode: false
    property bool artworkMode: false
    property bool featured: false
    property bool compact: false
    property bool singleClickOpen: false
    property bool selected: false
    property bool selectionEnabled: false
    property var contextHandler: null
    signal toggleSelection()
    property real textScale: 1.0
    required property var lang
    signal activated()
    signal pressed(int modifiers)
    signal browseRequested(string location)
    signal revealRequested(string location, string itemUrl)
    signal previewRequested(var item, var anchor)
    signal previewDismissed(var anchor)

    radius: featured ? 18 : 13
    color: card.selected ? AppTheme.accentSoft : AppTheme.surface
    border.width: card.selected ? 2 : 1
    border.color: card.selected ? AppTheme.accent : (mouse.containsMouse ? AppTheme.accentBorder : AppTheme.border)
    clip: true
    opacity: itemData ? (AppTheme.itemIsHidden(itemData.name, itemData.path || itemData.url, itemData.hidden)
                        ? AppTheme.hiddenItemOpacity : 1) : 0

    Behavior on border.color { ColorAnimation { duration: 120 } }
    onItemDataChanged: previewDismissed(card)
    Component.onDestruction: previewDismissed(card)

    HoverHandler { cursorShape: Qt.PointingHandCursor }

    // Keep media and metadata in separate layout regions.  The old card
    // overlaid the title on top of PreserveAspectFit images, which became
    // especially visible for portrait/square media with letterboxing.
    readonly property int metadataHeight: card.compact ? 0 : Math.max(40, Math.round(44 * card.textScale))
        + AppTheme.fileLabelExtraHeight(Math.round(11 * card.textScale), Math.round(8 * card.textScale))

    // Decode size bucket, in logical pixels (the provider multiplies by the
    // device pixel ratio and caps at 512). Every card used to request 512 px
    // regardless of its size: 1 MiB of RAM and 1 MiB of VRAM per card even at
    // the default zoom, where ~200 px is shown. Buckets keep the texture
    // shared while the zoom slider moves within a step. Cropped (video and
    // artwork) cards must cover their box, so they ask one step higher.
    readonly property int thumbnailRequest: {
        const extent = Math.max(card.width, card.height) * ((card.videoMode || card.artworkMode) ? 1.5 : 1.0)
        return extent <= 192 ? 192 : (extent <= 256 ? 256 : (extent <= 384 ? 384 : 512))
    }
    // Cropped media is clipped to the card's rounded corners by one shader
    // pass. The previous layer + MultiEffect mask allocated two offscreen
    // 4x-multisampled textures per card (the content and its mask), which
    // multiplied VRAM use while scrolling and kept it for pooled delegates.
    // Fitted image thumbnails need no clip: the provider already rounds them.
    readonly property bool roundedCrop: (card.videoMode || card.artworkMode)
                                        && card.imageSource.length > 0
                                        && GraphicsInfo.api !== GraphicsInfo.Software

    Item {
        id: roundedContent
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: metadataPanel.visible ? metadataPanel.top : parent.bottom
        anchors.margins: card.videoMode ? 0 : 1
        // Only the plain-Image fallback (software renderer) crops outside.
        clip: !card.roundedCrop && (card.videoMode || card.artworkMode)

        Image {
            id: preview
            anchors.fill: parent
            anchors.margins: (card.videoMode || card.compact) ? 0 : 6
            source: card.imageSource
            sourceSize.width: card.thumbnailRequest
            sourceSize.height: card.thumbnailRequest
            fillMode: (card.videoMode || card.artworkMode) ? Image.PreserveAspectCrop : Image.PreserveAspectFit
            asynchronous: true
            cache: true
            // Drawn through roundedCropEffect when cropping.
            visible: !card.roundedCrop
        }

        ShaderEffect {
            id: roundedCropEffect
            anchors.fill: preview
            visible: card.roundedCrop
            property var source: preview
            property size itemSize: Qt.size(width, height)
            property size cardSize: Qt.size(width, height)
            property size sourceImageSize: Qt.size(preview.implicitWidth, preview.implicitHeight)
            property real padding: 0
            property real cornerRadius: Math.max(0, card.radius - (card.videoMode ? 0 : 1))
            property real shadowOffset: 0
            property real shadowSoftness: 1
            property real shadowAlpha: 0
            property real hasImage: preview.status === Image.Ready ? 1 : 0
            fragmentShader: "qrc:/qt/qml/Lurviko/App/qml/shaders/roundedthumb.frag.qsb"
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
                // Center the triangle's visual mass, not the SVG canvas.
                anchors.horizontalCenterOffset: width * 0.10
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
            anchors.top: parent.top
            anchors.topMargin: Math.max(5, Math.round(7 * card.textScale))
            spacing: 2
            GText {
                width: parent.width
                typographyRole: "body"
                text: card.itemData ? card.itemData.name : ""
                baseColor: AppTheme.text
                basePixelSize: Math.round(11 * card.textScale)
                baseFontWeight: Font.DemiBold
                wrapMode: Text.Wrap
                elide: Text.ElideRight
                maximumLineCount: AppTheme.fileTitleLines
            }
            GText {
                width: parent.width
                text: card.itemData ? card.formatSize(card.itemData.size) : ""
                baseColor: AppTheme.textMuted
                basePixelSize: Math.round(8 * card.textScale)
                elide: Text.ElideRight
                wrapMode: Text.Wrap
                maximumLineCount: AppTheme.fileSubtitleLines
            }
        }
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
                if (card.contextHandler) card.contextHandler(card.itemData, mouse, event.x, event.y)
                else menu.popup()
                return
            }
            if (event.button === Qt.LeftButton) {
                if (card.selectionEnabled && (event.modifiers & Qt.ControlModifier)) {
                    card.toggleSelection()
                    return
                }
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

    GCheckBox {
        objectName: "mediaSelectionCheck"
                    width: 30; height: 30
        visible: card.selectionEnabled
        anchors { top: parent.top; left: parent.left; margins: 5 }
        z: 5
        checked: card.selected
        onClicked: { card.previewDismissed(card); card.toggleSelection() }
        Accessible.name: card.lang.t(card.selected ? "deselect_all" : "select_all")
    }

    GMenu {
        id: menu
        GMenuItem {
            text: card.lang.language === "tr" ? "İçeren klasörü aç" : "Open containing folder"
            objectName: "mediaOpenContainingFolder"
            onTriggered: card.revealRequested(card.itemData.parentPath, card.itemData.url)
        }
        GMenuItem {
            text: card.lang.language === "tr" ? "Harici uygulamada aç" : "Open externally"
            onTriggered: cardOpenWith.openDefault(card.itemData.url, "")
        }
    }
}
