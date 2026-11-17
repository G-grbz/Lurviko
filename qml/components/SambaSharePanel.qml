import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lurviko.Backend
import Lurviko.App

// Samba sharing of a folder, in the file properties. Beyond Dolphin's
// sharing page: per-user and per-group rules (read only / full control /
// deny) with presets, guest access that follows smb.conf, every address the
// share can be reached at with copy buttons, the user's Samba password, and
// a status view of the service, connected clients and open shares.
Rectangle {
    id: panel
    property var lang
    property string location: ""
    property bool folder: false
    property bool active: false
    property var textEditMenu: null
    signal notify(string message)
    readonly property bool tr: lang && lang.language === "tr"
    function t(trText, enText) { return tr ? trText : enText }

    // Draft of the form; saved with the primary button.
    property bool shareEnabled: false
    property bool guestDraft: false
    property var aclDraft: []
    property bool addressesShown: false
    // Filesystem permissions the drafted access needs; recomputed when the
    // draft changes or permissions were fixed.
    property int permissionRevision: 0
    // Permissions this application opened for the folder that the current
    // draft does not need: all of them while the folder is not to be shared.
    // Shown whether or not anything else changed, so a fix that was never
    // followed by saving the share can always be taken back.
    readonly property var restoreSuggestion: {
        const revision = permissionRevision + (sharing.busy ? 0 : 0)
        return sharing.available ? sharing.restorePlan(aclDraft, guestDraft, !shareEnabled)
                                 : ({ needed: false, items: [] })
    }
    readonly property var permissionPlan: {
        const revision = permissionRevision + (sharing.busy ? 0 : 0)
        return shareEnabled && sharing.available ? sharing.permissionPlan(aclDraft, guestDraft)
                                                 : ({ needed: false, items: [] })
    }

    readonly property bool aclDirty: JSON.stringify(normalizedRules(aclDraft))
                                     !== JSON.stringify(normalizedRules(sharing.aclEntries))
    readonly property bool dirty: sharing.available && (sharing.shared
        ? (!shareEnabled || commentField.text !== sharing.comment
           || guestDraft !== sharing.guestOk || aclDirty)
        : (shareEnabled && nameField.text.trim().length > 0))
    readonly property int grantCount: aclDraft.filter(function(entry) {
        return entry.access === "R" || entry.access === "F"
    }).length

    implicitHeight: contents.implicitHeight + 30
    color: AppTheme.surfaceRaised
    border.color: AppTheme.border
    radius: 16

    readonly property var accessOptions: [
        { value: "", text: "—" },
        { value: "R", text: t("Salt okunur", "Read only") },
        { value: "F", text: t("Tam denetim", "Full control") },
        { value: "D", text: t("Reddet", "Deny") }
    ]

    function normalizedRules(entries) {
        const result = []
        for (const entry of entries || [])
            if (String(entry.access || "").length)
                result.push(String(entry.name) + ":" + String(entry.access))
        return result
    }
    function refresh() {
        if (active && folder && location.length) sharing.inspect(location)
        else sharing.clear()
    }
    function resetDraft() {
        shareEnabled = sharing.shared
        nameField.text = sharing.shared ? sharing.shareName : defaultShareName()
        commentField.text = sharing.comment
        guestDraft = sharing.guestOk
        aclDraft = sharing.aclEntries.map(function(entry) { return Object.assign({}, entry) })
        addressesShown = false
    }
    function defaultShareName() {
        const parts = String(sharing.path || "").split("/").filter(function(part) { return part.length > 0 })
        const base = parts.length ? parts[parts.length - 1] : ""
        // Letters (any script), digits, space, '_', '.', '-'; must start
        // with a letter or digit, as the manager requires.
        let name = ""
        for (const c of base) {
            const letter = c.toLowerCase() !== c.toUpperCase()
            const digit = c >= "0" && c <= "9"
            if (letter || digit || (name.length && " _.-".indexOf(c) >= 0)) name += c
            else if (name.length) name += "_"
        }
        return name.replace(/[ .]+$/, "").substring(0, 80)
    }
    function setAccess(index, value) {
        const next = aclDraft.slice()
        next[index] = Object.assign({}, next[index], { access: value })
        aclDraft = next
    }
    function removeRule(index) {
        const next = aclDraft.slice()
        next.splice(index, 1)
        aclDraft = next
    }
    function applyPreset(preset) {
        aclDraft = aclDraft.map(function(entry) {
            let access = ""
            if (preset === "read") access = entry.kind === "everyone" ? "R" : ""
            else if (preset === "full") access = entry.kind === "everyone" ? "F" : ""
            else if (preset === "me") access = entry.current ? "F" : ""
            return Object.assign({}, entry, { access: access })
        })
    }
    function addRule(name) {
        const account = sharing.normalizedAccount(name)
        if (!account.length) {
            addRuleError.text = t("Geçersiz kullanıcı veya grup adı.", "Invalid user or group name.")
            return
        }
        for (let i = 0; i < aclDraft.length; ++i) {
            if (String(aclDraft[i].name).toLowerCase() === account.toLowerCase()) {
                if (!aclDraft[i].access) setAccess(i, "R")
                addRuleField.text = ""
                addRuleError.text = ""
                return
            }
        }
        aclDraft = aclDraft.concat([{ name: account, access: "R", kind: "custom", current: false }])
        addRuleField.text = ""
        addRuleError.text = ""
    }
    function save() {
        if (!shareEnabled) {
            if (!sharing.shared)
                return
            // Offer to take back what was opened up for this share.
            if (sharing.restorePlan(aclDraft, guestDraft, true).needed)
                permissionPopup.show("restore", true, true)
            else
                sharing.remove()
            return
        }
        if (permissionPlan.needed) {
            permissionPopup.show("warning", true)
            return
        }
        // Permissions opened earlier (saved or not) that this access does
        // not need: offer to take them back first.
        if (sharing.restorePlan(aclDraft, guestDraft, false).needed) {
            permissionPopup.show("restore", true, false)
            return
        }
        commitShare()
    }
    function commitShare() {
        sharing.saveShare(nameField.text.trim(), commentField.text, aclDraft, guestDraft)
    }
    // What goes wrong without the fix, most important first.
    readonly property string permissionProblem: {
        const items = permissionPlan.items || []
        if (items.some(function(item) { return item.role === "traverse" })) return "reach"
        if (permissionPlan.level === "full") return "write"
        if (permissionPlan.level === "read") return "read"
        return "owner"
    }
    function roleText(role) {
        if (role === "traverse")
            return t("Üst klasör · diğer kullanıcıların içinden geçebilmesi için",
                     "Parent folder · so other users can pass through it")
        if (role === "owner")
            return t("Paylaşılan klasör · senin hesabının erişimi için", "Shared folder · for your own account")
        return permissionPlan.level === "full"
            ? t("Paylaşılan klasör · diğer kullanıcıların okuyup yazabilmesi için",
                "Shared folder · so other users can read and write")
            : t("Paylaşılan klasör · diğer kullanıcıların okuyabilmesi için",
                "Shared folder · so other users can read")
    }
    function copyText(text) {
        clipboardHelper.text = text
        clipboardHelper.selectAll()
        clipboardHelper.copy()
        clipboardHelper.text = ""
        panel.notify(t("Panoya kopyalandı.", "Copied to the clipboard."))
    }
    function serviceText() {
        if (sharing.serviceState === "active") return t("Samba çalışıyor", "Samba running")
        if (sharing.serviceState === "inactive") return t("Samba durdu", "Samba stopped")
        return t("Samba durumu bilinmiyor", "Samba status unknown")
    }

    onActiveChanged: refresh()
    onLocationChanged: if (active) refresh()
    onFolderChanged: if (active) refresh()

    TextEdit { id: clipboardHelper; visible: false }

    SambaShareManager {
        id: sharing
        objectName: "sambaShareManager"
        onChanged: if (!busy && available && !error.length) panel.resetDraft()
        onOperationFinished: function(success, message) { panel.notify(message.replace(/</g, "‹").replace(/>/g, "›")) }
        onPermissionsFinished: function(success, message) {
            panel.permissionRevision += 1
            if (!success) {
                permissionPopup.errorText = message
                return
            }
            const share = permissionPopup.shareAfterFix
            permissionPopup.close()
            if (share) panel.commitShare()
            else panel.notify(message)
        }
        onPermissionsRestored: function(success, message) {
            panel.permissionRevision += 1
            if (!success) {
                permissionPopup.errorText = message
                return
            }
            const removing = permissionPopup.removing
            const proceed = permissionPopup.shareAfterFix
            permissionPopup.close()
            panel.notify(message)
            // Only continue with the share when the dialog came from saving.
            if (!proceed) return
            if (removing) { if (sharing.shared) sharing.remove() }
            else panel.commitShare()
        }
        onPasswordFinished: function(success, message) {
            passwordStatus.text = message
            passwordStatus.success = success
            if (success) {
                passwordField.text = ""
                passwordAgainField.text = ""
                passwordPopup.close()
                panel.notify(message)
            }
        }
    }

    component Chip: Rectangle {
        property alias text: chipLabel.text
        property color tone: AppTheme.textMuted
        implicitWidth: chipLabel.implicitWidth + 18
        implicitHeight: 22
        radius: 11
        color: Qt.rgba(tone.r, tone.g, tone.b, 0.12)
        border.color: Qt.rgba(tone.r, tone.g, tone.b, 0.35)
        GText { id: chipLabel; anchors.centerIn: parent; baseColor: parent.tone; basePixelSize: 10; baseFontWeight: Font.DemiBold }
    }

    ColumnLayout {
        id: contents
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 15
        spacing: 12

        // Header: state at a glance.
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            GText {
                text: panel.t("Ağ paylaşımı", "Network sharing")
                basePixelSize: 13
                baseFontWeight: Font.DemiBold
                baseColor: AppTheme.text
            }
            Chip {
                visible: panel.folder && sharing.available
                objectName: "sambaShareStateChip"
                text: sharing.shared ? panel.t("Paylaşılıyor", "Shared") : panel.t("Paylaşılmıyor", "Not shared")
                tone: sharing.shared ? AppTheme.success : AppTheme.textMuted
            }
            Chip {
                visible: panel.folder && sharing.available
                text: panel.serviceText()
                tone: sharing.serviceState === "active" ? AppTheme.success
                    : sharing.serviceState === "inactive" ? AppTheme.danger : AppTheme.textMuted
            }
            Item { Layout.fillWidth: true }
            GBusyIndicator { running: sharing.busy; visible: running; implicitWidth: 24; implicitHeight: 24 }
            GModalButton {
                text: panel.t("Yenile", "Refresh")
                enabled: panel.folder && !sharing.busy
                onClicked: { sharing.refreshEnvironment(); panel.refresh() }
            }
        }

        GText {
            Layout.fillWidth: true
            visible: !panel.folder
            text: panel.t("Samba paylaşımı klasörler üzerinden yapılır. Bu dosyayı paylaşmak için bulunduğu klasörün özelliklerini aç.",
                          "Samba shares folders. To share this file, open its containing folder's properties.")
            wrapMode: Text.Wrap
            baseColor: AppTheme.textMuted
            basePixelSize: 11
        }
        GText {
            Layout.fillWidth: true
            visible: panel.folder && sharing.error.length > 0
            text: sharing.error
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            baseColor: AppTheme.danger
            basePixelSize: 11
        }

        ColumnLayout {
            visible: panel.folder && sharing.available
            enabled: !sharing.busy
            Layout.fillWidth: true
            spacing: 12

            GCheckBox {
                id: shareToggle
                objectName: "sambaShareToggle"
                Layout.fillWidth: true
                text: panel.t("Bu klasörü yerel ağda diğer bilgisayarlarla paylaş",
                              "Share this folder with other computers on the local network")
                checked: panel.shareEnabled
                onToggled: panel.shareEnabled = checked
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 12
                enabled: panel.shareEnabled
                opacity: enabled ? 1.0 : 0.5

                GComboBox {
                    visible: sharing.shareNames.length > 1
                    Layout.fillWidth: true
                    model: sharing.shareNames
                    onActivated: sharing.selectShare(currentText)
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 3
                    columnSpacing: 10
                    rowSpacing: 8
                    GText { text: panel.t("Ad", "Name"); baseColor: AppTheme.textMuted; basePixelSize: 11 }
                    GTextField {
                        id: nameField
                        objectName: "sambaShareName"
                        Layout.fillWidth: true
                        readOnly: sharing.shared
                        maximumLength: 80
                        placeholderText: panel.t("Örneğin Belgeler", "For example Documents")
                        selectByMouse: true
                        persistentSelection: true
                        EditorContextMenu { menu: panel.textEditMenu }
                    }
                    GModalButton {
                        objectName: "sambaShowAddresses"
                        enabled: sharing.shared
                        text: panel.addressesShown ? panel.t("Adresleri gizle", "Hide addresses")
                                                   : panel.t("Adresleri göster", "Show addresses")
                        onClicked: panel.addressesShown = !panel.addressesShown
                    }
                    GText { text: panel.t("Açıklama", "Comment"); baseColor: AppTheme.textMuted; basePixelSize: 11 }
                    GTextField {
                        id: commentField
                        objectName: "sambaShareComment"
                        Layout.fillWidth: true
                        Layout.columnSpan: 2
                        maximumLength: 256
                        placeholderText: panel.t("İsteğe bağlı, ağda görünen açıklama", "Optional description shown on the network")
                        selectByMouse: true
                        persistentSelection: true
                        EditorContextMenu { menu: panel.textEditMenu }
                    }
                }

                // Every address of the share, with copy buttons.
                Rectangle {
                    objectName: "sambaAddressList"
                    visible: panel.addressesShown && sharing.shared
                    Layout.fillWidth: true
                    Layout.preferredHeight: addressColumn.implicitHeight + 20
                    radius: 12
                    color: AppTheme.surface
                    border.color: AppTheme.border
                    ColumnLayout {
                        id: addressColumn
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 6
                        Repeater {
                            model: panel.addressesShown ? sharing.shareAddresses() : []
                            delegate: RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 8
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    GText { text: modelData.label; baseColor: AppTheme.textMuted; basePixelSize: 9 }
                                    GText {
                                        Layout.fillWidth: true
                                        text: modelData.url
                                        textFormat: Text.PlainText
                                        baseColor: AppTheme.text
                                        basePixelSize: 11
                                        elide: Text.ElideMiddle
                                    }
                                }
                                GModalButton {
                                    text: "smb://"
                                    onClicked: panel.copyText(modelData.url)
                                    GToolTip { text: panel.t("Adresi kopyala", "Copy address") }
                                }
                                GModalButton {
                                    text: "\\\\"
                                    onClicked: panel.copyText(modelData.unc)
                                    GToolTip { text: panel.t("Windows yolunu kopyala: ", "Copy Windows path: ") + modelData.unc }
                                }
                            }
                        }
                    }
                }

                GCheckBox {
                    id: guestBox
                    objectName: "sambaGuestAccess"
                    Layout.fillWidth: true
                    enabled: sharing.guestsAllowed || sharing.guestOk
                    text: panel.t("Konukların (parolasız) bağlanmasına izin ver", "Allow guests to connect without a password")
                    checked: panel.guestDraft
                    onToggled: panel.guestDraft = checked
                }
                GText {
                    Layout.fillWidth: true
                    visible: !sharing.guestsAllowed
                    text: panel.t("Konuk erişimi, sistemin Samba yapılandırmasında kapalı (usershare allow guests = no).",
                                  "Guest access is disabled in the system Samba configuration (usershare allow guests = no).")
                    wrapMode: Text.Wrap
                    baseColor: AppTheme.textFaint
                    basePixelSize: 10
                }

                // Access table.
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: aclColumn.implicitHeight + 24
                    radius: 12
                    color: AppTheme.surface
                    border.color: AppTheme.border
                    ColumnLayout {
                        id: aclColumn
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 8

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            GText {
                                Layout.fillWidth: true
                                text: panel.t("Erişim izinleri", "Access permissions")
                                baseColor: AppTheme.text
                                basePixelSize: 12
                                baseFontWeight: Font.DemiBold
                            }
                            GModalButton {
                                text: panel.t("Herkes okusun", "Everyone reads")
                                onClicked: panel.applyPreset("read")
                            }
                            GModalButton {
                                text: panel.t("Herkese tam", "Everyone full")
                                onClicked: panel.applyPreset("full")
                            }
                            GModalButton {
                                text: panel.t("Yalnızca ben", "Only me")
                                onClicked: panel.applyPreset("me")
                            }
                        }

                        Repeater {
                            model: panel.aclDraft
                            delegate: RowLayout {
                                id: ruleRow
                                objectName: "sambaAclRow"
                                required property var modelData
                                required property int index
                                Layout.fillWidth: true
                                spacing: 8
                                CrispIcon {
                                    Layout.preferredWidth: 16
                                    Layout.preferredHeight: 16
                                    source: AppTheme.icon(ruleRow.modelData.kind === "everyone" ? "network.svg" : "home.svg")
                                    opacity: 0.8
                                }
                                GText {
                                    Layout.fillWidth: true
                                    text: ruleRow.modelData.kind === "everyone" ? panel.t("Herkes (Everyone)", "Everyone")
                                                                                : String(ruleRow.modelData.name)
                                    textFormat: Text.PlainText
                                    baseColor: AppTheme.text
                                    basePixelSize: 11
                                    baseFontWeight: ruleRow.modelData.current ? Font.DemiBold : Font.Normal
                                    elide: Text.ElideRight
                                }
                                Chip {
                                    visible: !!ruleRow.modelData.current
                                    text: panel.t("Siz", "You")
                                    tone: AppTheme.accent
                                }
                                GModalButton {
                                    objectName: "sambaChangePassword"
                                    visible: !!ruleRow.modelData.current
                                    text: panel.t("Parolayı değiştir…", "Change password…")
                                    onClicked: passwordPopup.open()
                                }
                                GComboBox {
                                    objectName: "sambaAccessCombo"
                                    Layout.preferredWidth: 150
                                    model: panel.accessOptions
                                    textRole: "text"
                                    valueRole: "value"
                                    currentIndex: Math.max(0, ["", "R", "F", "D"].indexOf(String(ruleRow.modelData.access || "")))
                                    onActivated: function(index) { panel.setAccess(ruleRow.index, panel.accessOptions[index].value) }
                                }
                                GToolButton {
                                    visible: ruleRow.modelData.kind === "custom"
                                    implicitWidth: 28; implicitHeight: 28
                                    icon.source: AppTheme.icon("delete.svg")
                                    icon.width: 14; icon.height: 14
                                    onClicked: panel.removeRule(ruleRow.index)
                                    Accessible.name: panel.t("Kuralı kaldır", "Remove rule")
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            GTextField {
                                id: addRuleField
                                objectName: "sambaAddRuleField"
                                Layout.fillWidth: true
                                maximumLength: 128
                                placeholderText: panel.t("Kullanıcı veya grup ekle (ör. ayse, @ofis, ALAN\\kullanici)",
                                                         "Add a user or group (e.g. alice, @office, DOMAIN\\user)")
                                selectByMouse: true
                                onAccepted: panel.addRule(text)
                                EditorContextMenu { menu: panel.textEditMenu }
                            }
                            GModalButton {
                                text: panel.t("Ekle", "Add")
                                enabled: addRuleField.text.trim().length > 0
                                onClicked: panel.addRule(addRuleField.text)
                            }
                        }
                        GText {
                            id: addRuleError
                            Layout.fillWidth: true
                            visible: text.length > 0
                            baseColor: AppTheme.danger
                            basePixelSize: 10
                        }
                        GText {
                            Layout.fillWidth: true
                            text: panel.grantCount === 0
                                  ? panel.t("En az bir kullanıcıya okuma veya tam denetim ver.",
                                            "Give at least one user read or full control.")
                                  : panel.t("Reddet, aynı kişiye başka kuraldan verilen izni de geçersiz kılar. Dosya sistemi izinleri ayrıca geçerlidir.",
                                            "Deny overrides access granted to the same person by another rule. Filesystem permissions still apply.")
                            wrapMode: Text.Wrap
                            baseColor: panel.grantCount === 0 ? AppTheme.danger : AppTheme.textFaint
                            basePixelSize: 10
                        }
                    }
                }

                // Filesystem permissions do not allow the drafted access.
                Rectangle {
                    objectName: "sambaPermissionWarning"
                    visible: panel.permissionPlan.needed
                    Layout.fillWidth: true
                    Layout.preferredHeight: permissionWarningRow.implicitHeight + 20
                    radius: 12
                    color: Qt.rgba(AppTheme.warning.r, AppTheme.warning.g, AppTheme.warning.b, 0.12)
                    border.color: Qt.rgba(AppTheme.warning.r, AppTheme.warning.g, AppTheme.warning.b, 0.55)
                    RowLayout {
                        id: permissionWarningRow
                        anchors.fill: parent
                        anchors.margins: 10
                        anchors.leftMargin: 14
                        spacing: 10
                        GText {
                            Layout.fillWidth: true
                            text: panel.t("Klasör izinleri seçilen erişime yetmiyor: diğer kullanıcılar ",
                                          "The folder permissions do not allow the chosen access: other users ")
                                  + (panel.permissionProblem === "reach"
                                     ? panel.t("bu klasöre ulaşamaz", "cannot reach this folder")
                                     : panel.permissionProblem === "write"
                                     ? panel.t("bu klasöre yazamaz", "cannot write to this folder")
                                     : panel.t("bu klasörü okuyamaz", "cannot read this folder"))
                                  + panel.t(". Kaydederken düzeltmen önerilecek.", ". You will be offered a fix when saving.")
                            wrapMode: Text.Wrap
                            baseColor: AppTheme.text
                            basePixelSize: 11
                        }
                        GModalButton {
                            text: panel.t("Ayrıntılar…", "Details…")
                            onClicked: permissionPopup.show("review", false)
                        }
                    }
                }

                GText {
                    Layout.fillWidth: true
                    visible: !sharing.passwordChangeAvailable
                    text: panel.t("Samba parolası için terminalde şunu çalıştır: sudo smbpasswd -a ", "To set the Samba password run: sudo smbpasswd -a ")
                          + sharing.userName
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    baseColor: AppTheme.textMuted
                    basePixelSize: 10
                }
            }

            // Permissions opened for sharing that are no longer needed.
            Rectangle {
                objectName: "sambaRestoreSuggestion"
                visible: panel.restoreSuggestion.needed && !panel.permissionPlan.needed
                Layout.fillWidth: true
                Layout.preferredHeight: restoreSuggestionRow.implicitHeight + 20
                radius: 12
                color: AppTheme.accentSoft
                border.color: AppTheme.accentBorder
                RowLayout {
                    id: restoreSuggestionRow
                    anchors.fill: parent
                    anchors.margins: 10
                    anchors.leftMargin: 14
                    spacing: 10
                    GText {
                        Layout.fillWidth: true
                        text: panel.shareEnabled
                              ? panel.t("Bu klasör için daha önce açılan izinlerin bir kısmı seçtiğin erişim için artık gerekmiyor.",
                                        "Some permissions opened earlier for this folder are not needed for the access you chose.")
                              : panel.t("Bu klasörün izinleri paylaşım için açılmıştı. Klasör paylaşılmayacaksa bunlara gerek yok.",
                                        "This folder's permissions were opened for sharing. They are not needed if the folder is not shared.")
                        wrapMode: Text.Wrap
                        baseColor: AppTheme.text
                        basePixelSize: 11
                    }
                    GModalButton {
                        objectName: "sambaRestoreSuggestionButton"
                        text: panel.t("Geri al…", "Restore…")
                        enabled: !sharing.permissionsBusy
                        onClicked: permissionPopup.show("restore", false, !panel.shareEnabled)
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                GModalButton {
                    objectName: "sambaShowStatus"
                    text: panel.t("Samba durumunu göster", "Show Samba status")
                    onClicked: { statusPopup.open(); sharing.loadStatus(false) }
                }
                Item { Layout.fillWidth: true }
                GModalButton {
                    visible: panel.dirty
                    text: panel.t("Geri al", "Revert")
                    onClicked: panel.resetDraft()
                }
                GModalButton {
                    objectName: "sambaShareSave"
                    primary: true
                    danger: sharing.shared && !panel.shareEnabled
                    text: sharing.shared && !panel.shareEnabled ? panel.t("Paylaşımı kaldır", "Stop sharing")
                        : sharing.shared ? panel.t("Değişiklikleri kaydet", "Save changes")
                        : panel.t("Klasörü paylaş", "Share folder")
                    enabled: panel.dirty && (!panel.shareEnabled
                             || (nameField.text.trim().length > 0 && panel.grantCount > 0))
                    onClicked: panel.save()
                }
            }
        }
    }

    // --- Folder permissions ---------------------------------------------------
    GModalPopup {
        id: permissionPopup
        objectName: "sambaPermissionPopup"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(640, (parent ? parent.width : 640) - 48)
        // "warning": what is wrong; "review": every change before it is made.
        // "warning" / "review": add missing permissions; "restore": take back
        // permissions added earlier that the new access no longer needs.
        property string stage: "warning"
        property bool shareAfterFix: false
        property bool removing: false
        property string errorText: ""
        readonly property var restoreData: {
            const revision = panel.permissionRevision
            return stage === "restore" && opened
                   ? sharing.restorePlan(panel.aclDraft, panel.guestDraft, removing)
                   : ({ needed: false, items: [], skipped: [] })
        }
        readonly property var plan: stage === "restore" ? restoreData : panel.permissionPlan
        readonly property var items: plan.items || []
        readonly property bool hasTraverse: items.some(function(item) { return item.role === "traverse" })
        function show(nextStage, share, removal) {
            stage = nextStage
            shareAfterFix = share
            removing = !!removal
            errorText = ""
            open()
        }
        contentItem: ColumnLayout {
            spacing: 12
            GText {
                Layout.fillWidth: true
                text: permissionPopup.stage === "warning"
                      ? panel.t("Klasör izinleri yetersiz", "Folder permissions are insufficient")
                      : permissionPopup.stage === "restore"
                      ? (permissionPopup.removing ? panel.t("Paylaşım için açılan izinler", "Permissions opened for the share")
                                                  : panel.t("Artık gerekmeyen izinler", "Permissions no longer needed"))
                      : panel.t("Değişecek izinler", "Permission changes")
                typographyRole: "subtitle"
                basePixelSize: 16
                baseBold: true
            }

            // Stage 1: why the share would not work as chosen.
            GText {
                Layout.fillWidth: true
                visible: permissionPopup.stage === "warning"
                text: panel.permissionProblem === "reach"
                      ? panel.t("Seçtiğin erişim diğer kullanıcılara bu klasörü açıyor, ancak üst klasörlerden en az biri diğer kullanıcıların içinden geçmesine izin vermiyor. Bağlananlar paylaşımı hiç açamaz.",
                                "The chosen access opens this folder to other users, but at least one parent folder does not let other users pass through. People who connect cannot open the share at all.")
                      : panel.permissionProblem === "write"
                      ? panel.t("Seçtiğin erişim diğer kullanıcılara bu klasörde tam denetim veriyor, ancak klasörün dosya sistemi izinleri yazmaya izin vermiyor. Samba'da tam denetim görünse de bağlananlar dosya oluşturamaz, değiştiremez veya silemez.",
                                "The chosen access gives other users full control of this folder, but its filesystem permissions do not allow writing. Even with full control in Samba, people who connect cannot create, change or delete files.")
                      : panel.permissionProblem === "read"
                      ? panel.t("Seçtiğin erişim diğer kullanıcılara bu klasörü okuma izni veriyor, ancak klasörün dosya sistemi izinleri buna uygun değil. Bağlananlar klasörün içeriğini göremez.",
                                "The chosen access lets other users read this folder, but its filesystem permissions do not allow it. People who connect cannot see its contents.")
                      : panel.t("Seçtiğin erişim kendi hesabına, klasörün izinlerinin vermediği bir erişim veriyor.",
                                "The chosen access gives your own account more than the folder permissions allow.")
                wrapMode: Text.Wrap
                baseColor: AppTheme.text
                basePixelSize: 11
            }
            GText {
                Layout.fillWidth: true
                visible: permissionPopup.stage === "warning"
                text: panel.t("Samba erişim kuralları ile klasör izinleri birlikte uygulanır; ikisinin de izin vermesi gerekir.",
                              "Samba access rules and folder permissions apply together; both must allow the access.")
                wrapMode: Text.Wrap
                baseColor: AppTheme.textMuted
                basePixelSize: 10
            }

            GText {
                Layout.fillWidth: true
                visible: permissionPopup.stage === "restore"
                text: permissionPopup.removing
                      ? (sharing.shared
                         ? panel.t("Bu klasörü paylaşırken diğer kullanıcılar için açılan izinler var. Paylaşım kaldırılınca bunlara gerek kalmıyor; geri alınmaları önerilir.",
                                   "Permissions were opened for other users when this folder was shared. Without the share they are no longer needed; taking them back is recommended.")
                         : panel.t("Bu klasörün izinleri paylaşım için diğer kullanıcılara açılmış, ancak klasör paylaşılmıyor. Bunlara gerek yok; geri alınmaları önerilir.",
                                   "This folder's permissions were opened to other users for sharing, but the folder is not shared. They are not needed; taking them back is recommended."))
                      : panel.t("Bu paylaşım için daha önce eklenen izinlerin bir kısmı, seçtiğin daha düşük erişim için artık gerekmiyor. Geri almak klasörü yalnızca gerektiği kadar açık bırakır.",
                                "Some permissions added earlier for this share are not needed for the lower access you chose. Taking them back leaves the folder only as open as necessary.")
                wrapMode: Text.Wrap
                baseColor: AppTheme.text
                basePixelSize: 11
            }

            // Stage 2: each path with its current and new permissions.
            ColumnLayout {
                Layout.fillWidth: true
                visible: permissionPopup.stage === "review" || permissionPopup.stage === "restore"
                spacing: 6
                ScrollView {
                    id: permissionList
                    Layout.fillWidth: true
                    // Deep paths list many parents; keep the buttons on screen.
                    Layout.preferredHeight: Math.min(permissionColumn.implicitHeight,
                                                     Math.max(160, (permissionPopup.parent ? permissionPopup.parent.height : 600) - 360))
                    clip: true
                    contentWidth: availableWidth
                    ColumnLayout {
                        id: permissionColumn
                        width: permissionList.availableWidth
                        spacing: 6
                Repeater {
                    model: permissionPopup.stage !== "warning" ? permissionPopup.items : []
                    delegate: Rectangle {
                        required property var modelData
                        objectName: "sambaPermissionItem"
                        Layout.fillWidth: true
                        Layout.preferredHeight: itemColumn.implicitHeight + 18
                        radius: 10
                        color: AppTheme.surfaceRaised
                        border.color: AppTheme.border
                        ColumnLayout {
                            id: itemColumn
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 3
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                GText {
                                    Layout.fillWidth: true
                                    text: modelData.path
                                    textFormat: Text.PlainText
                                    baseColor: AppTheme.text
                                    basePixelSize: 11
                                    baseFontWeight: Font.DemiBold
                                    elide: Text.ElideMiddle
                                }
                                Chip {
                                    visible: !modelData.owned
                                    text: panel.t("Yönetici gerekir", "Needs administrator")
                                    tone: AppTheme.warning
                                }
                            }
                            GText {
                                Layout.fillWidth: true
                                text: permissionPopup.stage === "restore"
                                      ? (modelData.role === "traverse"
                                         ? panel.t("Üst klasör · artık gerekmeyen geçiş izni kaldırılır", "Parent folder · the pass-through permission is no longer needed")
                                         : panel.t("Paylaşılan klasör · artık gerekmeyen izinler kaldırılır", "Shared folder · permissions that are no longer needed are removed"))
                                        + panel.t("  ·  paylaşımdan önce: ", "  ·  before sharing: ") + modelData.originalMode
                                      : panel.roleText(modelData.role)
                                baseColor: AppTheme.textMuted
                                basePixelSize: 10
                            }
                            RowLayout {
                                spacing: 10
                                GText {
                                    text: modelData.currentMode + "  " + modelData.currentText
                                    baseFontFamily: "monospace"
                                    baseColor: AppTheme.textMuted
                                    basePixelSize: 11
                                }
                                GText { text: "→"; baseColor: AppTheme.textMuted; basePixelSize: 12 }
                                GText {
                                    text: modelData.newMode + "  " + modelData.newText
                                    baseFontFamily: "monospace"
                                    baseColor: AppTheme.success
                                    basePixelSize: 11
                                    baseFontWeight: Font.DemiBold
                                }
                            }
                        }
                    }
                }
                    }
                }
                GText {
                    Layout.fillWidth: true
                    visible: permissionPopup.stage === "restore"
                    text: "• " + panel.t("Yalnızca bu uygulamanın paylaşım için eklediği izinler kaldırılır; diğer izinlere dokunulmaz.",
                                         "Only permissions this application added for the share are removed; others are untouched.")
                          + "\n• " + panel.t("Başka bir paylaşımın hâlâ geçtiği üst klasörlerin izni korunur.",
                                              "Parent folders that another share still passes through keep their permission.")
                          + ((permissionPopup.plan.skipped || []).length
                             ? "\n• " + panel.t("Şu klasörlerin izinleri o zamandan beri değiştirilmiş, onlara dokunulmayacak: ",
                                                 "These folders were changed since then and are left alone: ")
                               + permissionPopup.plan.skipped.join(", ")
                             : "")
                          + (permissionPopup.plan.requiresAdmin
                             ? "\n• " + panel.t("Sana ait olmayan klasörler için yönetici parolası istenecek.",
                                                 "You will be asked for the administrator password for folders you do not own.")
                             : "")
                    wrapMode: Text.Wrap
                    baseColor: AppTheme.textMuted
                    basePixelSize: 10
                }
                GText {
                    Layout.fillWidth: true
                    visible: permissionPopup.stage === "review"
                    text: "• " + panel.t("Yalnızca eksik izinler eklenir; hiçbir izin kaldırılmaz.",
                                         "Only missing permissions are added; none are removed.")
                          + "\n• " + panel.t("Klasörün içindeki mevcut dosya ve klasörlerin izinleri değişmez. Bağlananların onları da düzenleyebilmesi gerekiyorsa İzinler sekmesinden alt öğelere uygula.",
                                              "Existing files and folders inside keep their permissions. If people who connect must edit them too, apply permissions to the contents in the Permissions tab.")
                          + (permissionPopup.hasTraverse
                             ? "\n• " + panel.t("Üst klasörlere yalnızca geçiş (x) izni eklenir: diğer kullanıcılar içeriklerini listeleyemez.",
                                                 "Parent folders only get the pass-through (x) permission: other users cannot list their contents.")
                             : "")
                          + (permissionPopup.plan.requiresAdmin
                             ? "\n• " + panel.t("Sana ait olmayan klasörler için yönetici parolası istenecek.",
                                                 "You will be asked for the administrator password for folders you do not own.")
                             : "")
                    wrapMode: Text.Wrap
                    baseColor: AppTheme.textMuted
                    basePixelSize: 10
                }
                GText {
                    Layout.fillWidth: true
                    visible: permissionPopup.stage === "review" && !!permissionPopup.plan.worldWritable
                    text: panel.t("Dikkat: bu bilgisayardaki diğer yerel kullanıcılar da bu klasöre yazabilecek.",
                                  "Note: other local users of this computer will also be able to write to this folder.")
                    wrapMode: Text.Wrap
                    baseColor: AppTheme.warning
                    basePixelSize: 10
                    baseFontWeight: Font.DemiBold
                }
            }

            GText {
                Layout.fillWidth: true
                visible: permissionPopup.errorText.length > 0
                text: permissionPopup.errorText
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                baseColor: AppTheme.danger
                basePixelSize: 11
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                GBusyIndicator { running: sharing.permissionsBusy; visible: running; implicitWidth: 22; implicitHeight: 22 }
                Item { Layout.fillWidth: true }
                GModalButton {
                    text: permissionPopup.stage === "review" && permissionPopup.shareAfterFix
                          ? panel.t("Geri", "Back") : panel.t("İptal", "Cancel")
                    enabled: !sharing.permissionsBusy
                    onClicked: {
                        if (permissionPopup.stage === "review" && permissionPopup.shareAfterFix)
                            permissionPopup.stage = "warning"
                        else
                            permissionPopup.close()
                    }
                }
                GModalButton {
                    objectName: "sambaShareWithoutFix"
                    visible: permissionPopup.stage === "warning"
                    text: panel.t("İzinleri değiştirmeden paylaş", "Share without changing permissions")
                    onClicked: { permissionPopup.close(); panel.commitShare() }
                }
                GModalButton {
                    objectName: "sambaFixPermissions"
                    visible: permissionPopup.stage !== "restore"
                    primary: true
                    enabled: !sharing.permissionsBusy
                    text: permissionPopup.stage === "warning" ? panel.t("İzinleri düzelt…", "Fix permissions…")
                        : permissionPopup.shareAfterFix
                          ? (sharing.shared ? panel.t("İzinleri değiştir ve kaydet", "Change permissions and save")
                                            : panel.t("İzinleri değiştir ve paylaş", "Change permissions and share"))
                          : panel.t("İzinleri değiştir", "Change permissions")
                    onClicked: {
                        if (permissionPopup.stage === "warning") {
                            permissionPopup.stage = "review"
                            return
                        }
                        permissionPopup.errorText = ""
                        sharing.fixPermissions(panel.aclDraft, panel.guestDraft)
                    }
                }
                GModalButton {
                    objectName: "sambaKeepPermissions"
                    visible: permissionPopup.stage === "restore" && permissionPopup.shareAfterFix
                    enabled: !sharing.permissionsBusy
                    text: permissionPopup.removing ? panel.t("İzinleri bırak, paylaşımı kaldır", "Keep permissions, stop sharing")
                                                   : panel.t("İzinleri bırak ve kaydet", "Keep permissions and save")
                    onClicked: {
                        const removing = permissionPopup.removing
                        permissionPopup.close()
                        if (removing) sharing.remove()
                        else panel.commitShare()
                    }
                }
                GModalButton {
                    objectName: "sambaRestorePermissions"
                    visible: permissionPopup.stage === "restore"
                    primary: true
                    enabled: !sharing.permissionsBusy && permissionPopup.items.length > 0
                    text: !permissionPopup.shareAfterFix ? panel.t("İzinleri geri al", "Restore permissions")
                        : permissionPopup.removing ? panel.t("İzinleri geri al, paylaşımı kaldır", "Restore permissions, stop sharing")
                        : panel.t("İzinleri geri al ve kaydet", "Restore permissions and save")
                    onClicked: {
                        permissionPopup.errorText = ""
                        sharing.restorePermissions(panel.aclDraft, panel.guestDraft, permissionPopup.removing)
                    }
                }
            }
        }
    }

    // --- Samba password ------------------------------------------------------
    GModalPopup {
        id: passwordPopup
        objectName: "sambaPasswordPopup"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(460, (parent ? parent.width : 460) - 48)
        onOpened: { passwordStatus.text = ""; passwordField.forceActiveFocus() }
        onClosed: { passwordField.text = ""; passwordAgainField.text = "" }
        readonly property bool matches: passwordField.text === passwordAgainField.text
        contentItem: ColumnLayout {
            spacing: 12
            GText {
                Layout.fillWidth: true
                text: panel.t("Samba parolası: ", "Samba password: ") + sharing.userName
                typographyRole: "subtitle"
                basePixelSize: 16
                baseBold: true
            }
            GText {
                Layout.fillWidth: true
                text: panel.t("Diğer bilgisayarlar bu paylaşıma bağlanırken bu parolayı kullanır. Oturum parolandan ayrıdır; hesabın yoksa oluşturulur. Kaydetmek için yönetici yetkisi istenir.",
                              "Other computers use this password to connect. It is separate from your login password; the Samba account is created when missing. Saving asks for administrator authorization.")
                wrapMode: Text.Wrap
                baseColor: AppTheme.textMuted
                basePixelSize: 11
            }
            GTextField {
                id: passwordField
                objectName: "sambaPasswordField"
                Layout.fillWidth: true
                echoMode: TextInput.Password
                maximumLength: 256
                placeholderText: panel.t("Yeni parola", "New password")
                onAccepted: passwordAgainField.forceActiveFocus()
            }
            GTextField {
                id: passwordAgainField
                objectName: "sambaPasswordAgainField"
                Layout.fillWidth: true
                echoMode: TextInput.Password
                maximumLength: 256
                placeholderText: panel.t("Yeni parolayı tekrar yaz", "Repeat the new password")
                onAccepted: if (passwordSave.enabled) passwordSave.clicked()
            }
            GText {
                id: passwordStatus
                property bool success: false
                Layout.fillWidth: true
                visible: text.length > 0 || (!passwordPopup.matches && passwordAgainField.text.length > 0)
                text: ""
                wrapMode: Text.Wrap
                baseColor: success ? AppTheme.success : AppTheme.danger
                basePixelSize: 11
            }
            GText {
                Layout.fillWidth: true
                visible: !passwordPopup.matches && passwordAgainField.text.length > 0 && passwordStatus.text.length === 0
                text: panel.t("Parolalar eşleşmiyor.", "The passwords do not match.")
                baseColor: AppTheme.danger
                basePixelSize: 11
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 8
                GBusyIndicator { running: sharing.passwordBusy; visible: running; implicitWidth: 22; implicitHeight: 22 }
                GModalButton {
                    text: panel.t("İptal", "Cancel")
                    onClicked: passwordPopup.close()
                }
                GModalButton {
                    id: passwordSave
                    primary: true
                    text: panel.t("Parolayı kaydet", "Save password")
                    enabled: !sharing.passwordBusy && passwordField.text.length > 0 && passwordPopup.matches
                    onClicked: { passwordStatus.text = ""; sharing.changePassword(passwordField.text) }
                }
            }
        }
    }

    // --- Samba status --------------------------------------------------------
    GModalPopup {
        id: statusPopup
        objectName: "sambaStatusPopup"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(620, (parent ? parent.width : 620) - 48)
        height: Math.min(560, (parent ? parent.height : 560) - 48)
        readonly property var status: sharing.status || ({})
        readonly property var sessions: status.sessions || []
        readonly property var connections: status.connections || []
        contentItem: ColumnLayout {
            spacing: 12
            RowLayout {
                Layout.fillWidth: true
                GText {
                    Layout.fillWidth: true
                    text: panel.t("Samba durumu", "Samba status")
                    typographyRole: "subtitle"
                    basePixelSize: 16
                    baseBold: true
                }
                GBusyIndicator { running: sharing.statusBusy; visible: running; implicitWidth: 22; implicitHeight: 22 }
                GModalButton {
                    text: panel.t("Yenile", "Refresh")
                    enabled: !sharing.statusBusy
                    onClicked: sharing.loadStatus(!!statusPopup.status.administrator)
                }
            }
            Flow {
                Layout.fillWidth: true
                spacing: 6
                Chip {
                    text: panel.serviceText()
                    tone: sharing.serviceState === "active" ? AppTheme.success
                        : sharing.serviceState === "inactive" ? AppTheme.danger : AppTheme.textMuted
                }
                Chip {
                    visible: String(statusPopup.status.version || "").length > 0
                    text: "Samba " + statusPopup.status.version
                }
                Chip {
                    text: sharing.guestsAllowed ? panel.t("Konuklara açık", "Guests allowed")
                                                : panel.t("Konuklar kapalı", "Guests disabled")
                }
                Chip {
                    visible: sharing.maxShares >= 0
                    text: sharing.maxShares === 0 ? panel.t("Kullanıcı paylaşımları kapalı", "User shares disabled")
                                                  : panel.t("En fazla ", "Up to ") + sharing.maxShares + panel.t(" paylaşım", " shares")
                    tone: sharing.maxShares === 0 ? AppTheme.danger : AppTheme.textMuted
                }
                Chip {
                    visible: statusPopup.status.openFiles !== undefined
                    text: (statusPopup.status.openFiles || 0) + panel.t(" açık dosya", " open files")
                }
            }
            GText {
                Layout.fillWidth: true
                visible: !!statusPopup.status.error
                text: String(statusPopup.status.error || "")
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                baseColor: AppTheme.danger
                basePixelSize: 11
            }
            RowLayout {
                Layout.fillWidth: true
                visible: !!statusPopup.status.requiresAdmin
                GText {
                    Layout.fillWidth: true
                    text: panel.t("Bağlı istemcileri görmek için yönetici yetkisi gerekiyor.",
                                  "Administrator authorization is needed to list connected clients.")
                    wrapMode: Text.Wrap
                    baseColor: AppTheme.textMuted
                    basePixelSize: 11
                }
                GModalButton {
                    objectName: "sambaStatusAsAdmin"
                    primary: true
                    text: panel.t("Yönetici olarak göster", "Show as administrator")
                    enabled: !sharing.statusBusy
                    onClicked: sharing.loadStatus(true)
                }
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent ? parent.width : 0
                    spacing: 8
                    GText {
                        text: panel.t("Oturumlar", "Sessions") + " · " + statusPopup.sessions.length
                        baseColor: AppTheme.text
                        basePixelSize: 12
                        baseFontWeight: Font.DemiBold
                    }
                    GText {
                        visible: statusPopup.sessions.length === 0
                        text: panel.t("Bağlı istemci yok.", "No connected clients.")
                        baseColor: AppTheme.textMuted
                        basePixelSize: 11
                    }
                    Repeater {
                        model: statusPopup.sessions
                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: sessionColumn.implicitHeight + 16
                            radius: 10
                            color: AppTheme.surfaceRaised
                            border.color: AppTheme.border
                            ColumnLayout {
                                id: sessionColumn
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 2
                                GText {
                                    text: String(modelData.user || "?") + "  ·  " + String(modelData.machine || "")
                                    textFormat: Text.PlainText
                                    baseColor: AppTheme.text
                                    basePixelSize: 11
                                    baseFontWeight: Font.DemiBold
                                }
                                GText {
                                    text: [modelData.protocol,
                                           modelData.encryption ? panel.t("şifreleme: ", "encryption: ") + modelData.encryption : "",
                                           modelData.signing ? panel.t("imza: ", "signing: ") + modelData.signing : ""]
                                          .filter(function(part) { return String(part || "").length > 0 }).join("  ·  ")
                                    textFormat: Text.PlainText
                                    baseColor: AppTheme.textMuted
                                    basePixelSize: 10
                                }
                            }
                        }
                    }
                    GText {
                        Layout.topMargin: 6
                        text: panel.t("Bağlı paylaşımlar", "Connected shares") + " · " + statusPopup.connections.length
                        baseColor: AppTheme.text
                        basePixelSize: 12
                        baseFontWeight: Font.DemiBold
                    }
                    Repeater {
                        model: statusPopup.connections
                        delegate: GText {
                            required property var modelData
                            Layout.fillWidth: true
                            text: String(modelData.share || "") + "  ←  " + String(modelData.machine || "")
                                  + (modelData.connectedAt ? "  ·  " + String(modelData.connectedAt).replace("T", " ").substring(0, 19) : "")
                            textFormat: Text.PlainText
                            baseColor: AppTheme.textMuted
                            basePixelSize: 11
                            elide: Text.ElideRight
                        }
                    }
                }
            }
            GModalButton {
                Layout.alignment: Qt.AlignRight
                text: panel.t("Kapat", "Close")
                onClicked: statusPopup.close()
            }
        }
    }
}
