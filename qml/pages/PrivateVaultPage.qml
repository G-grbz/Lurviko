import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Lurviko.App
import Lurviko.Backend
import "../components"

Rectangle {
    id: page
    required property var lang
    required property var vault
    required property var musicPlayer
    signal closeRequested()

    color: AppTheme.workspace
    radius: Math.max(0, AppTheme.panelRadius - 1)
    antialiasing: true
    property string selectedId: ""
    property string selectedName: ""
    property bool selectedIsDir: false
    property string selectedMime: ""
    property string materializedUrl: ""
    property string pendingExportId: ""
    property string renameId: ""
    property string renameOriginal: ""
    property string vaultMusicUrl: ""

    function suffixOf(name) {
        const value = String(name || "")
        const dot = value.lastIndexOf(".")
        return dot >= 0 ? value.substring(dot + 1).toLowerCase() : ""
    }

    function vaultItemKind(name, mimeType) {
        const mime = String(mimeType || "").toLowerCase()
        const ext = suffixOf(name)
        if (mime.indexOf("image/") === 0 || ["jpg","jpeg","png","webp","gif","bmp","tif","tiff","avif","heic","heif","jxl","svg"].indexOf(ext) >= 0)
            return "image"
        if (mime.indexOf("video/") === 0 || ["mp4","mkv","avi","mov","webm","m4v","mpg","mpeg","ts","m2ts","mts","flv","wmv","ogv","3gp"].indexOf(ext) >= 0)
            return "video"
        if (mime.indexOf("audio/") === 0 || ["mp3","flac","wav","ogg","opus","m4a","aac","wma","alac","ape","aiff","aif"].indexOf(ext) >= 0)
            return "audio"
        if (["pdf","txt","rtf","odt","ods","odp","doc","docx","xls","xlsx","ppt","pptx","csv","tsv","epub","mobi","djvu","xps"].indexOf(ext) >= 0
                || mime.indexOf("application/pdf") === 0 || mime.indexOf("text/") === 0)
            return "document"
        return "external"
    }

    function formatSize(bytes) {
        const n = Number(bytes || 0)
        if (n < 1024) return n + " B"
        if (n < 1024 * 1024) return (n / 1024).toFixed(1) + " KiB"
        if (n < 1024 * 1024 * 1024) return (n / (1024 * 1024)).toFixed(1) + " MiB"
        return (n / (1024 * 1024 * 1024)).toFixed(1) + " GiB"
    }

    function releasePreview() {
        if (materializedUrl.length) {
            vault.releaseMaterialized(materializedUrl)
            materializedUrl = ""
        }
    }

    function lockWhenLeavingIfNeeded() {
        if (vault.unlocked && !vault.autoLockEnabled)
            vault.lock()
    }

    function leavePrivatePage() {
        closeVaultMedia()
        lockWhenLeavingIfNeeded()
        closeRequested()
    }

    function closeVaultMedia() {
        if (photoViewer.opened)
            photoViewer.close()
        if (videoViewer.opened)
            videoViewer.close()
        if (page.vaultMusicUrl.length) {
            if (page.musicPlayer && page.musicPlayer.currentUrl === page.vaultMusicUrl)
                page.musicPlayer.closePlayer()
            vault.releaseMaterialized(page.vaultMusicUrl)
            page.vaultMusicUrl = ""
        }
        page.releasePreview()
        page.selectedId = ""
    }

    function openVaultItem(id, name, isDir, mimeType, size, modifiedMs) {
        if (isDir) {
            selectedId = ""
            vault.enterFolder(id)
            return
        }
        const mime = String(mimeType || "")
        const kind = vaultItemKind(name, mime)
        const url = vault.materializeForOpen(id)
        if (!url || !url.length)
            return
        const item = ({
            url: url,
            itemUrl: url,
            path: "vault://" + id,
            name: name,
            size: Number(size || 0),
            modifiedMs: Number(modifiedMs || 0),
            mimeType: mime,
            thumbnailSource: ""
        })

        if (kind === "image") {
            releasePreview()
            materializedUrl = url
            photoViewer.files = [item]
            photoViewer.openAt(0)
            return
        }
        if (kind === "video") {
            releasePreview()
            materializedUrl = url
            // VideoViewer keeps its currentIndex after close. Reset the source
            // model first so reopening the same vault video is a real source
            // change for QMediaPlayer instead of reusing the stopped source.
            videoViewer.currentIndex = -1
            videoViewer.files = []
            Qt.callLater(function() {
                videoViewer.files = [item]
                videoViewer.openAt(0)
            })
            return
        }
        if (kind === "audio") {
            if (page.vaultMusicUrl.length && page.vaultMusicUrl !== url) {
                if (page.musicPlayer && page.musicPlayer.currentUrl === page.vaultMusicUrl)
                    page.musicPlayer.closePlayer()
                vault.releaseMaterialized(page.vaultMusicUrl)
            }
            page.vaultMusicUrl = url
            if (page.musicPlayer)
                page.musicPlayer.openUrl(url, name, "", [item])
            return
        }

        // Documents and other supported desktop formats are opened from a
        // random-name 0600 runtime copy.  This is not an export: the plaintext
        // copy remains inside Lurviko's private runtime area and is removed when
        // the vault locks / Lurviko exits.
        if (!Qt.openUrlExternally(url)) {
            vault.releaseMaterialized(url)
            infoPopupTitle.text = lang.language === "tr" ? "Dosya açılamadı" : "Could not open file"
            infoPopupBody.text = lang.language === "tr"
                    ? "Bu dosya türü için sistemde ilişkilendirilmiş bir uygulama bulunamadı."
                    : "No associated desktop application was found for this file type."
            infoPopup.open()
        }
    }

    onVisibleChanged: {
        if (!visible) {
            closeVaultMedia()
            lockWhenLeavingIfNeeded()
        }
    }

    Component.onDestruction: {
        releasePreview()
        lockWhenLeavingIfNeeded()
    }


    Connections {
        target: vault
        function onAboutToLock() { page.closeVaultMedia() }
        function onStateChanged() {
            if (!vault.unlocked) {
                page.selectedId = ""
                securityPopup.close()
                currentPassword.text = ""
                newPassword.text = ""
                newPasswordAgain.text = ""
            }
        }
    }

    FileDialog {
        id: importDialog
        title: lang.language === "tr" ? "Sana Özel'e dosya ekle" : "Add files to Private"
        fileMode: FileDialog.OpenFiles
        onAccepted: vault.importUrls(selectedFiles)
    }

    FolderDialog {
        id: importFolderDialog
        title: lang.language === "tr" ? "Sana Özel'e klasör ekle" : "Add folder to Private"
        onAccepted: vault.importUrls([selectedFolder])
    }

    FolderDialog {
        id: exportDialog
        title: lang.language === "tr" ? "Şifresi çözülmüş kopyanın kaydedileceği klasör" : "Folder for decrypted copy"
        onAccepted: {
            if (page.pendingExportId.length)
                vault.exportItem(page.pendingExportId, selectedFolder)
            page.pendingExportId = ""
        }
        onRejected: page.pendingExportId = ""
    }

    GModalPopup {
        id: newFolderPopup
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 420
        height: 210
        modal: true
        focus: true
        background: GModalSurface { }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 22
            spacing: 12
            Text { text: lang.language === "tr" ? "Yeni şifreli klasör" : "New encrypted folder"; color: AppTheme.text; font.pixelSize: 18; font.bold: true }
            GTextField {
                id: newFolderName
                Layout.fillWidth: true
                placeholderText: lang.language === "tr" ? "Klasör adı" : "Folder name"
                color: AppTheme.text
                onAccepted: createFolderButton.clicked()
            }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                GModalButton { text: lang.language === "tr" ? "İptal" : "Cancel"; onClicked: newFolderPopup.close() }
                GModalButton {
                    id: createFolderButton
                    text: lang.language === "tr" ? "Oluştur" : "Create"
                    primary: true
                    onClicked: {
                        if (vault.createFolder(newFolderName.text)) {
                            newFolderName.text = ""
                            newFolderPopup.close()
                        }
                    }
                }
            }
        }
        onOpened: { newFolderName.text = ""; newFolderName.forceActiveFocus() }
    }

    GModalPopup {
        id: renamePopup
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 420
        height: 210
        modal: true
        focus: true
        background: GModalSurface { }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 22
            spacing: 12
            Text { text: lang.language === "tr" ? "Yeniden adlandır" : "Rename"; color: AppTheme.text; font.pixelSize: 18; font.bold: true }
            GTextField { id: renameInput; Layout.fillWidth: true; color: AppTheme.text; onAccepted: renameConfirm.clicked() }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                GModalButton { text: lang.language === "tr" ? "İptal" : "Cancel"; onClicked: renamePopup.close() }
                GModalButton {
                    id: renameConfirm
                    text: lang.language === "tr" ? "Kaydet" : "Save"
                    primary: true
                    onClicked: {
                        if (vault.renameItem(page.renameId, renameInput.text))
                            renamePopup.close()
                    }
                }
            }
        }
        onOpened: { renameInput.text = page.renameOriginal; renameInput.selectAll(); renameInput.forceActiveFocus() }
    }

    GModalPopup {
        id: infoPopup
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 480
        height: 240
        modal: true
        background: GModalSurface { }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 24
            spacing: 12
            Text { id: infoPopupTitle; Layout.fillWidth: true; color: AppTheme.text; font.pixelSize: 18; font.bold: true }
            Text { id: infoPopupBody; Layout.fillWidth: true; Layout.fillHeight: true; wrapMode: Text.Wrap; color: AppTheme.textMuted; font.pixelSize: 11 }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                GModalButton {
                    text: "Tamam"
                    onClicked: infoPopup.close()
                }
            }
        }
    }

    GModalPopup {
        id: deletePopup
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 440
        height: 220
        modal: true
        background: GModalSurface { }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 24
            spacing: 12
            Text { text: lang.language === "tr" ? "Kasadan kalıcı olarak sil?" : "Permanently delete from vault?"; color: AppTheme.text; font.pixelSize: 18; font.bold: true }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: AppTheme.textMuted
                text: lang.language === "tr"
                      ? "Bu işlem geri alınamaz. Şifreli blob ve manifest kaydı silinecek."
                      : "This cannot be undone. The encrypted blob and manifest entry will be removed."
            }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                GModalButton { text: lang.language === "tr" ? "İptal" : "Cancel"; onClicked: deletePopup.close() }
                GModalButton {
                    text: lang.language === "tr" ? "Kalıcı Sil" : "Delete"
                    danger: true
                    onClicked: {
                        if (page.selectedId.length && vault.deleteItem(page.selectedId)) {
                            page.selectedId = ""
                            deletePopup.close()
                        }
                    }
                }
            }
        }
    }


    GModalPopup {
        id: securityPopup
        objectName: "vaultSecurityPopup"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(620, page.width - 40)
        height: Math.min(760, page.height - 30)
        modal: true
        focus: true
        background: GModalSurface { }
        onOpened: {
            currentPassword.text = ""
            newPassword.text = ""
            newPasswordAgain.text = ""
            passwordMismatch.visible = false
            vault.clearStatus()
            passwordProtectionCheck.checked = vault.passwordProtectionEnabled
            passwordAttemptLimit.value = vault.maxPasswordAttempts
            passwordLockoutDuration.value = vault.passwordLockoutSeconds
            passwordRetryDelay.value = vault.passwordRetrySeconds
        }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 24
            spacing: 14
            Text {
                text: lang.language === "tr" ? "Sana Özel · Güvenlik" : "Private · Security"
                color: AppTheme.text
                font.pixelSize: 20
                font.bold: true
            }
            Flickable {
                id: securityScroll
                objectName: "vaultSecurityScroll"
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: width
                contentHeight: securityContent.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ColumnLayout {
                    id: securityContent
                    width: Math.max(0, securityScroll.width - 16)
                    spacing: 14
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: AppTheme.textMuted
                        font.pixelSize: 10
                        text: lang.language === "tr"
                              ? "Kasa kapanınca ana anahtar RAM'den temizlenir. KWallet etkinse bu cihazda parolayı tekrar yazmadan güvenli hızlı açma kullanılabilir."
                              : "The master key is cleared from RAM when the vault locks. With KWallet enabled, this device can securely quick-unlock without retyping the vault password."
                    }

                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: AppTheme.border }
                    Text { text: lang.language === "tr" ? "Parolayı değiştir" : "Change password"; color: AppTheme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
                    GTextField { id: currentPassword; Layout.fillWidth: true; echoMode: TextInput.Password; placeholderText: lang.language === "tr" ? "Mevcut parola" : "Current password"; color: AppTheme.text }
                    GTextField { id: newPassword; Layout.fillWidth: true; echoMode: TextInput.Password; placeholderText: lang.language === "tr" ? "Yeni parola · en az 8 karakter" : "New password · at least 8 characters"; color: AppTheme.text }
                    GTextField { id: newPasswordAgain; Layout.fillWidth: true; echoMode: TextInput.Password; placeholderText: lang.language === "tr" ? "Yeni parolayı tekrar yaz" : "Repeat new password"; color: AppTheme.text }
                    Text { id: passwordMismatch; visible: false; color: AppTheme.danger; font.pixelSize: 10; text: lang.language === "tr" ? "Yeni parolalar eşleşmiyor." : "New passwords do not match." }
                    GModalButton {
                        text: lang.language === "tr" ? "Parolayı Değiştir" : "Change Password"
                        enabled: currentPassword.text.length > 0 && newPassword.text.length >= 8 && !vault.busy
                        onClicked: {
                            passwordMismatch.visible = newPassword.text !== newPasswordAgain.text
                            if (passwordMismatch.visible)
                                return
                            if (vault.changePassword(currentPassword.text, newPassword.text)) {
                                currentPassword.text = ""
                                newPassword.text = ""
                                newPasswordAgain.text = ""
                            }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: AppTheme.border }
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text { text: lang.language === "tr" ? "KWallet ile hızlı kilit aç" : "Quick unlock with KWallet"; color: AppTheme.text; font.pixelSize: 12; font.weight: Font.DemiBold }
                            Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: lang.language === "tr" ? "Ana kasa anahtarını KDE KWallet içinde saklar; parola kasanın kendi şifrelemesi için yine geçerlidir." : "Stores the vault master key inside KDE KWallet; the vault password still remains valid."; color: AppTheme.textMuted; font.pixelSize: 9 }
                        }
                        GSwitch {
                            id: kwalletSwitch
                            checked: vault.kwalletEnabled
                            onClicked: {
                                if (!vault.setKWalletEnabled(checked))
                                    checked = vault.kwalletEnabled
                            }
                        }
                    }

                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: AppTheme.border }
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text { text: lang.language === "tr" ? "Açık kalma süresi" : "Unlocked duration"; color: AppTheme.text; font.pixelSize: 12; font.weight: Font.DemiBold }
                            Text { Layout.fillWidth: true; wrapMode: Text.Wrap; text: lang.language === "tr" ? "Açıksa kasa kilidi açıldıktan sonra seçilen süre dolunca otomatik kilitlenir. Kapalıysa Sana Özel'den çıkar çıkmaz veya Lurviko kapanınca kilitlenir." : "When enabled, the vault locks after the selected time from unlock. When disabled, it locks as soon as you leave Private or when Lurviko exits."; color: AppTheme.textMuted; font.pixelSize: 9 }
                        }
                        GSwitch {
                            id: autoLockSwitch
                            checked: vault.autoLockEnabled
                            onClicked: {
                                if (!vault.setAutoLockEnabled(checked))
                                    checked = vault.autoLockEnabled
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        enabled: vault.autoLockEnabled
                        opacity: enabled ? 1.0 : 0.45
                        Text { text: lang.language === "tr" ? "Süre" : "Duration"; color: AppTheme.textMuted; font.pixelSize: 10 }
                        Item { Layout.fillWidth: true }
                        GComboBox {
                            id: autoLockDuration
                            model: [5, 15, 30, 60]
                            currentIndex: vault.autoLockMinutes === 5 ? 0 : (vault.autoLockMinutes === 15 ? 1 : (vault.autoLockMinutes === 30 ? 2 : 3))
                            textRole: ""
                            displayText: currentValue + " " + (lang.language === "tr" ? "dakika" : "minutes")
                            onActivated: function(index) { vault.setAutoLockMinutes(Number(model[index])) }
                        }
                    }


                    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: AppTheme.border }
                    GCheckBox {
                        id: passwordProtectionCheck
                        objectName: "vaultPasswordProtection"
                        Layout.fillWidth: true
                        text: lang.language === "tr" ? "Hatalı parola denemelerine karşı koruma" : "Protect against failed password attempts"
                        checked: vault.passwordProtectionEnabled
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: AppTheme.textMuted
                        font.pixelSize: 10
                        text: lang.language === "tr"
                              ? "Deneme sınırına ulaşılınca giriş geçici olarak kilitlenir. Başarılı giriş ve kilit süresinin dolması sayacı sıfırlar. Uygulamayı yeniden açmak beklemeyi sıfırlamaz."
                              : "Reaching the attempt limit temporarily blocks sign-in. Successful sign-in and lockout expiry reset the counter. Restarting the app keeps the remaining wait."
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        enabled: passwordProtectionCheck.checked
                        opacity: enabled ? 1 : 0.45
                        spacing: 10
                        RowLayout {
                            Layout.fillWidth: true
                            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: lang.language === "tr" ? "En fazla hatalı deneme" : "Maximum failed attempts"; color: AppTheme.text; font.pixelSize: 11 }
                            GSpinBox { id: passwordAttemptLimit; objectName: "vaultPasswordAttemptLimit"; editable: true; from: 1; to: 100; value: vault.maxPasswordAttempts; implicitWidth: 146 }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: lang.language === "tr" ? "Geçici kilit süresi (saniye)" : "Lockout duration (seconds)"; color: AppTheme.text; font.pixelSize: 11 }
                            GSpinBox { id: passwordLockoutDuration; objectName: "vaultPasswordLockoutDuration"; editable: true; from: 1; to: 86400; value: vault.passwordLockoutSeconds; implicitWidth: 146 }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: lang.language === "tr" ? "Hatalı denemeler arası bekleme (saniye)" : "Delay after a failed attempt (seconds)"; color: AppTheme.text; font.pixelSize: 11 }
                            GSpinBox { id: passwordRetryDelay; objectName: "vaultPasswordRetryDelay"; editable: true; from: 0; to: 3600; value: vault.passwordRetrySeconds; implicitWidth: 146 }
                        }
                    }
                    GModalButton {
                        objectName: "vaultSavePasswordProtection"
                        text: lang.language === "tr" ? "Koruma ayarlarını kaydet" : "Save protection settings"
                        enabled: !vault.busy
                        onClicked: vault.setPasswordProtection(passwordProtectionCheck.checked, passwordAttemptLimit.value,
                                                               passwordLockoutDuration.value, passwordRetryDelay.value)
                    }
                }
                ScrollBar.vertical: ScrollBar {
                    width: 8
                    policy: ScrollBar.AsNeeded
                    active: hovered || pressed || securityScroll.moving
                    background: null
                    contentItem: Rectangle { radius: 4; color: AppTheme.textMuted; opacity: parent.active ? 0.75 : 0 }
                }
            }

            Text {
                visible: vault.lastError.length > 0 || vault.statusMessage.length > 0
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: vault.lastError.length > 0 ? vault.lastError : vault.statusMessage
                color: vault.lastError.length > 0 ? AppTheme.danger : AppTheme.success
                font.pixelSize: 10
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                GModalButton { text: lang.language === "tr" ? "Kapat" : "Close"; primary: true; onClicked: securityPopup.close() }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            spacing: 9
            GToolButton {
                implicitWidth: 36; implicitHeight: 36
                icon.source: AppTheme.icon("nav-back.svg")
                onClicked: page.leavePrivatePage()
                GToolTip { text: lang.language === "tr" ? "Keşfet'e dön" : "Back to Discover" }
                background: Rectangle { radius: 9; color: parent.hovered ? AppTheme.surfaceHover : "transparent" }
            }
            CrispIcon { Layout.preferredWidth: 30; Layout.preferredHeight: 30; width: 30; height: 30; source: AppTheme.icon("private.svg") }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                Text { text: lang.language === "tr" ? "Sana Özel" : "Private"; color: AppTheme.text; font.pixelSize: 20; font.bold: true }
                Text {
                    text: vault.unlocked ? vault.currentPathLabel
                                         : (lang.language === "tr" ? "Lurviko şifreli kasası" : "Lurviko encrypted vault")
                    color: AppTheme.textMuted
                    font.pixelSize: 10
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }
            }
            Rectangle {
                visible: vault.unlocked
                radius: 10
                color: AppTheme.accentSoft
                border.color: AppTheme.accentBorder
                implicitWidth: securityLabel.implicitWidth + 22
                implicitHeight: 32
                Text {
                    id: securityLabel
                    anchors.centerIn: parent
                    text: lang.language === "tr" ? "AES-256-GCM · Argon2id" : "AES-256-GCM · Argon2id"
                    color: AppTheme.accent
                    font.pixelSize: 9
                    font.weight: Font.DemiBold
                }
            }
        }

        Rectangle {
            visible: !vault.exists
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 20
            color: AppTheme.surface
            border.color: AppTheme.border
            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(560, parent.width - 60)
                spacing: 14
                CrispIcon { Layout.alignment: Qt.AlignHCenter; Layout.preferredWidth: 88; Layout.preferredHeight: 88; width: 88; height: 88; source: AppTheme.icon("private.svg") }
                Text { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; text: lang.language === "tr" ? "Sana Özel kasanı oluştur" : "Create your Private vault"; color: AppTheme.text; font.pixelSize: 24; font.bold: true }
                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    color: AppTheme.textMuted
                    text: lang.language === "tr"
                          ? "Dosya adları, klasör yapısı, metadata ve içerikler şifreli tutulur. Diskte yalnız rastgele isimli bloblar vardır; Lurviko kasayı mount etmez."
                          : "Names, folder structure, metadata and contents stay encrypted. Only randomly named blobs exist on disk; Lurviko never mounts a plaintext filesystem."
                }
                GTextField { id: createPassword; Layout.fillWidth: true; echoMode: TextInput.Password; placeholderText: lang.language === "tr" ? "En az 8 karakter parola" : "Password, at least 8 characters"; color: AppTheme.text }
                GTextField { id: createPasswordAgain; Layout.fillWidth: true; echoMode: TextInput.Password; placeholderText: lang.language === "tr" ? "Parolayı tekrar yaz" : "Repeat password"; color: AppTheme.text }
                Text { visible: vault.lastError.length > 0; Layout.fillWidth: true; wrapMode: Text.Wrap; color: AppTheme.danger; text: vault.lastError; font.pixelSize: 10 }
                GModalButton {
                    Layout.alignment: Qt.AlignHCenter
                    text: lang.language === "tr" ? "Şifreli Kasayı Oluştur" : "Create Encrypted Vault"
                    primary: true
                    enabled: !vault.busy
                    onClicked: {
                        vault.clearStatus()
                        if (createPassword.text !== createPasswordAgain.text) {
                            mismatchText.visible = true
                            return
                        }
                        mismatchText.visible = false
                        if (vault.createVault(createPassword.text)) {
                            createPassword.text = ""
                            createPasswordAgain.text = ""
                        }
                    }
                }
                Text { id: mismatchText; visible: false; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; color: AppTheme.danger; text: lang.language === "tr" ? "Parolalar eşleşmiyor." : "Passwords do not match."; font.pixelSize: 10 }
            }
        }

        Rectangle {
            visible: vault.exists && !vault.unlocked
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 20
            color: AppTheme.surface
            border.color: AppTheme.border
            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(470, parent.width - 60)
                spacing: 14
                CrispIcon { Layout.alignment: Qt.AlignHCenter; Layout.preferredWidth: 76; Layout.preferredHeight: 76; width: 76; height: 76; source: AppTheme.icon("lock.svg") }
                Text { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; text: lang.language === "tr" ? "Kasa kilitli" : "Vault locked"; color: AppTheme.text; font.pixelSize: 22; font.bold: true }
                Text { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap; text: lang.language === "tr" ? "Parola diske kaydedilmez. Kasa anahtarı yalnız bu oturumda bellekte tutulur." : "The password is never stored. The vault key stays in memory only for this session."; color: AppTheme.textMuted; font.pixelSize: 10 }
                GTextField { id: unlockPassword; objectName: "vaultUnlockPassword"; Layout.fillWidth: true; echoMode: TextInput.Password; placeholderText: lang.language === "tr" ? "Kasa parolası" : "Vault password"; color: AppTheme.text; onAccepted: unlockButton.clicked() }
                VaultAttemptStatus { Layout.fillWidth: true; vault: page.vault; lang: page.lang }
                Text { visible: vault.lastError.length > 0; Layout.fillWidth: true; wrapMode: Text.Wrap; color: AppTheme.danger; text: vault.lastError; font.pixelSize: 10 }
                GModalButton {
                    id: unlockButton
                    objectName: "vaultUnlockButton"
                    Layout.alignment: Qt.AlignHCenter
                    text: lang.language === "tr" ? "Kilidi Aç" : "Unlock"
                    primary: true
                    enabled: !vault.busy && unlockPassword.text.length > 0 && vault.passwordWaitSeconds === 0
                    onClicked: {
                        if (vault.unlock(unlockPassword.text))
                            unlockPassword.text = ""
                    }
                }
                GModalButton {
                    visible: vault.kwalletEnabled
                    Layout.alignment: Qt.AlignHCenter
                    text: lang.language === "tr" ? "KWallet ile Hızlı Aç" : "Quick Unlock with KWallet"
                    enabled: !vault.busy && vault.passwordWaitSeconds === 0
                    onClicked: vault.quickUnlock()
                }
            }
        }

        Item {
            visible: vault.unlocked
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 7
                    GToolButton {
                        implicitWidth: 34; implicitHeight: 34
                        enabled: vault.currentFolderId !== "root" && !vault.busy
                        icon.source: AppTheme.icon("nav-back.svg")
                        onClicked: vault.goUp()
                        background: Rectangle { radius: 9; color: parent.hovered ? AppTheme.surfaceHover : "transparent" }
                    }
                    GButton {
                        text: lang.language === "tr" ? "+ Dosya" : "+ Files"
                        enabled: !vault.busy
                        onClicked: importDialog.open()
                    }
                    GButton {
                        text: lang.language === "tr" ? "+ Klasör İçe Aktar" : "+ Import Folder"
                        enabled: !vault.busy
                        onClicked: importFolderDialog.open()
                    }
                    GButton {
                        text: lang.language === "tr" ? "Yeni Klasör" : "New Folder"
                        enabled: !vault.busy
                        onClicked: newFolderPopup.open()
                    }
                    Item { Layout.fillWidth: true }
                    Text { visible: vault.statusMessage.length > 0; text: vault.statusMessage; color: AppTheme.success; font.pixelSize: 9; elide: Text.ElideRight; Layout.maximumWidth: 360 }
                    GButton {
                        text: lang.language === "tr" ? "Güvenlik" : "Security"
                        enabled: !vault.busy
                        icon.source: AppTheme.icon("settings.svg")
                        onClicked: securityPopup.open()
                    }
                    GButton {
                        text: lang.language === "tr" ? "Kilitle" : "Lock"
                        enabled: !vault.busy
                        onClicked: vault.lock()
                    }
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: AppTheme.border }

                GridView {
                    id: vaultGrid
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: vault
                    cellWidth: Math.max(150, Math.floor(width / Math.max(1, Math.floor(width / 180))))
                    cellHeight: 150
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    delegate: Item {
                        id: tile
                        required property string objectId
                        required property string name
                        required property bool isDir
                        required property var size
                        required property string mimeType
                        required property var modifiedMs
                        required property string iconName
                        property string thumbSource: isDir ? "" : vault.thumbnailUrl(objectId)
                        width: vaultGrid.cellWidth
                        height: vaultGrid.cellHeight

                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: 6
                            radius: 14
                            color: page.selectedId === tile.objectId ? AppTheme.accentSoft : (tileMouse.containsMouse ? AppTheme.surfaceHover : AppTheme.surface)
                            border.color: page.selectedId === tile.objectId ? AppTheme.accent : AppTheme.border
                            border.width: 1

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 6
                                Item {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: Math.min(parent.width, 104)
                                        height: Math.min(parent.height, 82)
                                        radius: 11
                                        color: "transparent"
                                        clip: true
                                        Image {
                                            anchors.fill: parent
                                            source: tile.thumbSource
                                            fillMode: Image.PreserveAspectCrop
                                            asynchronous: true
                                            cache: false
                                            smooth: true
                                            visible: tile.thumbSource.length > 0 && status === Image.Ready
                                        }
                                        CrispIcon {
                                            anchors.centerIn: parent
                                            width: 58
                                            height: 58
                                            visible: tile.thumbSource.length === 0
                                            source: AppTheme.useSystemIcons
                                                    ? AppTheme.systemIconAtSize(AppTheme.systemIcon(AppTheme.systemIconName(tile.iconName)), 58)
                                                    : AppTheme.icon(tile.iconName)
                                        }
                                    }
                                    Rectangle {
                                        anchors.right: parent.right
                                        anchors.bottom: parent.bottom
                                        width: 24; height: 24; radius: 12
                                        color: AppTheme.accentSoft
                                        border.color: AppTheme.accentBorder
                                        Text { anchors.centerIn: parent; text: "🔒"; font.pixelSize: 11 }
                                    }
                                }
                                Text { Layout.fillWidth: true; text: tile.name; color: AppTheme.text; font.pixelSize: 11; font.weight: Font.DemiBold; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideMiddle }
                                Text { Layout.fillWidth: true; text: tile.isDir ? (lang.language === "tr" ? "Şifreli klasör" : "Encrypted folder") : page.formatSize(tile.size); color: AppTheme.textMuted; font.pixelSize: 8; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight }
                            }
                        }

                        MouseArea {
                            id: tileMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            onClicked: function(event) {
                                page.selectedId = tile.objectId
                                page.selectedName = tile.name
                                page.selectedIsDir = tile.isDir
                                page.selectedMime = tile.mimeType
                                if (event.button === Qt.RightButton) {
                                    const point = tileMouse.mapToItem(Overlay.overlay, event.x, event.y)
                                    itemMenu.parent = Overlay.overlay
                                    itemMenu.x = point.x
                                    itemMenu.y = point.y
                                    itemMenu.open()
                                }
                            }
                            onDoubleClicked: page.openVaultItem(tile.objectId, tile.name, tile.isDir, tile.mimeType, tile.size, tile.modifiedMs)
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: vault.itemCount === 0 && !vault.busy
                        text: lang.language === "tr" ? "Bu şifreli klasör boş" : "This encrypted folder is empty"
                        color: AppTheme.textMuted
                        font.pixelSize: 13
                    }
                    GBusyIndicator { anchors.centerIn: parent; visible: vault.busy; running: vault.busy; width: 48; height: 48 }
                }
            }
        }
    }

    GMenu {
        id: itemMenu
        GMenuItem {
            text: lang.language === "tr" ? "Aç" : "Open"
            onTriggered: page.openVaultItem(page.selectedId, page.selectedName, page.selectedIsDir, page.selectedMime, 0, 0)
        }
        GMenuItem {
            text: lang.language === "tr" ? "Yeniden adlandır" : "Rename"
            onTriggered: { page.renameId = page.selectedId; page.renameOriginal = page.selectedName; renamePopup.open() }
        }
        GMenuItem {
            text: lang.language === "tr" ? "Dışa aktar…" : "Export…"
            onTriggered: { page.pendingExportId = page.selectedId; exportDialog.open() }
        }
        GMenuSeparator { }
        GMenuItem {
            text: lang.language === "tr" ? "Kalıcı sil" : "Delete permanently"
            onTriggered: deletePopup.open()
        }
    }

    PhotoViewer {
        id: photoViewer
        lang: page.lang
        hostWindow: ApplicationWindow.window
        normalOverlayParent: Overlay.overlay
        files: []
        onClosed: page.releasePreview()
    }

    VideoViewer {
        id: videoViewer
        lang: page.lang
        hostWindow: ApplicationWindow.window
        normalOverlayParent: Overlay.overlay
        files: []
        resetMediaOnClose: true
        onClosed: page.releasePreview()
    }
}
