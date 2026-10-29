import QtQuick
import QtQuick.Controls
import QtTest
import Lurviko.App
import Lurviko.Backend

Item {
    width: 800; height: 700
    ApplicationWindow {
        id: window
        width: 800; height: 700; visible: true
        LanguageManager { id: language; language: "en" }
        EncryptedArchiveDialog { id: dialog; lang: language }
        SignalSpy { id: created; target: dialog; signalName: "createRequested" }
        TestCase {
            name: "EncryptedArchiveDialog"
            when: windowShown
            function init() {
                created.clear()
                findChild(dialog, "encryptedArchiveFormat").currentIndex = 0
                dialog.openFor(["file:///tmp/original.txt"])
                tryCompare(dialog, "opened", true)
            }
            function cleanup() {
                dialog.close()
                tryCompare(dialog, "visible", false)
                language.language = "en"
            }
            function test_requires_matching_nonempty_password_and_cancels_cleanly() {
                const password = findChild(dialog, "encryptedArchivePassword")
                const confirmation = findChild(dialog, "encryptedArchiveConfirmation")
                const submit = findChild(dialog, "encryptedArchiveCreate")
                verify(!submit.enabled)
                compare(password.echoMode, TextInput.Password)
                compare(confirmation.echoMode, TextInput.Password)
                password.text = "test only password"
                confirmation.text = "different"
                verify(!submit.enabled)
                confirmation.text = password.text
                verify(submit.enabled)
                mouseClick(findChild(dialog, "encryptedArchiveCancel"))
                tryCompare(dialog, "visible", false)
                compare(created.count, 0)
                compare(password.text, "")
                compare(confirmation.text, "")
                compare(dialog.sourceUrls.length, 0)
            }
            function test_submits_snapshot_with_format_then_forgets_password() {
                const urls = ["file:///tmp/first/a", "file:///tmp/second/a"]
                dialog.close()
                tryCompare(dialog, "visible", false)
                dialog.openFor(urls)
                tryCompare(dialog, "opened", true)
                urls[0] = "file:///tmp/changed"
                findChild(dialog, "encryptedArchiveFormat").currentIndex = 1
                findChild(dialog, "encryptedArchivePassword").text = "ASCII Password 123"
                findChild(dialog, "encryptedArchiveConfirmation").text = "ASCII Password 123"
                mouseClick(findChild(dialog, "encryptedArchiveCreate"))
                compare(created.count, 1)
                compare(created.signalArguments[0][0][0], "file:///tmp/first/a")
                compare(created.signalArguments[0][1], "zip")
                compare(created.signalArguments[0][2], "ASCII Password 123")
                tryCompare(dialog, "visible", false)
                compare(findChild(dialog, "encryptedArchivePassword").text, "")
                compare(findChild(dialog, "encryptedArchiveConfirmation").text, "")
            }
            function test_unicode_password_requires_7z() {
                const password = findChild(dialog, "encryptedArchivePassword")
                const confirmation = findChild(dialog, "encryptedArchiveConfirmation")
                const format = findChild(dialog, "encryptedArchiveFormat")
                password.text = "Türkçe şifre 123"
                confirmation.text = password.text
                format.currentIndex = 1
                verify(!dialog.validPassword)
                format.currentIndex = 0
                verify(dialog.validPassword)
            }
            function test_localized_and_uses_existing_modal_surface() {
                language.language = "tr"
                verify(language.t("compress_encrypted").startsWith("Şifreli"))
                tryCompare(findChild(dialog, "encryptedArchiveCancel"), "text", "İptal")
                verify(dialog.background !== null)
                verify(dialog.width < window.width && dialog.height < window.height)
                grabImage(window.contentItem).save("/tmp/lurviko-encrypted-dialog.png")
            }
        }
}
}
