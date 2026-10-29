import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Lurviko.App
import Lurviko.Backend

Item {
    id: shell
    required property var lang
    required property var favoritesModel
    required property var storageModel
    property var musicPlayer: null
    property string currentLocation: ""
    property bool unifiedSurface: false
    readonly property bool collapsed: AppTheme.sidebarAutoHide
    readonly property bool iconOnly: AppTheme.sidebarRailMode === "icon"
    readonly property bool titleOnly: AppTheme.sidebarRailMode === "title"
    readonly property var sections: {
        const tr = lang.language === "tr"
        return [
            {key: "places", title: tr ? "Konumlar" : "Places", icon: "folder.svg"},
            {key: "favorites", title: tr ? "Favoriler" : "Favorites", icon: "favorite.svg"},
            {key: "network", title: tr ? "Ağ" : "Network", icon: "network.svg"},
            {key: "disks", title: tr ? "Diskler" : "Devices", icon: "drive.svg"}
        ]
    }
    readonly property real railWidth: iconOnly ? 54 : Math.max(54, Math.ceil(railFont.height) + 20)
    readonly property real badgeLength: iconOnly ? 44 : Math.max(76,
        Math.max.apply(null, sections.map(function(section) {
            return railFont.advanceWidth(section.title)
        })) + (titleOnly ? 20 : 44))
    readonly property alias panel: sidebarPanel
    readonly property alias sectionPopup: sectionPopup
    property string activeSection: "places"
    property string hoveredSection: ""
    property string dragSection: ""
    property string pendingSection: ""
    property bool pinnedOpen: false
    property real badgeY: 8
    signal homeRequested()
    signal locationRequested(string location)
    signal locationNewTabRequested(string location)
    signal folderIconChanged(string path)

    implicitWidth: collapsed ? railWidth : 248
    onCollapsedChanged: closeSection()
    onVisibleChanged: if (!visible) closeSection()

    FontMetrics {
        id: railFont
        font: AppTheme.sidebarTypography.fontFor(11, Font.Normal, false, false, false, false, false)
    }
    component RailBadge: GToolButton {
        id: railBadge
        required property string badgeKey
        required property string title
        required property url artwork
        property bool selected: false
        objectName: "sidebarBadge_" + badgeKey
        text: title
        Accessible.name: title
        leftPadding: 4; rightPadding: 4
        topPadding: 4; bottomPadding: 4
        background: Rectangle {
            radius: AppTheme.radiusSmall
            color: shell.iconOnly ? "transparent"
                : railBadge.hovered || railBadge.selected ? AppTheme.accentSoft : AppTheme.surfaceRaised
            border.color: railBadge.selected ? AppTheme.accentBorder : AppTheme.border
            border.width: shell.iconOnly ? 0 : 0.75
            border.pixelAligned: false
        }
        contentItem: Item {
            id: badgeContent
            readonly property real iconExtent: shell.titleOnly ? 0 : (shell.iconOnly ? 28 : 18)
            readonly property real gap: !shell.titleOnly && !shell.iconOnly ? 6 : 0
            readonly property real labelExtent: shell.iconOnly ? 0 : Math.min(
                Math.ceil(railFont.advanceWidth(railBadge.title)) + 2, Math.max(0, height - iconExtent - gap))
            readonly property real groupHeight: iconExtent + gap + labelExtent
            CrispIcon {
                objectName: "sidebarBadgeIcon_" + railBadge.badgeKey
                visible: !shell.titleOnly
                source: railBadge.artwork
                anchors.horizontalCenter: parent.horizontalCenter
                y: (parent.height - badgeContent.groupHeight) / 2
                width: shell.iconOnly ? 28 : 18
                height: width
            }
            Item {
                width: parent.width
                height: badgeContent.labelExtent
                y: (parent.height - badgeContent.groupHeight) / 2 + badgeContent.iconExtent + badgeContent.gap
                GText {
                    objectName: "sidebarBadgeTitle_" + railBadge.badgeKey
                    visible: !shell.iconOnly
                    anchors.centerIn: parent
                    width: parent.height
                    height: railFont.height
                    rotation: -90
                    typographyRole: "sidebar"
                    basePixelSize: 11
                    text: railBadge.title
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }
    function validSection(key) { return sections.some(function(section) { return section.key === key }) }
    function showSection(key, keyboard) {
        if (!collapsed || !visible || !validSection(key)) return
        if (sidebarPanel.interactionActive && key !== activeSection) return
        openDelay.stop()
        closeDelay.stop()
        pendingSection = ""
        activeSection = key
        const badge = badgeRepeater.itemAt(sections.findIndex(function(section) { return section.key === key }))
        badgeY = badge ? badge.mapToItem(shell, 0, 0).y : 8
        if (keyboard) pinnedOpen = true
        sectionPopup.focus = !!keyboard
        sectionPopup.open()
    }
    function queueSection(key) {
        if (!collapsed) return
        closeDelay.stop()
        if (sectionPopup.visible && key === activeSection) return
        pendingSection = key
        openDelay.restart()
    }
    function scheduleClose() {
        if (!pinnedOpen && !hoveredSection.length && !dragSection.length && !panelHover.hovered
                && !sidebarPanel.interactionActive) closeDelay.restart()
        else closeDelay.stop()
    }
    function closeSection() {
        openDelay.stop()
        closeDelay.stop()
        pendingSection = ""
        hoveredSection = ""
        dragSection = ""
        pinnedOpen = false
        sectionPopup.close()
    }
    Timer { id: openDelay; interval: 140; onTriggered: shell.showSection(shell.pendingSection, false) }
    Timer {
        id: closeDelay
        interval: 320
        onTriggered: {
            if (!shell.hoveredSection.length && !shell.dragSection.length && !panelHover.hovered
                    && !sidebarPanel.interactionActive && !shell.pinnedOpen) shell.closeSection()
        }
    }
    Connections {
        target: sidebarPanel
        function onInteractionActiveChanged() {
            if (!sidebarPanel.interactionActive && shell.hoveredSection.length
                    && shell.hoveredSection !== shell.activeSection) shell.queueSection(shell.hoveredSection)
            else shell.scheduleClose()
        }
    }
    Connections {
        target: shell.Window.window
        function onVisibleChanged() { if (!shell.Window.window.visible) shell.closeSection() }
        function onActiveChanged() {
            if (!shell.Window.window.active && !sidebarPanel.interactionActive) shell.closeSection()
        }
    }

    Rectangle {
        objectName: "sidebarRailBackground"
        anchors.fill: parent
        visible: shell.collapsed
        topLeftRadius: 0
        bottomLeftRadius: 0
        topRightRadius: AppTheme.radiusSmall
        bottomRightRadius: AppTheme.radiusSmall
        color: shell.unifiedSurface ? "transparent" : AppTheme.sidebar
    }
    Item {
        id: navigationArea
        objectName: "sidebarRailNavigationArea"
        visible: shell.collapsed
        x: 8; y: 8
        width: Math.max(0, shell.width - 16)
        height: Math.max(0, railControls.y - y - 8)
        readonly property real dockGap: 6
        readonly property real dockItemHeight: Math.max(0, Math.min(44,
            (height - shell.sections.length * dockGap) / (shell.sections.length + 1)))
        readonly property real dockHeight: dockItemHeight * (shell.sections.length + 1)
            + dockGap * shell.sections.length

    RailBadge {
        id: discoverButton
        visible: shell.collapsed
        badgeKey: "dashboard"
        title: shell.lang.t("dashboard")
        artwork: sidebarPanel.sidebarItemIcon(sidebarPanel.systemItems[0])
        selected: shell.currentLocation.length === 0
        y: shell.iconOnly ? (navigationArea.height - navigationArea.dockHeight) / 2 : 0
        width: navigationArea.width
        height: shell.iconOnly ? navigationArea.dockItemHeight : shell.badgeLength
        onClicked: { shell.closeSection(); shell.homeRequested() }
    }
    Item {
        id: badges
        objectName: "sidebarBadges"
        visible: shell.collapsed
        y: discoverButton.y + discoverButton.height + (shell.iconOnly ? navigationArea.dockGap : 8)
        width: navigationArea.width
        height: shell.iconOnly
            ? navigationArea.dockItemHeight * shell.sections.length + navigationArea.dockGap * (shell.sections.length - 1)
            : Math.max(0, navigationArea.height - y)
        readonly property real slotHeight: height / shell.sections.length
        Repeater {
            id: badgeRepeater
            model: shell.sections
            delegate: RailBadge {
                id: badge
                required property var modelData
                required property int index
                badgeKey: modelData.key
                title: modelData.title
                artwork: modelData.key === "favorites" ? sidebarPanel.favoritesSectionIcon
                    : sidebarPanel.sidebarModeIcon(modelData.icon, "")
                selected: sectionPopup.visible && shell.activeSection === modelData.key
                width: badges.width
                height: shell.iconOnly ? navigationArea.dockItemHeight
                    : Math.max(0, Math.min(shell.badgeLength, badges.slotHeight - 8))
                y: shell.iconOnly ? index * (height + navigationArea.dockGap)
                    : index * badges.slotHeight + (badges.slotHeight - height) / 2
                onHoveredChanged: {
                    if (hovered) {
                        shell.hoveredSection = modelData.key
                        shell.queueSection(modelData.key)
                    } else {
                        if (shell.hoveredSection === modelData.key) shell.hoveredSection = ""
                        if (shell.pendingSection === modelData.key && shell.dragSection !== modelData.key) {
                            openDelay.stop()
                            shell.pendingSection = ""
                        }
                        shell.scheduleClose()
                    }
                }
                onClicked: {
                    if (modelData.key === "places") {
                        shell.closeSection()
                        shell.locationRequested(sidebarPanel.homePath)
                    } else if (sectionPopup.visible && shell.activeSection === modelData.key && shell.pinnedOpen)
                        shell.closeSection()
                    else shell.showSection(modelData.key, true)
                }
                DropArea {
                    anchors.fill: parent
                    onEntered: function(drag) {
                        if (!drag.hasUrls && !(drag.source && drag.source.dragUrl)) { drag.accepted = false; return }
                        shell.dragSection = badge.modelData.key
                        shell.queueSection(badge.modelData.key)
                    }
                    onExited: {
                        if (shell.dragSection === badge.modelData.key) shell.dragSection = ""
                        if (shell.pendingSection === badge.modelData.key && shell.hoveredSection !== badge.modelData.key) {
                            openDelay.stop()
                            shell.pendingSection = ""
                        }
                        shell.scheduleClose()
                    }
                    onDropped: { shell.dragSection = ""; shell.scheduleClose() }
                }
            }
        }
    }
    }
    ColumnLayout {
        id: railControls
        objectName: "sidebarRailControls"
        visible: shell.collapsed
        anchors {
            left: parent.left; right: parent.right; bottom: parent.bottom
            leftMargin: 8; rightMargin: 8; bottomMargin: 8
        }
        spacing: 6
        GToolButton {
            id: administratorButton
            objectName: "sidebarRailAdministrator"
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            checked: sidebarPanel.locationEquals(shell.currentLocation, "admin:///")
            Accessible.name: shell.lang.language === "tr" ? "Yönetici erişimi" : "Administrator access"
            onClicked: { shell.closeSection(); shell.locationRequested("admin:///") }
            contentItem: Item {
                CrispIcon {
                    anchors.centerIn: parent
                    width: 24; height: 24
                    source: sidebarPanel.sidebarModeIcon("root.svg", sidebarPanel.savedSidebarIcon("admin"))
                }
            }
            TapHandler {
                acceptedButtons: Qt.MiddleButton
                onTapped: { shell.closeSection(); shell.locationNewTabRequested("admin:///") }
            }
            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: administratorMenu.popup()
            }
            GMenu {
                id: administratorMenu
                GMenuItem {
                    text: shell.lang.t("properties")
                    onTriggered: sidebarPanel.openSidebarFolderProperties({ key: "admin", label: shell.lang.t("root"), path: "admin:///", icon: "root.svg" })
                }
            }
            GToolTip { text: parent.Accessible.name }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: AppTheme.border }
        Item {
            id: railControlsHost
            objectName: "sidebarRailControlsHost"
            Layout.fillWidth: true
            Layout.preferredHeight: sidebarPanel.controlsHeight
        }
    }

    Popup {
        id: sectionPopup
        objectName: "sidebarSectionPopup"
        parent: shell
        popupType: Popup.Item
        x: shell.width + 4
        y: Math.max(0, Math.min(shell.height - height, shell.badgeY))
        width: Math.max(180, Math.min(260,
            shell.Window.window ? shell.Window.window.width - shell.mapToItem(null, 0, 0).x - shell.width - 16 : 260))
        height: Math.min(shell.height, Math.max(112, sidebarPanel.drawerContentHeight))
        padding: 0
        modal: false
        dim: false
        focus: false
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onClosed: {
            shell.pinnedOpen = false
            closeDelay.stop()
        }
        contentItem: Item {
            HoverHandler {
                id: panelHover
                onHoveredChanged: shell.scheduleClose()
            }
        }
        background: GPopupSurface { popup: sectionPopup; radius: 12; border.width: 0.75; border.pixelAligned: false }
        enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 90 } }
        exit: Transition { enabled: shell.collapsed; NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 80 } }
    }
    AppSidebar {
        id: sidebarPanel
        objectName: "sidebarSharedPanel"
        parent: shell.collapsed ? sectionPopup.contentItem : shell
        anchors.fill: parent
        visible: !shell.collapsed || sectionPopup.visible
        sectionFilter: shell.collapsed ? shell.activeSection : ""
        drawerPresentation: shell.collapsed
        unifiedSurface: shell.unifiedSurface && !shell.collapsed
        controlsHost: shell.collapsed ? railControlsHost : null
        lang: shell.lang
        favoritesModel: shell.favoritesModel
        storageModel: shell.storageModel
        musicPlayer: shell.musicPlayer
        currentLocation: shell.currentLocation
        onHomeRequested: { shell.closeSection(); shell.homeRequested() }
        onLocationRequested: function(location) { shell.closeSection(); shell.locationRequested(location) }
        onLocationNewTabRequested: function(location) { shell.closeSection(); shell.locationNewTabRequested(location) }
        onFolderIconChanged: function(path) { shell.folderIconChanged(path) }
    }
}
