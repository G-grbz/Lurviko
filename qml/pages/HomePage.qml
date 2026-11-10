import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtCore
import Lurviko.App
import Lurviko.Backend
import "../components"

Item {
    id: page
    required property var storageModel
    // Window y of the clock block's centre as if Home were scrolled to the top.
    // The icon-only sidebar rail centres its Discover button on it.
    readonly property real clockAnchorY: {
        void (page.width + page.height + homeClockBlock.y + homeClockBlock.height + homeClockTime.y)
        if (!homeClockBlock.visible || !page.Window.window)
            return -1
        const flick = homeScroll.contentItem
        // Anchor on the time itself, not the whole block with greeting and chips.
        return homeClockTime.mapToItem(null, 0, homeClockTime.height / 2).y
               + (flick && flick.contentY !== undefined ? flick.contentY : 0)
    }
    required property var favoritesModel
    required property var quickAccessModel
    required property var contentIndexModel
    required property var lang
    required property var cloudAuth
    required property var cloudIntegrationPreferences
    required property var privateVault
    property var updateChecker: null
    property var operationPresentation: null
    property Item headerHost: null
    readonly property real tabStripHeight: homeHeader.tabStripHeight
    readonly property real headerHeight: homeHeader.headerHeight
    readonly property Item activeTabSurface: homeHeader.activeTabSurface
    readonly property real activeTabScrollPosition: 0
    readonly property bool activeTabAdministrator: false
    readonly property color navigationColor: AppTheme.useQtTheme
        ? AppTheme.tint(AppTheme.surfaceRaised, AppTheme.translucentWindow ? 0.30 : 1)
        : AppTheme.mix(AppTheme.workspace, AppTheme.surfaceRaised, 0.42)

    signal browseRequested(string location)
    signal browseNewTabRequested(string location)
    signal watermarkEditorRequested()
    signal backNavigationRequested()
    signal forwardNavigationRequested()
    signal searchRequested(string query)
    signal categoryRequested(string key, string title, string iconName)

    readonly property string homePath: StandardPaths.writableLocation(StandardPaths.HomeLocation)
    property double wheelBurstLastMs: 0
    property int wheelBurstCount: 0
    property int wheelBurstDirection: 0
    property date now: new Date()
    readonly property string greeting: {
        const hour = page.now.getHours()
        const tr = lang.language === "tr"
        if (hour >= 5 && hour < 12) return tr ? "Günaydın" : "Good morning"
        if (hour >= 12 && hour < 17) return tr ? "İyi günler" : "Good afternoon"
        if (hour >= 17 && hour < 22) return tr ? "İyi akşamlar" : "Good evening"
        return tr ? "İyi geceler" : "Good night"
    }
    // Free space of the disk holding the home folder, refreshed each minute.
    readonly property real homeFreeBytes: {
        void Math.floor(page.now.getTime() / 60000)
        return page.contentIndexModel.homeStorageAvailableBytes()
    }
    readonly property int recentFileCount: {
        page.categoryTabsRevision
        page.contentIndexModel.totalIndexed
        return page.contentIndexModel.countForCategory("recent")
    }
    Timer { interval: 1000; running: true; repeat: true; onTriggered: page.now = new Date() }

    property string diskPropertiesPath: ""
    property string diskPropertiesTitle: ""
    property string diskPropertiesIconName: "drive.svg"

    function openDiskProperties(path, title, iconName) {
        diskPropertiesPath = path || ""
        diskPropertiesTitle = title || path || ""
        diskPropertiesIconName = iconName || "drive.svg"
        diskPropertiesPopup.open()
    }


    GModalPopup {
        id: privateUnlockPopup
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 430
        height: Math.min(350, page.height - 30)
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
            GText {
                text: lang.language === "tr" ? "Sana Özel kilidini aç" : "Unlock Private"
                baseColor: AppTheme.text
                basePixelSize: 18
                baseBold: true
            }
            GTextField {
                id: privateUnlockPassword
                Layout.fillWidth: true
                echoMode: TextInput.Password
                placeholderText: lang.language === "tr" ? "Kasa parolası" : "Vault password"
                color: AppTheme.text
                onAccepted: privateUnlockConfirm.clicked()
            }
            VaultAttemptStatus { Layout.fillWidth: true; vault: page.privateVault; lang: page.lang }
            GText {
                visible: page.privateVault.lastError.length > 0
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: page.privateVault.lastError
                baseColor: AppTheme.danger
                basePixelSize: 10
            }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.fillWidth: true
                GModalButton {
                    visible: page.privateVault.kwalletEnabled
                    text: lang.language === "tr" ? "KWallet ile Aç" : "Open with KWallet"
                    enabled: !page.privateVault.busy && page.privateVault.passwordWaitSeconds === 0
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
                             && page.privateVault.passwordWaitSeconds === 0
                    onClicked: {
                        if (page.privateVault.unlock(privateUnlockPassword.text))
                            privateUnlockPopup.close()
                    }
                }
            }
        }
    }

    FilePropertiesManager {
        id: diskFileProperties
        backgroundStats: true
    }

    GModalPopup {
        id: diskPropertiesPopup
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(620, page.width - 40)
        height: Math.min(700, page.height - 40)
        modal: true
        focus: true
        padding: 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: GModalSurface { }

        property int permissionDraft: 0
        property string ownerDraft: ""
        property string groupDraft: ""
        property bool recursiveDraft: false
        property bool administratorDraft: false
        property var ownerChoices: []
        property var groupChoices: []
        readonly property bool canEditAccess: !diskFileProperties.accessBusy
                && diskFileProperties.allLocal && diskFileProperties.linkType !== "symlink"
                && (diskFileProperties.permissionsEditable || administratorDraft)
        readonly property bool permissionsDirty: permissionDraft !== diskFileProperties.permissionMode
                || ownerDraft !== diskFileProperties.ownerName
                || groupDraft !== diskFileProperties.groupName
                || recursiveDraft
        readonly property bool validMode: permissionModeInput.acceptableInput
                && ownerPicker.acceptableInput && groupPicker.acceptableInput

        function reloadAccessChoices() {
            ownerChoices = diskFileProperties.availableOwners(administratorDraft)
            groupChoices = diskFileProperties.availableGroups(administratorDraft)
            if (ownerChoices.indexOf(ownerDraft) < 0) ownerDraft = diskFileProperties.ownerName
            if (groupChoices.indexOf(groupDraft) < 0) groupDraft = diskFileProperties.groupName
        }
        function resetAccessDraft() {
            permissionDraft = diskFileProperties.permissionMode
            ownerDraft = diskFileProperties.ownerName
            groupDraft = diskFileProperties.groupName
            recursiveDraft = false
            reloadAccessChoices()
            permissionModeInput.syncModeText()
        }
        function applyAccessDraft() {
            if (!canEditAccess || !validMode)
                return
            diskFileProperties.applyAccess(permissionDraft, ownerDraft, groupDraft, recursiveDraft, administratorDraft)
        }

        onAdministratorDraftChanged: reloadAccessChoices()
        onAboutToShow: {
            diskFileProperties.inspect(page.diskPropertiesPath)
            administratorDraft = false
            resetAccessDraft()
        }
        onOpened: resetAccessDraft()
        onClosed: { diskFileProperties.cancelAccess(); diskFileProperties.cancelInspection() }

        Connections {
            target: diskFileProperties
            function onPermissionsChanged() {
                if (diskPropertiesPopup.visible && !diskFileProperties.accessBusy)
                    diskPropertiesPopup.resetAccessDraft()
            }
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 22
            spacing: 14

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 100
                radius: 18
                color: AppTheme.surfaceRaised
                border.color: AppTheme.border

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 14
                    Rectangle {
                        Layout.preferredWidth: 64
                        Layout.preferredHeight: 64
                        radius: 16
                        color: AppTheme.accentSoft
                        border.color: AppTheme.accentBorder
                        CrispIcon {
                            anchors.centerIn: parent
                            width: 42
                            height: 42
                            source: AppTheme.useSystemIcons
                                    ? AppTheme.systemIconAtSize(AppTheme.discoveryIcon(page.diskPropertiesIconName, "drive.svg"), 42)
                                    : AppTheme.discoveryIcon(page.diskPropertiesIconName, "drive.svg")
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        GText { text: lang.language === "tr" ? "ÖZELLİKLER" : "PROPERTIES"; baseColor: AppTheme.accent; basePixelSize: 9; baseBold: true; baseLetterSpacing: 1.0 }
                        GText { Layout.fillWidth: true; text: page.diskPropertiesTitle; baseColor: AppTheme.text; basePixelSize: 18; baseFontWeight: Font.DemiBold; elide: Text.ElideRight }
                        GText { Layout.fillWidth: true; text: page.diskPropertiesPath; baseColor: AppTheme.textMuted; basePixelSize: 10; elide: Text.ElideMiddle }
                        GText { text: diskFileProperties.permissionsEditable ? (lang.language === "tr" ? "İzinler düzenlenebilir" : "Permissions can be edited") : (lang.language === "tr" ? "İzinler salt okunur" : "Permissions are read-only"); baseColor: diskFileProperties.permissionsEditable ? AppTheme.success : AppTheme.textMuted; basePixelSize: 10 }
                    }
                }
            }

            Flickable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: width
                contentHeight: diskPropertiesContent.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { }

                ColumnLayout {
                    id: diskPropertiesContent
                    width: diskPropertiesPopup.width - 44
                    spacing: 14

                    GText {
                        text: lang.language === "tr" ? "GENEL BİLGİLER" : "GENERAL INFORMATION"
                        baseColor: AppTheme.textMuted
                        basePixelSize: 10
                        baseBold: true
                        baseLetterSpacing: 0.8
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: infoGrid.implicitHeight + 28
                        radius: 16
                        color: AppTheme.surfaceRaised
                        border.color: AppTheme.border
                        GridLayout {
                            id: infoGrid
                            anchors.fill: parent
                            anchors.margins: 14
                            columns: 2
                            columnSpacing: 18
                            rowSpacing: 10
                            GText { text: lang.language === "tr" ? "Boyut" : "Size"; baseColor: AppTheme.textMuted; basePixelSize: 11 }
                            RowLayout {
                                GBusyIndicator { visible: diskFileProperties.calculating; running: visible; Layout.preferredWidth: 16; Layout.preferredHeight: 16 }
                                GText { text: diskFileProperties.calculating ? "…" : diskFileProperties.formatBytes(diskFileProperties.totalSize); baseColor: AppTheme.text; basePixelSize: 11 }
                            }
                            GText { text: lang.language === "tr" ? "İçerik" : "Contents"; baseColor: AppTheme.textMuted; basePixelSize: 11 }
                            GText { text: diskFileProperties.calculating ? "…" : diskFileProperties.fileCount + (lang.language === "tr" ? " dosya · " : " files · ") + diskFileProperties.folderCount + (lang.language === "tr" ? " klasör" : " folders"); baseColor: AppTheme.text; basePixelSize: 11 }
                            GText { text: lang.language === "tr" ? "Oluşturma" : "Created"; baseColor: AppTheme.textMuted; basePixelSize: 11 }
                            GText { text: diskFileProperties.created && !isNaN(diskFileProperties.created.getTime()) ? Qt.formatDateTime(diskFileProperties.created, "dd.MM.yyyy HH:mm") : "—"; baseColor: AppTheme.text; basePixelSize: 11 }
                            GText { text: lang.language === "tr" ? "Değiştirme" : "Modified"; baseColor: AppTheme.textMuted; basePixelSize: 11 }
                            GText { text: diskFileProperties.modified && !isNaN(diskFileProperties.modified.getTime()) ? Qt.formatDateTime(diskFileProperties.modified, "dd.MM.yyyy HH:mm") : "—"; baseColor: AppTheme.text; basePixelSize: 11 }
                            GText { text: lang.language === "tr" ? "Son erişim" : "Last accessed"; baseColor: AppTheme.textMuted; basePixelSize: 11 }
                            GText { text: diskFileProperties.accessed && !isNaN(diskFileProperties.accessed.getTime()) ? Qt.formatDateTime(diskFileProperties.accessed, "dd.MM.yyyy HH:mm") : "—"; baseColor: AppTheme.text; basePixelSize: 11 }
                            GText { text: lang.language === "tr" ? "İzinler" : "Permissions"; baseColor: AppTheme.textMuted; basePixelSize: 11 }
                            GText { text: diskFileProperties.permissionsText || "—"; baseColor: AppTheme.text; basePixelSize: 11 }
                            GText { text: lang.language === "tr" ? "Sahip" : "Owner"; baseColor: AppTheme.textMuted; basePixelSize: 11 }
                            GText { text: (diskFileProperties.ownerName || "—") + " / " + (diskFileProperties.groupName || "—"); baseColor: AppTheme.text; basePixelSize: 11 }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: permissionsContent.implicitHeight + 28
                        radius: 16
                        color: AppTheme.surfaceRaised
                        border.color: AppTheme.border
                        ColumnLayout {
                            id: permissionsContent
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 10

                            GText { text: lang.language === "tr" ? "ERİŞİM İZİNLERİ" : "ACCESS PERMISSIONS"; baseColor: AppTheme.textMuted; basePixelSize: 10; baseBold: true; baseLetterSpacing: 0.8 }

                            RowLayout {
                                Layout.fillWidth: true
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 3
                                    GText { text: lang.language === "tr" ? "Sahibi" : "Owner"; baseColor: AppTheme.textMuted; basePixelSize: 10 }
                                    GAccountPicker {
                                        id: ownerPicker
                                        Layout.fillWidth: true
                                        model: diskPropertiesPopup.ownerChoices
                                        value: diskPropertiesPopup.ownerDraft
                                        allowKeep: false
                                        keepText: lang.language === "tr" ? "Kullanıcı ara…" : "Find a user…"
                                        enabled: diskPropertiesPopup.canEditAccess && count > 0
                                        onEdited: function(value) { diskPropertiesPopup.ownerDraft = value }
                                    }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 3
                                    GText { text: lang.language === "tr" ? "Grup" : "Group"; baseColor: AppTheme.textMuted; basePixelSize: 10 }
                                    GAccountPicker {
                                        id: groupPicker
                                        Layout.fillWidth: true
                                        model: diskPropertiesPopup.groupChoices
                                        value: diskPropertiesPopup.groupDraft
                                        allowKeep: false
                                        keepText: lang.language === "tr" ? "Grup ara…" : "Find a group…"
                                        enabled: diskPropertiesPopup.canEditAccess && count > 0
                                        onEdited: function(value) { diskPropertiesPopup.groupDraft = value }
                                    }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10
                                GText { text: lang.language === "tr" ? "Mod" : "Mode"; baseColor: AppTheme.textMuted; basePixelSize: 10 }
                                GTextField {
                                    id: permissionModeInput
                                    Layout.preferredWidth: 90
                                    text: ""
                                    enabled: diskPropertiesPopup.canEditAccess
                                    validator: RegularExpressionValidator { regularExpression: /0?[0-7]{3}/ }
                                    function syncModeText() {
                                        text = "0" + Math.max(0, Math.min(511, diskPropertiesPopup.permissionDraft)).toString(8).padStart(3, "0")
                                    }
                                    onTextEdited: {
                                        if (acceptableInput)
                                            diskPropertiesPopup.permissionDraft = parseInt(text, 8)
                                    }
                                    Component.onCompleted: syncModeText()
                                }
                                Item { Layout.fillWidth: true }
                                GCheckBox {
                                    visible: diskFileProperties.administratorAvailable
                                    text: lang.language === "tr" ? "Yönetici olarak uygula" : "Apply as administrator"
                                    checked: diskPropertiesPopup.administratorDraft
                                    onToggled: diskPropertiesPopup.administratorDraft = checked
                                }
                            }

                            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: AppTheme.border }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                Item { Layout.preferredWidth: 92 }
                                Repeater {
                                    model: [lang.language === "tr" ? "Okuma" : "Read", lang.language === "tr" ? "Yazma" : "Write", diskFileProperties.directory ? (lang.language === "tr" ? "Dizine girme" : "Enter folder") : (lang.language === "tr" ? "Çalıştırma" : "Execute")]
                                    GText { required property string modelData; Layout.fillWidth: true; Layout.preferredWidth: 1; text: modelData; horizontalAlignment: Text.AlignHCenter; baseColor: AppTheme.textMuted; basePixelSize: 10 }
                                }
                            }
                            Repeater {
                                model: [
                                    { label: lang.language === "tr" ? "Sahibi" : "Owner", bits: [256, 128, 64] },
                                    { label: lang.language === "tr" ? "Grup" : "Group", bits: [32, 16, 8] },
                                    { label: lang.language === "tr" ? "Diğerleri" : "Others", bits: [4, 2, 1] }
                                ]
                                RowLayout {
                                    id: permissionRow
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: 8
                                    GText { Layout.preferredWidth: 92; text: permissionRow.modelData.label; baseColor: AppTheme.text; basePixelSize: 11 }
                                    Repeater {
                                        model: permissionRow.modelData.bits
                                        Item {
                                            required property int modelData
                                            Layout.fillWidth: true
                                            Layout.preferredWidth: 1
                                            Layout.preferredHeight: 28
                                            GCheckBox {
                                                anchors.centerIn: parent
                                                enabled: diskPropertiesPopup.canEditAccess
                                                checked: (diskPropertiesPopup.permissionDraft & parent.modelData) !== 0
                                                onToggled: {
                                                    if (checked) diskPropertiesPopup.permissionDraft |= parent.modelData
                                                    else diskPropertiesPopup.permissionDraft &= ~parent.modelData
                                                    permissionModeInput.syncModeText()
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            GCheckBox {
                                visible: diskFileProperties.directory
                                Layout.fillWidth: true
                                enabled: diskPropertiesPopup.canEditAccess
                                text: lang.language === "tr" ? "Alt dosya ve klasörlere de uygula" : "Apply to enclosed files and folders"
                                checked: diskPropertiesPopup.recursiveDraft
                                onToggled: diskPropertiesPopup.recursiveDraft = checked
                            }
                            GText {
                                visible: diskPropertiesPopup.recursiveDraft
                                Layout.fillWidth: true
                                text: lang.language === "tr"
                                    ? "Dikkat: bu işlem alt klasör ve dosyaların izinlerini de değiştirebilir."
                                    : "Caution: this can also change permissions of enclosed files and folders."
                                baseColor: AppTheme.danger
                                basePixelSize: 10
                                wrapMode: Text.Wrap
                            }

                            GText {
                                Layout.fillWidth: true
                                text: diskFileProperties.specialPermissionsHint()
                                baseColor: AppTheme.textMuted
                                basePixelSize: 10
                                wrapMode: Text.Wrap
                            }
                            GText {
                                visible: diskFileProperties.accessError.length > 0
                                Layout.fillWidth: true
                                text: diskFileProperties.accessError
                                baseColor: AppTheme.danger
                                basePixelSize: 10
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                GModalButton { text: lang.language === "tr" ? "Kapat" : "Close"; onClicked: diskPropertiesPopup.close() }
                Item { Layout.fillWidth: true }
                GBusyIndicator { visible: diskFileProperties.accessBusy; running: visible; Layout.preferredWidth: 20; Layout.preferredHeight: 20 }
                GText {
                    visible: diskFileProperties.accessBusy
                    text: lang.language === "tr" ? "İzinler uygulanıyor…" : "Applying permissions…"
                    baseColor: AppTheme.textMuted
                    basePixelSize: 10
                }
                GModalButton {
                    primary: true
                    text: lang.language === "tr" ? "Uygula" : "Apply"
                    enabled: diskPropertiesPopup.canEditAccess && diskPropertiesPopup.permissionsDirty && diskPropertiesPopup.validMode
                    onClicked: diskPropertiesPopup.applyAccessDraft()
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
    FileOperations { id: homeFileOps }
    // Launches the file type's preferred application directly, like the file
    // browser. Qt.openUrlExternally went through the portal and asked for an
    // application every time.
    OpenWithModel { id: homeOpenWith }

    // ---- Discover section layouts (remembered per section) -------------------
    Settings {
        id: discoverViews
        category: "DiscoverViews"
        property string categories: "grid"
        property string clouds: "grid"
        property string quickAccess: "grid"
        property string disks: "cards"
    }
    readonly property alias discoverViewSettings: discoverViews
    property string selectedCategoryKey: ""
    property int categoryTabsRevision: 0
    // Categories can show a selected category's newest files behind a tab;
    // Clouds and Quick Access offer a compact dock instead.
    readonly property var categoryViewModes: lang.language === "tr"
        ? [{id: "list", label: "Liste"}, {id: "grid", label: "Izgara"}, {id: "tab", label: "Sekme"}]
        : [{id: "list", label: "List"}, {id: "grid", label: "Grid"}, {id: "tab", label: "Tabs"}]
    readonly property var compactViewModes: lang.language === "tr"
        ? [{id: "list", label: "Liste"}, {id: "grid", label: "Izgara"}, {id: "compact", label: "Kompakt"}]
        : [{id: "list", label: "List"}, {id: "grid", label: "Grid"}, {id: "compact", label: "Compact"}]
    readonly property var diskViewModes: lang.language === "tr"
        ? [{id: "cards", label: "Kart"}, {id: "compact", label: "Kompakt"}]
        : [{id: "cards", label: "Cards"}, {id: "compact", label: "Compact"}]
    // Category tab rows open files with one or two clicks, following the
    // global setting or the category's own "open with single click".
    function categoryOpensOnSingleClick(key) {
        if (AppTheme.singleClickOpen) return true
        const revision = page.contentIndexModel.categoryInteractionRevision
        return revision >= 0 && key.length > 0 && !!page.contentIndexModel.categorySingleClickOpen(key)
    }
    function openIndexedFile(key, url) {
        if (key === "appimage" || String(url).toLowerCase().endsWith(".appimage"))
            homeFileOps.runExecutable(url)
        else
            homeOpenWith.openDefault(url, "")
    }
    function categoryArtwork(icon, key) {
        const artwork = AppTheme.discoveryIcon(icon, AppTheme.categoryDefaultIcon(key))
        return AppTheme.useSystemIcons ? AppTheme.systemIconAtSize(artwork, 22) : artwork
    }
    function categoryItemCount(key, count) {
        if (key !== "private") return count
        return page.privateVault.unlocked ? page.privateVault.totalItemCount : 0
    }
    readonly property var categoryTabs: {
        page.categoryTabsRevision
        contentIndexModel.totalIndexed
        const tr = lang.language === "tr"
        return contentIndexModel.visibleCategories().map(function(category) {
            const locked = category.key === "private" && page.privateVault.exists && !page.privateVault.unlocked
            return {key: category.key, title: tr ? category.titleTr : category.titleEn,
                    iconName: category.icon, icon: page.categoryArtwork(category.icon, category.key),
                    count: page.categoryItemCount(category.key, category.count),
                    badge: locked ? "" : page.categoryItemCount(category.key, category.count)}
        })
    }
    readonly property var currentCategoryTab: {
        const tabs = page.categoryTabs
        for (let i = 0; i < tabs.length; ++i)
            if (tabs[i].key === page.selectedCategoryKey) return tabs[i]
        return tabs.length ? tabs[0] : null
    }
    readonly property var cloudTabs: {
        page.visibleCloudCount
        return page.cloudOrderKeys.filter(function(key) { return page.cloudVisible(key) }).map(function(key) {
            return {key: key, title: page.cloudDisplayName(key),
                    icon: AppTheme.discoveryIcon(page.cloudDisplayIcon(key), page.cloudDefaultIcon(key))}
        })
    }
    function cloudReady(key) {
        if (key === "google") return cloudAuth.googleConnected
        if (key === "onedrive") return cloudAuth.oneDriveConnected
        return page.externalCloudLocation(key).length > 0
    }
    function cloudStatus(key) {
        return key === "google" || key === "onedrive" ? page.oauthStatusText(key) : page.externalCloudStatus(key)
    }
    function cloudCardShown(key) { return page.cloudVisible(key) }
    function fileGlyph(suffix) {
        const s = String(suffix || "").toLowerCase()
        if (["png", "jpg", "jpeg", "webp", "gif", "bmp", "svg", "svgz", "heic", "avif", "tif", "tiff"].indexOf(s) >= 0) return "image.svg"
        if (["mp3", "flac", "ogg", "opus", "m4a", "wav", "aac"].indexOf(s) >= 0) return "file-audio.svg"
        if (["mp4", "mkv", "webm", "avi", "mov", "m4v", "ts"].indexOf(s) >= 0) return "file-video.svg"
        if (["zip", "rar", "7z", "tar", "gz", "xz", "zst", "bz2"].indexOf(s) >= 0) return "file-archive.svg"
        if (s === "pdf") return "pdf.svg"
        return "file.svg"
    }
    Connections {
        target: page.contentIndexModel
        function onRowsMoved() { page.categoryTabsRevision += 1 }
        function onDataChanged() { page.categoryTabsRevision += 1 }
        function onModelReset() { page.categoryTabsRevision += 1 }
        function onCategoryInteractionRevisionChanged() { page.categoryTabsRevision += 1 }
    }
    KeyboardShortcutsDialog { id: keyboardShortcutsDialog; lang: page.lang }
    Loader {
        id: settingsDialogLoader
        objectName: "homeSettingsDialogLoader"
        active: false
        sourceComponent: SettingsDialog {
            lang: page.lang
            onWatermarkEditorRequested: page.watermarkEditorRequested()
            onClosed: Qt.callLater(function() { settingsDialogLoader.active = false })
        }
        onLoaded: item.open()
    }
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

    PageChrome {
        id: homeHeader
        pageOutlineJoined: page.headerHost !== null
        objectName: "homeHeader"
        parent: page.headerHost || page
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: headerHeight
        visible: page.visible && (!page.StackView.view || page.StackView.view.currentItem === page)
        tabTitle: lang.t("dashboard")
        tabIcon: AppTheme.icon("dashboard.svg")
        navigationColor: page.navigationColor
        canGoBack: false
        canGoForward: true
        canGoUp: true
        backTip: lang.language === "tr" ? "Geri" : "Back"
        forwardTip: lang.language === "tr" ? "Son açılan dizine git" : "Open last folder"
        upTip: lang.language === "tr" ? "Ev dizinini aç" : "Open home folder"
        refreshTip: lang.language === "tr" ? "Keşfet'i yenile" : "Refresh Discover"
        historyTip: lang.language === "tr" ? "Son konuma dön" : "Return to last location"
        newTabTip: lang.t("new_tab")
        onNewTabRequested: page.browseNewTabRequested(page.homePath)
        onForwardRequested: page.forwardNavigationRequested()
        onUpRequested: page.browseRequested(page.homePath)
        onHistoryRequested: page.forwardNavigationRequested()
        onRefreshRequested: {
            page.now = new Date()
            if (homeScroll.contentItem)
                homeScroll.contentItem.contentY = homeScroll.contentItem.originY || 0
        }
        onTabSelected: {
            if (homeScroll.contentItem)
                homeScroll.contentItem.contentY = homeScroll.contentItem.originY || 0
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: page.headerHost ? 0 : Math.max(0, AppTheme.panelRadius - 1)
        color: page.headerHost ? "transparent" : AppTheme.workspace
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
        anchors.leftMargin: page.width < 900 ? 14 : 26
        anchors.rightMargin: page.width < 900 ? 14 : 26
        anchors.topMargin: 22
        anchors.bottomMargin: 22
        spacing: page.width < 900 ? 12 : 20

        ScrollView {
            id: homeScroll
            objectName: "homeScroll"
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

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        ColumnLayout {
                            id: homeClockBlock
                            objectName: "homeClockBlock"
                            Layout.preferredWidth: Math.min(560, homeScroll.availableWidth * 0.5)
                            Layout.minimumWidth: 0
                            spacing: 2
                            GText {
                                objectName: "homeGreeting"
                                text: page.greeting
                                baseColor: AppTheme.accent
                                basePixelSize: 11
                                baseFontWeight: Font.DemiBold
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 12
                                GText {
                                    id: homeClockTime
                                    objectName: "homeClockTime"
                                    text: Qt.formatTime(page.now, "HH:mm")
                                    baseColor: AppTheme.text
                                    basePixelSize: 34
                                    baseFontWeight: Font.Bold
                                }
                                GText {
                                    Layout.alignment: Qt.AlignBottom
                                    Layout.bottomMargin: 7
                                    Layout.fillWidth: true
                                    text: page.now.toLocaleDateString(
                                              Qt.locale(lang.language === "tr" ? "tr_TR" : "en_US"),
                                              lang.language === "tr" ? "dddd, d MMMM yyyy" : "dddd, MMMM d, yyyy")
                                    baseColor: AppTheme.textMuted
                                    basePixelSize: 11
                                    elide: Text.ElideRight
                                }
                            }
                            Flow {
                                objectName: "homeSummaryChips"
                                Layout.fillWidth: true
                                Layout.topMargin: 6
                                spacing: 6
                                SummaryChip {
                                    objectName: "homeRecentChip"
                                    visible: page.recentFileCount > 0
                                    iconName: "history.svg"
                                    // 0 is the "All" window, 1 is "Today" (see the category settings).
                                    readonly property int days: page.contentIndexModel.recentWindowDays
                                    text: lang.language === "tr"
                                          ? (days <= 0 ? page.recentFileCount + " son dosya"
                                             : days === 1 ? "Bugün " + page.recentFileCount + " dosya"
                                             : "Son " + days + " günde " + page.recentFileCount + " dosya")
                                          : (days <= 0 ? page.recentFileCount + " recent files"
                                             : days === 1 ? page.recentFileCount + " files today"
                                             : page.recentFileCount + " files in the last " + days + " days")
                                    onClicked: page.categoryRequested("recent", page.contentIndexModel.titleForCategory("recent", lang.language), "recent.svg")
                                }
                                SummaryChip {
                                    objectName: "homeFreeSpaceChip"
                                    visible: page.homeFreeBytes > 0
                                    iconName: "drive-symbolic.svg"
                                    text: lang.language === "tr"
                                          ? "Ev diskinde " + page.storageModel.formatBytes(page.homeFreeBytes) + " boş"
                                          : page.storageModel.formatBytes(page.homeFreeBytes) + " free on home disk"
                                    onClicked: page.browseRequested(page.homePath)
                                }
                                SummaryChip {
                                    objectName: "homeOperationsChip"
                                    visible: !!page.operationPresentation && page.operationPresentation.activeCount > 0
                                    accent: true
                                    iconName: "edit-copy.svg"
                                    text: page.operationPresentation
                                          ? page.operationPresentation.activeCount + (lang.language === "tr" ? " işlem sürüyor" : " operations running")
                                          : ""
                                    onClicked: page.infoPanelOpen = true
                                }
                                SummaryChip {
                                    objectName: "homeVaultChip"
                                    visible: page.privateVault.unlocked
                                    warning: true
                                    iconName: "unlock.svg"
                                    text: lang.language === "tr" ? "Kasa açık" : "Vault unlocked"
                                    onClicked: page.privateVault.lock()
                                    toolTip: lang.language === "tr" ? "Kilitlemek için tıkla" : "Click to lock"
                                }
                            }
                        }

                        Item { Layout.fillWidth: true }

                        TopSearchBar {
                            visible: homeScroll.availableWidth >= 780
                            Layout.preferredWidth: Math.min(440, homeScroll.availableWidth * 0.43)
                            Layout.minimumWidth: visible ? 200 : 0
                            Layout.maximumWidth: 520
                            Layout.preferredHeight: visible ? 42 : 0
                            lang: page.lang
                            onSearchRequested: function(query) { page.searchRequested(query) }
                        }
                        Item {
                            id: infoToggleButton
                            readonly property bool hovered: infoToggleMouse.containsMouse
                            Layout.preferredWidth: 34
                            Layout.preferredHeight: 34

                            Rectangle {
                                anchors.fill: parent
                                radius: 11
                                color: page.infoPanelOpen ? AppTheme.accent : (infoToggleButton.hovered ? AppTheme.accentSoft : AppTheme.surfaceRaised)
                                border.color: page.infoPanelOpen ? AppTheme.accent : AppTheme.border
                            }
                            CrispIcon {
                                anchors.centerIn: parent
                                source: AppTheme.icon("info.svg", page.infoPanelOpen ? "#ffffff" : AppTheme.text)
                                width: 16
                                height: 16
                            }
                            Rectangle {
                                objectName: "updateNotificationDot"
                                visible: page.updateChecker !== null && page.updateChecker.updateAvailable
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.rightMargin: -2
                                anchors.topMargin: -2
                                width: 10; height: 10; radius: 5
                                color: AppTheme.accent
                                border.width: 2
                                border.color: AppTheme.background
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
                        GButton {
                            objectName: "keyboardShortcutsButton"
                            Layout.preferredWidth: 34
                            Layout.preferredHeight: 34
                            icon.source: AppTheme.icon("keyboard.svg")
                            icon.width: 16; icon.height: 16
                            onClicked: keyboardShortcutsDialog.open()
                            GToolTip { text: lang.language === "tr" ? "Klavye kısayolları" : "Keyboard shortcuts" }
                        }
                        GButton {
                            objectName: "homeSettingsButton"
                            Layout.preferredWidth: 34
                            Layout.preferredHeight: 34
                            icon.source: AppTheme.icon("settings.svg")
                            icon.width: 16; icon.height: 16
                            onClicked: settingsDialogLoader.active = true
                            GToolTip { text: lang.language === "tr" ? "Ayarlar" : "Settings" }
                        }
                    }

                    // The search field moves below the clock/actions on narrow windows.
                    // Do not leave its 320px minimum alongside fixed toolbar buttons.
                    TopSearchBar {
                        visible: homeScroll.availableWidth < 780
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.preferredHeight: visible ? 42 : 0
                        lang: page.lang
                        onSearchRequested: function(query) { page.searchRequested(query) }
                    }
                }

                DiscoverSectionHeader {
                    objectName: "categoriesHeader"
                    switcherName: "categoriesViewSwitcher"
                    title: lang.language === "tr" ? "Kategoriler" : "Categories"
                    subtitle: lang.language === "tr"
                              ? "Her kategori kendi kaynaklarını ve indeks ayarlarını yönetir"
                              : "Each category manages its own sources and indexing settings"
                    modes: page.categoryViewModes
                    viewMode: discoverViews.categories
                    onViewModeRequested: function(mode) { discoverViews.categories = mode }
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
                            GText {
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
                                baseColor: AppTheme.text
                                basePixelSize: 9
                                elide: Text.ElideRight
                            }
                            GText {
                                text: lang.language === "tr"
                                      ? (page.formatIndexNumber(contentIndexModel.scanPendingDirectories) + " klasör kaldı")
                                      : (page.formatIndexNumber(contentIndexModel.scanPendingDirectories) + " folders remaining")
                                baseColor: AppTheme.accent
                                basePixelSize: 9
                                baseFontWeight: Font.DemiBold
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
                    visible: discoverViews.categories !== "tab"
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? categoryFlow.height : 0

                    Flow {
                        id: categoryFlow
                        width: parent.width
                        height: childrenRect.height
                        spacing: 12
                        // Match the Discover cloud grid: Categories scale 4 → 3 → 2 → 1.
                        // The list layout is a single column of slimmer rows.
                        property int columnCount: discoverViews.categories === "list" ? 1
                            : (width >= 1160 ? 4 : (width >= 860 ? 3 : (width >= 560 ? 2 : 1)))
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
                                    language: page.lang.language
                                    id: categoryTile
                                    anchors.fill: parent
                                    compact: discoverViews.categories === "list"
                                    detailText: !compact || parent.categoryKey === "private" ? ""
                                        : page.storageModel.formatBytes(parent.categoryCount >= 0
                                              ? page.contentIndexModel.bytesForCategory(parent.categoryKey) : 0)
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
                                    actionIcon: parent.categoryKey === "private"
                                                ? (page.privateVault.unlocked ? "unlock.svg" : "lock.svg") : ""
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

                ColumnLayout {
                    id: categoryTabView
                    objectName: "categoryTabView"
                    visible: discoverViews.categories === "tab"
                    Layout.fillWidth: true
                    spacing: 12

                    DiscoverTabStrip {
                        Layout.fillWidth: true
                        tabs: page.categoryTabs
                        current: page.currentCategoryTab ? page.currentCategoryTab.key : ""
                        onSelected: function(key) { page.selectedCategoryKey = key }
                    }

                    Rectangle {
                        id: categoryTabPanel
                        objectName: "categoryTabPanel"
                        readonly property var info: page.currentCategoryTab
                        readonly property string key: info ? info.key : ""
                        readonly property bool locked: key === "private" && !page.privateVault.unlocked
                        readonly property var files: {
                            page.categoryTabsRevision
                            page.contentIndexModel.totalIndexed
                            return !categoryTabView.visible || !key.length || locked ? []
                                 : page.contentIndexModel.latestFilesForCategory(key, 8)
                        }
                        Layout.fillWidth: true
                        implicitHeight: categoryTabColumn.implicitHeight + 32
                        radius: AppTheme.cardRadius
                        color: AppTheme.cardSurface
                        border.color: AppTheme.border

                        ColumnLayout {
                            id: categoryTabColumn
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 16
                            spacing: 10

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 14
                                CrispIcon {
                                    Layout.preferredWidth: 44; Layout.preferredHeight: 44
                                    source: categoryTabPanel.info ? page.categoryArtwork(categoryTabPanel.info.iconName, categoryTabPanel.key) : ""
                                }
                                ColumnLayout {
                                    spacing: 2
                                    GText {
                                        text: categoryTabPanel.info ? categoryTabPanel.info.title : ""
                                        baseColor: AppTheme.text
                                        basePixelSize: 15
                                        baseFontWeight: Font.DemiBold
                                    }
                                    GText {
                                        text: !categoryTabPanel.info ? ""
                                              : categoryTabPanel.locked ? (lang.language === "tr" ? "Kasa kilitli" : "Vault is locked")
                                              : categoryTabPanel.info.count + (lang.language === "tr" ? " öğe · " : " items · ")
                                                + page.storageModel.formatBytes(page.contentIndexModel.bytesForCategory(categoryTabPanel.key))
                                        baseColor: AppTheme.textMuted
                                        basePixelSize: 11
                                    }
                                }
                                Item { Layout.fillWidth: true }
                                DiscoverHeaderButton {
                                    objectName: "categoryTabOpenButton"
                                    accented: true
                                    iconName: "nav-forward.svg"
                                    text: lang.language === "tr" ? "Kategoriyi aç" : "Open category"
                                    onClicked: if (categoryTabPanel.info)
                                        page.categoryRequested(categoryTabPanel.key, categoryTabPanel.info.title, categoryTabPanel.info.iconName)
                                }
                            }

                            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: AppTheme.border }

                            GText {
                                visible: categoryTabPanel.files.length === 0
                                Layout.fillWidth: true
                                Layout.topMargin: 6
                                Layout.bottomMargin: 6
                                horizontalAlignment: Text.AlignHCenter
                                text: categoryTabPanel.locked
                                      ? (lang.language === "tr" ? "İçeriği görmek için kasanın kilidini aç." : "Unlock the vault to see its contents.")
                                      : (lang.language === "tr" ? "Bu kategoride henüz dosya yok." : "No files in this category yet.")
                                baseColor: AppTheme.textMuted
                                basePixelSize: 11
                            }

                            Repeater {
                                model: categoryTabPanel.files
                                delegate: Rectangle {
                                    id: recentRow
                                    required property var modelData
                                    Layout.fillWidth: true
                                    implicitHeight: 40
                                    radius: 9
                                    color: recentRowMouse.containsMouse ? AppTheme.surfaceHover : "transparent"
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 10
                                        anchors.rightMargin: 12
                                        spacing: 12
                                        CrispIcon {
                                            Layout.preferredWidth: 22; Layout.preferredHeight: 22
                                            source: AppTheme.icon(page.fileGlyph(recentRow.modelData.suffix))
                                        }
                                        GText {
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 80
                                            text: recentRow.modelData.name
                                            baseColor: AppTheme.text
                                            basePixelSize: 12
                                            elide: Text.ElideMiddle
                                        }
                                        GText {
                                            Layout.preferredWidth: Math.min(320, categoryTabColumn.width * 0.3)
                                            text: String(recentRow.modelData.parentPath).replace(page.homePath, "~")
                                            baseColor: AppTheme.textMuted
                                            basePixelSize: 10
                                            elide: Text.ElideMiddle
                                        }
                                        GText {
                                            Layout.preferredWidth: 72
                                            horizontalAlignment: Text.AlignRight
                                            text: page.storageModel.formatBytes(recentRow.modelData.size)
                                            baseColor: AppTheme.textMuted
                                            basePixelSize: 10
                                        }
                                        GText {
                                            Layout.preferredWidth: 104
                                            horizontalAlignment: Text.AlignRight
                                            text: Qt.formatDateTime(new Date(recentRow.modelData.modifiedMs), "dd.MM.yyyy HH:mm")
                                            baseColor: AppTheme.textFaint
                                            basePixelSize: 10
                                        }
                                    }
                                    readonly property bool singleClick: page.categoryOpensOnSingleClick(categoryTabPanel.key)
                                    MouseArea {
                                        id: recentRowMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                                        onClicked: function(mouse) {
                                            if (mouse.button === Qt.MiddleButton)
                                                page.browseNewTabRequested(recentRow.modelData.parentPath)
                                            else if (recentRow.singleClick)
                                                page.openIndexedFile(categoryTabPanel.key, recentRow.modelData.url)
                                        }
                                        onDoubleClicked: function(mouse) {
                                            if (mouse.button === Qt.LeftButton && !recentRow.singleClick)
                                                page.openIndexedFile(categoryTabPanel.key, recentRow.modelData.url)
                                        }
                                    }
                                    GToolButton {
                                        objectName: "categoryTabRevealButton"
                                        z: 3
                                        visible: recentRowMouse.containsMouse || hovered
                                        anchors.right: parent.right
                                        anchors.rightMargin: 4
                                        anchors.verticalCenter: parent.verticalCenter
                                        implicitWidth: 30; implicitHeight: 30
                                        icon.source: AppTheme.icon("viewer-folder.svg")
                                        icon.width: 16; icon.height: 16
                                        onClicked: page.browseRequested(recentRow.modelData.parentPath)
                                        background: Rectangle {
                                            radius: 9
                                            color: parent.hovered ? AppTheme.surfacePressed : AppTheme.surfaceRaised
                                            border.color: AppTheme.border
                                        }
                                        GToolTip { text: lang.language === "tr" ? "İçeren klasörü aç" : "Open containing folder" }
                                    }
                                    GToolTip {
                                        visible: recentRowMouse.containsMouse
                                        text: recentRow.modelData.path + "\n"
                                              + (recentRow.singleClick
                                                 ? (lang.language === "tr" ? "Açmak için tıkla" : "Click to open")
                                                 : (lang.language === "tr" ? "Açmak için çift tıkla" : "Double-click to open"))
                                    }
                                }
                            }
                        }
                    }
                }

                DiscoverSectionHeader {
                    objectName: "cloudsHeader"
                    switcherName: "cloudsViewSwitcher"
                    title: lang.t("cloud_integrations")
                    countText: page.visibleCloudCount > 0 ? String(page.visibleCloudCount) : ""
                    modes: page.visibleCloudCount > 0 ? page.compactViewModes : []
                    viewMode: discoverViews.clouds
                    onViewModeRequested: function(mode) { discoverViews.clouds = mode }
                    DiscoverHeaderButton {
                        id: manageCloudIntegrationsButton
                        objectName: "manageCloudIntegrationsButton"
                        iconName: "settings.svg"
                        text: lang.language === "tr" ? "Bulutları yönet" : "Manage clouds"
                        onClicked: page.openCloudManager("")
                    }
                }

                GridLayout {
                    id: cloudGrid
                    objectName: "cloudGrid"
                    // Responsive cloud cards: 4 → 3 → 2 → 1 as the Discover area narrows.
                    // List: one full-width column. Compact: fixed-size tiles.
                    readonly property bool compactLayout: discoverViews.clouds === "compact"
                    // Columns follow the section width; compact tiles pack left.
                    readonly property real availableWidth: parent ? parent.width : width
                    Layout.fillWidth: !compactLayout
                    columns: discoverViews.clouds === "list" ? 1
                        : compactLayout ? Math.max(1, Math.floor((availableWidth + columnSpacing) / (152 + columnSpacing)))
                        : (availableWidth >= 1160 ? 4 : (availableWidth >= 860 ? 3 : (availableWidth >= 560 ? 2 : 1)))
                    columnSpacing: 12
                    rowSpacing: 12

                    CloudCard {
                        language: page.lang.language
                        visible: page.cloudCardShown("google")
                        cloudKey: "google"
                        Layout.row: Math.floor(Math.max(0, page.visibleCloudRank("google")) / cloudGrid.columns)
                        Layout.column: Math.max(0, page.visibleCloudRank("google")) % cloudGrid.columns
                        compact: cloudGrid.compactLayout
                        Layout.fillWidth: !compact
                        Layout.preferredWidth: compact ? implicitWidth : -1
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
                        language: page.lang.language
                        visible: page.cloudCardShown("onedrive")
                        cloudKey: "onedrive"
                        Layout.row: Math.floor(Math.max(0, page.visibleCloudRank("onedrive")) / cloudGrid.columns)
                        Layout.column: Math.max(0, page.visibleCloudRank("onedrive")) % cloudGrid.columns
                        compact: cloudGrid.compactLayout
                        Layout.fillWidth: !compact
                        Layout.preferredWidth: compact ? implicitWidth : -1
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
                        language: page.lang.language
                        visible: page.cloudCardShown("dropbox")
                        cloudKey: "dropbox"
                        Layout.row: Math.floor(Math.max(0, page.visibleCloudRank("dropbox")) / cloudGrid.columns)
                        Layout.column: Math.max(0, page.visibleCloudRank("dropbox")) % cloudGrid.columns
                        compact: cloudGrid.compactLayout
                        Layout.fillWidth: !compact
                        Layout.preferredWidth: compact ? implicitWidth : -1
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
                        language: page.lang.language
                        visible: page.cloudCardShown("nextcloud")
                        cloudKey: "nextcloud"
                        Layout.row: Math.floor(Math.max(0, page.visibleCloudRank("nextcloud")) / cloudGrid.columns)
                        Layout.column: Math.max(0, page.visibleCloudRank("nextcloud")) % cloudGrid.columns
                        compact: cloudGrid.compactLayout
                        Layout.fillWidth: !compact
                        Layout.preferredWidth: compact ? implicitWidth : -1
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
                        language: page.lang.language
                        visible: page.cloudCardShown("owncloud")
                        cloudKey: "owncloud"
                        Layout.row: Math.floor(Math.max(0, page.visibleCloudRank("owncloud")) / cloudGrid.columns)
                        Layout.column: Math.max(0, page.visibleCloudRank("owncloud")) % cloudGrid.columns
                        compact: cloudGrid.compactLayout
                        Layout.fillWidth: !compact
                        Layout.preferredWidth: compact ? implicitWidth : -1
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
                        language: page.lang.language
                        visible: page.cloudCardShown("mega")
                        cloudKey: "mega"
                        Layout.row: Math.floor(Math.max(0, page.visibleCloudRank("mega")) / cloudGrid.columns)
                        Layout.column: Math.max(0, page.visibleCloudRank("mega")) % cloudGrid.columns
                        compact: cloudGrid.compactLayout
                        Layout.fillWidth: !compact
                        Layout.preferredWidth: compact ? implicitWidth : -1
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
                        language: page.lang.language
                        visible: page.cloudCardShown("pcloud")
                        cloudKey: "pcloud"
                        Layout.row: Math.floor(Math.max(0, page.visibleCloudRank("pcloud")) / cloudGrid.columns)
                        Layout.column: Math.max(0, page.visibleCloudRank("pcloud")) % cloudGrid.columns
                        compact: cloudGrid.compactLayout
                        Layout.fillWidth: !compact
                        Layout.preferredWidth: compact ? implicitWidth : -1
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
                        language: page.lang.language
                        visible: page.cloudCardShown("webdav")
                        cloudKey: "webdav"
                        Layout.row: Math.floor(Math.max(0, page.visibleCloudRank("webdav")) / cloudGrid.columns)
                        Layout.column: Math.max(0, page.visibleCloudRank("webdav")) % cloudGrid.columns
                        compact: cloudGrid.compactLayout
                        Layout.fillWidth: !compact
                        Layout.preferredWidth: compact ? implicitWidth : -1
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
                        color: AppTheme.useQtTheme ? AppTheme.panelSurface : AppTheme.surface
                        border.color: AppTheme.border
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 16
                            spacing: 12
                            CrispIcon { source: AppTheme.icon("cloud.svg"); Layout.preferredWidth: 28; Layout.preferredHeight: 28 }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                GText {
                                    text: lang.language === "tr" ? "Ana ekranda gösterilen bulut yok" : "No cloud integrations are shown"
                                    baseColor: AppTheme.text
                                    basePixelSize: 12
                                    baseFontWeight: Font.DemiBold
                                }
                                GText {
                                    text: lang.language === "tr" ? "Sağ üstteki Bulutları yönet düğmesinden istediğini açabilirsin." : "Use Manage clouds above to enable the integrations you want."
                                    baseColor: AppTheme.textMuted
                                    basePixelSize: 10
                                }
                            }
                        }
                    }
                }

                DiscoverSectionHeader {
                    objectName: "quickAccessHeader"
                    switcherName: "quickAccessViewSwitcher"
                    title: lang.t("quick_access")
                    // Only while something is being dragged over the shortcuts.
                    subtitle: quickDrop.containsDrag
                              ? (lang.language === "tr" ? "Bırakınca kısayol eklenir" : "Drop to add a shortcut") : ""
                    modes: page.compactViewModes
                    viewMode: discoverViews.quickAccess
                    onViewModeRequested: function(mode) { discoverViews.quickAccess = mode }
                    DiscoverHeaderButton {
                        id: addShortcutButton
                        objectName: "addShortcutButton"
                        accented: true
                        iconName: "plus.svg"
                        text: lang.language === "tr" ? "Yeni kısayol" : "New shortcut"
                        onClicked: page.addShortcut()
                    }
                }

                Rectangle {
                    id: quickAccessPanel
                    readonly property bool compactLayout: discoverViews.quickAccess === "compact"
                    Layout.fillWidth: true
                    Layout.preferredHeight: quickGrid.implicitHeight + 12
                    radius: 18
                    // No frame of its own: the tiles sit directly on the page,
                    // like the disk cards. Only a drag shows the drop area.
                    color: quickDrop.containsDrag ? AppTheme.tint(AppTheme.accent, 0.08) : "transparent"
                    border.color: quickDrop.containsDrag ? AppTheme.accent : "transparent"
                    border.width: quickDrop.containsDrag ? 2 : 0

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
                        objectName: "quickAccessGrid"
                        // Compact tiles keep their size and pack to the left: the
                        // grid is only as wide as its tiles, so columns come from
                        // the panel width instead of the grid's own width.
                        readonly property real availableWidth: quickAccessPanel.width - 12
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.margins: 6
                        width: quickAccessPanel.compactLayout ? implicitWidth : availableWidth
                        columns: discoverViews.quickAccess === "list" ? 1
                            : quickAccessPanel.compactLayout ? Math.max(1, Math.floor((availableWidth + columnSpacing) / (108 + columnSpacing)))
                            : (availableWidth > 940 ? 4 : (availableWidth > 700 ? 3 : (availableWidth > 460 ? 2 : 1)))
                        columnSpacing: 10
                        rowSpacing: 10

                        Repeater {
                            model: page.quickAccessModel
                            delegate: QuickAccessTile {
                                language: page.lang.language
                                required property int index
                                required property string shortcutTitle
                                required property string shortcutLocation
                                required property string shortcutIcon
                                required property bool shortcutFixed
                                compact: quickAccessPanel.compactLayout
                                Layout.fillWidth: !compact
                                Layout.preferredWidth: compact ? implicitWidth : -1
                                Layout.preferredHeight: compact ? implicitHeight : Math.max(74, AppTheme.effectiveHomeIconSize + 28)
                                shortcutIndex: index
                                fixedShortcut: shortcutFixed
                                recentLocations: shortcutFixed ? page.quickAccessModel.recentLocations : []
                                title: shortcutFixed
                                       ? (shortcutTitle.length ? shortcutTitle : (lang.language === "tr" ? "Son Konum" : "Last Location"))
                                       : shortcutTitle
                                iconSource: AppTheme.discoveryIcon(shortcutIcon, "folder.svg")
                                systemIconPixels: page.systemHomeIconSize
                                path: shortcutLocation
                                FolderStats { id: shortcutStats; path: shortcutFixed ? "" : shortcutLocation }
                                infoText: !shortcutStats.available ? ""
                                          : (shortcutStats.files + shortcutStats.folders).toLocaleString(Qt.locale(), "f", 0)
                                            + (lang.language === "tr" ? " öğe" : " items")
                                            + " · " + page.storageModel.formatBytes(shortcutStats.bytes)
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
                            Layout.columnSpan: quickGrid.columns
                            Layout.fillWidth: true
                            Layout.preferredHeight: 86
                            radius: 14
                            color: "transparent"
                            border.color: AppTheme.borderStrong
                            border.width: 1
                            GText {
                                anchors.centerIn: parent
                                text: lang.language === "tr"
                                      ? "İlk kısayolunu sol menüden sürükle veya Yeni kısayol'a bas."
                                      : "Drag your first shortcut from the sidebar or choose New shortcut."
                                baseColor: AppTheme.textMuted
                                basePixelSize: 11
                            }
                        }
                    }
                }

                /*
                   Favorites remain in the sidebar and can be dragged into Quick Access.
                   Keeping one home collection avoids duplicate shortcut surfaces.
                */
                DiscoverSectionHeader {
                    objectName: "disksHeader"
                    switcherName: "disksViewSwitcher"
                    title: lang.t("disks")
                    modes: page.diskViewModes
                    viewMode: discoverViews.disks
                    onViewModeRequested: function(mode) { discoverViews.disks = mode }
                    DiscoverHeaderButton {
                        objectName: "refreshDisksButton"
                        iconName: "refresh.svg"
                        text: lang.t("refresh")
                        onClicked: page.storageModel.refresh()
                    }
                }

                Rectangle {
                    id: diskOverviewPanel
                    objectName: "diskOverviewPanel"
                    Layout.fillWidth: true
                    Layout.leftMargin: 6
                    Layout.rightMargin: 6
                    Layout.preferredHeight: overviewColumn.implicitHeight + 32
                    radius: AppTheme.cardRadius + 2
                    // Opaque even with a translucent window (Qt colours, full opacity).
                    color: AppTheme.useQtTheme && AppTheme.translucentWindow ? AppTheme.surface : AppTheme.cardSurface
                    border.color: AppTheme.border

                    readonly property real totalBytes: page.storageModel.totalBytes
                    readonly property real usedBytes: page.storageModel.usedBytes
                    readonly property real freeBytes: page.storageModel.freeBytes
                    readonly property real usageRatio: totalBytes > 0 ? usedBytes / totalBytes : 0
                    readonly property string usagePercent: "%" + Math.round(usageRatio * 100)
                    // Used space per disk (largest five + "other"), then free space.
                    readonly property var segments: {
                        const list = []
                        const disks = page.storageModel.usageSegments || []
                        for (let i = 0; i < disks.length; ++i) {
                            const disk = disks[i]
                            list.push({
                                bytes: Number(disk.usedBytes || 0),
                                label: disk.other ? (page.lang.language === "tr" ? "Diğer diskler" : "Other disks") : disk.name,
                                color: disk.other ? AppTheme.textFaint : segmentPalette[i % segmentPalette.length],
                                free: false
                            })
                        }
                        list.push({ bytes: freeBytes, label: page.lang.language === "tr" ? "Boş alan" : "Free space",
                                    // An empty track: a shade of the card (Qt theme borders are translucent text).
                                    color: AppTheme.dark ? Qt.rgba(1, 1, 1, 0.12)
                                                         : AppTheme.mix(AppTheme.cardSurface, Qt.rgba(AppTheme.text.r, AppTheme.text.g, AppTheme.text.b, AppTheme.cardSurface.a), 0.09),
                                    free: true })
                        return list.filter(function(segment) { return segment.bytes > 0 })
                    }
                    readonly property var segmentPalette: ["#6E8BFF", "#A77BFA", "#E27BE8", "#FF8FB1", "#3FBFA0"]

                    ColumnLayout {
                        id: overviewColumn
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 10

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            CrispIcon { source: AppTheme.icon("drive.svg"); Layout.preferredWidth: 16; Layout.preferredHeight: 16 }
                            GText {
                                text: page.lang.language === "tr" ? "Toplam Depolama" : "Total Storage"
                                baseColor: AppTheme.textMuted
                                basePixelSize: 11
                                baseFontWeight: Font.DemiBold
                            }
                            Rectangle {
                                radius: height / 2
                                implicitHeight: 20
                                implicitWidth: diskCountText.implicitWidth + 16
                                color: AppTheme.accentSoft
                                border.color: AppTheme.accentBorder
                                GText {
                                    id: diskCountText
                                    anchors.centerIn: parent
                                    text: page.storageModel.count + " " + (page.lang.language === "tr" ? "disk" : (page.storageModel.count === 1 ? "disk" : "disks"))
                                    baseColor: AppTheme.accent
                                    basePixelSize: 10
                                    baseFontWeight: Font.DemiBold
                                }
                            }
                            Item { Layout.fillWidth: true }
                            GText {
                                text: diskOverviewPanel.usagePercent + (page.lang.language === "tr" ? " kullanımda" : " in use")
                                baseColor: AppTheme.textMuted
                                basePixelSize: 11
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            GText {
                                objectName: "diskOverviewUsed"
                                text: page.storageModel.formatBytes(diskOverviewPanel.usedBytes)
                                baseColor: AppTheme.text
                                basePixelSize: 22
                                baseFontWeight: Font.DemiBold
                            }
                            GText {
                                Layout.alignment: Qt.AlignBaseline
                                text: page.lang.language === "tr" ? "kullanıldı" : "used"
                                baseColor: AppTheme.textMuted
                                basePixelSize: 12
                            }
                            Item { Layout.fillWidth: true }
                            GText {
                                Layout.alignment: Qt.AlignBaseline
                                text: page.lang.language === "tr" ? "Toplam" : "From"
                                baseColor: AppTheme.textMuted
                                basePixelSize: 12
                            }
                            GText {
                                objectName: "diskOverviewTotal"
                                text: page.storageModel.formatBytes(diskOverviewPanel.totalBytes)
                                baseColor: AppTheme.text
                                basePixelSize: 16
                                baseFontWeight: Font.DemiBold
                            }
                        }

                        // Segmented usage bar: one rounded part per disk, then free space.
                        Item {
                            id: segmentBar
                            objectName: "diskOverviewSegments"
                            Layout.fillWidth: true
                            Layout.preferredHeight: 10
                            readonly property real gap: 6
                            readonly property var widths: {
                                const parts = diskOverviewPanel.segments
                                const total = parts.reduce(function(sum, part) { return sum + part.bytes }, 0)
                                const space = Math.max(0, width - gap * Math.max(0, parts.length - 1))
                                if (total <= 0 || space <= 0) return parts.map(function() { return 0 })
                                // Keep tiny disks visible, then shrink the rest to fit.
                                const minimum = Math.min(8, space / Math.max(1, parts.length))
                                let raw = parts.map(function(part) { return Math.max(minimum, space * part.bytes / total) })
                                const excess = raw.reduce(function(sum, value) { return sum + value }, 0) - space
                                if (excess > 0) {
                                    const shrinkable = raw.reduce(function(sum, value) { return sum + Math.max(0, value - minimum) }, 0)
                                    raw = raw.map(function(value) {
                                        return shrinkable > 0 ? value - excess * Math.max(0, value - minimum) / shrinkable : value
                                    })
                                }
                                return raw
                            }
                            Row {
                                anchors.fill: parent
                                spacing: segmentBar.gap
                                Repeater {
                                    model: diskOverviewPanel.segments.length
                                    Rectangle {
                                        required property int index
                                        width: segmentBar.widths[index] || 0
                                        height: segmentBar.height
                                        radius: height / 2
                                        color: diskOverviewPanel.segments[index].color
                                        Behavior on width { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
                                        HoverHandler { id: segmentHover }
                                        GToolTip {
                                            visible: segmentHover.hovered
                                            text: diskOverviewPanel.segments[index].label + " · "
                                                  + page.storageModel.formatBytes(diskOverviewPanel.segments[index].bytes)
                                        }
                                    }
                                }
                            }
                        }

                        Flow {
                            Layout.fillWidth: true
                            spacing: 16
                            Repeater {
                                model: diskOverviewPanel.segments.length
                                Row {
                                    required property int index
                                    spacing: 6
                                    readonly property var segment: diskOverviewPanel.segments[index]
                                    Rectangle {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 8; height: 8; radius: 4
                                        color: parent.segment.free ? AppTheme.textFaint : parent.segment.color
                                        opacity: parent.segment.free ? 0.45 : 1
                                    }
                                    GText {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: parent.segment.free
                                              ? parent.segment.label + "  " + page.storageModel.formatBytes(diskOverviewPanel.freeBytes)
                                              : parent.segment.label + "  " + page.storageModel.formatBytes(parent.segment.bytes)
                                        baseColor: AppTheme.textMuted
                                        basePixelSize: 10
                                    }
                                }
                            }
                        }
                    }
                }

                GridLayout {
                    id: diskGrid
                    objectName: "diskGrid"
                    readonly property bool compactLayout: discoverViews.disks === "compact"
                    Layout.fillWidth: true
                    Layout.leftMargin: 6
                    Layout.rightMargin: 6
                    columns: compactLayout
                        ? (width > 1180 ? 6 : (width > 940 ? 5 : (width > 700 ? 4 : (width > 460 ? 3 : 2))))
                        : (width > 940 ? 4 : (width > 700 ? 3 : (width > 460 ? 2 : 1)))
                    columnSpacing: compactLayout ? 10 : 14
                    rowSpacing: compactLayout ? 10 : 14

                    Repeater {
                        model: page.storageModel
                        delegate: DiskCard {
                            language: page.lang.language
                            diskIndex: index
                            compact: diskGrid.compactLayout
                            unmountInMenu: compact
                            Layout.fillWidth: true
                            Layout.preferredHeight: responsiveHeight
                            title: name
                            stableId: model.stableId
                            iconName: model.icon
                            systemIconPixels: page.systemHomeIconSize
                            subtitle: rootPath
                            fileSystem: model.fileSystem
                            rootPath: model.rootPath
                            device: model.device
                            mounted: model.mounted
                            canMountToggle: model.canMountToggle
                            mountBusy: model.busy
                            mountActionText: model.busy
                                             ? (model.mounted ? lang.t("unmounting") : lang.t("mounting"))
                                             : (model.canMountToggle ? (model.mounted ? (lang.language === "tr" ? "Bağlantıyı kes" : "Disconnect")
                                                                             : lang.t("mount")) : "")
                            mountedStatusText: model.mounted ? (lang.language === "tr" ? "Bağlı" : "Connected")
                                                             : lang.t("not_mounted")
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
                            onPropertiesRequested: function(path, title, iconName) {
                                page.openDiskProperties(path, title, iconName)
                            }
                            onRefreshRequested: page.storageModel.refresh()
                            onReorderRequested: function(from, to) { page.storageModel.moveDisk(from, to) }
                            onReorderDragStateChanged: function(active) { page.storageModel.setReorderActive(active) }
                            onFavoriteToggleRequested: function(path, diskTitle) {
                                page.storageModel.toggleFavorite(path)
                            }
                        }
                    }
                }

                // Mounted disk images. Shown with the same cards as Disks, and
                // not at all (header included) while no image is mounted.
                DiscoverSectionHeader {
                    objectName: "mountedImagesHeader"
                    visible: DiskImages.count > 0
                    title: lang.t("mounted_images")
                    countText: String(DiskImages.count)
                    DiscoverHeaderButton {
                        iconName: "refresh.svg"
                        text: lang.t("refresh")
                        onClicked: DiskImages.refresh()
                    }
                }

                GridLayout {
                    visible: DiskImages.count > 0
                    Layout.fillWidth: true
                    Layout.leftMargin: 6
                    Layout.rightMargin: 6
                    columns: width > 940 ? 4 : (width > 700 ? 3 : (width > 460 ? 2 : 1))
                    columnSpacing: 14
                    rowSpacing: 14

                    Repeater {
                        model: DiskImages
                        delegate: DiskCard {
                            language: page.lang.language
                            diskReorder: false
                            favoritesEnabled: false
                            editable: false
                            unmountInMenu: true
                            Layout.fillWidth: true
                            Layout.preferredHeight: responsiveHeight
                            title: model.name
                            stableId: model.stableId
                            iconName: "drive-optical.svg"
                            systemIconPixels: page.systemHomeIconSize
                            subtitle: model.rootPath
                            fileSystem: model.fileSystem
                            rootPath: model.rootPath
                            // The card hands this back when it is asked to unmount;
                            // for images that is the image file, not a block device.
                            device: model.imagePath
                            mounted: true
                            canMountToggle: true
                            mountBusy: model.busy
                            mountActionText: model.busy ? lang.t("unmounting")
                                                        : (lang.language === "tr" ? "Bağlantıyı kes" : "Disconnect")
                            mountedStatusText: lang.language === "tr" ? "Bağlı" : "Connected"
                            usedText: model.totalBytes > 0 ? page.storageModel.formatBytes(model.usedBytes) : ""
                            totalText: model.totalBytes > 0 ? page.storageModel.formatBytes(model.totalBytes)
                                                            : (lang.language === "tr" ? "Kapasite bilinmiyor" : "Capacity unavailable")
                            ratio: model.usedRatio
                            onOpenRequested: function(path) { page.browseRequested(path) }
                            onOpenNewTabRequested: function(path) { page.browseNewTabRequested(path) }
                            onMountToggleRequested: function(imagePath) { DiskImages.unmountImage(imagePath) }
                            onPropertiesRequested: function(path, diskTitle, iconName) {
                                page.openDiskProperties(path, diskTitle, iconName)
                            }
                            onRefreshRequested: DiskImages.refresh()
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
                            GText {
                                text: lang.language === "tr" ? "İkon boyutları" : "Icon sizes"
                                baseColor: AppTheme.text
                                basePixelSize: 13
                                baseFontWeight: Font.DemiBold
                            }
                            GText {
                                Layout.fillWidth: true
                                text: lang.language === "tr"
                                      ? "Değişiklikler anında uygulanır ve kaydedilir"
                                      : "Changes are applied and saved immediately"
                                baseColor: AppTheme.textMuted
                                basePixelSize: 10
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

        DiscoverInfoPanel {
            visible: page.infoPanelOpen
            Layout.preferredWidth: visible ? 320 : 0
            Layout.minimumWidth: visible ? 320 : 0
            Layout.maximumWidth: visible ? 320 : 0
            Layout.fillHeight: true
            home: page
            lang: page.lang
            storageModel: page.storageModel
            contentIndexModel: page.contentIndexModel
            updateChecker: page.updateChecker
            operationPresentation: page.operationPresentation
        }
    }

    // Small status pill under the Home clock.
    component SummaryChip: Rectangle {
        id: chip
        property string iconName
        property string text
        property string toolTip
        property bool accent: false
        property bool warning: false
        signal clicked()
        readonly property color tone: warning ? "#E8A23A" : accent ? AppTheme.accent : AppTheme.textMuted
        height: 26
        width: chipRow.implicitWidth + 20
        radius: 13
        color: chipMouse.containsMouse ? AppTheme.surfaceHover
             : accent ? AppTheme.accentSoft
             : warning ? Qt.rgba(0.91, 0.64, 0.23, AppTheme.dark ? 0.16 : 0.12)
             : (AppTheme.dark ? Qt.rgba(1, 1, 1, 0.04) : AppTheme.surfaceRaised)
        border.width: 1
        border.color: accent ? AppTheme.accentBorder : warning ? Qt.rgba(0.91, 0.64, 0.23, 0.5) : AppTheme.border
        Row {
            id: chipRow
            anchors.centerIn: parent
            spacing: 6
            CrispIcon {
                anchors.verticalCenter: parent.verticalCenter
                width: 13; height: 13
                source: chip.iconName.length ? AppTheme.icon(chip.iconName, chip.tone) : ""
            }
            GText {
                anchors.verticalCenter: parent.verticalCenter
                text: chip.text
                baseColor: chip.accent || chip.warning ? chip.tone : AppTheme.text
                basePixelSize: 10
                baseFontWeight: Font.Medium
            }
        }
        MouseArea {
            id: chipMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: chip.clicked()
        }
        GToolTip { visible: chipMouse.containsMouse && chip.toolTip.length > 0; text: chip.toolTip }
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
                    GText {
                        text: cloudManageRow.providerName
                        baseColor: AppTheme.text
                        basePixelSize: 12
                        baseFontWeight: Font.DemiBold
                    }
                    GText {
                        Layout.fillWidth: true
                        text: cloudManageRow.providerDetail
                        baseColor: AppTheme.textMuted
                        basePixelSize: 9
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
                    contentItem: GText {
        typographyRole: "menu"
                        text: cloudRowActionButton.text
                        baseColor: AppTheme.textMuted
                        basePixelSize: 9
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
                basePixelSize: 10
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
                    GText {
                        text: lang.language === "tr" ? "Bulut entegrasyonlarını yönet" : "Manage cloud integrations"
                        baseColor: AppTheme.text
                        basePixelSize: 17
                        baseFontWeight: Font.DemiBold
                    }
                    GText {
                        Layout.fillWidth: true
                        text: lang.language === "tr"
                              ? "Yalnızca kullandıklarını aç. Pasif servisler Keşfet ana ekranında yer kaplamaz."
                              : "Enable only what you use. Disabled services do not take space on the Discover home page."
                        baseColor: AppTheme.textMuted
                        basePixelSize: 10
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
                    contentItem: GText {
        typographyRole: "menu"
                        text: closeCloudManagerXButton.text
                        baseColor: AppTheme.textMuted
                        basePixelSize: 22
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
                id: cloudManagerScroll
                GPopupWheelScroll {
                    parent: cloudManagerScroll
                    anchors.fill: parent
                    view: cloudManagerScroll.contentItem
                    scrollbar: cloudManagerScroll.ScrollBar.vertical
                }
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
                            GText {
                                Layout.fillWidth: true
                                text: lang.language === "tr"
                                      ? "Nextcloud, ownCloud ve WebDAV adreslerinde https:// kullanabilirsin; Lurviko bunu KIO için webdavs:// biçimine dönüştürür. Kullanıcı adı/parolayı URL'ye yazma; KIO gerektiğinde kimlik bilgilerini ister ve KDE Wallet ile saklayabilir."
                                      : "You may enter https:// URLs for Nextcloud, ownCloud and WebDAV; Lurviko converts them to webdavs:// for KIO. Do not put credentials in the URL; KIO can request them when needed and store them with KDE Wallet."
                                baseColor: AppTheme.textMuted
                                basePixelSize: 9
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
                    contentItem: GText {
        typographyRole: "menu"
                        text: closeCloudManagerButton.text
                        baseColor: "white"
                        basePixelSize: 10
                        baseFontWeight: Font.DemiBold
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
                        GText {
                            text: page.editingShortcutIndex < 0
                                  ? (lang.language === "tr" ? "Yeni hızlı erişim" : "New quick access")
                                  : (page.editingShortcutFixed
                                     ? (lang.language === "tr" ? "Son Konumu düzenle" : "Edit Last Location")
                                     : (lang.language === "tr" ? "Hızlı erişimi düzenle" : "Edit quick access"))
                            baseColor: AppTheme.text
                            basePixelSize: 18
                            baseFontWeight: Font.DemiBold
                        }
                        GText {
                            Layout.fillWidth: true
                            text: page.editingShortcutFixed
                                  ? (lang.language === "tr" ? "Adı ve simgeyi özelleştir" : "Customize its name and icon")
                                  : (lang.language === "tr" ? "Adı, konumu ve simgeyi özelleştir" : "Customize its name, location, and icon")
                            baseColor: AppTheme.textMuted
                            basePixelSize: 10
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
                        contentItem: GText {
        typographyRole: "menu"
                            text: shortcutCloseButton.text
                            baseColor: AppTheme.textMuted
                            basePixelSize: 21
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }

            GText { text: lang.language === "tr" ? "AD" : "NAME"; baseColor: AppTheme.textFaint; basePixelSize: 9; baseBold: true; baseLetterSpacing: 1 }
            GTextField {
                id: shortcutNameInput
                Layout.fillWidth: true
                color: AppTheme.text
                placeholderText: lang.language === "tr" ? "Kısayol adı" : "Shortcut name"
                placeholderTextColor: AppTheme.textFaint
                background: Rectangle { radius: 11; color: AppTheme.surfaceRaised; border.color: shortcutNameInput.activeFocus ? AppTheme.accent : AppTheme.border }
            }

            GText {
                visible: !page.editingShortcutFixed
                text: lang.language === "tr" ? "KONUM" : "LOCATION"
                baseColor: AppTheme.textFaint
                basePixelSize: 9
                baseBold: true
                baseLetterSpacing: 1
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

            GText { text: lang.language === "tr" ? "SİMGE" : "ICON"; baseColor: AppTheme.textFaint; basePixelSize: 9; baseBold: true; baseLetterSpacing: 1 }
            GButton {
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                visible: AppTheme.useSystemIcons
                Layout.fillWidth: true
                text: lang.language === "tr" ? "Plasma sistem ikonlarından seç…" : "Choose from Plasma system icons…"
                onClicked: {
                    var initial = AppTheme.iconPickerInitial(page.editingShortcutIcon)
                    var chosen = shortcutIconPicker.chooseSystemIcon(initial,
                        lang.language === "tr" ? "Hızlı erişim ikonu seç" : "Choose quick access icon")
                    if (chosen && chosen.length)
                        page.editingShortcutIcon = AppTheme.iconSelection(chosen)
                }
            }

            GButton {
                objectName: "shortcutIconFileButton"
                Layout.fillWidth: true
                text: lang.language === "tr" ? "Dosyadan ikon seç…" : "Choose icon from file…"
                onClicked: {
                    const chosen = shortcutIconPicker.chooseIconFile(AppTheme.localIconSource(page.editingShortcutIcon),
                        lang.language === "tr" ? "Hızlı erişim ikonu seç" : "Choose quick access icon")
                    if (chosen.length)
                        page.editingShortcutIcon = AppTheme.iconSelection(chosen)
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

            GText {
                visible: page.shortcutError.length > 0
                Layout.fillWidth: true
                text: page.shortcutError
                baseColor: AppTheme.danger
                basePixelSize: 10
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
                        GText {
                            text: lang.language === "tr" ? "Öğeyi düzenle" : "Edit item"
                            baseColor: AppTheme.text
                            basePixelSize: 18
                            baseFontWeight: Font.DemiBold
                        }
                        GText {
                            Layout.fillWidth: true
                            text: lang.language === "tr"
                                  ? "Keşfet kartının adını ve simgesini özelleştir"
                                  : "Customize the Discover card name and icon"
                            baseColor: AppTheme.textMuted
                            basePixelSize: 10
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

            GText { text: lang.language === "tr" ? "AD" : "NAME"; baseColor: AppTheme.textFaint; basePixelSize: 9; baseBold: true; baseLetterSpacing: 1 }
            GTextField {
                id: discoverEditNameInput
                Layout.fillWidth: true
                color: AppTheme.text
                placeholderText: lang.language === "tr" ? "Görünen ad" : "Display name"
                placeholderTextColor: AppTheme.textFaint
                selectByMouse: true
                background: Rectangle { radius: 11; color: AppTheme.surfaceRaised; border.color: discoverEditNameInput.activeFocus ? AppTheme.accent : AppTheme.border }
            }

            GText { text: lang.language === "tr" ? "SİMGE" : "ICON"; baseColor: AppTheme.textFaint; basePixelSize: 9; baseBold: true; baseLetterSpacing: 1 }
            GButton {
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                visible: AppTheme.useSystemIcons
                Layout.fillWidth: true
                text: lang.language === "tr" ? "Plasma sistem ikonlarından seç…" : "Choose from Plasma system icons…"
                onClicked: {
                    var initial = AppTheme.iconPickerInitial(page.editingDiscoverIcon)
                    var chosen = shortcutIconPicker.chooseSystemIcon(initial,
                        lang.language === "tr" ? "Öğe ikonu seç" : "Choose item icon")
                    if (chosen && chosen.length)
                        page.editingDiscoverIcon = AppTheme.iconSelection(chosen)
                }
            }

            GButton {
                objectName: "discoverIconFileButton"
                Layout.fillWidth: true
                text: lang.language === "tr" ? "Dosyadan ikon seç…" : "Choose icon from file…"
                onClicked: {
                    const chosen = shortcutIconPicker.chooseIconFile(AppTheme.localIconSource(page.editingDiscoverIcon),
                        lang.language === "tr" ? "Öğe ikonu seç" : "Choose item icon")
                    if (chosen.length)
                        page.editingDiscoverIcon = AppTheme.iconSelection(chosen)
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

            GText {
                visible: page.discoverEditError.length > 0
                Layout.fillWidth: true
                text: page.discoverEditError
                baseColor: AppTheme.danger
                basePixelSize: 10
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
            GText { text: page.oauthProvider === "google" ? "Google Drive OAuth" : "OneDrive OAuth"; basePixelSize: 16; baseBold: true; baseColor: AppTheme.text }
            GText {
                text: page.oauthProvider === "google"
                      ? (lang.language === "tr"
                         ? "Desktop OAuth istemcisinin Client ID'sini gir. İndirilen JSON'da client_secret varsa onu da ekleyebilirsin; Google masaüstü akışında bu alan opsiyoneldir."
                         : "Enter the Desktop OAuth client's Client ID. If the downloaded JSON contains client_secret, you can add it too; Google marks it optional for desktop token exchange.")
                      : (lang.language === "tr"
                         ? "Uygulama (istemci) kimliğini gir. Kiracı alanı Client ID'nin yerine geçmez; yalnızca hangi Microsoft hesaplarının giriş yapacağını belirler. Sistem tarayıcısı için Entra > Authentication > Mobile and desktop applications bölümüne http://localhost redirect URI'sini ekle."
                         : "Enter the Application (client) ID. The tenant value does not replace the Client ID; it only selects which Microsoft accounts can sign in. For the system-browser flow, register http://localhost under Entra > Authentication > Mobile and desktop applications.")
                baseColor: AppTheme.textMuted
                basePixelSize: 11
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
            GText {
                visible: page.oauthProvider === "onedrive"
                Layout.fillWidth: true
                text: (lang.language === "tr" ? "Kayıtlı redirect URI: " : "Registered redirect URI: ") + cloudAuth.oneDriveRedirectUri
                baseColor: AppTheme.accent
                basePixelSize: 11
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
            GText {
                text: page.oauthProvider === "google"
                    ? (lang.language === "tr" ? "Google Drive bağlantı hatası" : "Google Drive connection error")
                    : (lang.language === "tr" ? "OneDrive bağlantı hatası" : "OneDrive connection error")
                baseColor: AppTheme.text
                basePixelSize: 17
                baseBold: true
            }
            GText {
                Layout.fillWidth: true
                text: page.oauthErrorText
                baseColor: AppTheme.textMuted
                basePixelSize: 11
                wrapMode: Text.WordWrap
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: AppTheme.border }
            GText {
                Layout.fillWidth: true
                visible: true
                text: page.oauthProvider === "google"
                      ? (lang.language === "tr"
                         ? "Client türü Desktop app olmalı. Hata invalid_client veya client_secret ile ilgiliyse Google Cloud'dan Desktop client JSON'unu indirip Client ID ve varsa Client Secret alanlarını Lurviko OAuth ayarlarına gir."
                         : "The client type must be Desktop app. If the error mentions invalid_client or client_secret, download the Desktop client JSON and enter its Client ID and optional Client Secret in Lurviko.")
                      : (lang.language === "tr"
                         ? "OneDrive için Application (client) ID kullanılır. Entra > Authentication > Add a platform > Mobile and desktop applications altında http://localhost redirect URI'sini kaydet. Tenant genelde common bırakılabilir; kişisel hesap için consumers, yalnızca iş/okul hesapları için organizations kullanabilirsin."
                         : "OneDrive uses the Application (client) ID. Under Entra > Authentication > Add a platform > Mobile and desktop applications, register http://localhost. Tenant can usually stay common; use consumers for personal accounts or organizations for work/school accounts only.")
                baseColor: AppTheme.textMuted
                basePixelSize: 11
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
