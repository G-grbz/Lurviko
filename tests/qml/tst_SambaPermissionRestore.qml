import QtQuick
import QtQuick.Controls
import QtTest
import Lurviko.App
import Lurviko.Backend

// Permissions opened for a share can be taken back even when the share was
// never saved: full control -> fix permissions -> back to read only.
Item {
    width: 760
    height: 1000
    ApplicationWindow {
        id: window
        width: 760; height: 1000; visible: true
        LanguageManager { id: language; language: "tr" }
        SambaSharePanel {
            id: panel
            x: 16; y: 16; width: 700
            lang: language
            folder: true
            active: true
            location: String(fileArtworkFixtureUrl) + "/visible-folder"
        }
        TestCase {
            name: "SambaPermissionRestore"
            when: windowShown
            function manager() { return findChild(panel, "sambaShareManager") }
            function popup() { return findChild(panel, "sambaPermissionPopup") }
            function button(name) { return findChild(popup().contentItem, name) }

            function test_fix_without_saving_then_restore() {
                tryVerify(function() { return manager().available }, 5000)
                verify(!manager().shared)
                panel.shareEnabled = true
                panel.applyPreset("full")
                verify(panel.permissionPlan.needed)
                verify(!panel.restoreSuggestion.needed)

                // "Details…" -> change permissions, without saving the share.
                popup().show("review", false)
                tryCompare(popup(), "opened", true)
                mouseClick(button("sambaFixPermissions"))
                tryVerify(function() { return !panel.permissionPlan.needed }, 5000)
                tryCompare(popup(), "visible", false)
                verify(!manager().shared)

                // Back to read only: the write bit is offered back at once.
                panel.applyPreset("read")
                tryVerify(function() { return panel.restoreSuggestion.needed })
                const box = findChild(panel, "sambaRestoreSuggestion")
                verify(box.visible)
                const items = panel.restoreSuggestion.items
                compare(items.length, 1)
                compare(items[0].role, "share")
                verify(items[0].newText.substring(6) === "r-x")

                mouseClick(findChild(panel, "sambaRestoreSuggestionButton"))
                tryCompare(popup(), "opened", true)
                compare(popup().stage, "restore")
                verify(!button("sambaKeepPermissions").visible)
                mouseClick(button("sambaRestorePermissions"))
                tryVerify(function() { return !panel.restoreSuggestion.needed }, 5000)
                tryCompare(popup(), "visible", false)
                verify(!manager().shared)

                // Not sharing after all: the parent's pass-through goes too.
                panel.shareEnabled = false
                tryVerify(function() { return panel.restoreSuggestion.needed })
                compare(panel.restoreSuggestion.items[0].role, "traverse")
                mouseClick(findChild(panel, "sambaRestoreSuggestionButton"))
                tryCompare(popup(), "opened", true)
                mouseClick(button("sambaRestorePermissions"))
                tryVerify(function() { return !panel.restoreSuggestion.needed }, 5000)
                verify(!findChild(panel, "sambaRestoreSuggestion").visible)
            }
        }
    }
}
