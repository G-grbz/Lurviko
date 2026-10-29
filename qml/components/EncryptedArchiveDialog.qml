import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lurviko.App

GModalPopup {
    id: dialog
    objectName: "encryptedArchiveDialog"
    required property var lang
    property var sourceUrls: []
    signal createRequested(var urls, string format, string password)
    readonly property bool supportedPassword: format.currentIndex === 0
        || (password.text.length <= 99 && /^[\x20-\x7e]*$/.test(password.text))
    readonly property bool validPassword: password.text.length > 0
        && password.text === confirmation.text && !/[\r\n\u0000]/.test(password.text) && supportedPassword
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(490, parent ? parent.width - 24 : 490)
    height: Math.min(contentItem.implicitHeight + topPadding + bottomPadding,
                     parent ? parent.height - 24 : 600)
    // Invokable dictionary lookups need an explicit language dependency so
    // an open dialog follows an in-app language change.
    function label(key) {
        return lang && lang.language.length ? lang.t(key) : key
    }
    function openFor(urls) {
        sourceUrls = urls.slice()
        open()
    }
    function submit() {
        if (!validPassword || !sourceUrls.length) return
        createRequested(sourceUrls, format.currentIndex === 0 ? "7z" : "zip", password.text)
        password.clear()
        confirmation.clear()
        close()
    }
    onOpened: password.forceActiveFocus()
    onClosed: {
        // Do not retain credentials in reusable popup state or edit history.
        password.clear()
        confirmation.clear()
        sourceUrls = []
    }
    contentItem: ColumnLayout {
        spacing: 12
        GText {
            Layout.fillWidth: true
            text: dialog.label("compress_encrypted")
            basePixelSize: 18
            wrapMode: Text.Wrap
        }
        GText {
            Layout.fillWidth: true
            text: dialog.label("archive_encrypt_hint")
            basePixelSize: 11
            baseColor: AppTheme.textMuted
            wrapMode: Text.Wrap
        }
        GText { text: dialog.label("archive_format"); basePixelSize: 11 }
        GComboBox {
            id: format
            objectName: "encryptedArchiveFormat"
            Layout.fillWidth: true
            model: ["7z · AES-256", "ZIP · AES-256"]
        }
        GText {
            Layout.fillWidth: true
            visible: format.currentIndex === 1
            text: dialog.label("archive_zip_password_hint")
            basePixelSize: 11
            baseColor: dialog.supportedPassword ? AppTheme.textMuted : AppTheme.danger
            wrapMode: Text.Wrap
        }
        GTextField {
            id: password
            objectName: "encryptedArchivePassword"
            Layout.fillWidth: true
            placeholderText: dialog.label("archive_password")
            echoMode: TextInput.Password
            maximumLength: 256
            inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
            onAccepted: confirmation.forceActiveFocus()
            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: { editMenu.editor = password; editMenu.popup(password, point.position.x, point.position.y) }
            }
        }
        GTextField {
            id: confirmation
            objectName: "encryptedArchiveConfirmation"
            Layout.fillWidth: true
            placeholderText: dialog.label("archive_password_confirm")
            echoMode: TextInput.Password
            maximumLength: 256
            inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
            onAccepted: dialog.submit()
            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: { editMenu.editor = confirmation; editMenu.popup(confirmation, point.position.x, point.position.y) }
            }
        }
        GText {
            Layout.fillWidth: true
            visible: confirmation.text.length > 0 && password.text !== confirmation.text
            text: dialog.label("archive_password_mismatch")
            baseColor: AppTheme.danger
            basePixelSize: 11
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            GModalButton {
                objectName: "encryptedArchiveCancel"
                text: dialog.label("cancel")
                onClicked: dialog.close()
            }
            GModalButton {
                objectName: "encryptedArchiveCreate"
                primary: true
                text: dialog.label("compress")
                enabled: dialog.validPassword && dialog.sourceUrls.length > 0
                onClicked: dialog.submit()
            }
        }
    }
    TextEditMenu { id: editMenu; langManager: dialog.lang }
}
