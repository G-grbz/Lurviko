import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import GFile.App
import GFile.Backend

Rectangle {
    id: sidebar
    required property var lang
    required property var favoritesModel
    required property var storageModel
    property var musicPlayer: null

    signal homeRequested()
    signal locationRequested(string location)
    signal locationNewTabRequested(string location)
    signal folderIconChanged(string path)

    property string selectedKey: "dashboard"
    property double wheelBurstLastMs: 0
    property int wheelBurstCount: 0
    property int wheelBurstDirection: 0
    // Visual selection follows the location that is actually being shown, not the last clicked shortcut.
    property string currentLocation: ""
    property int editingFavoriteIndex: -1
    property string editingFavoriteIcon: "favorite.svg"
    property string sidebarPropertiesKey: ""
    property string sidebarPropertiesPath: ""
    property string sidebarPropertiesLabel: ""
    property string sidebarPropertiesDefaultIcon: "folder.svg"
    property bool sidebarPropertiesHasFileInfo: false
    readonly property var bundledFavoriteIcons: [
        "favorite.svg", "folder.svg", "home-folder.svg", "desktop.svg", "downloads.svg",
        "documents.svg", "image.svg", "audio.svg", "video.svg", "archive.svg",
        "drive.svg", "network.svg", "cloud.svg", "trash.svg", "trashfull.svg"
    ]
    readonly property string homePath: StandardPaths.writableLocation(StandardPaths.HomeLocation)
    readonly property string configuredDownloadsPath: StandardPaths.writableLocation(StandardPaths.DownloadLocation)
    // XDG user-dirs permits DOWNLOAD="$HOME". This machine keeps downloads
    // in the conventional ~/Downloads folder, so avoid turning the Downloads
    // shortcut into a second Home shortcut when Qt reports that fallback.
    readonly property string downloadsPath: normalizedLocation(configuredDownloadsPath) === normalizedLocation(homePath)
                                            ? homePath + "/Downloads" : configuredDownloadsPath
    readonly property string trashPath: "trash:/"
    readonly property bool trashHasItems: trashMonitor.hasItems

    function formatMediaTime(ms) {
        const total = Math.max(0, Math.floor(Number(ms || 0) / 1000))
        const minutes = Math.floor(total / 60)
        const seconds = total % 60
        return minutes + ":" + (seconds < 10 ? "0" : "") + seconds
    }

    function toggleMiniMusicPopup() {
        if (miniMusicPopup.interactionOpen) {
            miniMusicPopup.close()
            return
        }
        // Anchor to the sidebar after its layout has settled, independently
        // of the opener's position and press animation.
        miniMusicPopup.x = (sidebar.width - miniMusicPopup.width) / 2
        miniMusicPopup.y = Math.max(8, sidebar.height - miniMusicPopup.height - 48)
        miniMusicPopup.open()
    }

    function normalizedLocation(value) {
        let v = String(value || "").trim()
        if (!v.length)
            return ""
        if (v.startsWith("file://")) {
            try { v = decodeURIComponent(v.substring(7)) } catch (e) { v = v.substring(7) }
        }
        // Keep root and URL roots intact, otherwise ignore a cosmetic trailing slash.
        if (v.length > 1 && v.endsWith("/") && !/^[A-Za-z][A-Za-z0-9+.-]*:\/$/.test(v)
                && !/^[A-Za-z][A-Za-z0-9+.-]*:\/\/$/.test(v))
            v = v.substring(0, v.length - 1)
        return v
    }

    function locationEquals(a, b) {
        return normalizedLocation(a) === normalizedLocation(b)
    }

    function pathContainsLocation(base, location) {
        const b = normalizedLocation(base)
        const l = normalizedLocation(location)
        if (!b.length || !l.length)
            return false
        if (l === b)
            return true
        // Local folders and hierarchical KIO locations only match on a path
        // boundary.  /home/user/Downloads must not match /home/user/Downloads2.
        if (b === "/")
            return l.startsWith("/")
        return l.startsWith(b + "/")
    }

    function specialSystemKeyForLocation(location) {
        const current = normalizedLocation(location)
        const home = normalizedLocation(homePath)
        if (!current.length || !home.length)
            return ""

        // Prefer the real XDG user-dir path when it is configured, but also
        // recognise the common English/Turkish directory names below $HOME.
        // This keeps the sidebar correct on systems whose XDG user-dirs file
        // is missing, stale, or differs from Qt's StandardPaths result.
        const aliases = [
            { key: "desktop", path: StandardPaths.writableLocation(StandardPaths.DesktopLocation), names: ["Desktop", "Masaüstü", "Masaustu"] },
            { key: "downloads", path: downloadsPath, names: ["Downloads", "İndirilenler", "Indirilenler"] },
            { key: "documents", path: StandardPaths.writableLocation(StandardPaths.DocumentsLocation), names: ["Documents", "Belgeler"] },
            { key: "pictures", path: StandardPaths.writableLocation(StandardPaths.PicturesLocation), names: ["Pictures", "Images", "Resimler"] },
            { key: "music", path: StandardPaths.writableLocation(StandardPaths.MusicLocation), names: ["Music", "Müzik", "Muzik"] },
            { key: "videos", path: StandardPaths.writableLocation(StandardPaths.MoviesLocation), names: ["Videos", "Video", "Videolar"] }
        ]

        let bestKey = ""
        let bestLength = -1
        for (let i = 0; i < aliases.length; ++i) {
            const entry = aliases[i]
            const candidates = []
            const xdgPath = normalizedLocation(entry.path)
            if (xdgPath.length && xdgPath !== home)
                candidates.push(xdgPath)
            for (let n = 0; n < entry.names.length; ++n)
                candidates.push(home + "/" + entry.names[n])

            for (let c = 0; c < candidates.length; ++c) {
                const candidate = normalizedLocation(candidates[c])
                if (candidate.length > bestLength && locationEquals(candidate, current)) {
                    bestKey = entry.key
                    bestLength = candidate.length
                }
            }
        }
        return bestKey
    }

    function bestSystemKeyForLocation(location) {
        const current = normalizedLocation(location)
        if (!current.length)
            return "dashboard"

        if (current.startsWith("trash:"))
            return "trash"

        // Sidebar shortcuts describe concrete locations. A child folder that
        // has no own shortcut must leave the sidebar without a selection.
        const specialKey = specialSystemKeyForLocation(current)
        if (specialKey.length)
            return specialKey

        if (locationEquals(homePath, current))
            return "homefolder"

        return ""
    }

    function systemItemActive(item) {
        return bestSystemKeyForLocation(currentLocation) === item.key
    }
    property var systemItems: [
        { key: "dashboard", label: lang.t("dashboard"), path: "", icon: "dashboard.svg", dashboard: true },
        { key: "homefolder", label: lang.t("home_folder"), path: homePath, icon: "home-folder.svg", dashboard: false },
        { key: "desktop", label: lang.t("desktop"), path: StandardPaths.writableLocation(StandardPaths.DesktopLocation), icon: "desktop.svg", dashboard: false },
        { key: "downloads", label: lang.t("downloads"), path: downloadsPath, icon: "downloads.svg", dashboard: false },
        { key: "documents", label: lang.t("documents"), path: StandardPaths.writableLocation(StandardPaths.DocumentsLocation), icon: "documents.svg", dashboard: false },
        { key: "pictures", label: lang.t("pictures"), path: StandardPaths.writableLocation(StandardPaths.PicturesLocation), icon: "image.svg", dashboard: false },
        { key: "music", label: lang.t("music"), path: StandardPaths.writableLocation(StandardPaths.MusicLocation), icon: "audio.svg", dashboard: false },
        { key: "videos", label: lang.t("videos"), path: StandardPaths.writableLocation(StandardPaths.MoviesLocation), icon: "video.svg", dashboard: false },
        { key: "trash", label: lang.t("trash"), path: trashPath, icon: "trash.svg", dashboard: false }
    ]

    Settings {
        id: sidebarFolderSettings
        category: "SidebarFolderIcons"
        property string dashboardIcon: ""
        property string homefolderIcon: ""
        property string desktopIcon: ""
        property string downloadsIcon: ""
        property string documentsIcon: ""
        property string picturesIcon: ""
        property string musicIcon: ""
        property string videosIcon: ""
        property string trashIcon: ""
        property string smbIcon: ""
        property string sftpIcon: ""
        property string adminIcon: ""
    }

    function savedSidebarIcon(key) {
        switch (key) {
        case "dashboard": return sidebarFolderSettings.dashboardIcon
        case "homefolder": return sidebarFolderSettings.homefolderIcon
        case "desktop": return sidebarFolderSettings.desktopIcon
        case "downloads": return sidebarFolderSettings.downloadsIcon
        case "documents": return sidebarFolderSettings.documentsIcon
        case "pictures": return sidebarFolderSettings.picturesIcon
        case "music": return sidebarFolderSettings.musicIcon
        case "videos": return sidebarFolderSettings.videosIcon
        case "trash": return sidebarFolderSettings.trashIcon
        case "smb": return sidebarFolderSettings.smbIcon
        case "sftp": return sidebarFolderSettings.sftpIcon
        case "admin": return sidebarFolderSettings.adminIcon
        default: return ""
        }
    }

    function setSavedSidebarIcon(key, iconName) {
        switch (key) {
        case "dashboard": sidebarFolderSettings.dashboardIcon = iconName; break
        case "homefolder": sidebarFolderSettings.homefolderIcon = iconName; break
        case "desktop": sidebarFolderSettings.desktopIcon = iconName; break
        case "downloads": sidebarFolderSettings.downloadsIcon = iconName; break
        case "documents": sidebarFolderSettings.documentsIcon = iconName; break
        case "pictures": sidebarFolderSettings.picturesIcon = iconName; break
        case "music": sidebarFolderSettings.musicIcon = iconName; break
        case "videos": sidebarFolderSettings.videosIcon = iconName; break
        case "trash": sidebarFolderSettings.trashIcon = iconName; break
        case "smb": sidebarFolderSettings.smbIcon = iconName; break
        case "sftp": sidebarFolderSettings.sftpIcon = iconName; break
        case "admin": sidebarFolderSettings.adminIcon = iconName; break
        }
    }

    function sidebarModeIcon(iconName, customSystemName) {
        if (!AppTheme.useSystemIcons)
            return AppTheme.icon(iconName)
        const custom = String(customSystemName || "").trim()
        if (custom.length > 0)
            return AppTheme.systemIcon(custom)
        return AppTheme.systemIcon(AppTheme.systemIconName(iconName))
    }

    function sidebarFavoriteIcon(iconName, fallbackName) {
        const raw = String(iconName || "").trim()
        const fallback = String(fallbackName || "folder.svg").trim() || "folder.svg"
        if (!AppTheme.useSystemIcons)
            return AppTheme.discoveryIcon(raw, fallback)
        if (raw.indexOf("system:") === 0)
            return AppTheme.systemIcon(raw.substring(7))
        return AppTheme.systemIcon(AppTheme.systemIconName(raw.length > 0 ? raw : fallback))
    }

    function sidebarItemIcon(item) {
        if (item.key === "trash") {
            if (!AppTheme.useSystemIcons)
                return AppTheme.icon(sidebar.trashHasItems ? "trashfull.svg" : "trash.svg")
            const trashCustom = savedSidebarIcon(item.key)
            return trashCustom && trashCustom.length
                    ? AppTheme.systemIcon(trashCustom)
                    : AppTheme.systemIcon(sidebar.trashHasItems ? "user-trash-full" : "user-trash")
        }
        const custom = savedSidebarIcon(item.key)
        return sidebarModeIcon(item.icon, custom)
    }

    function openSidebarFolderProperties(item) {
        sidebarPropertiesKey = item.key || ""
        sidebarPropertiesPath = item.path || ""
        sidebarPropertiesLabel = item.label || ""
        sidebarPropertiesDefaultIcon = item.icon || "folder.svg"
        const path = sidebarPropertiesPath
        sidebarPropertiesHasFileInfo = path.length > 0
                && path.indexOf("://") < 0
                && path.indexOf("trash:") !== 0
                && path.indexOf("admin:") !== 0
        if (sidebarPropertiesHasFileInfo)
            sidebarFileProperties.inspect(path)
        sidebarFolderPropertiesPopup.open()
    }

    radius: AppTheme.panelRadius
    color: AppTheme.sidebar
    // The sidebar is a floating surface now; its own fill/radius already
    // separates it from the workspace.  Do not draw an outer stroke here:
    // a Rectangle border is all-or-nothing and masking only the right edge
    // leaves tiny rounded-corner "antennae".
    border.width: 0

    IconPickerManager { id: favoriteIconPicker }
    FilePropertiesManager { id: sidebarFileProperties }

    // Lightweight filesystem-backed trash state. Unlike the old hidden
    // DirectoryModel this never lists trash:/ through KIO, so opening the Trash
    // page has no competing background listing or 2.5 s refresh loop.
    TrashMonitor {
        id: trashMonitor
    }

    NumberAnimation {
        id: sidebarWheelScrollAnimation
        property: "contentY"
        easing.type: Easing.OutCubic
    }

    function normalizedSidebarWheelDelta(event) {
        const angleY = event.angleDelta.y
        const pixelY = event.pixelDelta.y
        const mousePixelOnly = angleY === 0 && pixelY !== 0
                               && (event.phase === Qt.NoScrollPhase
                                   || (event.device && event.device.type === PointerDevice.Mouse))

        if (angleY !== 0 || mousePixelOnly) {
            const now = Date.now()
            const direction = (angleY !== 0 ? angleY : pixelY) > 0 ? 1 : -1
            const gap = wheelBurstLastMs > 0 ? now - wheelBurstLastMs : 9999
            if (direction !== wheelBurstDirection || gap > 300)
                wheelBurstCount = 0
            else
                wheelBurstCount = Math.min(5, wheelBurstCount + 1)
            wheelBurstLastMs = now
            wheelBurstDirection = direction

            const rawFraction = angleY !== 0 ? Math.abs(angleY) / 120.0
                                             : Math.abs(pixelY) / 30.0
            const stepFraction = Math.max(0.125, Math.min(2.0, rawFraction))
            const acceleration = 1.0 + wheelBurstCount * 0.08
            return direction * AppTheme.wheelScrollStep * stepFraction * acceleration
        }

        if (pixelY !== 0) {
            wheelBurstCount = 0
            wheelBurstDirection = 0
            wheelBurstLastMs = 0
            return pixelY * 2.0
        }
        return 0
    }

    function scrollSidebarFromWheel(event) {
        const view = navScroll.contentItem
        if (!view)
            return
        const delta = normalizedSidebarWheelDelta(event)
        if (delta === 0)
            return
        const top = view.originY
        const bottom = top + Math.max(0, view.contentHeight - view.height)
        const continuing = sidebarWheelScrollAnimation.running && sidebarWheelScrollAnimation.target === view
        const previousTarget = continuing ? sidebarWheelScrollAnimation.to : view.contentY
        const sameDirection = !continuing || Math.sign(previousTarget - view.contentY) === -Math.sign(delta)
        const base = sameDirection ? previousTarget : view.contentY
        const limit = Math.max(AppTheme.wheelScrollStep, view.height * 1.8, 288)
        const destination = Math.max(top, Math.min(bottom,
                                Math.max(view.contentY - limit,
                                         Math.min(view.contentY + limit, base - delta))))
        sidebarWheelScrollAnimation.stop()
        if (Math.abs(destination - view.contentY) < 0.5)
            return
        sidebarWheelScrollAnimation.target = view
        sidebarWheelScrollAnimation.from = view.contentY
        sidebarWheelScrollAnimation.to = destination
        sidebarWheelScrollAnimation.duration = Math.min(400, Math.max(220,
                                            160 + Math.abs(destination - view.contentY) / 4))
        sidebarWheelScrollAnimation.start()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            spacing: 11

            Rectangle {
                Layout.preferredWidth: 48
                Layout.preferredHeight: 48
                radius: 16
                color: AppTheme.accentSoft
                border.width: 1
                border.color: AppTheme.accentBorder
                CrispIcon {
                    anchors.centerIn: parent
                    source: AppTheme.icon("logo.png")
                    width: 34
                    height: 34
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                Text {
                    text: "g-File"
                    color: AppTheme.text
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                }
                Text {
                    text: lang.language === "tr" ? "Dosyaların, tek yerde" : "Your files, one place"
                    color: AppTheme.textMuted
                    font.pixelSize: 9
                }
            }


        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: AppTheme.border; opacity: 0.58 }

        ScrollView {
            id: navScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            // Keep the sidebar wheel/trackpad-scrollable, but never render a
            // scrollbar. The styled Qt ScrollBar could briefly leave a tiny
            // handle/dot at the edge while scrolling on some themes.
            rightPadding: 0

            WheelHandler {
                target: null
                blocking: true
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                acceptedModifiers: Qt.NoModifier
                onWheel: function(event) {
                    sidebar.scrollSidebarFromWheel(event)
                    event.accepted = true
                }
            }

            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical: ScrollBar {
                id: navVerticalBar
                policy: ScrollBar.AlwaysOff
                visible: false
                interactive: false
                width: 0
            }

            ColumnLayout {
                width: Math.max(0, navScroll.availableWidth - 4)
                spacing: 4

                Repeater {
                    model: sidebar.systemItems
                    delegate: SidebarItem {
                        Layout.fillWidth: true
                        title: modelData.label
                        iconSource: sidebar.sidebarItemIcon(modelData)
                        active: sidebar.systemItemActive(modelData)
                        propertiesEnabled: true
                        propertiesText: lang.t("properties")
                        dragUrl: modelData.dashboard ? "" : modelData.path
                        dragIcon: modelData.icon
                        onClicked: {
                            sidebar.selectedKey = modelData.key
                            if (modelData.dashboard)
                                sidebar.homeRequested()
                            else
                                sidebar.locationRequested(modelData.path)
                        }
                        onMiddleClicked: {
                            if (!modelData.dashboard)
                                sidebar.locationNewTabRequested(modelData.path)
                        }
                        onPropertiesRequested: sidebar.openSidebarFolderProperties(modelData)
                    }
                }

                Item { Layout.preferredHeight: 7 }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Item {
                        Layout.preferredWidth: 16
                        Layout.preferredHeight: 16
                        CrispIcon {
                            anchors.centerIn: parent
                            source: sidebar.sidebarModeIcon("favorite.svg", "")
                            width: 13
                            height: 13
                        }
                    }
                    Text {
                        text: lang.t("favorites").toUpperCase()
                        color: AppTheme.textFaint
                        font.pixelSize: 10
                        font.bold: true
                        font.letterSpacing: 0.8
                    }
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: AppTheme.border; opacity: 0.58 }
                }

                Rectangle {
                    id: favoriteDropPanel
                    Layout.fillWidth: true
                    Layout.preferredHeight: favoriteColumn.implicitHeight + 8
                    radius: 12
                    color: favoriteDrop.containsDrag ? AppTheme.accentSoft
                                                     : (favoritesModel.count === 0 ? AppTheme.surfaceRaised : "transparent")
                    border.color: favoriteDrop.containsDrag ? AppTheme.accent
                                                            : (favoritesModel.count === 0 ? AppTheme.border : "transparent")
                    border.width: favoriteDrop.containsDrag ? 2 : 1

                    DropArea {
                        id: favoriteDrop
                        anchors.fill: parent
                        onDropped: function(drop) {
                            if (drop.source && drop.source.dragUrl
                                    && (typeof drop.source.isDir === "undefined" || drop.source.isDir)) {
                                favoritesModel.addFavorite(drop.source.dragUrl)
                                drop.acceptProposedAction()
                            }
                        }
                    }

                    ColumnLayout {
                        id: favoriteColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 4
                        spacing: 3

                        Text {
                            visible: favoritesModel.count === 0
                            Layout.fillWidth: true
                            Layout.leftMargin: 6
                            Layout.rightMargin: 6
                            Layout.preferredHeight: visible ? 46 : 0
                            text: lang.language === "tr"
                                  ? "Klasörleri sağ tıkla veya buraya sürükle."
                                  : "Right-click folders or drag them here."
                            color: AppTheme.textMuted
                            font.pixelSize: 10
                            wrapMode: Text.WordWrap
                            verticalAlignment: Text.AlignVCenter
                        }

                        Repeater {
                            model: favoritesModel
                            delegate: SidebarItem {
                                required property int index
                                required property string name
                                required property string location
                                required property string favoriteIcon
                                Layout.fillWidth: true
                                title: name
                                subtitle: location
                                iconSource: sidebar.sidebarFavoriteIcon(favoriteIcon, "folder.svg")
                                iconScale: 0.86
                                removable: true
                                editable: true
                                editText: lang.language === "tr" ? "Düzenle" : "Edit"
                                removeText: lang.language === "tr" ? "Kaldır" : "Remove"
                                dragUrl: location
                                dragIcon: favoriteIcon
                                active: sidebar.locationEquals(sidebar.currentLocation, location)
                                onClicked: {
                                    sidebar.selectedKey = "favorite:" + location
                                    sidebar.locationRequested(location)
                                }
                                onMiddleClicked: sidebar.locationNewTabRequested(location)
                                onRemoveRequested: favoritesModel.removeFavorite(location)
                                onEditRequested: {
                                    sidebar.editingFavoriteIndex = index
                                    favoriteNameInput.text = name
                                    sidebar.editingFavoriteIcon = favoriteIcon
                                    favoriteEditPopup.open()
                                }
                            }
                        }
                    }
                }

                Item { Layout.preferredHeight: 7 }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text {
                        text: lang.t("network").toUpperCase()
                        color: AppTheme.textFaint
                        font.pixelSize: 10
                        font.bold: true
                        font.letterSpacing: 0.8
                    }
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: AppTheme.border; opacity: 0.58 }
                }

                SidebarItem {
                    Layout.fillWidth: true
                    title: "SMB"
                    subtitle: "smb://"
                    iconSource: sidebar.sidebarModeIcon("network.svg", sidebar.savedSidebarIcon("smb"))
                    active: sidebar.locationEquals(sidebar.currentLocation, "smb://")
                    dragUrl: "smb://"
                    dragIcon: "network.svg"
                    propertiesEnabled: true
                    propertiesText: lang.t("properties")
                    onPropertiesRequested: sidebar.openSidebarFolderProperties({ key: "smb", label: "SMB", path: "smb://", icon: "network.svg" })
                    onClicked: { sidebar.selectedKey = "smb"; sidebar.locationRequested("smb://") }
                    onMiddleClicked: sidebar.locationNewTabRequested("smb://")
                }

                SidebarItem {
                    Layout.fillWidth: true
                    title: "SFTP"
                    subtitle: "sftp://"
                    iconSource: sidebar.sidebarModeIcon("network.svg", sidebar.savedSidebarIcon("sftp"))
                    active: sidebar.locationEquals(sidebar.currentLocation, "sftp://")
                    dragUrl: "sftp://"
                    dragIcon: "network.svg"
                    propertiesEnabled: true
                    propertiesText: lang.t("properties")
                    onPropertiesRequested: sidebar.openSidebarFolderProperties({ key: "sftp", label: "SFTP", path: "sftp://", icon: "network.svg" })
                    onClicked: { sidebar.selectedKey = "sftp"; sidebar.locationRequested("sftp://") }
                    onMiddleClicked: sidebar.locationNewTabRequested("sftp://")
                }

                Item { Layout.preferredHeight: 7 }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    CrispIcon {
                        source: sidebar.sidebarModeIcon("drive.svg", "")
                        Layout.preferredWidth: 13
                        Layout.preferredHeight: 13
                    }
                    Text {
                        text: lang.t("disks").toUpperCase()
                        color: AppTheme.textFaint
                        font.pixelSize: 10
                        font.bold: true
                        font.letterSpacing: 0.8
                    }
                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: AppTheme.border; opacity: 0.58 }
                }

                Repeater {
                    model: sidebar.storageModel.favoriteItems
                    delegate: SidebarItem {
                        required property var modelData
                        Layout.fillWidth: true
                        title: modelData.name
                        subtitle: modelData.rootPath
                        capacityText: !modelData.mounted
                                      ? (lang.language === "tr" ? "Bağlı değil" : "Not mounted")
                                      : modelData.totalBytes > 0
                                        ? (lang.language === "tr" ? "Boş " : "Free ")
                                          + sidebar.storageModel.formatBytes(modelData.freeBytes)
                                          + " / " + sidebar.storageModel.formatBytes(modelData.totalBytes)
                                        : (lang.language === "tr" ? "Kapasite bilinmiyor" : "Capacity unavailable")
                        showUsageBar: modelData.mounted && modelData.totalBytes > 0
                        usageRatio: modelData.usedRatio
                        iconSource: sidebar.sidebarModeIcon("drive.svg", "")
                        dragUrl: modelData.rootPath
                        dragIcon: "drive.svg"
                        active: sidebar.locationEquals(sidebar.currentLocation, modelData.rootPath)
                        removable: true
                        removeText: lang.language === "tr" ? "Disklerden kaldır" : "Remove from Disks"
                        onClicked: {
                            sidebar.selectedKey = "disk:" + modelData.rootPath
                            sidebar.locationRequested(modelData.rootPath)
                        }
                        onMiddleClicked: sidebar.locationNewTabRequested(modelData.rootPath)
                        onRemoveRequested: sidebar.storageModel.setFavorite(modelData.rootPath, false)
                    }
                }

                SidebarItem {
                    Layout.fillWidth: true
                    title: lang.t("root")
                    subtitle: lang.language === "tr" ? "Yönetici erişimi" : "Administrator access"
                    iconSource: sidebar.sidebarModeIcon("root.svg", sidebar.savedSidebarIcon("admin"))
                    danger: true
                    active: sidebar.locationEquals(sidebar.currentLocation, "admin:///")
                    dragUrl: "admin:///"
                    dragIcon: "root.svg"
                    propertiesEnabled: true
                    propertiesText: lang.t("properties")
                    onPropertiesRequested: sidebar.openSidebarFolderProperties({ key: "admin", label: lang.t("root"), path: "admin:///", icon: "root.svg" })
                    onClicked: { sidebar.selectedKey = "admin"; sidebar.locationRequested("admin:///") }
                    onMiddleClicked: sidebar.locationNewTabRequested("admin:///")
                }
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: AppTheme.border; opacity: 0.58 }

        RowLayout {
            Layout.fillWidth: true
            spacing: 7
            GButton {
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                id: trButton
                text: "TR"
                implicitWidth: 40
                implicitHeight: 32
                onClicked: lang.language = "tr"
                background: Rectangle {
                    radius: 9
                    color: lang.language === "tr" ? AppTheme.accentSoft : "transparent"
                    border.color: lang.language === "tr" ? AppTheme.accentBorder : AppTheme.border
                }
                contentItem: Text { text: trButton.text; color: AppTheme.text; font.pixelSize: 10; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            }
            GButton {
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                id: enButton
                text: "EN"
                implicitWidth: 40
                implicitHeight: 32
                onClicked: lang.language = "en"
                background: Rectangle {
                    radius: 9
                    color: lang.language === "en" ? AppTheme.accentSoft : "transparent"
                    border.color: lang.language === "en" ? AppTheme.accentBorder : AppTheme.border
                }
                contentItem: Text { text: enButton.text; color: AppTheme.text; font.pixelSize: 10; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
            }
            GButton {
                id: miniMusicButton
                visible: sidebar.musicPlayer && sidebar.musicPlayer.opened
                implicitWidth: visible ? 32 : 0
                implicitHeight: 32
                onClicked: sidebar.toggleMiniMusicPopup()
                GToolTip { text: lang.language === "tr" ? "Müzik oynatıcı" : "Music player" }
                background: Rectangle {
                    radius: 9
                    color: sidebar.musicPlayer && sidebar.musicPlayer.playing ? AppTheme.accentSoft : (miniMusicButton.hovered ? AppTheme.surfaceHover : "transparent")
                    border.color: sidebar.musicPlayer && sidebar.musicPlayer.opened ? AppTheme.accentBorder : AppTheme.border
                }
                contentItem: Item {
                    CrispIcon {
                        anchors.centerIn: parent
                        width: 16
                        height: 16
                        source: AppTheme.icon("music-note.svg")
                    }
                    Rectangle {
                        visible: sidebar.musicPlayer && sidebar.musicPlayer.playing
                        width: 6; height: 6; radius: 3
                        anchors.right: parent.right
                        anchors.top: parent.top
                        color: AppTheme.accent
                    }
                }
            }
            Item { Layout.fillWidth: true }
            GButton {
                id: themeSwitch
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                Layout.preferredWidth: 68
                Layout.preferredHeight: 32
                onClicked: AppTheme.toggle()
                GToolTip {
                    text: AppTheme.dark
                          ? (lang.language === "tr" ? "Aydınlık görünüm" : "Light appearance")
                          : (lang.language === "tr" ? "Karanlık görünüm" : "Dark appearance")
                }
                background: Rectangle {
                    radius: height / 2
                    color: AppTheme.dark ? AppTheme.accentSoft : AppTheme.surfaceRaised
                    border.color: AppTheme.dark ? AppTheme.accentBorder : AppTheme.borderStrong
                    Behavior on color { ColorAnimation { duration: 140 } }

                    CrispIcon {
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        width: 14
                        height: 14
                        source: AppTheme.icon("sun.svg")
                        opacity: AppTheme.dark ? 0.38 : 0.92
                    }

                    CrispIcon {
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        width: 14
                        height: 14
                        source: AppTheme.icon("moon.svg")
                        opacity: AppTheme.dark ? 0.92 : 0.38
                    }

                    Rectangle {
                        width: 26; height: 26; radius: 13
                        y: 3
                        x: AppTheme.dark ? parent.width - width - 3 : 3
                        color: AppTheme.dark ? AppTheme.accent : AppTheme.surface
                        border.color: AppTheme.dark ? AppTheme.accent : AppTheme.borderStrong
                        Behavior on x { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
                        CrispIcon {
                            anchors.centerIn: parent
                            width: 15; height: 15
                            source: AppTheme.icon(AppTheme.dark ? "moon.svg" : "sun.svg")
                        }
                    }
                }
                contentItem: Item {}
            }
        }
    }

    GPopupDismissHandler { popup: miniMusicPopup; opener: miniMusicButton }

    Popup {
        id: miniMusicPopup
        property bool interactionOpen: false
        onAboutToShow: interactionOpen = true
        onAboutToHide: interactionOpen = false
        parent: sidebar
        popupType: Popup.Item
        modal: false
        dim: false
        focus: true
        closePolicy: Popup.CloseOnEscape
        width: Math.max(218, sidebar.width - 20)
        height: 246
        padding: 10
        background: Rectangle {
            radius: 14
            color: AppTheme.surface
            border.width: 1
            border.color: AppTheme.borderStrong
        }
        contentItem: ColumnLayout {
            spacing: 7

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Rectangle {
                    Layout.preferredWidth: 48
                    Layout.preferredHeight: 48
                    radius: 8
                    color: AppTheme.surfaceRaised
                    border.color: AppTheme.border
                    clip: true
                    Image {
                        id: miniArt
                        anchors.fill: parent
                        source: sidebar.musicPlayer ? sidebar.musicPlayer.currentArtwork : ""
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        cache: true
                        visible: status === Image.Ready
                    }
                    CrispIcon {
                        anchors.centerIn: parent
                        width: 24; height: 24
                        source: AppTheme.icon("music-note.svg")
                        visible: miniArt.status !== Image.Ready
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        Layout.fillWidth: true
                        text: sidebar.musicPlayer ? sidebar.musicPlayer.currentTitle : ""
                        color: AppTheme.text
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                        elide: Text.ElideMiddle
                    }
                    Text {
                        Layout.fillWidth: true
                        text: sidebar.musicPlayer && sidebar.musicPlayer.queue.length
                              ? (sidebar.musicPlayer.currentIndex + 1) + " / " + sidebar.musicPlayer.queue.length : ""
                        color: AppTheme.textMuted
                        font.pixelSize: 8
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                Text {
                    text: sidebar.formatMediaTime(sidebar.musicPlayer ? sidebar.musicPlayer.position : 0)
                    color: AppTheme.textMuted
                    font.pixelSize: 8
                }
                GSlider {
                    id: miniSeek
                    Layout.fillWidth: true
                    Layout.preferredHeight: 18
                    from: 0
                    to: Math.max(1, sidebar.musicPlayer ? sidebar.musicPlayer.duration : 1)
                    value: sidebar.musicPlayer ? sidebar.musicPlayer.position : 0
                    onMoved: if (sidebar.musicPlayer) sidebar.musicPlayer.seekTo(value)
                    background: Rectangle {
                        x: miniSeek.leftPadding
                        y: miniSeek.topPadding + miniSeek.availableHeight / 2 - height / 2
                        width: miniSeek.availableWidth
                        height: 3
                        radius: 2
                        color: AppTheme.surfaceHover
                        Rectangle {
                            width: miniSeek.visualPosition * parent.width
                            height: parent.height
                            radius: parent.radius
                            color: AppTheme.accent
                        }
                    }
                    handle: Rectangle {
                        x: miniSeek.leftPadding + miniSeek.visualPosition * (miniSeek.availableWidth - width)
                        y: miniSeek.topPadding + miniSeek.availableHeight / 2 - height / 2
                        implicitWidth: 10
                        implicitHeight: 10
                        radius: 5
                        color: miniSeek.pressed ? AppTheme.accent : AppTheme.surface
                        border.width: 1
                        border.color: AppTheme.accentBorder
                    }
                }
                Text {
                    text: sidebar.formatMediaTime(sidebar.musicPlayer ? sidebar.musicPlayer.duration : 0)
                    color: AppTheme.textMuted
                    font.pixelSize: 8
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 3
                GToolButton {
                    background: Rectangle {
                        radius: 8
                        color: parent.checked ? AppTheme.accentSoft : (parent.hovered ? AppTheme.surfaceHover : "transparent")
                        border.width: parent.checked ? 1 : 0
                        border.color: parent.checked ? AppTheme.accentBorder : "transparent"
                        Behavior on color { ColorAnimation { duration: 110 } }
                    }
                    implicitWidth: 30; implicitHeight: 28
                    icon.source: AppTheme.icon("viewer-prev.svg")
                    onClicked: if (sidebar.musicPlayer) sidebar.musicPlayer.previous()
                }
                GToolButton {
                    background: Rectangle {
                        radius: 8
                        color: parent.checked ? AppTheme.accentSoft : (parent.hovered ? AppTheme.surfaceHover : "transparent")
                        border.width: parent.checked ? 1 : 0
                        border.color: parent.checked ? AppTheme.accentBorder : "transparent"
                        Behavior on color { ColorAnimation { duration: 110 } }
                    }
                    implicitWidth: 34; implicitHeight: 30
                    icon.source: AppTheme.icon(sidebar.musicPlayer && sidebar.musicPlayer.playing ? "viewer-pause.svg" : "viewer-play.svg")
                    onClicked: if (sidebar.musicPlayer) sidebar.musicPlayer.toggle()
                }
                GToolButton {
                    background: Rectangle {
                        radius: 8
                        color: parent.checked ? AppTheme.accentSoft : (parent.hovered ? AppTheme.surfaceHover : "transparent")
                        border.width: parent.checked ? 1 : 0
                        border.color: parent.checked ? AppTheme.accentBorder : "transparent"
                        Behavior on color { ColorAnimation { duration: 110 } }
                    }
                    implicitWidth: 30; implicitHeight: 28
                    icon.source: AppTheme.icon("viewer-next.svg")
                    onClicked: if (sidebar.musicPlayer) sidebar.musicPlayer.next()
                }
                GToolButton {
                    implicitWidth: 30; implicitHeight: 28
                    icon.source: AppTheme.icon("music-shuffle.svg")
                    checked: sidebar.musicPlayer && sidebar.musicPlayer.shuffleMode
                    onClicked: if (sidebar.musicPlayer) sidebar.musicPlayer.toggleShuffle()
                    background: Rectangle {
                        radius: 7
                        color: parent.checked ? AppTheme.accentSoft : (parent.hovered ? AppTheme.surfaceHover : "transparent")
                        border.color: parent.checked ? AppTheme.accentBorder : "transparent"
                    }
                }
                GToolButton {
                    implicitWidth: 30; implicitHeight: 28
                    icon.source: AppTheme.icon(sidebar.musicPlayer && sidebar.musicPlayer.repeatMode === 2
                                               ? "music-repeat-one.svg" : "music-repeat.svg")
                    checked: sidebar.musicPlayer && sidebar.musicPlayer.repeatMode !== 0
                    onClicked: if (sidebar.musicPlayer) sidebar.musicPlayer.cycleRepeatMode()
                    background: Rectangle {
                        radius: 7
                        color: parent.checked ? AppTheme.accentSoft : (parent.hovered ? AppTheme.surfaceHover : "transparent")
                        border.color: parent.checked ? AppTheme.accentBorder : "transparent"
                    }
                }
                Item { Layout.fillWidth: true }
                GToolButton {
                    background: Rectangle {
                        radius: 8
                        color: parent.checked ? AppTheme.accentSoft : (parent.hovered ? AppTheme.surfaceHover : "transparent")
                        border.width: parent.checked ? 1 : 0
                        border.color: parent.checked ? AppTheme.accentBorder : "transparent"
                        Behavior on color { ColorAnimation { duration: 110 } }
                    }
                    implicitWidth: 30; implicitHeight: 28
                    icon.source: AppTheme.icon("music-expand.svg")
                    onClicked: {
                        if (sidebar.musicPlayer) sidebar.musicPlayer.restorePanel()
                        miniMusicPopup.close()
                    }
                    ToolTip.visible: hovered
                    ToolTip.text: lang.language === "tr" ? "Tam oynatıcı" : "Full player"
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                GToolButton {
                    background: Rectangle {
                        radius: 8
                        color: parent.checked ? AppTheme.accentSoft : (parent.hovered ? AppTheme.surfaceHover : "transparent")
                        border.width: parent.checked ? 1 : 0
                        border.color: parent.checked ? AppTheme.accentBorder : "transparent"
                        Behavior on color { ColorAnimation { duration: 110 } }
                    }
                    implicitWidth: 28; implicitHeight: 26
                    icon.source: AppTheme.icon(sidebar.musicPlayer && (sidebar.musicPlayer.muted || sidebar.musicPlayer.volume <= 0.001)
                                               ? "viewer-volume-muted.svg" : "viewer-volume.svg")
                    onClicked: if (sidebar.musicPlayer) sidebar.musicPlayer.toggleMute()
                }
                GSlider {
                    id: miniVolume
                    Layout.fillWidth: true
                    Layout.preferredHeight: 18
                    from: 0; to: 1
                    value: sidebar.musicPlayer ? sidebar.musicPlayer.volume : 0
                    onMoved: if (sidebar.musicPlayer) sidebar.musicPlayer.setVolume(value)
                    background: Rectangle {
                        x: miniVolume.leftPadding
                        y: miniVolume.topPadding + miniVolume.availableHeight / 2 - height / 2
                        width: miniVolume.availableWidth
                        height: 3
                        radius: 2
                        color: AppTheme.surfaceHover
                        Rectangle {
                            width: miniVolume.visualPosition * parent.width
                            height: parent.height
                            radius: parent.radius
                            color: AppTheme.accent
                        }
                    }
                    handle: Rectangle {
                        x: miniVolume.leftPadding + miniVolume.visualPosition * (miniVolume.availableWidth - width)
                        y: miniVolume.topPadding + miniVolume.availableHeight / 2 - height / 2
                        implicitWidth: 9
                        implicitHeight: 9
                        radius: 5
                        color: miniVolume.pressed ? AppTheme.accent : AppTheme.surface
                        border.width: 1
                        border.color: AppTheme.accentBorder
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: AppTheme.border; opacity: 0.58 }

            RowLayout {
                Layout.fillWidth: true
                spacing: 5
                GToolButton {
                    background: Rectangle {
                        radius: 8
                        color: parent.checked ? AppTheme.accentSoft : (parent.hovered ? AppTheme.surfaceHover : "transparent")
                        border.width: parent.checked ? 1 : 0
                        border.color: parent.checked ? AppTheme.accentBorder : "transparent"
                        Behavior on color { ColorAnimation { duration: 110 } }
                    }
                    implicitWidth: 30; implicitHeight: 28
                    icon.source: AppTheme.icon("music-queue.svg")
                    onClicked: {
                        if (sidebar.musicPlayer) { sidebar.musicPlayer.restorePanel(); sidebar.musicPlayer.toggleQueue(0) }
                        miniMusicPopup.close()
                    }
                    ToolTip.visible: hovered
                    ToolTip.text: lang.language === "tr" ? "Kuyruk" : "Queue"
                }
                GToolButton {
                    background: Rectangle {
                        radius: 8
                        color: parent.checked ? AppTheme.accentSoft : (parent.hovered ? AppTheme.surfaceHover : "transparent")
                        border.width: parent.checked ? 1 : 0
                        border.color: parent.checked ? AppTheme.accentBorder : "transparent"
                        Behavior on color { ColorAnimation { duration: 110 } }
                    }
                    implicitWidth: 30; implicitHeight: 28
                    icon.source: AppTheme.icon("music-playlist.svg")
                    onClicked: {
                        if (sidebar.musicPlayer) { sidebar.musicPlayer.restorePanel(); sidebar.musicPlayer.toggleQueue(1) }
                        miniMusicPopup.close()
                    }
                    ToolTip.visible: hovered
                    ToolTip.text: lang.language === "tr" ? "Çalma listeleri" : "Playlists"
                }
                Item { Layout.fillWidth: true }
                GToolButton {
                    background: Rectangle {
                        radius: 8
                        color: parent.checked ? AppTheme.accentSoft : (parent.hovered ? AppTheme.surfaceHover : "transparent")
                        border.width: parent.checked ? 1 : 0
                        border.color: parent.checked ? AppTheme.accentBorder : "transparent"
                        Behavior on color { ColorAnimation { duration: 110 } }
                    }
                    implicitWidth: 30; implicitHeight: 28
                    icon.source: AppTheme.icon("viewer-close.svg")
                    onClicked: {
                        if (sidebar.musicPlayer) sidebar.musicPlayer.closePlayer()
                        miniMusicPopup.close()
                    }
                    ToolTip.visible: hovered
                    ToolTip.text: lang.language === "tr" ? "Oynatıcıyı kapat" : "Close player"
                }
            }
        }
    }


    GModalPopup {
        id: favoriteEditPopup
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        focus: true
        width: 480
        height: AppTheme.useSystemIcons ? 300 : 430
        padding: 20
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: GModalSurface { }

        ColumnLayout {
            anchors.fill: parent
            spacing: 12
            Text {
                text: lang.language === "tr" ? "Favoriyi düzenle" : "Edit favorite"
                color: AppTheme.text
                font.pixelSize: 17
                font.bold: true
            }
            GTextField {
                id: favoriteNameInput
                Layout.fillWidth: true
                placeholderText: lang.language === "tr" ? "Görünen ad" : "Display name"
                color: AppTheme.text
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Rectangle {
                    Layout.preferredWidth: 54
                    Layout.preferredHeight: 54
                    radius: 12
                    color: AppTheme.surfaceRaised
                    border.color: AppTheme.border
                    CrispIcon {
                        anchors.centerIn: parent
                        source: AppTheme.customIcon(sidebar.editingFavoriteIcon)
                        width: 32; height: 32
                    }
                }
                GButton {
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    visible: AppTheme.useSystemIcons
                    Layout.fillWidth: true
                    text: lang.language === "tr" ? "Plasma sistem ikonlarından seç…" : "Choose from Plasma system icons…"
                    onClicked: {
                        var initial = sidebar.editingFavoriteIcon.indexOf("system:") === 0
                                ? sidebar.editingFavoriteIcon.substring(7)
                                : AppTheme.systemIconName(sidebar.editingFavoriteIcon)
                        var chosen = favoriteIconPicker.chooseSystemIcon(initial,
                            lang.language === "tr" ? "Favori ikonu seç" : "Choose favorite icon")
                        if (chosen && chosen.length)
                            sidebar.editingFavoriteIcon = "system:" + chosen
                    }
                }
                Text {
                    visible: !AppTheme.useSystemIcons
                    Layout.fillWidth: true
                    text: lang.language === "tr" ? "g-File ikonlarından seç" : "Choose a g-File icon"
                    color: AppTheme.textMuted
                    font.pixelSize: 11
                }
            }
            GridLayout {
                visible: !AppTheme.useSystemIcons
                Layout.fillWidth: true
                columns: 7
                columnSpacing: 7
                rowSpacing: 7
                Repeater {
                    model: sidebar.bundledFavoriteIcons
                    delegate: Rectangle {
                        required property string modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: 42
                        radius: 10
                        color: sidebar.editingFavoriteIcon === modelData ? AppTheme.accentSoft : AppTheme.surfaceRaised
                        border.color: sidebar.editingFavoriteIcon === modelData ? AppTheme.accent : AppTheme.border
                        CrispIcon { anchors.centerIn: parent; source: AppTheme.icon(modelData); width: 22; height: 22 }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: sidebar.editingFavoriteIcon = modelData }
                    }
                }
            }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                GModalButton { text: lang.t("cancel"); onClicked: favoriteEditPopup.close() }
                GModalButton {
                    primary: true
                    text: lang.t("save")
                    onClicked: {
                        favoritesModel.updateFavorite(sidebar.editingFavoriteIndex,
                                                     favoriteNameInput.text,
                                                     sidebar.editingFavoriteIcon)
                        favoriteEditPopup.close()
                    }
                }
            }
        }
    }

    GModalPopup {
        id: sidebarFolderPropertiesPopup
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        focus: true
        width: 520
        height: sidebar.sidebarPropertiesHasFileInfo ? 465 : 310
        padding: 22
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: GModalSurface { }

        ColumnLayout {
            anchors.fill: parent
            spacing: 14

            RowLayout {
                Layout.fillWidth: true
                spacing: 14
                Rectangle {
                    Layout.preferredWidth: 72
                    Layout.preferredHeight: 72
                    radius: 18
                    color: AppTheme.accentSoft
                    border.color: AppTheme.accentBorder
                    CrispIcon {
                        anchors.centerIn: parent
                        width: 46
                        height: 46
                        source: {
                            if (!AppTheme.useSystemIcons)
                                return AppTheme.icon(sidebar.sidebarPropertiesDefaultIcon)
                            const custom = sidebar.savedSidebarIcon(sidebar.sidebarPropertiesKey)
                            if (custom && custom.length)
                                return AppTheme.systemIcon(custom)
                            return AppTheme.systemIcon(AppTheme.systemIconName(sidebar.sidebarPropertiesDefaultIcon))
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        enabled: AppTheme.useSystemIcons
                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: {
                            var initial = sidebar.savedSidebarIcon(sidebar.sidebarPropertiesKey)
                            if (!initial || !initial.length)
                                initial = AppTheme.systemIconName(sidebar.sidebarPropertiesDefaultIcon)
                            const chosen = favoriteIconPicker.chooseSystemIcon(initial,
                                lang.language === "tr" ? "Klasör ikonu seç" : "Choose folder icon")
                            if (chosen && chosen.length) {
                                sidebar.setSavedSidebarIcon(sidebar.sidebarPropertiesKey, chosen)
                                if (sidebar.sidebarPropertiesHasFileInfo && sidebarFileProperties.directory) {
                                    if (sidebarFileProperties.setFolderIcon(chosen))
                                        sidebar.folderIconChanged(sidebar.sidebarPropertiesPath)
                                }
                            }
                        }
                    }
                    GToolTip {
                        text: lang.language === "tr" ? "İkonu değiştirmek için tıkla" : "Click to change icon"
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 3
                    Text {
                        text: lang.language === "tr" ? "ÖZELLİKLER" : "PROPERTIES"
                        color: AppTheme.accent
                        font.pixelSize: 9
                        font.bold: true
                        font.letterSpacing: 1.1
                    }
                    Text {
                        Layout.fillWidth: true
                        text: sidebar.sidebarPropertiesLabel
                        color: AppTheme.text
                        font.pixelSize: 19
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: sidebar.sidebarPropertiesPath.length > 0
                        text: sidebar.sidebarPropertiesPath
                        color: AppTheme.textMuted
                        font.pixelSize: 10
                        elide: Text.ElideMiddle
                    }
                    Text {
                        visible: AppTheme.useSystemIcons
                        text: lang.language === "tr" ? "Simgeye tıklayarak Plasma ikonunu değiştirebilirsin" : "Click the icon to choose a Plasma icon"
                        color: AppTheme.accent
                        font.pixelSize: 10
                    }
                }
            }

            Rectangle { visible: sidebar.sidebarPropertiesHasFileInfo; Layout.fillWidth: true; Layout.preferredHeight: visible ? 1 : 0; color: AppTheme.border }

            GridLayout {
                visible: sidebar.sidebarPropertiesHasFileInfo
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 16
                rowSpacing: 11
                Text { text: lang.language === "tr" ? "Boyut" : "Size"; color: AppTheme.textMuted; font.pixelSize: 11 }
                RowLayout {
                    GBusyIndicator { visible: sidebarFileProperties.calculating; running: visible; Layout.preferredWidth: 16; Layout.preferredHeight: 16 }
                    Text {
                        text: sidebarFileProperties.calculating ? "…" : sidebarFileProperties.formatBytes(sidebarFileProperties.totalSize)
                        color: AppTheme.text; font.pixelSize: 11
                    }
                }
                Text { text: lang.language === "tr" ? "İçerik" : "Contents"; color: AppTheme.textMuted; font.pixelSize: 11 }
                Text {
                    text: sidebarFileProperties.calculating ? "…"
                          : sidebarFileProperties.fileCount + (lang.language === "tr" ? " dosya · " : " files · ")
                            + sidebarFileProperties.folderCount + (lang.language === "tr" ? " klasör" : " folders")
                    color: AppTheme.text; font.pixelSize: 11
                }
                Text { text: lang.language === "tr" ? "Oluşturma" : "Created"; color: AppTheme.textMuted; font.pixelSize: 11 }
                Text { text: sidebarFileProperties.created && !isNaN(sidebarFileProperties.created.getTime()) ? Qt.formatDateTime(sidebarFileProperties.created, "dd.MM.yyyy HH:mm") : "—"; color: AppTheme.text; font.pixelSize: 11 }
                Text { text: lang.language === "tr" ? "Değiştirme" : "Modified"; color: AppTheme.textMuted; font.pixelSize: 11 }
                Text { text: sidebarFileProperties.modified && !isNaN(sidebarFileProperties.modified.getTime()) ? Qt.formatDateTime(sidebarFileProperties.modified, "dd.MM.yyyy HH:mm") : "—"; color: AppTheme.text; font.pixelSize: 11 }
                Text { text: lang.language === "tr" ? "Son erişim" : "Last accessed"; color: AppTheme.textMuted; font.pixelSize: 11 }
                Text { text: sidebarFileProperties.accessed && !isNaN(sidebarFileProperties.accessed.getTime()) ? Qt.formatDateTime(sidebarFileProperties.accessed, "dd.MM.yyyy HH:mm") : "—"; color: AppTheme.text; font.pixelSize: 11 }
                Text { text: lang.language === "tr" ? "İzinler" : "Permissions"; color: AppTheme.textMuted; font.pixelSize: 11 }
                Text { text: sidebarFileProperties.permissionsText || "—"; color: AppTheme.text; font.pixelSize: 11 }
            }

            Item { Layout.fillHeight: true }

            RowLayout {
                Layout.fillWidth: true
                GModalButton {
                    visible: AppTheme.useSystemIcons
                    text: lang.language === "tr" ? "Otomatik ikona dön" : "Use automatic icon"
                    onClicked: {
                        sidebar.setSavedSidebarIcon(sidebar.sidebarPropertiesKey, "")
                        if (sidebar.sidebarPropertiesHasFileInfo && sidebarFileProperties.directory) {
                            if (sidebarFileProperties.setFolderIcon("folder"))
                                sidebar.folderIconChanged(sidebar.sidebarPropertiesPath)
                        }
                    }
                }
                Item { Layout.fillWidth: true }
                GModalButton {
                    primary: true
                    text: lang.language === "tr" ? "Kapat" : "Close"
                    onClicked: sidebarFolderPropertiesPopup.close()
                }
            }
        }
    }

}
