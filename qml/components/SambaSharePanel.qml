import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lurviko.Backend
import Lurviko.App

Rectangle {
    id: panel
    property var lang
    property string location: ""
    property bool folder: false
    property bool active: false
    property var textEditMenu: null
    signal notify(string message)
    readonly property bool tr: lang && lang.language === "tr"
    readonly property bool dirty: sharing.available && (sharing.shared
        ? commentField.text !== sharing.comment || readOnlyBox.checked !== sharing.readOnly
        : nameField.text.trim().length > 0)
    implicitHeight: contents.implicitHeight + 30
    color: AppTheme.surfaceRaised
    border.color: AppTheme.border
    radius: 16
    function refresh() {
        if (active && folder && location.length) sharing.inspect(location)
        else sharing.clear()
    }
    function resetDraft() {
        nameField.text = sharing.shared ? sharing.shareName : ""
        commentField.text = sharing.comment
        readOnlyBox.checked = sharing.readOnly
    }
    onActiveChanged: refresh()
    onLocationChanged: if (active) refresh()
    onFolderChanged: if (active) refresh()
    SambaShareManager {
        id: sharing
        objectName: "sambaShareManager"
        onChanged: if (!busy && available && !error.length) panel.resetDraft()
        onOperationFinished: function(success, message) { panel.notify(message.replace(/</g, "‹").replace(/>/g, "›")) }
    }
    ColumnLayout {
        id: contents
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 15
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            GText {
                text: panel.tr ? "Ağ paylaşımı" : "Network sharing"
                basePixelSize: 13
                baseColor: AppTheme.text
                Layout.fillWidth: true
            }
            GBusyIndicator { running: sharing.busy; visible: running; implicitWidth: 24; implicitHeight: 24 }
            GModalButton {
                text: panel.tr ? "Yenile" : "Refresh"
                enabled: panel.folder && !sharing.busy
                onClicked: panel.refresh()
            }
        }
        GText {
            Layout.fillWidth: true
            text: !panel.folder
                ? (panel.tr ? "Samba paylaşımı klasörler üzerinden yapılır. Bu dosyayı paylaşmak için bulunduğu klasörün özelliklerini aç."
                            : "Samba shares folders. To share this file, open its containing folder's properties.")
                : (panel.tr ? "Bağlanırken bu bilgisayardaki Linux kullanıcı adını ve ayrıca belirlediğin Samba parolasını kullan. Paylaşım oluşturmak bir Samba hesabı veya parola oluşturmaz. Dosya sistemi izinleri geçerli kalır."
                            : "Connect with your Linux username on this computer and a separately configured Samba password. Creating a share does not create a Samba account or password. Filesystem permissions still apply.")
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
            spacing: 10
            GText {
                Layout.fillWidth: true
                text: panel.tr ? "Samba hesabı eklemek veya parola belirlemek için aşağıdaki komutu terminalde çalıştır."
                               : "Run the command below in a terminal to add a Samba account or set its password."
                wrapMode: Text.Wrap; basePixelSize: 11; baseColor: AppTheme.textMuted
            }
            GTextField {
                objectName: "sambaAccountSetupCommand"
                Layout.fillWidth: true
                readOnly: true
                text: 'sudo smbpasswd -a "$USER"'
                persistentSelection: true
                ContextMenu.menu: panel.textEditMenu
                Accessible.name: panel.tr ? "Samba hesabı oluşturma komutu" : "Samba account setup command"
            }
            GComboBox {
                visible: sharing.shareNames.length > 1
                Layout.fillWidth: true
                model: sharing.shareNames
                onActivated: sharing.selectShare(currentText)
            }
            GText {
                text: sharing.shared ? (panel.tr ? "Bu klasör paylaşılıyor" : "This folder is shared")
                                     : (panel.tr ? "Bu klasör paylaşılmıyor" : "This folder is not shared")
                baseColor: sharing.shared ? AppTheme.success : AppTheme.textMuted
                basePixelSize: 11
            }
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 12
                GText { text: panel.tr ? "Paylaşım adı" : "Share name"; baseColor: AppTheme.textMuted; basePixelSize: 11 }
                GTextField {
                    id: nameField
                    objectName: "sambaShareName"
                    Layout.fillWidth: true
                    enabled: sharing.editable
                    readOnly: sharing.shared
                    maximumLength: 80
                    placeholderText: panel.tr ? "Örneğin Belgeler" : "For example Documents"
                    selectByMouse: true
                    persistentSelection: true
                    ContextMenu.menu: panel.textEditMenu
                }
                GText { text: panel.tr ? "Açıklama" : "Comment"; baseColor: AppTheme.textMuted; basePixelSize: 11 }
                GTextField {
                    id: commentField
                    objectName: "sambaShareComment"
                    Layout.fillWidth: true
                    enabled: sharing.editable
                    maximumLength: 256
                    selectByMouse: true
                    persistentSelection: true
                    ContextMenu.menu: panel.textEditMenu
                }
            }
            GCheckBox {
                id: readOnlyBox
                enabled: sharing.editable
                text: panel.tr ? "Yalnızca okumaya izin ver" : "Allow read-only access"
                checked: true
            }
            GText {
                Layout.fillWidth: true
                visible: sharing.shared && !sharing.editable
                text: panel.tr ? "Bu paylaşım özel erişim kuralları kullanıyor. Mevcut kurallar bu panelden değiştirilmez."
                               : "This share uses custom access rules. This panel preserves those rules."
                baseColor: AppTheme.textMuted
                basePixelSize: 11
                wrapMode: Text.Wrap
            }
            GTextField {
                objectName: "sambaShareAddress"
                visible: sharing.shared
                Layout.fillWidth: true
                text: sharing.address
                readOnly: true
                selectByMouse: true
                persistentSelection: true
                ContextMenu.menu: panel.textEditMenu
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                GModalButton {
                    visible: sharing.shared
                    text: panel.tr ? "Paylaşımı kaldır" : "Remove share"
                    danger: true
                    onClicked: sharing.remove()
                }
                GModalButton {
                    objectName: "sambaShareSave"
                    text: sharing.shared ? (panel.tr ? "Paylaşımı güncelle" : "Update share")
                                         : (panel.tr ? "Klasörü paylaş" : "Share folder")
                    primary: true
                    enabled: sharing.editable && panel.dirty && nameField.text.trim().length > 0
                    onClicked: sharing.save(nameField.text.trim(), commentField.text, readOnlyBox.checked)
                }
            }
        }
    }
}
