import QtQuick
import QtQuick.Layouts
import Lurviko.App

ColumnLayout {
    id: root
    required property var vault
    required property var lang
    visible: vault.passwordProtectionEnabled && (vault.failedPasswordAttempts > 0 || vault.passwordWaitSeconds > 0)
    spacing: 4

    function remainingTime(seconds) {
        const minutes = Math.floor(seconds / 60)
        return minutes + ":" + String(seconds % 60).padStart(2, "0")
    }

    Text {
        Layout.fillWidth: true
        visible: root.vault.passwordWaitSeconds > 0
        wrapMode: Text.WordWrap
        color: root.vault.passwordLockedOut ? AppTheme.danger : AppTheme.warning
        font.pixelSize: 11
        text: root.vault.passwordLockedOut
              ? (root.lang.language === "tr" ? "Giriş geçici olarak kilitlendi. Kalan süre: " : "Sign-in temporarily locked. Time remaining: ")
                + root.remainingTime(root.vault.passwordWaitSeconds)
              : (root.lang.language === "tr" ? "Sonraki parola denemesi: " : "Next password attempt in: ")
                + root.remainingTime(root.vault.passwordWaitSeconds)
    }
    Text {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: AppTheme.textMuted
        font.pixelSize: 10
        text: (root.lang.language === "tr" ? "Hatalı deneme: " : "Failed attempts: ")
              + root.vault.failedPasswordAttempts + " / " + root.vault.maxPasswordAttempts
    }
}
