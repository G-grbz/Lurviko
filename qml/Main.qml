import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import GFile.Backend
import "components"
import "pages"

ApplicationWindow {
    id: root
    width: 1540
    height: 960
    minimumWidth: 1160
    minimumHeight: 740
    visible: true
    title: "g-File"
    color: AppTheme.background
    palette.window: AppTheme.background
    palette.windowText: AppTheme.text
    palette.base: AppTheme.surface
    palette.alternateBase: AppTheme.surfaceRaised
    palette.text: AppTheme.text
    palette.button: AppTheme.surfaceRaised
    palette.buttonText: AppTheme.text
    palette.highlight: AppTheme.accent
    palette.highlightedText: "white"
    palette.placeholderText: AppTheme.textFaint
    property string launchLocation: ""
    property bool fileManagerActivation: false
    property bool waitingForExternalLocation: fileManagerActivation && launchLocation.length === 0
    property bool windowStateReady: false
    property string lastExternalRevealKey: ""
    property double lastExternalRevealTime: 0

    Settings {
        id: windowSettings
        category: "Window"
        // -1 means a pre-v0.5.13 settings file that only had fullScreen.
        property int windowMode: -1
        property bool fullScreen: false
    }

    // Single live source of truth for cloud visibility and external targets.
    // Home/Discover and every BrowserPane receive this same object, so a toggle
    // immediately updates both the cards and the right-click Share menu.
    Settings {
        id: cloudIntegrationPreferences
        category: "cloudIntegrations"
        property bool showGoogle: true
        property bool showOneDrive: true
        property bool showDropbox: false
        property bool showNextcloud: false
        property bool showOwnCloud: false
        property bool showMega: false
        property bool showPCloud: false
        property bool showWebDav: false
        property string cloudOrder: "google,onedrive,dropbox,nextcloud,owncloud,mega,pcloud,webdav"
        property string dropboxLocation: ""
        property string nextcloudLocation: ""
        property string ownCloudLocation: ""
        property string megaLocation: ""
        property string pCloudLocation: ""
        property string webDavLocation: ""
        property string googleCustomName: ""
        property string googleCustomIcon: ""
        property string oneDriveCustomName: ""
        property string oneDriveCustomIcon: ""
        property string dropboxCustomName: ""
        property string dropboxCustomIcon: ""
        property string nextcloudCustomName: ""
        property string nextcloudCustomIcon: ""
        property string ownCloudCustomName: ""
        property string ownCloudCustomIcon: ""
        property string megaCustomName: ""
        property string megaCustomIcon: ""
        property string pCloudCustomName: ""
        property string pCloudCustomIcon: ""
        property string webDavCustomName: ""
        property string webDavCustomIcon: ""
    }

    StorageModel { id: storageModel }
    FavoritesModel { id: favoritesModel }
    QuickAccessModel { id: quickAccessModel }
    ContentIndexModel { id: contentIndexModel }
    LanguageManager { id: lang }
    CloudAuthManager { id: cloudAuth }
    AdminEditManager { id: adminEditor }
    GoogleDriveManager { id: googleDrive; accessToken: cloudAuth.googleAccessToken }
    OneDriveManager { id: oneDrive; accessToken: cloudAuth.oneDriveAccessToken }
    PrivateVaultManager { id: privateVault }

    property var appLang: lang
    property var appStorage: storageModel
    property var appFavorites: favoritesModel
    property var appQuickAccess: quickAccessModel
    property var appContentIndex: contentIndexModel
    property var appAdminEditor: adminEditor
    property var appCloudIntegrationPreferences: cloudIntegrationPreferences

    Shortcut {
        sequence: StandardKey.Quit
        context: Qt.ApplicationShortcut
        onActivated: Qt.quit()
    }
    property int modeBeforeFullScreen: Window.Windowed
    // Media viewers temporarily own F11 while open so the application-level
    // shortcut does not fight the viewer-specific fullscreen lifecycle.
    property bool mediaViewerConsumesF11: false
    // One authoritative keyboard target for video playback.  Keeping this on
    // the top-level ApplicationWindow avoids Shortcut ambiguity with the file
    // browser and also works while the dedicated fullscreen window is active.
    property var activeVideoViewer: null

    function videoPlayerAction(action) {
        const target = root.activeVideoViewer
        if (!target || !target.visible || typeof target.handleGlobalAction !== "function")
            return
        target.handleGlobalAction(action)
    }

    function toggleFullScreen() {
        if (root.visibility === Window.FullScreen) {
            root.visibility = root.modeBeforeFullScreen === Window.Maximized
                              ? Window.Maximized : Window.Windowed
        } else {
            root.modeBeforeFullScreen = root.visibility === Window.Maximized
                                        ? Window.Maximized : Window.Windowed
            root.visibility = Window.FullScreen
        }
    }

    Shortcut {
        sequence: "F11"
        context: Qt.ApplicationShortcut
        enabled: !root.mediaViewerConsumesF11 && !root.activeVideoViewer
        onActivated: root.toggleFullScreen()
    }

    Shortcut { sequence: "Escape"; context: Qt.ApplicationShortcut; enabled: root.activeVideoViewer !== null && !root.activeVideoViewer.hostFullScreen; onActivated: root.videoPlayerAction("escape") }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; enabled: root.activeVideoViewer !== null; onActivated: root.videoPlayerAction("toggle") }
    Shortcut { sequence: "Left"; context: Qt.ApplicationShortcut; enabled: root.activeVideoViewer !== null; onActivated: root.videoPlayerAction("left") }
    Shortcut { sequence: "Right"; context: Qt.ApplicationShortcut; enabled: root.activeVideoViewer !== null; onActivated: root.videoPlayerAction("right") }
    Shortcut { sequence: "Up"; context: Qt.ApplicationShortcut; enabled: root.activeVideoViewer !== null; onActivated: root.videoPlayerAction("up") }
    Shortcut { sequence: "Down"; context: Qt.ApplicationShortcut; enabled: root.activeVideoViewer !== null; onActivated: root.videoPlayerAction("down") }
    Shortcut { sequence: "M"; context: Qt.ApplicationShortcut; enabled: root.activeVideoViewer !== null; onActivated: root.videoPlayerAction("mute") }
    Shortcut { sequence: "F11"; context: Qt.ApplicationShortcut; enabled: root.activeVideoViewer !== null; onActivated: root.videoPlayerAction("fullscreen") }
    property var appCloudAuth: cloudAuth
    property var appGoogleDrive: googleDrive
    property var appOneDrive: oneDrive
    property var appPrivateVault: privateVault

    Connections {
        target: privateVault
        function onAboutToLock() {
            // The vault page may no longer be on the StackView when the user
            // locks from Discover. Stop any G-File-owned media that is still
            // reading a decrypted runtime file before PrivateVaultManager
            // removes that runtime directory.
            if (root.activeVideoViewer && root.activeVideoViewer.currentItem) {
                const videoUrl = String(root.activeVideoViewer.currentItem.url || "")
                if (privateVault.isRuntimeUrl(videoUrl))
                    root.activeVideoViewer.close()
            }
            if (globalMusicPlayer && globalMusicPlayer.currentUrl
                    && privateVault.isRuntimeUrl(String(globalMusicPlayer.currentUrl)))
                globalMusicPlayer.closePlayer()
        }
    }
    readonly property var transferManager: oneDrive.transferActive ? oneDrive : googleDrive
    property string homeForwardLocation: launchLocation
    // Empty means the Discover/home page; otherwise this mirrors the visible browser pane.
    property string sidebarLocation: launchLocation

    onSidebarLocationChanged: {
        if (!globalMusicPlayer.opened)
            return
        if (sidebarLocation === "category:/music") {
            globalMusicPlayer.restorePanel()
        } else if (globalMusicPlayer.backgroundPlayback) {
            globalMusicPlayer.minimize()
        } else {
            globalMusicPlayer.closePlayer()
        }
    }

    Component.onCompleted: {
        const savedMode = windowSettings.windowMode
        if (savedMode === Window.FullScreen || (savedMode < 0 && windowSettings.fullScreen)) {
            root.modeBeforeFullScreen = Window.Windowed
            root.visibility = Window.FullScreen
        } else if (savedMode === Window.Maximized) {
            root.visibility = Window.Maximized
        } else {
            root.visibility = Window.Windowed
        }
        windowStateReady = true
    }

    onVisibilityChanged: function(newVisibility) {
        if (!windowStateReady)
            return
        // Hidden/Minimized are temporary states and must not replace the
        // user's normal startup mode. Use the declared signal parameter so
        // Qt does not rely on deprecated context-parameter injection.
        if (newVisibility === Window.FullScreen ||
                newVisibility === Window.Maximized ||
                newVisibility === Window.Windowed) {
            windowSettings.windowMode = newVisibility
            windowSettings.fullScreen = newVisibility === Window.FullScreen
        }
    }

    onClosing: function(close) {
        // Persist the stable mode before Plasma/Qt starts hiding the window.
        // This avoids a close transition accidentally replacing Maximized or
        // FullScreen with Windowed.
        if (visibility === Window.FullScreen ||
                visibility === Window.Maximized ||
                visibility === Window.Windowed) {
            windowSettings.windowMode = visibility
            windowSettings.fullScreen = visibility === Window.FullScreen
        }

        // Do not keep a hidden single-instance process alive after the main
        // window is closed.  A future `g-file` launch must create a fresh
        // process and therefore start from the normal Discover page.
        close.accepted = true
        Qt.callLater(function() { Qt.quit() })
    }

    function browse(location) {
        if (!location || location.length === 0)
            return
        sidebarLocation = location
        quickAccessModel.setLastLocation(location)
        const current = stack.currentItem
        if (current && typeof current.navigateCurrentTab === "function") {
            current.navigateCurrentTab(location)
            return
        }
        homeForwardLocation = location
        stack.push(browseComponent, { initialLocation: location })
    }

    function openInNewTab(location) {
        if (!location || location.length === 0)
            return
        sidebarLocation = location
        quickAccessModel.setLastLocation(location)
        const current = stack.currentItem
        if (current && typeof current.addTab === "function") {
            current.addTab(location)
            return
        }
        homeForwardLocation = location
        stack.push(browseComponent, { initialLocation: location })
    }

    function handleExternalOpen(location) {
        if (!location || location.length === 0)
            return
        sidebarLocation = location
        if (waitingForExternalLocation) {
            waitingForExternalLocation = false
            homeForwardLocation = location
            stack.replace(browseComponent, { initialLocation: location })
            return
        }
        const current = stack.currentItem
        if (current && typeof current.currentTabLocation === "function"
                && current.currentTabLocation() === location) {
            current.navigateCurrentTab(location)
            return
        }
        // Plasma/file-association requests must never replace the user's
        // current tab. They arrive as new tabs in the already-running window.
        openInNewTab(location)
    }

    function handleInitialExternalReveal(location, itemUrl) {
        if (!location || !location.length || !itemUrl || !itemUrl.length)
            return
        sidebarLocation = location
        const current = stack.currentItem
        if (current && current.activePane) {
            current.activePane.revealExternalItem(location, itemUrl)
            return
        }
        Qt.callLater(function() {
            const page = stack.currentItem
            if (page && page.activePane)
                page.activePane.revealExternalItem(location, itemUrl)
        })
    }

    function handleExternalReveal(location, itemUrl) {
        if (!location || !location.length || !itemUrl || !itemUrl.length)
            return
        sidebarLocation = location
        const revealKey = location + "|" + itemUrl
        const now = Date.now()
        // File dialogs and KIO can repeat the same request while a cold
        // process is still becoming visible. Treat those clicks as one open.
        if (revealKey === lastExternalRevealKey && now - lastExternalRevealTime < 1800)
            return
        lastExternalRevealKey = revealKey
        lastExternalRevealTime = now
        if (waitingForExternalLocation) {
            waitingForExternalLocation = false
            homeForwardLocation = location
            stack.replace(browseComponent, { initialLocation: location })
            Qt.callLater(function() {
                const startupPage = stack.currentItem
                if (startupPage && startupPage.activePane)
                    startupPage.activePane.revealExternalItem(location, itemUrl)
            })
            return
        }
        const current = stack.currentItem
        if (current && typeof current.revealInNewTab === "function") {
            current.revealInNewTab(location, itemUrl)
            return
        }
        stack.push(browseComponent, { initialLocation: location })
        Qt.callLater(function() {
            const browsePage = stack.currentItem
            if (browsePage && browsePage.activePane)
                browsePage.activePane.revealExternalItem(location, itemUrl)
        })
    }

    function searchHome(query) {
        const q = (query || "").trim()
        if (!q.length)
            return
        const home = StandardPaths.writableLocation(StandardPaths.HomeLocation)
        const current = stack.currentItem
        if (current && current.activePane && typeof current.activePane.startSearch === "function") {
            current.activePane.startSearch(q, true)
            return
        }
        stack.push(browseComponent, { initialLocation: home })
        Qt.callLater(function() {
            const page = stack.currentItem
            if (page && page.activePane && typeof page.activePane.startSearch === "function")
                page.activePane.startSearch(q, true)
        })
    }

    function openContentCategory(key, title, iconName) {
        if (!key || !key.length)
            return
        if (key === "private") {
            sidebarLocation = "private:/"
            homeForwardLocation = ""
            stack.push(privateVaultComponent)
            return
        }
        const location = "category:/" + key
        sidebarLocation = location
        homeForwardLocation = location
        stack.push(browseComponent, { initialLocation: location })
    }

    function showHome() {
        sidebarLocation = ""
        const current = stack.currentItem
        if (current && typeof current.currentTabLocation === "function") {
            const location = current.currentTabLocation()
            if (location && location.length)
                homeForwardLocation = location
        }
        waitingForExternalLocation = false
        if (stack.depth > 1)
            stack.pop(null)
        else if (current && typeof current.currentTabLocation === "function")
            stack.replace(homeComponent)
    }

    function homeForward() {
        if (homeForwardLocation.length)
            browse(homeForwardLocation)
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: AppTheme.shellMargin
        spacing: AppTheme.shellMargin

        AppSidebar {
            id: appSidebar
            Layout.preferredWidth: 248
            Layout.minimumWidth: 248
            Layout.maximumWidth: 248
            Layout.fillHeight: true
            lang: lang
            favoritesModel: favoritesModel
            storageModel: storageModel
            musicPlayer: globalMusicPlayer
            // Read the location from the panel that is actually on screen.
            // A freshly constructed BrowserPane starts at Home for one frame;
            // keeping a separate cached value made that transient path stick.
            currentLocation: {
                const currentPage = stack ? stack.currentItem : null
                return currentPage && currentPage.visibleLocation !== undefined
                        ? currentPage.visibleLocation : ""
            }
            onHomeRequested: root.showHome()
            onLocationRequested: function(location) { root.browse(location) }
            onLocationNewTabRequested: function(location) { root.openInNewTab(location) }
            onFolderIconChanged: function(path) {
                const current = stack.currentItem
                if (current && current.activePane && typeof current.activePane.refreshDirectory === "function")
                    current.activePane.refreshDirectory()
            }
        }

        Rectangle {
            id: workspaceShell
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: AppTheme.panelRadius
            color: AppTheme.workspace
            border.color: AppTheme.border
            border.width: 1
            clip: true

            StackView {
                id: stack
                anchors.fill: parent
                anchors.margins: 1
                clip: true
                initialItem: root.launchLocation.length > 0
                             ? browseComponent
                             : (root.waitingForExternalLocation ? externalWaitComponent : homeComponent)
    
                pushEnter: Transition {
                    ParallelAnimation {
                        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 170 }
                        NumberAnimation { property: "x"; from: 20; to: 0; duration: 170; easing.type: Easing.OutCubic }
                    }
                }
                pushExit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0.88; duration: 120 } }
                popEnter: Transition { NumberAnimation { property: "opacity"; from: 0.88; to: 1; duration: 140 } }
                popExit: Transition {
                    ParallelAnimation {
                        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 150 }
                        NumberAnimation { property: "x"; from: 0; to: 20; duration: 150 }
                    }
                }
            }
        }
    }

    readonly property var activeMusicDockHost: {
        const page = stack.currentItem
        const pane = page && page.activePane ? page.activePane : null
        if (!pane || !pane.musicDockHost || !pane.musicDockHost.visible)
            return null
        return pane.musicDockHost
    }

    readonly property var activeMusicPlayerArea: {
        const page = stack.currentItem
        const pane = page && page.activePane ? page.activePane : null
        return pane && pane.musicPlayerArea ? pane.musicPlayerArea : null
    }

    MusicPlayer {
        id: globalMusicPlayer
        z: 12000
        embeddedMode: root.activeMusicDockHost !== null && root.activeMusicDockHost.embeddedPlayer
        x: {
            const host = root.activeMusicDockHost || root.activeMusicPlayerArea
            if (!host)
                return appSidebar.x + appSidebar.width + AppTheme.shellMargin
            // Explicit reads keep this binding live across pane/layout changes.
            const revision = host.x + host.y + host.width + host.height
                             + (stack.currentItem ? stack.currentItem.x : 0)
            return host.mapToItem(root.contentItem, 0, 0).x
        }
        y: {
            const host = root.activeMusicDockHost
            if (!host)
                return root.height - implicitHeight - 14
            const revision = host.x + host.y + host.width + host.height
                             + (stack.currentItem ? stack.currentItem.y : 0)
            return host.mapToItem(root.contentItem, 0, 0).y
        }
        width: {
            const host = root.activeMusicDockHost || root.activeMusicPlayerArea
            return host ? host.width : Math.max(1, root.width - x - AppTheme.shellMargin)
        }
        height: implicitHeight
        lang: root.appLang
    }

    MprisController {
        id: mprisController
        active: globalMusicPlayer.opened && globalMusicPlayer.currentUrl.length > 0
        playbackStatus: globalMusicPlayer.playbackStatus
        title: globalMusicPlayer.currentTitle
        trackUrl: globalMusicPlayer.currentUrl
        artworkHint: globalMusicPlayer.currentArtwork
        duration: globalMusicPlayer.duration
        position: globalMusicPlayer.position
        volume: globalMusicPlayer.volume
        shuffle: globalMusicPlayer.shuffleMode
        repeatMode: globalMusicPlayer.repeatMode
    }

    Connections {
        target: mprisController
        function onPlayRequested() { globalMusicPlayer.play() }
        function onPauseRequested() { globalMusicPlayer.pause() }
        function onPlayPauseRequested() { globalMusicPlayer.toggle() }
        function onStopRequested() { globalMusicPlayer.stopPlayback() }
        function onNextRequested() { globalMusicPlayer.next() }
        function onPreviousRequested() { globalMusicPlayer.previous() }
        function onSeekRequested(offsetMs) { globalMusicPlayer.seekBy(offsetMs) }
        function onSetPositionRequested(positionMs) { globalMusicPlayer.seekTo(positionMs) }
        function onVolumeRequested(value) { globalMusicPlayer.setVolume(value) }
        function onShuffleRequested(enabled) { globalMusicPlayer.setShuffleEnabled(enabled) }
        function onRepeatModeRequested(mode) { globalMusicPlayer.setRepeatMode(mode) }
        function onRaiseRequested() {
            root.show()
            root.raise()
            root.requestActivate()
        }
        function onQuitRequested() { Qt.quit() }
    }

    Connections {
        target: googleDrive
        function onOperationFinished(success, message) {
            globalToastLabel.text = lang.localizeMessage(message)
            globalToast.open()
        }
    }

    Connections {
        target: oneDrive
        function onOperationFinished(success, message) {
            globalToastLabel.text = lang.localizeMessage(message)
            globalToast.open()
        }
    }

    Connections {
        target: storageModel
        function onMountOperationFinished(success, mountedNow, detail) {
            var base = success
                    ? (mountedNow ? lang.t("disk_mounted") : lang.t("disk_unmounted"))
                    : (mountedNow ? lang.t("mount_failed") : lang.t("unmount_failed"))
            globalToastLabel.text = (!success && detail && detail.length) ? base + " · " + detail : base
            globalToast.open()
        }
    }

    Popup {
        id: globalToast
        x: root.width - width - 26
        y: root.height - height - 26
        width: Math.min(380, Math.max(190, globalToastLabel.implicitWidth + 42))
        height: 54
        z: 11000
        closePolicy: Popup.NoAutoClose
        background: Rectangle { radius: 18; color: AppTheme.commandBar; border.color: AppTheme.accentBorder; opacity: 0.99 }
        contentItem: Text {
            id: globalToastLabel
            color: AppTheme.text
            font.pixelSize: 12
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.WordWrap
        }
        onOpened: globalToastTimer.restart()
    }
    Timer { id: globalToastTimer; interval: 2800; onTriggered: globalToast.close() }

    Timer {
        interval: 6000
        running: root.waitingForExternalLocation
        repeat: false
        onTriggered: {
            if (!root.waitingForExternalLocation)
                return
            root.waitingForExternalLocation = false
            stack.replace(homeComponent)
        }
    }

    Rectangle {
        id: globalDriveTransferCard
        visible: root.transferManager.transferActive
        z: 10000
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 26
        anchors.bottomMargin: 26
        width: 340
        height: 92
        radius: 20
        color: AppTheme.commandBar
        border.color: AppTheme.borderStrong
        border.width: 1

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 14
            spacing: 7
            RowLayout {
                Layout.fillWidth: true
                Text { text: root.transferManager.transferLabel; color: AppTheme.text; font.pixelSize: 12; font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
                Text { text: root.transferManager.transferTotal > 0 ? root.transferManager.transferPercent + "%" : "…"; color: AppTheme.accent; font.pixelSize: 11; font.bold: true }
            }
            Text { text: root.transferManager.transferDetail; color: AppTheme.textMuted; font.pixelSize: 10; Layout.fillWidth: true; elide: Text.ElideMiddle }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 7
                radius: 4
                color: AppTheme.surfaceHover
                Rectangle {
                    width: parent.width * (root.transferManager.transferTotal > 0 ? root.transferManager.transferPercent / 100 : 0.22)
                    height: parent.height
                    radius: 4
                    color: AppTheme.accent
                    SequentialAnimation on opacity {
                        running: root.transferManager.transferActive && root.transferManager.transferTotal <= 0
                        loops: Animation.Infinite
                        NumberAnimation { from: 0.45; to: 1.0; duration: 650; easing.type: Easing.InOutSine }
                        NumberAnimation { from: 1.0; to: 0.45; duration: 650; easing.type: Easing.InOutSine }
                    }
                }
            }
        }
    }

    Component {
        id: homeComponent
        HomePage {
            storageModel: root.appStorage
            favoritesModel: root.appFavorites
            quickAccessModel: root.appQuickAccess
            contentIndexModel: root.appContentIndex
            lang: root.appLang
            cloudAuth: root.appCloudAuth
            cloudIntegrationPreferences: root.appCloudIntegrationPreferences
            privateVault: root.appPrivateVault
            onBrowseRequested: function(location) { root.browse(location) }
            onBrowseNewTabRequested: function(location) { root.openInNewTab(location) }
            onBackNavigationRequested: { }
            onForwardNavigationRequested: root.homeForward()
            onSearchRequested: function(query) { root.searchHome(query) }
            onCategoryRequested: function(key, title, iconName) { root.openContentCategory(key, title, iconName) }
        }
    }

    Component {
        id: privateVaultComponent
        PrivateVaultPage {
            lang: root.appLang
            vault: root.appPrivateVault
            musicPlayer: globalMusicPlayer
            onCloseRequested: {
                root.sidebarLocation = ""
                if (stack.depth > 1)
                    stack.pop()
                else
                    stack.replace(homeComponent)
            }
        }
    }

    Component {
        id: externalWaitComponent
        Rectangle {
            color: AppTheme.workspace
            radius: Math.max(0, AppTheme.panelRadius - 1)
            antialiasing: true
            Column {
                anchors.centerIn: parent
                spacing: 14
                GBusyIndicator {
                    anchors.horizontalCenter: parent.horizontalCenter
                    running: true
                    width: 42
                    height: 42
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: root.appLang.language === "tr"
                          ? "Dizin isteği bekleniyor…"
                          : "Waiting for folder request…"
                    color: AppTheme.text
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }
            }
        }
    }

    Component {
        id: browseComponent
        BrowsePage {
            initialLocation: root.homeForwardLocation
            lang: root.appLang
            contentIndexModel: root.appContentIndex
            musicPlayer: globalMusicPlayer
            favoritesModel: root.appFavorites
            quickAccessModel: root.appQuickAccess
            adminEditor: root.appAdminEditor
            cloudAuth: root.appCloudAuth
            cloudIntegrationPreferences: root.appCloudIntegrationPreferences
            googleDrive: root.appGoogleDrive
            oneDrive: root.appOneDrive
            onBackRequested: function(lastLocation) {
                if (lastLocation && lastLocation.length)
                    root.homeForwardLocation = lastLocation
                // A window that was opened directly on a folder starts with
                // BrowsePage as StackView depth 1. Popping that only page used
                // to tear down the browser (and all of its tabs) and bounce to
                // Discover. Only return to Discover when there is an actual
                // page underneath this BrowsePage.
                if (stack.depth > 1) {
                    root.sidebarLocation = ""
                    stack.pop()
                }
            }
            onSidebarLocationChanged: function(location) { root.sidebarLocation = location }
        }
    }
}
