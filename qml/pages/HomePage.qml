import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import GFile.App
import GFile.Backend
import "../components"

Item {
    id: page
    required property var storageModel
    required property var favoritesModel
    required property var quickAccessModel
    required property var contentIndexModel
    required property var lang
    required property var cloudAuth
    required property var cloudIntegrationPreferences
    required property var privateVault

    signal browseRequested(string location)
    signal browseNewTabRequested(string location)
    signal backNavigationRequested()
    signal forwardNavigationRequested()
    signal searchRequested(string query)
    signal categoryRequested(string key, string title, string iconName)

    readonly property string homePath: StandardPaths.writableLocation(StandardPaths.HomeLocation)
    property double wheelBurstLastMs: 0
    property int wheelBurstCount: 0
    property int wheelBurstDirection: 0
    property date now: new Date()
    Timer { interval: 1000; running: true; repeat: true; onTriggered: page.now = new Date() }


    GModalPopup {
        id: privateUnlockPopup
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 430
        height: 265
        modal: true
        focus: true
        background: GModalSurface { }
        onOpened: {
            privateUnlockPassword.text = ""
            page.privateVault.clearStatus()
            privateUnlockPassword.forceActiveFocus()
        }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 22
            spacing: 12
            Text {
                text: lang.language === "tr" ? "Sana Özel kilidini aç" : "Unlock Private"
                color: AppTheme.text
                font.pixelSize: 18
                font.bold: true
            }
            GTextField {
                id: privateUnlockPassword
                Layout.fillWidth: true
                echoMode: TextInput.Password
                placeholderText: lang.language === "tr" ? "Kasa parolası" : "Vault password"
                color: AppTheme.text
                onAccepted: privateUnlockConfirm.clicked()
            }
            Text {
                visible: page.privateVault.lastError.length > 0
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: page.privateVault.lastError
                color: AppTheme.danger
                font.pixelSize: 10
            }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.fillWidth: true
                GModalButton {
                    visible: page.privateVault.kwalletEnabled
                    text: lang.language === "tr" ? "KWallet ile Aç" : "Open with KWallet"
                    onClicked: {
                        if (page.privateVault.quickUnlock())
                            privateUnlockPopup.close()
                    }
                }
                Item { Layout.fillWidth: true }
                GModalButton { text: lang.language === "tr" ? "İptal" : "Cancel"; onClicked: privateUnlockPopup.close() }
                GModalButton {
                    id: privateUnlockConfirm
                    text: lang.language === "tr" ? "Kilidi Aç" : "Unlock"
                    primary: true
                    enabled: privateUnlockPassword.text.length > 0 && !page.privateVault.busy
                    onClicked: {
                        if (page.privateVault.unlock(privateUnlockPassword.text))
                            privateUnlockPopup.close()
                    }
                }
            }
        }
    }

    readonly property int visibleCloudCount:
        (cloudIntegrationPreferences.showGoogle ? 1 : 0)
        + (cloudIntegrationPreferences.showOneDrive ? 1 : 0)
        + (cloudIntegrationPreferences.showDropbox ? 1 : 0)
        + (cloudIntegrationPreferences.showNextcloud ? 1 : 0)
        + (cloudIntegrationPreferences.showOwnCloud ? 1 : 0)
        + (cloudIntegrationPreferences.showMega ? 1 : 0)
        + (cloudIntegrationPreferences.showPCloud ? 1 : 0)
        + (cloudIntegrationPreferences.showWebDav ? 1 : 0)
    readonly property var defaultCloudOrder: ["google", "onedrive", "dropbox", "nextcloud", "owncloud", "mega", "pcloud", "webdav"]
    property var cloudOrderKeys: ["google", "onedrive", "dropbox", "nextcloud", "owncloud", "mega", "pcloud", "webdav"]

    function cloudVisible(key) {
        if (key === "google") return cloudIntegrationPreferences.showGoogle
        if (key === "onedrive") return cloudIntegrationPreferences.showOneDrive
        if (key === "dropbox") return cloudIntegrationPreferences.showDropbox
        if (key === "nextcloud") return cloudIntegrationPreferences.showNextcloud
        if (key === "owncloud") return cloudIntegrationPreferences.showOwnCloud
        if (key === "mega") return cloudIntegrationPreferences.showMega
        if (key === "pcloud") return cloudIntegrationPreferences.showPCloud
        if (key === "webdav") return cloudIntegrationPreferences.showWebDav
        return false
    }

    function rebuildCloudOrder() {
        var raw = String(cloudIntegrationPreferences.cloudOrder || "")
        var pieces = raw.split(",")
        var order = []
        for (var i = 0; i < pieces.length; ++i) {
            var key = String(pieces[i]).trim().toLowerCase()
            if (page.defaultCloudOrder.indexOf(key) >= 0 && order.indexOf(key) < 0)
                order.push(key)
        }
        for (var j = 0; j < page.defaultCloudOrder.length; ++j) {
            var fallback = page.defaultCloudOrder[j]
            if (order.indexOf(fallback) < 0)
                order.push(fallback)
        }
        page.cloudOrderKeys = order
        var normalized = order.join(",")
        if (normalized !== raw)
            cloudIntegrationPreferences.cloudOrder = normalized
    }

    function visibleCloudRank(key) {
        var rank = 0
        for (var i = 0; i < page.cloudOrderKeys.length; ++i) {
            var current = page.cloudOrderKeys[i]
            if (!page.cloudVisible(current))
                continue
            if (current === key)
                return rank
            ++rank
        }
        return -1
    }

    function moveCloudProvider(fromKey, toKey) {
        if (!fromKey || !toKey || fromKey === toKey)
            return
        var order = page.cloudOrderKeys.slice(0)
        var from = order.indexOf(fromKey)
        var to = order.indexOf(toKey)
        if (from < 0 || to < 0)
            return
        var moved = order.splice(from, 1)[0]
        order.splice(to, 0, moved)
        page.cloudOrderKeys = order
        cloudIntegrationPreferences.cloudOrder = order.join(",")
    }
    property string cloudManagerFocus: ""

    function openCloudManager(provider) {
        page.cloudManagerFocus = provider || ""
        cloudManagerPopup.open()
    }

    function normalizedDavLocation(value) {
        var raw = String(value || "").trim()
        if (!raw.length)
            return ""
        if (raw.startsWith("https://"))
            return "webdavs://" + raw.substring(8)
        if (raw.startsWith("http://"))
            return "webdav://" + raw.substring(7)
        if (raw.indexOf("://") < 0)
            return "webdavs://" + raw
        return raw
    }

    function externalCloudLocation(provider) {
        if (provider === "dropbox") return String(cloudIntegrationPreferences.dropboxLocation || "").trim()
        if (provider === "nextcloud") return page.normalizedDavLocation(cloudIntegrationPreferences.nextcloudLocation)
        if (provider === "owncloud") return page.normalizedDavLocation(cloudIntegrationPreferences.ownCloudLocation)
        if (provider === "mega") return String(cloudIntegrationPreferences.megaLocation || "").trim()
        if (provider === "pcloud") return String(cloudIntegrationPreferences.pCloudLocation || "").trim()
        if (provider === "webdav") return page.normalizedDavLocation(cloudIntegrationPreferences.webDavLocation)
        return ""
    }

    function externalCloudStatus(provider) {
        var location = page.externalCloudLocation(provider)
        if (!location.length)
            return lang.language === "tr" ? "Konum ayarlanmadı" : "Location not configured"
        if (provider === "dropbox" || provider === "mega" || provider === "pcloud")
            return lang.language === "tr" ? "Senkron / mount klasörü hazır" : "Sync / mount folder configured"
        return lang.language === "tr" ? "KIO · WebDAV bağlantısı hazır" : "KIO · WebDAV connection configured"
    }

    function openExternalCloud(provider) {
        var location = page.externalCloudLocation(provider)
        if (!location.length) {
            page.openCloudManager(provider)
            return
        }
        page.browseRequested(location)
    }

    function formatIndexNumber(value) {
        var locale = lang.language === "tr" ? Qt.locale("tr_TR") : Qt.locale("en_US")
        return Number(value || 0).toLocaleString(locale, 'f', 0)
    }

    function oauthStatusText(provider) {
        var state = provider === "google" ? cloudAuth.googleAuthState : cloudAuth.oneDriveAuthState
        var configured = provider === "google" ? cloudAuth.googleConfigured : cloudAuth.oneDriveConfigured
        var connected = provider === "google" ? cloudAuth.googleConnected : cloudAuth.oneDriveConnected
        if (connected || state === "connected")
            return lang.t("connected")
        if (!configured)
            return lang.t("not_configured")
        if (state === "starting")
            return lang.language === "tr" ? "OAuth başlatılıyor…" : "Starting OAuth…"
        if (state === "waiting_browser")
            return lang.language === "tr" ? "Tarayıcı doğrulaması bekleniyor…" : "Waiting for browser authorization…"
        if (state === "exchanging_token")
            return lang.language === "tr" ? "Yetki kodu alındı · token alınıyor…" : "Authorization code received · getting token…"
        if (state === "restoring")
            return lang.language === "tr" ? "Oturum geri yükleniyor…" : "Restoring session…"
        if (state === "reauthorize_required")
            return lang.language === "tr" ? "Yeniden yetkilendirme gerekli" : "Authorization required again"
        if (state === "error")
            return lang.language === "tr" ? "Bağlantı hatası · ayrıntı için tıkla" : "Connection error · click for details"
        return lang.t("oauth_pending")
    }
    function prepareOAuthSettings(provider) {
        page.oauthProvider = provider
        clientIdInput.text = provider === "google" ? cloudAuth.googleClientId : cloudAuth.oneDriveClientId
        googleSecretInput.text = provider === "google" ? cloudAuth.googleClientSecret : ""
        oneDriveTenantInput.text = provider === "onedrive" ? cloudAuth.oneDriveTenant : "common"
        oauthDialog.open()
    }

    function showOAuthError(provider) {
        page.oauthProvider = provider
        page.oauthErrorText = provider === "google" ? cloudAuth.googleLastError : cloudAuth.oneDriveLastError
        if (!page.oauthErrorText || page.oauthErrorText.length === 0)
            page.oauthErrorText = lang.language === "tr" ? "Ayrıntılı OAuth hatası alınamadı." : "No detailed OAuth error was captured."
        oauthErrorPopup.open()
    }

    property string oauthProvider: ""
    property string oauthErrorText: ""
    property bool infoPanelOpen: false
    onWidthChanged: { if (width < 1240 && infoPanelOpen) infoPanelOpen = false }
    property int editingShortcutIndex: -1
    property bool editingShortcutFixed: false
    property string editingShortcutIcon: "folder.svg"
    property string shortcutError: ""
    property string editingDiscoverKind: ""
    property string editingDiscoverKey: ""
    property string editingDiscoverIcon: "folder.svg"
    property string discoverEditError: ""
    readonly property var shortcutIcons: [
        "folder.svg", "home-folder.svg", "desktop.svg", "downloads.svg",
        "documents.svg", "image.svg", "audio.svg", "video.svg",
        "archive.svg", "drive.svg", "favorite.svg", "network.svg",
        "cloud.svg", "trash.svg"
    ]

    IconPickerManager { id: shortcutIconPicker }
    Component.onCompleted: {
        page.clampSystemIconSizes()
        page.rebuildCloudOrder()
    }

    Connections {
        target: cloudIntegrationPreferences
        function onCloudOrderChanged() { page.rebuildCloudOrder() }
    }
    readonly property var systemIconSizes: shortcutIconPicker.availableSystemIconSizes("folder", 128)
    readonly property int systemIconMinimum: systemIconSizes && systemIconSizes.length > 0 ? Number(systemIconSizes[0]) : 16
    readonly property int systemIconMaximum: systemIconSizes && systemIconSizes.length > 0 ? Number(systemIconSizes[systemIconSizes.length - 1]) : 128
    readonly property int systemHomeIconSize: AppTheme.systemHomeIconSize

    function clampSystemIconSizes() {
        // Native theme sizes are anchors, not the only allowed slider values.
        // Between two anchors QML scales the lower native artwork; on the next
        // anchor the icon provider switches to that theme's real asset.
        AppTheme.setSystemSidebarIconSize(Math.max(systemIconMinimum, Math.min(systemIconMaximum, AppTheme.systemSidebarIconSize)))
        AppTheme.setSystemHomeIconSize(Math.max(systemIconMinimum, Math.min(systemIconMaximum, AppTheme.systemHomeIconSize)))
    }

    function cloudDefaultName(key) {
        if (key === "google") return lang.t("google_drive")
        if (key === "onedrive") return lang.t("onedrive")
        if (key === "dropbox") return "Dropbox"
        if (key === "nextcloud") return "Nextcloud"
        if (key === "owncloud") return "ownCloud"
        if (key === "mega") return "MEGA"
        if (key === "pcloud") return "pCloud"
        if (key === "webdav") return "WebDAV"
        return key
    }

    function cloudDefaultIcon(key) {
        if (key === "google") return "cloud-google.svg"
        if (key === "onedrive") return "cloud-onedrive.svg"
        if (key === "dropbox") return "cloud-dropbox.svg"
        if (key === "nextcloud") return "cloud-nextcloud.svg"
        if (key === "owncloud") return "cloud-owncloud.svg"
        if (key === "mega") return "cloud-mega.svg"
        if (key === "pcloud") return "cloud-pcloud.svg"
        if (key === "webdav") return "network.svg"
        return "cloud.svg"
    }

    function cloudCustomName(key) {
        if (key === "google") return cloudIntegrationPreferences.googleCustomName
        if (key === "onedrive") return cloudIntegrationPreferences.oneDriveCustomName
        if (key === "dropbox") return cloudIntegrationPreferences.dropboxCustomName
        if (key === "nextcloud") return cloudIntegrationPreferences.nextcloudCustomName
        if (key === "owncloud") return cloudIntegrationPreferences.ownCloudCustomName
        if (key === "mega") return cloudIntegrationPreferences.megaCustomName
        if (key === "pcloud") return cloudIntegrationPreferences.pCloudCustomName
        if (key === "webdav") return cloudIntegrationPreferences.webDavCustomName
        return ""
    }

    function cloudCustomIcon(key) {
        if (key === "google") return cloudIntegrationPreferences.googleCustomIcon
        if (key === "onedrive") return cloudIntegrationPreferences.oneDriveCustomIcon
        if (key === "dropbox") return cloudIntegrationPreferences.dropboxCustomIcon
        if (key === "nextcloud") return cloudIntegrationPreferences.nextcloudCustomIcon
        if (key === "owncloud") return cloudIntegrationPreferences.ownCloudCustomIcon
        if (key === "mega") return cloudIntegrationPreferences.megaCustomIcon
        if (key === "pcloud") return cloudIntegrationPreferences.pCloudCustomIcon
        if (key === "webdav") return cloudIntegrationPreferences.webDavCustomIcon
        return ""
    }

    function cloudDisplayName(key) {
        var custom = String(page.cloudCustomName(key) || "").trim()
        return custom.length ? custom : page.cloudDefaultName(key)
    }

    function cloudDisplayIcon(key) {
        var custom = String(page.cloudCustomIcon(key) || "").trim()
        return custom.length ? custom : page.cloudDefaultIcon(key)
    }

    function setCloudAppearance(key, title, icon) {
        if (key === "google") { cloudIntegrationPreferences.googleCustomName = title; cloudIntegrationPreferences.googleCustomIcon = icon }
        else if (key === "onedrive") { cloudIntegrationPreferences.oneDriveCustomName = title; cloudIntegrationPreferences.oneDriveCustomIcon = icon }
        else if (key === "dropbox") { cloudIntegrationPreferences.dropboxCustomName = title; cloudIntegrationPreferences.dropboxCustomIcon = icon }
        else if (key === "nextcloud") { cloudIntegrationPreferences.nextcloudCustomName = title; cloudIntegrationPreferences.nextcloudCustomIcon = icon }
        else if (key === "owncloud") { cloudIntegrationPreferences.ownCloudCustomName = title; cloudIntegrationPreferences.ownCloudCustomIcon = icon }
        else if (key === "mega") { cloudIntegrationPreferences.megaCustomName = title; cloudIntegrationPreferences.megaCustomIcon = icon }
        else if (key === "pcloud") { cloudIntegrationPreferences.pCloudCustomName = title; cloudIntegrationPreferences.pCloudCustomIcon = icon }
        else if (key === "webdav") { cloudIntegrationPreferences.webDavCustomName = title; cloudIntegrationPreferences.webDavCustomIcon = icon }
    }

    function editDiscoverItem(kind, key, title, icon) {
        page.editingDiscoverKind = kind
        page.editingDiscoverKey = key
        page.editingDiscoverIcon = icon || "folder.svg"
        page.discoverEditError = ""
        discoverEditNameInput.text = title || ""
        discoverItemEditDialog.open()
    }

    function editShortcut(index, title, location, icon, fixed) {
        editingShortcutIndex = index
        editingShortcutFixed = !!fixed
        shortcutError = ""
        shortcutNameInput.text = title
        shortcutLocationInput.text = location
        editingShortcutIcon = icon || "folder.svg"
        shortcutDialog.open()
    }

    function addShortcut() {
        editingShortcutIndex = -1
        editingShortcutFixed = false
        shortcutError = ""
        shortcutNameInput.text = ""
        shortcutLocationInput.text = page.homePath
        editingShortcutIcon = "folder.svg"
        shortcutDialog.open()
    }

    property real iconResizePinnedViewportY: NaN

    function beginIconResize() {
        iconResizePinnedViewportY = iconSizePanel.mapToItem(homeScroll, 0, 0).y
    }

    function keepIconResizePanelPinned() {
        if (isNaN(iconResizePinnedViewportY))
            return
        Qt.callLater(function() {
            if (isNaN(page.iconResizePinnedViewportY) || !homeScroll.contentItem)
                return
            const currentY = iconSizePanel.mapToItem(homeScroll, 0, 0).y
            const delta = currentY - page.iconResizePinnedViewportY
            if (Math.abs(delta) < 0.5)
                return
            const flick = homeScroll.contentItem
            const minY = flick.originY
            const maxY = Math.max(minY, flick.originY + flick.contentHeight - flick.height)
            flick.contentY = Math.max(minY, Math.min(maxY, flick.contentY + delta))
        })
    }

    function endIconResize() {
        keepIconResizePanelPinned()
        Qt.callLater(function() { page.iconResizePinnedViewportY = NaN })
    }

    NumberAnimation {
        id: homeWheelScrollAnimation
        property: "contentY"
        easing.type: Easing.OutCubic
    }

    function normalizedWheelDelta(event) {
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

    function scrollHomeFromWheel(event) {
        const view = homeScroll.contentItem
        if (!view)
            return
        const delta = normalizedWheelDelta(event)
        if (delta === 0)
            return
        const top = view.originY
        const bottom = top + Math.max(0, view.contentHeight - view.height)
        const continuing = homeWheelScrollAnimation.running && homeWheelScrollAnimation.target === view
        const previousTarget = continuing ? homeWheelScrollAnimation.to : view.contentY
        const sameDirection = !continuing || Math.sign(previousTarget - view.contentY) === -Math.sign(delta)
        const base = sameDirection ? previousTarget : view.contentY
        const limit = Math.max(AppTheme.wheelScrollStep, view.height * 1.8, 720)
        const destination = Math.max(top, Math.min(bottom,
                                Math.max(view.contentY - limit,
                                         Math.min(view.contentY + limit, base - delta))))
        homeWheelScrollAnimation.stop()
        if (Math.abs(destination - view.contentY) < 0.5)
            return
        homeWheelScrollAnimation.target = view
        homeWheelScrollAnimation.from = view.contentY
        homeWheelScrollAnimation.to = destination
        homeWheelScrollAnimation.duration = Math.min(400, Math.max(220,
                                         160 + Math.abs(destination - view.contentY) / 4))
        homeWheelScrollAnimation.start()
    }

    Rectangle {
        anchors.fill: parent
        radius: Math.max(0, AppTheme.panelRadius - 1)
        color: AppTheme.workspace
        antialiasing: true
    }

    MouseArea {
        anchors.fill: parent
        z: 10000
        acceptedButtons: Qt.BackButton | Qt.ForwardButton
        propagateComposedEvents: true
        onPressed: function(mouse) {
            if (mouse.button === Qt.BackButton) {
                page.backNavigationRequested()
                mouse.accepted = true
            } else if (mouse.button === Qt.ForwardButton) {
                page.forwardNavigationRequested()
                mouse.accepted = true
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 26
        anchors.rightMargin: 26
        anchors.topMargin: 22
        anchors.bottomMargin: 22
        spacing: 20

        ScrollView {
            id: homeScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            rightPadding: 14

            WheelHandler {
                target: null
                blocking: true
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                acceptedModifiers: Qt.NoModifier
                onWheel: function(event) {
                    page.scrollHomeFromWheel(event)
                    event.accepted = true
                }
            }

            ScrollBar.vertical: ScrollBar {
                id: homeVerticalBar
                policy: ScrollBar.AsNeeded
                width: 8
                contentItem: Rectangle {
                    implicitWidth: 6
                    radius: 3
                    color: homeVerticalBar.pressed ? AppTheme.accent
                                                   : (homeVerticalBar.hovered ? AppTheme.textMuted : AppTheme.borderStrong)
                    opacity: homeVerticalBar.active ? 0.85 : 0.0
                    Behavior on opacity { NumberAnimation { duration: 140 } }
                }
                background: Item {}
            }

            ColumnLayout {
                width: homeScroll.availableWidth
                spacing: 20

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    ColumnLayout {
                        Layout.preferredWidth: 260
                        spacing: 0
                        Text {
                            text: Qt.formatTime(page.now, "HH:mm")
                            color: AppTheme.text
                            font.pixelSize: 30
                            font.weight: Font.Bold
                        }
                        Text {
                            text: page.now.toLocaleDateString(
                                      Qt.locale(lang.language === "tr" ? "tr_TR" : "en_US"),
                                      lang.language === "tr" ? "dddd, d MMMM yyyy" : "dddd, MMMM d, yyyy")
                            color: AppTheme.textMuted
                            font.pixelSize: 10
                        }
                    }

                    Item { Layout.fillWidth: true }

                    TopSearchBar {
                        Layout.preferredWidth: 440
                        Layout.minimumWidth: 320
                        Layout.maximumWidth: 520
                        Layout.preferredHeight: 42
                        lang: page.lang
                        onSearchRequested: function(query) { page.searchRequested(query) }
                    }
                    Item {
                        id: infoToggleButton
                        readonly property bool hovered: infoToggleMouse.containsMouse
                        Layout.preferredWidth: 42
                        Layout.preferredHeight: 42

                        Rectangle {
                            anchors.fill: parent
                            radius: 14
                            color: page.infoPanelOpen ? AppTheme.accent : (infoToggleButton.hovered ? AppTheme.accentSoft : AppTheme.surfaceRaised)
                            border.color: page.infoPanelOpen ? AppTheme.accent : AppTheme.border
                        }
                        CrispIcon {
                            anchors.centerIn: parent
                            source: AppTheme.icon("info.svg")
                            width: 20
                            height: 20
                        }
                        MouseArea {
                            id: infoToggleMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: page.infoPanelOpen = !page.infoPanelOpen
                        }
                        GToolTip {
                            text: lang.language === "tr" ? "Bilgi paneli" : "Information panel"
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text {
                        text: lang.language === "tr" ? "Kategoriler" : "Categories"
                        color: AppTheme.text
                        font.pixelSize: 17
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: lang.language === "tr"
                              ? "Her kategori kendi kaynaklarını ve indeks ayarlarını yönetir"
                              : "Each category manages its own sources and indexing settings"
                        color: AppTheme.textMuted
                        font.pixelSize: 10
                    }
                    Item { Layout.fillWidth: true }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: contentIndexModel.scanProgressVisible ? 42 : 0
                    visible: contentIndexModel.scanProgressVisible
                    radius: 10
                    color: AppTheme.accentSoft
                    border.color: AppTheme.accentBorder
                    clip: true

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        anchors.topMargin: 6
                        anchors.bottomMargin: 6
                        spacing: 4
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Text {
                                Layout.fillWidth: true
                                text: lang.language === "tr"
                                      ? (page.formatIndexNumber(contentIndexModel.scanVisitedEntries)
                                         + " öğe tarandı · "
                                         + page.formatIndexNumber(contentIndexModel.scanMatchedFiles)
                                         + " eşleşme bulundu")
                                      : (page.formatIndexNumber(contentIndexModel.scanVisitedEntries)
                                         + " entries scanned · "
                                         + page.formatIndexNumber(contentIndexModel.scanMatchedFiles)
                                         + " matches found")
                                color: AppTheme.text
                                font.pixelSize: 9
                                elide: Text.ElideRight
                            }
                            Text {
                                text: lang.language === "tr"
                                      ? (page.formatIndexNumber(contentIndexModel.scanPendingDirectories) + " klasör kaldı")
                                      : (page.formatIndexNumber(contentIndexModel.scanPendingDirectories) + " folders remaining")
                                color: AppTheme.accent
                                font.pixelSize: 9
                                font.weight: Font.DemiBold
                            }
                        }
                        GProgressBar {
                            Layout.fillWidth: true
                            from: 0
                            to: 100
                            value: contentIndexModel.scanProgressPercent
                        }
                    }
                }

                Item {
                    id: categoryFlowHost
                    Layout.fillWidth: true
                    Layout.preferredHeight: categoryFlow.height

                    Flow {
                        id: categoryFlow
                        width: parent.width
                        height: childrenRect.height
                        spacing: 12
                        // Match the Discover cloud grid: Categories scale 4 → 3 → 2 → 1.
                        property int columnCount: width >= 1160 ? 4 : (width >= 860 ? 3 : (width >= 560 ? 2 : 1))
                        property real tileWidth: Math.floor((width - (columnCount - 1) * spacing) / columnCount)

                        Repeater {
                            model: page.contentIndexModel
                            delegate: Item {
                                required property int index
                                required property string categoryKey
                                required property string categoryTitleTr
                                required property string categoryTitleEn
                                required property string categoryIcon
                                required property int categoryCount
                                required property bool categoryVisible
                                visible: categoryVisible
                                width: categoryVisible ? categoryFlow.tileWidth : 0
                                height: categoryVisible ? categoryTile.implicitHeight : 0
                                ContentCategoryTile {
                                    id: categoryTile
                                    anchors.fill: parent
                                    categoryIndex: parent.index
                                    categoryKey: parent.categoryKey
                                    title: lang.language === "tr" ? parent.categoryTitleTr : parent.categoryTitleEn
                                    iconName: parent.categoryIcon
                                    itemCount: parent.categoryKey === "private"
                                               ? (page.privateVault.unlocked ? page.privateVault.totalItemCount : 0)
                                               : parent.categoryCount
                                    itemLabel: lang.language === "tr" ? "öğe" : "items"
                                    secondaryText: parent.categoryKey === "private"
                                                   && page.privateVault.exists
                                                   && !page.privateVault.unlocked
                                                   ? (lang.language === "tr" ? "Kilitli" : "Locked") : ""
                                    systemIconPixels: page.systemHomeIconSize
                                    editable: parent.categoryKey !== "private"
                                    actionVisible: parent.categoryKey === "private" && page.privateVault.exists
                                    actionText: parent.categoryKey === "private"
                                                ? (page.privateVault.unlocked ? "🔒" : "🔓") : ""
                                    actionToolTip: parent.categoryKey !== "private" ? ""
                                                   : (page.privateVault.unlocked
                                                      ? (lang.language === "tr" ? "Kasayı şimdi kilitle" : "Lock vault now")
                                                      : (lang.language === "tr" ? "Kasa kilidini aç" : "Unlock vault"))
                                    onActionRequested: {
                                        if (parent.categoryKey !== "private")
                                            return
                                        if (page.privateVault.unlocked)
                                            page.privateVault.lock()
                                        else
                                            privateUnlockPopup.open()
                                    }
                                    onOpenRequested: function(key, title, iconName) {
                                        page.categoryRequested(key, title, iconName)
                                    }
                                    onEditRequested: function(key, title, iconName) {
                                        if (key !== "private")
                                            page.editDiscoverItem("category", key, title, iconName)
                                    }
                                    onReorderRequested: function(from, to) {
                                        page.contentIndexModel.moveCategory(from, to)
                                    }
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Text {
                        text: lang.t("cloud_integrations")
                        color: AppTheme.text
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                        Layout.fillWidth: true
                    }

                    GButton {
                        id: manageCloudIntegrationsButton
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        text: lang.language === "tr" ? "⚙  Bulutları yönet" : "⚙  Manage clouds"
                        onClicked: page.openCloudManager("")
                        background: Rectangle {
                            radius: 10
                            color: manageCloudIntegrationsButton.hovered ? AppTheme.surfaceActive : AppTheme.surfaceRaised
                            border.color: manageCloudIntegrationsButton.hovered ? AppTheme.accentBorder : AppTheme.border
                        }
                        contentItem: Text {
                            text: manageCloudIntegrationsButton.text
                            color: AppTheme.text
                            font.pixelSize: 10
                            font.weight: Font.DemiBold
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }

                GridLayout {
                    id: cloudGrid
                    Layout.fillWidth: true
                    // Responsive cloud cards: 4 → 3 → 2 → 1 as the Discover area narrows.
                    columns: width >= 1160 ? 4 : (width >= 860 ? 3 : (width >= 560 ? 2 : 1))
                    columnSpacing: 12
                    rowSpacing: 12

                    CloudCard {
                        visible: cloudIntegrationPreferences.showGoogle
                        cloudKey: "google"
                        Layout.row: Math.floor(Math.max(0, page.visibleCloudRank("google")) / cloudGrid.columns)
                        Layout.column: Math.max(0, page.visibleCloudRank("google")) % cloudGrid.columns
                        Layout.fillWidth: true
                        Layout.preferredHeight: implicitHeight
                        provider: page.cloudDisplayName("google")
                        iconName: page.cloudDisplayIcon("google")
                        iconSource: AppTheme.discoveryIcon(iconName, page.cloudDefaultIcon(cloudKey))
                        accentColor: "#34a853"
                        statusText: page.oauthStatusText("google")
                        settingsToolTip: lang.language === "tr" ? "Google Drive OAuth ayarları" : "Google Drive OAuth settings"
                        onActionRequested: {
                            if (cloudAuth.googleConnected) {
                                page.browseRequested("gdrive://root")
                            } else if (cloudAuth.googleAuthState === "error") {
                                page.showOAuthError("google")
                            } else if (cloudAuth.googleAuthState === "restoring") {
                                // KWallet refresh is in progress; do not start a second browser flow.
                            } else if (cloudAuth.googleConfigured) {
                                cloudAuth.connectGoogle()
                            } else {
                                page.prepareOAuthSettings("google")
                            }
                        }
                        onSettingsRequested: page.prepareOAuthSettings("google")
                        onEditRequested: function(key, title, icon) { page.editDiscoverItem("cloud", key, title, icon) }
                        onReorderRequested: function(fromKey, toKey) { page.moveCloudProvider(fromKey, toKey) }
                    }

                    CloudCard {
                        visible: cloudIntegrationPreferences.showOneDrive
                        cloudKey: "onedrive"
                        Layout.row: Math.floor(Math.max(0, page.visibleCloudRank("onedrive")) / cloudGrid.columns)
                        Layout.column: Math.max(0, page.visibleCloudRank("onedrive")) % cloudGrid.columns
                        Layout.fillWidth: true
                        Layout.preferredHeight: implicitHeight
                        provider: page.cloudDisplayName("onedrive")
                        iconName: page.cloudDisplayIcon("onedrive")
                        iconSource: AppTheme.discoveryIcon(iconName, page.cloudDefaultIcon(cloudKey))
                        accentColor: "#1688e5"
                        statusText: page.oauthStatusText("onedrive")
                        settingsToolTip: lang.language === "tr" ? "OneDrive OAuth ayarları" : "OneDrive OAuth settings"
                        onActionRequested: {
                            if (cloudAuth.oneDriveConnected)
                                page.browseRequested("onedrive://root")
                            else if (cloudAuth.oneDriveAuthState === "error")
                                page.showOAuthError("onedrive")
                            else if (cloudAuth.oneDriveAuthState === "restoring") {
                                // Stored refresh token is being exchanged silently.
                            } else if (cloudAuth.oneDriveConfigured)
                                cloudAuth.connectOneDrive()
                            else
                                page.prepareOAuthSettings("onedrive")
                        }
                        onSettingsRequested: page.prepareOAuthSettings("onedrive")
                        onEditRequested: function(key, title, icon) { page.editDiscoverItem("cloud", key, title, icon) }
                        onReorderRequested: function(fromKey, toKey) { page.moveCloudProvider(fromKey, toKey) }
                    }

                    CloudCard {
                        visible: cloudIntegrationPreferences.showDropbox
                        cloudKey: "dropbox"
                        Layout.row: Math.floor(Math.max(0, page.visibleCloudRank("dropbox")) / cloudGrid.columns)
                        Layout.column: Math.max(0, page.visibleCloudRank("dropbox")) % cloudGrid.columns
                        Layout.fillWidth: true
                        Layout.preferredHeight: implicitHeight
                        provider: page.cloudDisplayName("dropbox")
                        iconName: page.cloudDisplayIcon("dropbox")
                        iconSource: AppTheme.discoveryIcon(iconName, page.cloudDefaultIcon(cloudKey))
                        accentColor: "#0061ff"
                        statusText: page.externalCloudStatus("dropbox")
                        settingsToolTip: lang.language === "tr" ? "Dropbox konumunu ayarla" : "Configure Dropbox location"
                        onActionRequested: page.openExternalCloud("dropbox")
                        onSettingsRequested: page.openCloudManager("dropbox")
                        onEditRequested: function(key, title, icon) { page.editDiscoverItem("cloud", key, title, icon) }
                        onReorderRequested: function(fromKey, toKey) { page.moveCloudProvider(fromKey, toKey) }
                    }

                    CloudCard {
                        visible: cloudIntegrationPreferences.showNextcloud
                        cloudKey: "nextcloud"
                        Layout.row: Math.floor(Math.max(0, page.visibleCloudRank("nextcloud")) / cloudGrid.columns)
                        Layout.column: Math.max(0, page.visibleCloudRank("nextcloud")) % cloudGrid.columns
                        Layout.fillWidth: true
                        Layout.preferredHeight: implicitHeight
                        provider: page.cloudDisplayName("nextcloud")
                        iconName: page.cloudDisplayIcon("nextcloud")
                        iconSource: AppTheme.discoveryIcon(iconName, page.cloudDefaultIcon(cloudKey))
                        accentColor: "#0082c9"
                        statusText: page.externalCloudStatus("nextcloud")
                        settingsToolTip: lang.language === "tr" ? "Nextcloud WebDAV adresini ayarla" : "Configure Nextcloud WebDAV URL"
                        onActionRequested: page.openExternalCloud("nextcloud")
                        onSettingsRequested: page.openCloudManager("nextcloud")
                        onEditRequested: function(key, title, icon) { page.editDiscoverItem("cloud", key, title, icon) }
                        onReorderRequested: function(fromKey, toKey) { page.moveCloudProvider(fromKey, toKey) }
                    }

                    CloudCard {
                        visible: cloudIntegrationPreferences.showOwnCloud
                        cloudKey: "owncloud"
                        Layout.row: Math.floor(Math.max(0, page.visibleCloudRank("owncloud")) / cloudGrid.columns)
                        Layout.column: Math.max(0, page.visibleCloudRank("owncloud")) % cloudGrid.columns
                        Layout.fillWidth: true
                        Layout.preferredHeight: implicitHeight
                        provider: page.cloudDisplayName("owncloud")
                        iconName: page.cloudDisplayIcon("owncloud")
                        iconSource: AppTheme.discoveryIcon(iconName, page.cloudDefaultIcon(cloudKey))
                        accentColor: "#041e42"
                        statusText: page.externalCloudStatus("owncloud")
                        settingsToolTip: lang.language === "tr" ? "ownCloud WebDAV adresini ayarla" : "Configure ownCloud WebDAV URL"
                        onActionRequested: page.openExternalCloud("owncloud")
                        onSettingsRequested: page.openCloudManager("owncloud")
                        onEditRequested: function(key, title, icon) { page.editDiscoverItem("cloud", key, title, icon) }
                        onReorderRequested: function(fromKey, toKey) { page.moveCloudProvider(fromKey, toKey) }
                    }

                    CloudCard {
                        visible: cloudIntegrationPreferences.showMega
                        cloudKey: "mega"
                        Layout.row: Math.floor(Math.max(0, page.visibleCloudRank("mega")) / cloudGrid.columns)
                        Layout.column: Math.max(0, page.visibleCloudRank("mega")) % cloudGrid.columns
                        Layout.fillWidth: true
                        Layout.preferredHeight: implicitHeight
                        provider: page.cloudDisplayName("mega")
                        iconName: page.cloudDisplayIcon("mega")
                        iconSource: AppTheme.discoveryIcon(iconName, page.cloudDefaultIcon(cloudKey))
                        accentColor: "#d9272e"
                        statusText: page.externalCloudStatus("mega")
                        settingsToolTip: lang.language === "tr" ? "MEGA mount / senkron klasörünü ayarla" : "Configure MEGA mount / sync folder"
                        onActionRequested: page.openExternalCloud("mega")
                        onSettingsRequested: page.openCloudManager("mega")
                        onEditRequested: function(key, title, icon) { page.editDiscoverItem("cloud", key, title, icon) }
                        onReorderRequested: function(fromKey, toKey) { page.moveCloudProvider(fromKey, toKey) }
                    }

                    CloudCard {
                        visible: cloudIntegrationPreferences.showPCloud
                        cloudKey: "pcloud"
                        Layout.row: Math.floor(Math.max(0, page.visibleCloudRank("pcloud")) / cloudGrid.columns)
                        Layout.column: Math.max(0, page.visibleCloudRank("pcloud")) % cloudGrid.columns
                        Layout.fillWidth: true
                        Layout.preferredHeight: implicitHeight
                        provider: page.cloudDisplayName("pcloud")
                        iconName: page.cloudDisplayIcon("pcloud")
                        iconSource: AppTheme.discoveryIcon(iconName, page.cloudDefaultIcon(cloudKey))
                        accentColor: "#1d8fea"
                        statusText: page.externalCloudStatus("pcloud")
                        settingsToolTip: lang.language === "tr" ? "pCloud mount / senkron klasörünü ayarla" : "Configure pCloud mount / sync folder"
                        onActionRequested: page.openExternalCloud("pcloud")
                        onSettingsRequested: page.openCloudManager("pcloud")
                        onEditRequested: function(key, title, icon) { page.editDiscoverItem("cloud", key, title, icon) }
                        onReorderRequested: function(fromKey, toKey) { page.moveCloudProvider(fromKey, toKey) }
                    }

                    CloudCard {
                        visible: cloudIntegrationPreferences.showWebDav
                        cloudKey: "webdav"
                        Layout.row: Math.floor(Math.max(0, page.visibleCloudRank("webdav")) / cloudGrid.columns)
                        Layout.column: Math.max(0, page.visibleCloudRank("webdav")) % cloudGrid.columns
                        Layout.fillWidth: true
                        Layout.preferredHeight: implicitHeight
                        provider: page.cloudDisplayName("webdav")
                        iconName: page.cloudDisplayIcon("webdav")
                        iconSource: AppTheme.discoveryIcon(iconName, page.cloudDefaultIcon(cloudKey))
                        accentColor: "#7b61ff"
                        statusText: page.externalCloudStatus("webdav")
                        settingsToolTip: lang.language === "tr" ? "WebDAV adresini ayarla" : "Configure WebDAV URL"
                        onActionRequested: page.openExternalCloud("webdav")
                        onSettingsRequested: page.openCloudManager("webdav")
                        onEditRequested: function(key, title, icon) { page.editDiscoverItem("cloud", key, title, icon) }
                        onReorderRequested: function(fromKey, toKey) { page.moveCloudProvider(fromKey, toKey) }
                    }

                    Rectangle {
                        visible: page.visibleCloudCount === 0
                        Layout.columnSpan: cloudGrid.columns
                        Layout.fillWidth: true
                        Layout.preferredHeight: 82
                        radius: 14
                        color: AppTheme.surface
                        border.color: AppTheme.border
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 16
                            spacing: 12
                            CrispIcon { source: AppTheme.icon("cloud.svg"); Layout.preferredWidth: 28; Layout.preferredHeight: 28 }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    text: lang.language === "tr" ? "Ana ekranda gösterilen bulut yok" : "No cloud integrations are shown"
                                    color: AppTheme.text
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                }
                                Text {
                                    text: lang.language === "tr" ? "Sağ üstteki Bulutları yönet düğmesinden istediğini açabilirsin." : "Use Manage clouds above to enable the integrations you want."
                                    color: AppTheme.textMuted
                                    font.pixelSize: 10
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: lang.t("quick_access")
                        color: AppTheme.text
                        font.pixelSize: 17
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: lang.language === "tr" ? "Sol menüden buraya sürükle" : "Drag here from the sidebar"
                        color: AppTheme.textMuted
                        font.pixelSize: 10
                    }
                    Item { Layout.fillWidth: true }
                    GButton {
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        id: addShortcutButton
                        text: lang.language === "tr" ? "+  Yeni kısayol" : "+  New shortcut"
                        onClicked: page.addShortcut()
                        background: Rectangle {
                            radius: 10
                            color: addShortcutButton.hovered ? AppTheme.accentHover : AppTheme.accent
                        }
                        contentItem: Text {
                            text: addShortcutButton.text
                            color: "white"
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }

                Rectangle {
                    id: quickAccessPanel
                    Layout.fillWidth: true
                    Layout.preferredHeight: quickGrid.implicitHeight + 28
                    radius: 18
                    color: quickDrop.containsDrag ? AppTheme.accentSoft : AppTheme.surfaceRaised
                    border.color: quickDrop.containsDrag ? AppTheme.accent : AppTheme.border
                    border.width: quickDrop.containsDrag ? 2 : 1

                    DropArea {
                        id: quickDrop
                        anchors.fill: parent
                        onDropped: function(drop) {
                            if (drop.source && drop.source.dragUrl) {
                                page.quickAccessModel.addShortcut(
                                    drop.source.dragUrl,
                                    drop.source.title || "",
                                    drop.source.dragIcon || "folder.svg")
                                drop.acceptProposedAction()
                            }
                        }
                    }

                    GridLayout {
                        id: quickGrid
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 14
                        columns: width > 760 ? 3 : 2
                        columnSpacing: 10
                        rowSpacing: 10

                        Repeater {
                            model: page.quickAccessModel
                            delegate: QuickAccessTile {
                                required property int index
                                required property string shortcutTitle
                                required property string shortcutLocation
                                required property string shortcutIcon
                                required property bool shortcutFixed
                                Layout.fillWidth: true
                                Layout.preferredHeight: Math.max(74, AppTheme.effectiveHomeIconSize + 28)
                                shortcutIndex: index
                                fixedShortcut: shortcutFixed
                                recentLocations: shortcutFixed ? page.quickAccessModel.recentLocations : []
                                title: shortcutFixed
                                       ? (shortcutTitle.length ? shortcutTitle : (lang.language === "tr" ? "Son Konum" : "Last Location"))
                                       : shortcutTitle
                                iconSource: AppTheme.discoveryIcon(shortcutIcon, "folder.svg")
                                systemIconPixels: page.systemHomeIconSize
                                path: shortcutLocation
                                onOpenRequested: function(path) { page.browseRequested(path) }
                                onOpenNewTabRequested: function(path) { page.browseNewTabRequested(path) }
                                onEditRequested: function(index, title, path) {
                                    page.editShortcut(index, title, path, shortcutIcon, shortcutFixed)
                                }
                                onRemoveRequested: function(index) { page.quickAccessModel.removeShortcut(index) }
                                onReorderRequested: function(from, to) { page.quickAccessModel.moveShortcut(from, to) }
                                onExternalDropRequested: function(url, title, icon) {
                                    page.quickAccessModel.addShortcut(url, title, icon)
                                }
                                onPruneRecentRequested: page.quickAccessModel.pruneRecentLocations()
                            }
                        }

                        Rectangle {
                            visible: page.quickAccessModel.count === 0
                            Layout.columnSpan: 3
                            Layout.fillWidth: true
                            Layout.preferredHeight: 86
                            radius: 14
                            color: "transparent"
                            border.color: AppTheme.borderStrong
                            border.width: 1
                            Text {
                                anchors.centerIn: parent
                                text: lang.language === "tr"
                                      ? "İlk kısayolunu sol menüden sürükle veya Yeni kısayol'a bas."
                                      : "Drag your first shortcut from the sidebar or choose New shortcut."
                                color: AppTheme.textMuted
                                font.pixelSize: 11
                            }
                        }
                    }
                }

                /*
                   Favorites remain in the sidebar and can be dragged into Quick Access.
                   Keeping one home collection avoids duplicate shortcut surfaces.
                */
                SectionHeader {
                    Layout.fillWidth: true
                    title: lang.t("disks")
                    actionText: lang.t("refresh")
                    onActionTriggered: page.storageModel.refresh()
                }

                GridLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 6
                    Layout.rightMargin: 6
                    columns: width > 1050 ? 4 : (width > 760 ? 3 : 2)
                    columnSpacing: 12
                    rowSpacing: 12

                    Repeater {
                        model: page.storageModel
                        delegate: DiskCard {
                            diskIndex: index
                            Layout.fillWidth: true
                            Layout.preferredHeight: responsiveHeight
                            title: name
                            stableId: model.stableId
                            iconName: model.icon
                            systemIconPixels: page.systemHomeIconSize
                            subtitle: rootPath + (fileSystem.length ? " · " + fileSystem : "")
                            rootPath: model.rootPath
                            device: model.device
                            mounted: model.mounted
                            canMountToggle: model.canMountToggle
                            mountBusy: model.busy
                            mountActionText: model.busy
                                             ? (model.mounted ? lang.t("unmounting") : lang.t("mounting"))
                                             : (model.canMountToggle ? (model.mounted ? lang.t("unmount") : lang.t("mount")) : "")
                            mountedStatusText: model.mounted ? lang.t("mounted") : lang.t("not_mounted")
                            usedText: mounted && totalBytes > 0 ? page.storageModel.formatBytes(usedBytes)
                                      : (mounted ? "" : lang.t("not_mounted"))
                            totalText: totalBytes > 0 ? page.storageModel.formatBytes(totalBytes)
                                      : (mounted ? (lang.language === "tr" ? "Kapasite bilinmiyor" : "Capacity unavailable") : "")
                            ratio: usedRatio
                            favorite: (page.storageModel.favoriteItems, page.storageModel.isFavorite(model.rootPath))
                            onOpenRequested: function(path) { if (mounted) page.browseRequested(path) }
                            onOpenNewTabRequested: function(path) { if (mounted) page.browseNewTabRequested(path) }
                            onMountToggleRequested: function(devicePath) { page.storageModel.toggleMount(devicePath) }
                            onEditRequested: function(stableId, title, iconName) {
                                page.editDiscoverItem("disk", stableId, title, iconName)
                            }
                            onReorderRequested: function(from, to) { page.storageModel.moveDisk(from, to) }
                            onReorderDragStateChanged: function(active) { page.storageModel.setReorderActive(active) }
                            onFavoriteToggleRequested: function(path, diskTitle) {
                                page.storageModel.toggleFavorite(path)
                            }
                        }
                    }
                }

                Rectangle {
                    id: iconSizePanel
                    Layout.fillWidth: true
                    Layout.preferredHeight: iconSizeContent.implicitHeight + 28
                    radius: AppTheme.cardRadius
                    color: AppTheme.cardSurface
                    border.color: AppTheme.border

                    ColumnLayout {
                        id: iconSizeContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 14
                        spacing: 10

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 9
                            Rectangle {
                                Layout.preferredWidth: 38
                                Layout.preferredHeight: 38
                                radius: 12
                                color: AppTheme.accentSoft
                                border.color: AppTheme.accentBorder
                                CrispIcon {
                                    anchors.centerIn: parent
                                    source: AppTheme.icon("settings.svg")
                                    width: 18
                                    height: 18
                                }
                            }
                            Text {
                                text: lang.language === "tr" ? "İkon boyutları" : "Icon sizes"
                                color: AppTheme.text
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                            }
                            Text {
                                Layout.fillWidth: true
                                text: lang.language === "tr"
                                      ? "Değişiklikler anında uygulanır ve kaydedilir"
                                      : "Changes are applied and saved immediately"
                                color: AppTheme.textMuted
                                font.pixelSize: 10
                                elide: Text.ElideRight
                            }
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: width > 680 ? 2 : 1
                            columnSpacing: 10
                            rowSpacing: 8

                            IconSizeControl {
                                Layout.fillWidth: true
                                title: lang.language === "tr" ? "Sol menü ikonları" : "Sidebar icons"
                                from: page.systemIconMinimum
                                to: page.systemIconMaximum
                                allowedValues: []
                                value: AppTheme.effectiveSidebarIconSize
                                onEditingStarted: page.beginIconResize()
                                onEditingFinished: page.endIconResize()
                                onValueEdited: function(size) {
                                    AppTheme.setSystemSidebarIconSize(size)
                                    page.keepIconResizePanelPinned()
                                }
                            }

                            IconSizeControl {
                                Layout.fillWidth: true
                                title: lang.language === "tr" ? "Ana alan ikonları" : "Home area icons"
                                from: page.systemIconMinimum
                                to: page.systemIconMaximum
                                allowedValues: []
                                value: AppTheme.effectiveHomeIconSize
                                onEditingStarted: page.beginIconResize()
                                onEditingFinished: page.endIconResize()
                                onValueEdited: function(size) {
                                    AppTheme.setSystemHomeIconSize(size)
                                    page.keepIconResizePanelPinned()
                                }
                            }
                        }
                    }
                }

                Item { Layout.preferredHeight: 12 }
            }
        }

        Rectangle {
            visible: page.infoPanelOpen
            Layout.preferredWidth: visible ? 304 : 0
            Layout.minimumWidth: visible ? 304 : 0
            Layout.maximumWidth: visible ? 304 : 0
            Layout.fillHeight: true
            radius: AppTheme.panelRadius
            color: AppTheme.cardSurface
            border.color: AppTheme.border

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 22
                spacing: 16

                RowLayout {
                    CrispIcon {
                        source: AppTheme.icon("logo.png")
                        Layout.preferredWidth: 40
                        Layout.preferredHeight: 40
                    }
                    ColumnLayout {
                        Text { text: "g-File"; color: AppTheme.text; font.pixelSize: 16; font.bold: true }
                        Text { text: "v0.5.10"; color: AppTheme.textMuted; font.pixelSize: 10 }
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: AppTheme.border }

                Text { text: lang.t("storage_overview"); color: AppTheme.text; font.pixelSize: 12; font.bold: true }
                RowLayout {
                    Text { text: storageModel.formatBytes(storageModel.usedBytes); color: AppTheme.text; font.bold: true; font.pixelSize: 11 }
                    Item { Layout.fillWidth: true }
                    Text { text: storageModel.formatBytes(storageModel.totalBytes); color: AppTheme.textMuted; font.pixelSize: 10 }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 7
                    radius: 4
                    color: AppTheme.surfaceHover
                    Rectangle {
                        width: parent.width * (storageModel.totalBytes > 0 ? storageModel.usedBytes / storageModel.totalBytes : 0)
                        height: parent.height
                        radius: 4
                        color: AppTheme.accent
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: AppTheme.border }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 9
                    Text { text: (lang.language === "tr" ? "Diskler" : "Disks") + ": " + storageModel.count; color: AppTheme.textMuted; font.pixelSize: 11 }
                    Text { text: (lang.language === "tr" ? "Favoriler" : "Favorites") + ": " + favoritesModel.count; color: AppTheme.textMuted; font.pixelSize: 11 }
                    Text { text: "SMB · SFTP · admin://"; color: AppTheme.textMuted; font.pixelSize: 11 }
                    Text { text: "Google Drive · OneDrive · Dropbox · Nextcloud · ownCloud · MEGA · pCloud · WebDAV"; color: AppTheme.textMuted; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: AppTheme.border }

                Text { text: lang.language === "tr" ? "Özellikler" : "Features"; color: AppTheme.text; font.pixelSize: 12; font.bold: true }

                Flow {
                    Layout.fillWidth: true
                    spacing: 7
                    Repeater {
                        model: ["KIO", "Tabs", "Split", "Drag & Drop", "Thumbnails", "ServiceMenus", "OAuth2"]
                        delegate: Rectangle {
                            required property string modelData
                            width: tagText.implicitWidth + 16
                            height: 26
                            radius: 9
                            color: AppTheme.surfaceRaised
                            border.color: AppTheme.border
                            Text { id: tagText; anchors.centerIn: parent; text: modelData; color: AppTheme.textMuted; font.pixelSize: 9 }
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 86
                    radius: 14
                    color: AppTheme.surfaceRaised
                    border.color: AppTheme.border
                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 5
                        Text { Layout.alignment: Qt.AlignHCenter; text: lang.language === "tr" ? "Yerel + Ağ + Bulut" : "Local + Network + Cloud"; color: AppTheme.text; font.pixelSize: 11; font.bold: true }
                        Text { Layout.alignment: Qt.AlignHCenter; text: lang.language === "tr" ? "Tek dosya yöneticisinde" : "in one file manager"; color: AppTheme.textMuted; font.pixelSize: 10 }
                    }
                }
            }
        }
    }

    component CloudManageRow: Rectangle {
        id: cloudManageRow
        property string providerName: ""
        property string providerDetail: ""
        property string iconSource: ""
        property color accentColor: AppTheme.accent
        property bool integrationEnabled: false
        property bool hasLocation: false
        property string locationText: ""
        property string locationPlaceholder: ""
        property string actionText: ""
        property bool emphasized: false
        signal integrationToggled(bool enabled)
        signal locationEdited(string value)
        signal actionRequested()

        implicitHeight: hasLocation ? 116 : 72
        radius: 18
        color: emphasized ? AppTheme.surfaceActive : AppTheme.cardSurface
        border.width: emphasized ? 2 : 1
        border.color: emphasized ? AppTheme.accent : AppTheme.border

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Rectangle {
                    Layout.preferredWidth: 36
                    Layout.preferredHeight: 36
                    radius: 10
                    color: AppTheme.surface
                    border.color: AppTheme.border
                    CrispIcon {
                        anchors.centerIn: parent
                        source: cloudManageRow.iconSource
                        width: 23
                        height: 23
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Text {
                        text: cloudManageRow.providerName
                        color: AppTheme.text
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }
                    Text {
                        Layout.fillWidth: true
                        text: cloudManageRow.providerDetail
                        color: AppTheme.textMuted
                        font.pixelSize: 9
                        elide: Text.ElideRight
                    }
                }

                GButton {
                    id: cloudRowActionButton
                    visible: cloudManageRow.actionText.length > 0
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    text: cloudManageRow.actionText
                    onClicked: cloudManageRow.actionRequested()
                    background: Rectangle {
                        radius: 9
                        color: cloudRowActionButton.hovered ? AppTheme.surfaceActive : AppTheme.surface
                        border.color: AppTheme.border
                    }
                    contentItem: Text {
                        text: cloudRowActionButton.text
                        color: AppTheme.textMuted
                        font.pixelSize: 9
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                GSwitch {
                    id: cloudRowSwitch
                    checked: cloudManageRow.integrationEnabled
                    onToggled: cloudManageRow.integrationToggled(checked)
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    GToolTip {
                        text: cloudRowSwitch.checked
                              ? (lang.language === "tr" ? "Ana ekranda gösteriliyor" : "Shown on Home")
                              : (lang.language === "tr" ? "Ana ekranda gizli" : "Hidden from Home")
                    }
                }
            }

            GTextField {
                id: cloudLocationField
                visible: cloudManageRow.hasLocation
                Layout.fillWidth: true
                text: cloudManageRow.locationText
                placeholderText: cloudManageRow.locationPlaceholder
                placeholderTextColor: AppTheme.textFaint
                color: AppTheme.text
                selectByMouse: true
                font.pixelSize: 10
                onTextEdited: cloudManageRow.locationEdited(text)
                background: Rectangle {
                    radius: 9
                    color: AppTheme.surface
                    border.color: cloudLocationField.activeFocus ? cloudManageRow.accentColor : AppTheme.border
                }
            }
        }
    }

    GModalPopup {
        id: cloudManagerPopup
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        focus: true
        width: Math.min(720, page.width - 60)
        height: Math.min(760, page.height - 50)
        padding: 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onClosed: page.cloudManagerFocus = ""
        background: GModalSurface { }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 13

            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Rectangle {
                    Layout.preferredWidth: 40
                    Layout.preferredHeight: 40
                    radius: 12
                    color: AppTheme.surfaceRaised
                    border.color: AppTheme.border
                    CrispIcon {
                        anchors.centerIn: parent
                        source: AppTheme.icon("cloud.svg")
                        width: 25
                        height: 25
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Text {
                        text: lang.language === "tr" ? "Bulut entegrasyonlarını yönet" : "Manage cloud integrations"
                        color: AppTheme.text
                        font.pixelSize: 17
                        font.weight: Font.DemiBold
                    }
                    Text {
                        Layout.fillWidth: true
                        text: lang.language === "tr"
                              ? "Yalnızca kullandıklarını aç. Pasif servisler Keşfet ana ekranında yer kaplamaz."
                              : "Enable only what you use. Disabled services do not take space on the Discover home page."
                        color: AppTheme.textMuted
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                    }
                }
                GToolButton {
                    id: closeCloudManagerXButton
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    text: ""
                    display: AbstractButton.IconOnly
                    icon.source: AppTheme.icon("close-ui.svg")
                    icon.width: 16
                    icon.height: 16
                    onClicked: cloudManagerPopup.close()
                    contentItem: Text {
                        text: closeCloudManagerXButton.text
                        color: AppTheme.textMuted
                        font.pixelSize: 22
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 9
                        color: closeCloudManagerXButton.hovered ? AppTheme.surfaceActive : "transparent"
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: AppTheme.border }

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth

                ColumnLayout {
                    width: parent.width
                    spacing: 9

                    CloudManageRow {
                        Layout.fillWidth: true
                        providerName: lang.t("google_drive")
                        providerDetail: lang.language === "tr" ? "Yerleşik OAuth2 bağlantısı" : "Built-in OAuth2 connection"
                        iconSource: AppTheme.icon("cloud-google.svg")
                        accentColor: "#34a853"
                        integrationEnabled: cloudIntegrationPreferences.showGoogle
                        emphasized: page.cloudManagerFocus === "google"
                        actionText: lang.language === "tr" ? "OAuth ayarları" : "OAuth settings"
                        onIntegrationToggled: function(enabled) { cloudIntegrationPreferences.showGoogle = enabled }
                        onActionRequested: { cloudManagerPopup.close(); page.prepareOAuthSettings("google") }
                    }

                    CloudManageRow {
                        Layout.fillWidth: true
                        providerName: lang.t("onedrive")
                        providerDetail: lang.language === "tr" ? "Yerleşik Microsoft OAuth2 bağlantısı" : "Built-in Microsoft OAuth2 connection"
                        iconSource: AppTheme.icon("cloud-onedrive.svg")
                        accentColor: "#1688e5"
                        integrationEnabled: cloudIntegrationPreferences.showOneDrive
                        emphasized: page.cloudManagerFocus === "onedrive"
                        actionText: lang.language === "tr" ? "OAuth ayarları" : "OAuth settings"
                        onIntegrationToggled: function(enabled) { cloudIntegrationPreferences.showOneDrive = enabled }
                        onActionRequested: { cloudManagerPopup.close(); page.prepareOAuthSettings("onedrive") }
                    }

                    CloudManageRow {
                        Layout.fillWidth: true
                        providerName: "Dropbox"
                        providerDetail: lang.language === "tr" ? "Dropbox istemcisinin senkron klasörünü açar" : "Opens the Dropbox client's sync folder"
                        iconSource: AppTheme.icon("cloud-dropbox.svg")
                        accentColor: "#0061ff"
                        integrationEnabled: cloudIntegrationPreferences.showDropbox
                        hasLocation: true
                        locationText: cloudIntegrationPreferences.dropboxLocation
                        locationPlaceholder: "~/Dropbox"
                        emphasized: page.cloudManagerFocus === "dropbox"
                        onIntegrationToggled: function(enabled) { cloudIntegrationPreferences.showDropbox = enabled }
                        onLocationEdited: function(value) { cloudIntegrationPreferences.dropboxLocation = value }
                    }

                    CloudManageRow {
                        Layout.fillWidth: true
                        providerName: "Nextcloud"
                        providerDetail: lang.language === "tr" ? "KIO üzerinden WebDAV/WebDAVS" : "WebDAV/WebDAVS through KIO"
                        iconSource: AppTheme.icon("cloud-nextcloud.svg")
                        accentColor: "#0082c9"
                        integrationEnabled: cloudIntegrationPreferences.showNextcloud
                        hasLocation: true
                        locationText: cloudIntegrationPreferences.nextcloudLocation
                        locationPlaceholder: "https://cloud.example.com/remote.php/dav/files/kullanici/"
                        emphasized: page.cloudManagerFocus === "nextcloud"
                        onIntegrationToggled: function(enabled) { cloudIntegrationPreferences.showNextcloud = enabled }
                        onLocationEdited: function(value) { cloudIntegrationPreferences.nextcloudLocation = value }
                    }

                    CloudManageRow {
                        Layout.fillWidth: true
                        providerName: "ownCloud"
                        providerDetail: lang.language === "tr" ? "KIO üzerinden WebDAV/WebDAVS" : "WebDAV/WebDAVS through KIO"
                        iconSource: AppTheme.icon("cloud-owncloud.svg")
                        accentColor: "#041e42"
                        integrationEnabled: cloudIntegrationPreferences.showOwnCloud
                        hasLocation: true
                        locationText: cloudIntegrationPreferences.ownCloudLocation
                        locationPlaceholder: "https://cloud.example.com/remote.php/dav/files/kullanici/"
                        emphasized: page.cloudManagerFocus === "owncloud"
                        onIntegrationToggled: function(enabled) { cloudIntegrationPreferences.showOwnCloud = enabled }
                        onLocationEdited: function(value) { cloudIntegrationPreferences.ownCloudLocation = value }
                    }

                    CloudManageRow {
                        Layout.fillWidth: true
                        providerName: "MEGA"
                        providerDetail: lang.language === "tr" ? "MEGA Desktop / FUSE mount klasörünü açar" : "Opens a MEGA Desktop / FUSE mount folder"
                        iconSource: AppTheme.icon("cloud-mega.svg")
                        accentColor: "#d9272e"
                        integrationEnabled: cloudIntegrationPreferences.showMega
                        hasLocation: true
                        locationText: cloudIntegrationPreferences.megaLocation
                        locationPlaceholder: "~/MEGA"
                        emphasized: page.cloudManagerFocus === "mega"
                        onIntegrationToggled: function(enabled) { cloudIntegrationPreferences.showMega = enabled }
                        onLocationEdited: function(value) { cloudIntegrationPreferences.megaLocation = value }
                    }

                    CloudManageRow {
                        Layout.fillWidth: true
                        providerName: "pCloud"
                        providerDetail: lang.language === "tr" ? "pCloud Drive mount / senkron klasörünü açar" : "Opens a pCloud Drive mount / sync folder"
                        iconSource: AppTheme.icon("cloud-pcloud.svg")
                        accentColor: "#1d8fea"
                        integrationEnabled: cloudIntegrationPreferences.showPCloud
                        hasLocation: true
                        locationText: cloudIntegrationPreferences.pCloudLocation
                        locationPlaceholder: "~/pCloudDrive"
                        emphasized: page.cloudManagerFocus === "pcloud"
                        onIntegrationToggled: function(enabled) { cloudIntegrationPreferences.showPCloud = enabled }
                        onLocationEdited: function(value) { cloudIntegrationPreferences.pCloudLocation = value }
                    }

                    CloudManageRow {
                        Layout.fillWidth: true
                        providerName: "WebDAV"
                        providerDetail: lang.language === "tr" ? "Herhangi bir standart WebDAV sunucusu" : "Any standard WebDAV server"
                        iconSource: AppTheme.icon("network.svg")
                        accentColor: "#7b61ff"
                        integrationEnabled: cloudIntegrationPreferences.showWebDav
                        hasLocation: true
                        locationText: cloudIntegrationPreferences.webDavLocation
                        locationPlaceholder: "https://sunucu.example.com/dav/"
                        emphasized: page.cloudManagerFocus === "webdav"
                        onIntegrationToggled: function(enabled) { cloudIntegrationPreferences.showWebDav = enabled }
                        onLocationEdited: function(value) { cloudIntegrationPreferences.webDavLocation = value }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 70
                        radius: 12
                        color: AppTheme.surfaceRaised
                        border.color: AppTheme.border
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 9
                            CrispIcon { source: AppTheme.icon("info.svg"); Layout.preferredWidth: 20; Layout.preferredHeight: 20 }
                            Text {
                                Layout.fillWidth: true
                                text: lang.language === "tr"
                                      ? "Nextcloud, ownCloud ve WebDAV adreslerinde https:// kullanabilirsin; g-File bunu KIO için webdavs:// biçimine dönüştürür. Kullanıcı adı/parolayı URL'ye yazma; KIO gerektiğinde kimlik bilgilerini ister ve KDE Wallet ile saklayabilir."
                                      : "You may enter https:// URLs for Nextcloud, ownCloud and WebDAV; g-File converts them to webdavs:// for KIO. Do not put credentials in the URL; KIO can request them when needed and store them with KDE Wallet."
                                color: AppTheme.textMuted
                                font.pixelSize: 9
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                GButton {
                    id: closeCloudManagerButton
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    text: lang.language === "tr" ? "Tamam" : "Done"
                    onClicked: cloudManagerPopup.close()
                    background: Rectangle {
                        radius: 10
                        color: closeCloudManagerButton.hovered ? AppTheme.accentHover : AppTheme.accent
                    }
                    contentItem: Text {
                        text: closeCloudManagerButton.text
                        color: "white"
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }
    }

    GModalPopup {
        id: shortcutDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        focus: true
        width: Math.min(520, Overlay.overlay.width - 48)
        height: Math.min(Math.max(420, shortcutDialogContent.implicitHeight + padding * 2),
                         Overlay.overlay.height - 48)
        padding: 22
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: GModalSurface { }

        ColumnLayout {
            id: shortcutDialogContent
            anchors.fill: parent
            spacing: 13

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 82
                radius: 17
                color: AppTheme.surfaceRaised
                border.color: AppTheme.border

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 13
                    spacing: 13

                    Rectangle {
                        Layout.preferredWidth: 54
                        Layout.preferredHeight: 54
                        radius: 16
                        color: AppTheme.accentSoft
                        border.color: AppTheme.accentBorder
                        CrispIcon {
                            anchors.centerIn: parent
                            source: AppTheme.systemIconAtSize(AppTheme.customIcon(page.editingShortcutIcon), 34)
                            width: 34
                            height: 34
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        Text {
                            text: page.editingShortcutIndex < 0
                                  ? (lang.language === "tr" ? "Yeni hızlı erişim" : "New quick access")
                                  : (page.editingShortcutFixed
                                     ? (lang.language === "tr" ? "Son Konumu düzenle" : "Edit Last Location")
                                     : (lang.language === "tr" ? "Hızlı erişimi düzenle" : "Edit quick access"))
                            color: AppTheme.text
                            font.pixelSize: 18
                            font.weight: Font.DemiBold
                        }
                        Text {
                            Layout.fillWidth: true
                            text: page.editingShortcutFixed
                                  ? (lang.language === "tr" ? "Adı ve simgeyi özelleştir" : "Customize its name and icon")
                                  : (lang.language === "tr" ? "Adı, konumu ve simgeyi özelleştir" : "Customize its name, location, and icon")
                            color: AppTheme.textMuted
                            font.pixelSize: 10
                            elide: Text.ElideRight
                        }
                    }

                    GToolButton {
                        id: shortcutCloseButton
                        Layout.preferredWidth: 34
                        Layout.preferredHeight: 34
                        Layout.alignment: Qt.AlignTop
                        text: ""
                    display: AbstractButton.IconOnly
                    icon.source: AppTheme.icon("close-ui.svg")
                    icon.width: 16
                    icon.height: 16
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        onClicked: shortcutDialog.close()
                        background: Rectangle {
                            radius: 10
                            color: shortcutCloseButton.hovered ? AppTheme.surfaceHover : "transparent"
                        }
                        contentItem: Text {
                            text: shortcutCloseButton.text
                            color: AppTheme.textMuted
                            font.pixelSize: 21
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }

            Text { text: lang.language === "tr" ? "AD" : "NAME"; color: AppTheme.textFaint; font.pixelSize: 9; font.bold: true; font.letterSpacing: 1 }
            GTextField {
                id: shortcutNameInput
                Layout.fillWidth: true
                color: AppTheme.text
                placeholderText: lang.language === "tr" ? "Kısayol adı" : "Shortcut name"
                placeholderTextColor: AppTheme.textFaint
                background: Rectangle { radius: 11; color: AppTheme.surfaceRaised; border.color: shortcutNameInput.activeFocus ? AppTheme.accent : AppTheme.border }
            }

            Text {
                visible: !page.editingShortcutFixed
                text: lang.language === "tr" ? "KONUM" : "LOCATION"
                color: AppTheme.textFaint
                font.pixelSize: 9
                font.bold: true
                font.letterSpacing: 1
            }
            GTextField {
                id: shortcutLocationInput
                visible: !page.editingShortcutFixed
                Layout.fillWidth: true
                color: AppTheme.text
                placeholderText: "/home/user/Folder"
                placeholderTextColor: AppTheme.textFaint
                selectByMouse: true
                background: Rectangle { radius: 11; color: AppTheme.surfaceRaised; border.color: shortcutLocationInput.activeFocus ? AppTheme.accent : AppTheme.border }
            }

            Text { text: lang.language === "tr" ? "SİMGE" : "ICON"; color: AppTheme.textFaint; font.pixelSize: 9; font.bold: true; font.letterSpacing: 1 }
            GButton {
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                visible: AppTheme.useSystemIcons
                Layout.fillWidth: true
                text: lang.language === "tr" ? "Plasma sistem ikonlarından seç…" : "Choose from Plasma system icons…"
                onClicked: {
                    var initial = page.editingShortcutIcon.indexOf("system:") === 0
                            ? page.editingShortcutIcon.substring(7)
                            : AppTheme.systemIconName(page.editingShortcutIcon)
                    var chosen = shortcutIconPicker.chooseSystemIcon(initial,
                        lang.language === "tr" ? "Hızlı erişim ikonu seç" : "Choose quick access icon")
                    if (chosen && chosen.length)
                        page.editingShortcutIcon = "system:" + chosen
                }
            }

            GridLayout {
                visible: !AppTheme.useSystemIcons
                Layout.fillWidth: true
                columns: 7
                columnSpacing: 8
                rowSpacing: 8
                Repeater {
                    model: page.shortcutIcons
                    delegate: Rectangle {
                        required property string modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: 44
                        radius: 11
                        color: page.editingShortcutIcon === modelData ? AppTheme.accentSoft : AppTheme.surfaceRaised
                        border.color: page.editingShortcutIcon === modelData ? AppTheme.accent : AppTheme.border
                        CrispIcon {
                            anchors.centerIn: parent
                            source: AppTheme.icon(modelData)
                            width: 23
                            height: 23
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: page.editingShortcutIcon = modelData
                        }
                    }
                }
            }

            Text {
                visible: page.shortcutError.length > 0
                Layout.fillWidth: true
                text: page.shortcutError
                color: AppTheme.danger
                font.pixelSize: 10
            }

            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 2
                spacing: 8
                Item { Layout.fillWidth: true }
                GModalButton {
                    text: lang.t("cancel")
                    onClicked: shortcutDialog.close()
                }
                GModalButton {
                    id: shortcutSaveButton
                    primary: true
                    text: lang.t("save")
                    onClicked: {
                        var ok = page.editingShortcutIndex < 0
                                 ? page.quickAccessModel.addShortcut(shortcutLocationInput.text,
                                                                     shortcutNameInput.text,
                                                                     page.editingShortcutIcon)
                                 : page.quickAccessModel.updateShortcut(page.editingShortcutIndex,
                                                                        shortcutNameInput.text,
                                                                        shortcutLocationInput.text,
                                                                        page.editingShortcutIcon)
                        if (ok)
                            shortcutDialog.close()
                        else
                            page.shortcutError = lang.language === "tr"
                                    ? "Konum boş ya da bu kısayol zaten mevcut."
                                    : "The location is empty or already exists."
                    }
                }
            }
        }
    }

    GModalPopup {
        id: discoverItemEditDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        focus: true
        width: Math.min(500, Overlay.overlay.width - 48)
        height: Math.min(Math.max(360, discoverItemEditContent.implicitHeight + padding * 2),
                         Overlay.overlay.height - 48)
        padding: 22
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: GModalSurface { }

        ColumnLayout {
            id: discoverItemEditContent
            anchors.fill: parent
            spacing: 13

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 82
                radius: 17
                color: AppTheme.surfaceRaised
                border.color: AppTheme.border

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 13
                    spacing: 13

                    Rectangle {
                        Layout.preferredWidth: 54
                        Layout.preferredHeight: 54
                        radius: 16
                        color: AppTheme.accentSoft
                        border.color: AppTheme.accentBorder
                        CrispIcon {
                            anchors.centerIn: parent
                            source: AppTheme.systemIconAtSize(AppTheme.customIcon(page.editingDiscoverIcon), 34)
                            width: 34
                            height: 34
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        Text {
                            text: lang.language === "tr" ? "Öğeyi düzenle" : "Edit item"
                            color: AppTheme.text
                            font.pixelSize: 18
                            font.weight: Font.DemiBold
                        }
                        Text {
                            Layout.fillWidth: true
                            text: lang.language === "tr"
                                  ? "Keşfet kartının adını ve simgesini özelleştir"
                                  : "Customize the Discover card name and icon"
                            color: AppTheme.textMuted
                            font.pixelSize: 10
                            elide: Text.ElideRight
                        }
                    }

                    Item {
                        id: discoverEditClose
                        readonly property bool hovered: discoverEditCloseMouse.containsMouse
                        Layout.preferredWidth: 34
                        Layout.preferredHeight: 34
                        Layout.alignment: Qt.AlignTop
                        Rectangle {
                            anchors.fill: parent
                            radius: 10
                            color: discoverEditClose.hovered ? AppTheme.surfaceHover : "transparent"
                        }
                        CrispIcon {
                            anchors.centerIn: parent
                            width: 18
                            height: 18
                            source: AppTheme.icon("close-ui.svg")
                            opacity: 0.9
                        }
                        MouseArea {
                            id: discoverEditCloseMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: discoverItemEditDialog.close()
                        }
                    }
                }
            }

            Text { text: lang.language === "tr" ? "AD" : "NAME"; color: AppTheme.textFaint; font.pixelSize: 9; font.bold: true; font.letterSpacing: 1 }
            GTextField {
                id: discoverEditNameInput
                Layout.fillWidth: true
                color: AppTheme.text
                placeholderText: lang.language === "tr" ? "Görünen ad" : "Display name"
                placeholderTextColor: AppTheme.textFaint
                selectByMouse: true
                background: Rectangle { radius: 11; color: AppTheme.surfaceRaised; border.color: discoverEditNameInput.activeFocus ? AppTheme.accent : AppTheme.border }
            }

            Text { text: lang.language === "tr" ? "SİMGE" : "ICON"; color: AppTheme.textFaint; font.pixelSize: 9; font.bold: true; font.letterSpacing: 1 }
            GButton {
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                visible: AppTheme.useSystemIcons
                Layout.fillWidth: true
                text: lang.language === "tr" ? "Plasma sistem ikonlarından seç…" : "Choose from Plasma system icons…"
                onClicked: {
                    var initial = page.editingDiscoverIcon.indexOf("system:") === 0
                            ? page.editingDiscoverIcon.substring(7)
                            : AppTheme.systemIconName(page.editingDiscoverIcon)
                    var chosen = shortcutIconPicker.chooseSystemIcon(initial,
                        lang.language === "tr" ? "Öğe ikonu seç" : "Choose item icon")
                    if (chosen && chosen.length)
                        page.editingDiscoverIcon = "system:" + chosen
                }
            }

            GridLayout {
                visible: !AppTheme.useSystemIcons
                Layout.fillWidth: true
                columns: 7
                columnSpacing: 8
                rowSpacing: 8
                Repeater {
                    model: page.shortcutIcons
                    delegate: Rectangle {
                        required property string modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: 44
                        radius: 11
                        color: page.editingDiscoverIcon === modelData ? AppTheme.accentSoft : AppTheme.surfaceRaised
                        border.color: page.editingDiscoverIcon === modelData ? AppTheme.accent : AppTheme.border
                        CrispIcon {
                            anchors.centerIn: parent
                            source: AppTheme.icon(modelData)
                            width: 23
                            height: 23
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: page.editingDiscoverIcon = modelData
                        }
                    }
                }
            }

            Text {
                visible: page.discoverEditError.length > 0
                Layout.fillWidth: true
                text: page.discoverEditError
                color: AppTheme.danger
                font.pixelSize: 10
            }

            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Item { Layout.fillWidth: true }
                GModalButton {
                    text: lang.t("cancel")
                    onClicked: discoverItemEditDialog.close()
                }
                GModalButton {
                    primary: true
                    text: lang.t("save")
                    onClicked: {
                        var title = String(discoverEditNameInput.text || "").trim()
                        if (!title.length) {
                            page.discoverEditError = lang.language === "tr" ? "Ad boş bırakılamaz." : "Name cannot be empty."
                            return
                        }
                        var ok = false
                        if (page.editingDiscoverKind === "category")
                            ok = page.contentIndexModel.updateCategoryAppearance(page.editingDiscoverKey, title, page.editingDiscoverIcon)
                        else if (page.editingDiscoverKind === "disk")
                            ok = page.storageModel.updateDiskAppearance(page.editingDiscoverKey, title, page.editingDiscoverIcon)
                        else if (page.editingDiscoverKind === "cloud") {
                            page.setCloudAppearance(page.editingDiscoverKey, title, page.editingDiscoverIcon)
                            ok = true
                        }
                        if (ok)
                            discoverItemEditDialog.close()
                        else
                            page.discoverEditError = lang.language === "tr" ? "Değişiklik kaydedilemedi." : "Could not save the changes."
                    }
                }
            }
        }
    }

    GModalPopup {
        id: oauthDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        focus: true
        width: 520
        height: page.oauthProvider === "google" ? 292 : 390
        padding: 20
        background: GModalSurface { }
        ColumnLayout {
            anchors.fill: parent
            spacing: 12
            Text { text: page.oauthProvider === "google" ? "Google Drive OAuth" : "OneDrive OAuth"; font.pixelSize: 16; font.bold: true; color: AppTheme.text }
            Text {
                text: page.oauthProvider === "google"
                      ? (lang.language === "tr"
                         ? "Desktop OAuth istemcisinin Client ID'sini gir. İndirilen JSON'da client_secret varsa onu da ekleyebilirsin; Google masaüstü akışında bu alan opsiyoneldir."
                         : "Enter the Desktop OAuth client's Client ID. If the downloaded JSON contains client_secret, you can add it too; Google marks it optional for desktop token exchange.")
                      : (lang.language === "tr"
                         ? "Uygulama (istemci) kimliğini gir. Kiracı alanı Client ID'nin yerine geçmez; yalnızca hangi Microsoft hesaplarının giriş yapacağını belirler. Sistem tarayıcısı için Entra > Authentication > Mobile and desktop applications bölümüne http://localhost redirect URI'sini ekle."
                         : "Enter the Application (client) ID. The tenant value does not replace the Client ID; it only selects which Microsoft accounts can sign in. For the system-browser flow, register http://localhost under Entra > Authentication > Mobile and desktop applications.")
                color: AppTheme.textMuted
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            GTextField { id: clientIdInput; Layout.fillWidth: true; placeholderText: "Application (client) ID" }
            GTextField {
                id: oneDriveTenantInput
                visible: page.oauthProvider === "onedrive"
                Layout.fillWidth: true
                placeholderText: "Tenant: common / consumers / organizations / GUID"
            }
            Text {
                visible: page.oauthProvider === "onedrive"
                Layout.fillWidth: true
                text: (lang.language === "tr" ? "Kayıtlı redirect URI: " : "Registered redirect URI: ") + cloudAuth.oneDriveRedirectUri
                color: AppTheme.accent
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
            GTextField {
                id: googleSecretInput
                visible: page.oauthProvider === "google"
                Layout.fillWidth: true
                placeholderText: "Client Secret (optional)"
                echoMode: TextInput.Password
            }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                GModalButton {
                    visible: page.oauthProvider === "onedrive" && cloudAuth.oneDriveConfigured
                    text: lang.language === "tr" ? "Sıfırla" : "Reset"
                    onClicked: {
                        cloudAuth.resetOneDriveConfiguration()
                        clientIdInput.text = ""
                        oneDriveTenantInput.text = "common"
                    }
                }
                GModalButton { text: lang.t("cancel"); onClicked: oauthDialog.close() }
                GModalButton {
                    primary: true
                    text: lang.t("save")
                    onClicked: {
                        if (page.oauthProvider === "google") {
                            cloudAuth.googleClientId = clientIdInput.text
                            cloudAuth.googleClientSecret = googleSecretInput.text
                        } else {
                            cloudAuth.oneDriveClientId = clientIdInput.text
                            cloudAuth.oneDriveTenant = oneDriveTenantInput.text
                        }
                        oauthDialog.close()
                    }
                }
            }
        }
    }

    GModalPopup {
        id: oauthErrorPopup
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        focus: true
        width: 560
        implicitHeight: oauthErrorColumn.implicitHeight + 40
        padding: 20
        background: GModalSurface { }

        ColumnLayout {
            id: oauthErrorColumn
            width: parent.width
            spacing: 12
            Text {
                text: page.oauthProvider === "google"
                    ? (lang.language === "tr" ? "Google Drive bağlantı hatası" : "Google Drive connection error")
                    : (lang.language === "tr" ? "OneDrive bağlantı hatası" : "OneDrive connection error")
                color: AppTheme.text
                font.pixelSize: 17
                font.bold: true
            }
            Text {
                Layout.fillWidth: true
                text: page.oauthErrorText
                color: AppTheme.textMuted
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: AppTheme.border }
            Text {
                Layout.fillWidth: true
                visible: true
                text: page.oauthProvider === "google"
                      ? (lang.language === "tr"
                         ? "Client türü Desktop app olmalı. Hata invalid_client veya client_secret ile ilgiliyse Google Cloud'dan Desktop client JSON'unu indirip Client ID ve varsa Client Secret alanlarını g-File OAuth ayarlarına gir."
                         : "The client type must be Desktop app. If the error mentions invalid_client or client_secret, download the Desktop client JSON and enter its Client ID and optional Client Secret in g-File.")
                      : (lang.language === "tr"
                         ? "OneDrive için Application (client) ID kullanılır. Entra > Authentication > Add a platform > Mobile and desktop applications altında http://localhost redirect URI'sini kaydet. Tenant genelde common bırakılabilir; kişisel hesap için consumers, yalnızca iş/okul hesapları için organizations kullanabilirsin."
                         : "OneDrive uses the Application (client) ID. Under Entra > Authentication > Add a platform > Mobile and desktop applications, register http://localhost. Tenant can usually stay common; use consumers for personal accounts or organizations for work/school accounts only.")
                color: AppTheme.textMuted
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                GModalButton {
                    visible: true
                    text: lang.language === "tr" ? "OAuth ayarları" : "OAuth settings"
                    onClicked: {
                        var provider = page.oauthProvider
                        oauthErrorPopup.close()
                        page.prepareOAuthSettings(provider)
                    }
                }
                GModalButton {
                    primary: true
                    text: lang.t("close")
                    onClicked: oauthErrorPopup.close()
                }
            }
        }
    }


    Connections {
        target: cloudAuth
        function onAuthError(provider, message) {
            page.oauthProvider = provider
            if (provider === "google" && (message.length === 0 || message.indexOf("access_denied") >= 0)) {
                page.oauthErrorText = lang.language === "tr"
                    ? "403 access_denied: Bu hesap OAuth uygulamasını kullanmaya yetkili değil. Uygulama test modundaysa hesabın Test users listesinde olması gerekir."
                    : "403 access_denied: This account is not authorized to use the OAuth app. If the app is in testing, the account must be listed as a Test user."
            } else {
                page.oauthErrorText = message
            }
            oauthErrorPopup.open()
        }
    }
}
