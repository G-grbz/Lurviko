import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Lurviko.App

// Discover is an application destination, not a directory shortcut.
// This navigation card is owned by the shared AppSidebar and therefore keeps
// the same geometry and styling on Home, folders, categories and Private.
Rectangle {
    id: card
    property string title: ""
    property string subtitle: ""
    property url iconSource: ""
    property bool active: false
    property string propertiesText: ""
    property string watermarkEditText: "Edit watermark"
    readonly property bool contextMenuVisible: propertiesMenu.visible
    property bool interactionTracked: false
    property bool beingDestroyed: false
    function refreshInteraction() {
        if (beingDestroyed || interactionTracked === contextMenuVisible)
            return
        interactionTracked = contextMenuVisible
        interactionChanged(contextMenuVisible)
    }
    signal clicked()
    signal propertiesRequested()
    signal watermarkEditorRequested()
    signal interactionChanged(bool active)

    implicitHeight: 66
    radius: 14
    // The Discover destination is deliberately frameless in every theme.
    // No hover or active fill: only the accent marker on the left indicates
    // the selected destination. The image remains clipped within the card.
    color: "transparent"
    clip: true
    border.width: 0

    onContextMenuVisibleChanged: refreshInteraction()
    Component.onDestruction: {
        beingDestroyed = true
        if (interactionTracked) interactionChanged(false)
    }

    // Live, theme-aware watermark. The same persisted settings apply on every
    // page. A small inset keeps artwork away from the rounded outline.
    Item {
        objectName: "exploreWatermarkViewport"
        anchors.fill: parent
        anchors.margins: 6
        clip: true
        Image {
            objectName: "exploreHomeWatermark"
            readonly property real glyphHeightFraction: 0.82
            readonly property real naturalWidth: Math.min(card.width * 0.44,
                      (parent.height - 4) / glyphHeightFraction)
            width: Math.max(0, naturalWidth * AppTheme.discoverWatermarkScale)
            height: width
            x: parent.width - width - 1 + AppTheme.discoverWatermarkOffsetX
            y: (parent.height - height) / 2 + 1 + AppTheme.discoverWatermarkOffsetY
            rotation: AppTheme.discoverWatermarkAngle
            source: AppTheme.discoverWatermarkSource
            sourceSize.width: Math.min(768, Math.ceil(width * Screen.devicePixelRatio))
            sourceSize.height: Math.min(768, Math.ceil(height * Screen.devicePixelRatio))
            fillMode: Image.PreserveAspectFit
            smooth: true
            opacity: AppTheme.discoverWatermarkOpacity
            asynchronous: true
            visible: status === Image.Ready
        }
    }

    // Accent is a persistent destination marker, not a file selection bar.
    Rectangle {
        objectName: "exploreActiveMarker"
        anchors.left: parent.left
        anchors.leftMargin: 0
        anchors.verticalCenter: parent.verticalCenter
        height: 26
        width: 3
        radius: 1.5
        color: AppTheme.accent
        visible: card.active
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 11
        anchors.rightMargin: 11
        spacing: 10

        Item {
            objectName: "exploreIconSlot"
            // No icon tile: retain placement but remove its frame and fill.
            Layout.preferredWidth: 40
            Layout.preferredHeight: 40
            CrispIcon {
                anchors.centerIn: parent
                width: 26
                height: 26
                source: card.iconSource
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: 3
            GText {
                objectName: "sidebarExploreTitle"
                Layout.fillWidth: true
                typographyRole: "sidebar"
                text: card.title
                basePixelSize: 13
                baseFontWeight: Font.DemiBold
                baseColor: AppTheme.text
                elide: Text.ElideRight
            }
            GText {
                Layout.fillWidth: true
                typographyRole: "sidebar"
                text: card.subtitle
                basePixelSize: 9
                baseColor: AppTheme.textMuted
                elide: Text.ElideRight
            }
        }
        CrispIcon {
            Layout.preferredWidth: 13
            Layout.preferredHeight: 13
            source: AppTheme.icon("nav-forward.svg")
            opacity: 0.55
        }
    }
    MouseArea {
        id: hitArea
        anchors.fill: parent
        // Keep pointer interactions while suppressing hover state styling.
        hoverEnabled: false
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: Qt.PointingHandCursor
        Accessible.role: Accessible.Button
        Accessible.name: card.title
        onClicked: function(mouse) {
            if (mouse.button === Qt.RightButton)
                propertiesMenu.popup()
            else
                card.clicked()
        }
    }
    // The edit action is also available on right-click (including on Home,
    // folders, category:/ and Private pages).
    GMenu {
        id: propertiesMenu
        GMenuItem {
            text: card.propertiesText
            onTriggered: card.propertiesRequested()
        }
        GMenuItem {
            objectName: "editExploreWatermarkMenuItem"
            text: card.watermarkEditText
            onTriggered: card.watermarkEditorRequested()
        }
    }
}
