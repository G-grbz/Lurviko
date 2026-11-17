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
            readonly property string fixtureSharedFolder: String(Qt.resolvedUrl(String(fileArtworkFixtureUrl) + "/../shared-folder"))
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

                // Not sharing after all: the test root's pass-through (x) was
                // added for this share, but the fixture share in the same
                // tree still passes through it, so it is kept.
                tryVerify(function() { return SambaShares.isShared(fixtureSharedFolder) }, 5000)
                panel.shareEnabled = false
                panel.permissionRevision += 1
                verify(!panel.restoreSuggestion.needed)
                verify(!findChild(panel, "sambaRestoreSuggestion").visible)
                const removal = manager().restorePlan(panel.aclDraft, false, true)
                compare(removal.items.length, 0)
            }
        }
    }
}
