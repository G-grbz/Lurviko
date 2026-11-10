import QtQuick
import QtQuick.Controls
import QtTest
import Lurviko.App
import Lurviko.Backend

Item {
    width: 1000; height: 900
    ApplicationWindow {
        id: window
        width: 1000; height: 900; visible: true
        LanguageManager { id: language; language: "en" }
        FavoritesModel { id: favorites }
        AdminEditManager { id: admin }
        QtObject { id: auth; property string googleAccessToken: ""; property string oneDriveAccessToken: "" }
        QtObject { id: prefs; property bool showGoogle: false; property bool showOneDrive: false; property bool showDropbox: false; property bool showNextcloud: false; property bool showOwnCloud: false; property bool showMega: false; property bool showPCloud: false; property bool showWebDav: false }
        QtObject { id: cloud; property string accessToken: ""; signal refreshRequested() }
        BrowserPane {
            id: pane
            anchors.fill: parent
            initialLocation: String(fileArtworkFixtureUrl)
            lang: language; favoritesModel: favorites; adminEditor: admin
            cloudAuth: auth; cloudIntegrationPreferences: prefs; googleDrive: cloud; oneDrive: cloud
        }
        GTextField { id: clipboardProbe; visible: false }
        TestCase {
            name: "PropertiesContextMenus"
            when: windowShown
            function init() {
                window.requestActivate()
                const url = String(fileArtworkFixtureUrl) + "/visible-folder"
                tryVerify(function() { return pane.directory.indexOfUrl(url) >= 0 })
                pane.selectIndex(pane.directory.indexOfUrl(url), Qt.NoModifier)
                pane.openPropertiesForSelection()
                tryCompare(findChild(pane, "filePropertiesDialog"), "opened", true)
                tryCompare(findChild(pane, "permissionModeInput"), "enabled", true)
            }
            function cleanup() {
                findChild(pane, "propertiesTextEditMenu").close()
                findChild(pane, "filePropertiesDialog").close()
                tryCompare(findChild(pane, "filePropertiesDialog"), "visible", false)
                pane.currentLocation = String(fileArtworkFixtureUrl)
                wait(160)
            }
            function reveal(field) {
                // Properties is tabbed: open the tab that holds the field first.
                verify(findChild(pane, "filePropertiesDialog").showTabFor(field))
                tryVerify(function() { return field.visible })
                const scroll = findChild(pane, "filePropertiesScroll")
                const flick = scroll.contentItem
                flick.contentY = Math.max(0, Math.min(flick.contentHeight - flick.height,
                    flick.contentY + field.mapToItem(scroll, 0, 0).y - scroll.height / 2))
                wait(50)
            }
            function test_properties_fields_use_the_application_menu_data() {
                return [{tag: "permission-mode", name: "permissionModeInput", account: false},
                    {tag: "owner", name: "permissionOwnerCombo", account: true},
                    {tag: "group", name: "permissionGroupCombo", account: true},
                    {tag: "share-name", name: "sambaShareName", account: false},
                    {tag: "share-comment", name: "sambaShareComment", account: false},
                    {tag: "account-help", name: "sambaAccountSetupCommand", account: false}]
            }
            function test_properties_fields_use_the_application_menu(data) {
                let field = findChild(pane, data.name)
                verify(field !== null)
                if (data.account) {
                    // Only change the draft; no Apply/elevation or account
                    // modification occurs in this editing-menu test.
                    findChild(pane, "filePropertiesDialog").administratorDraft = true
                    field = findChild(field, "accountEditor")
                }
                tryCompare(field, "enabled", true)
                if (data.name === "sambaShareComment") field.text = "Copy selected comment"
                else if (data.name === "sambaShareName") field.text = "SelectedShare"
                reveal(field)
                field.forceActiveFocus(); field.selectAll()
                const selected = field.selectedText
                verify(selected.length > 0)
                mouseClick(field, Math.min(field.width / 2, 28), field.height / 2, Qt.RightButton)
                const menu = findChild(pane, "propertiesTextEditMenu")
                tryCompare(menu, "opened", true)
                compare(menu.popupType, Popup.Item)
                compare(menu.editor, field)
                compare(field.selectedText, selected)
                const copy = menu.itemAt(4)
                verify(copy.enabled)
                mouseClick(copy)
                tryCompare(menu, "visible", false)
                clipboardProbe.text = ""; clipboardProbe.paste()
                compare(clipboardProbe.text, selected)
                compare(findChild(pane, "filePropertiesDialog").visible, true)
            }
            function test_smb_root_reads_own_shares_without_discovery_or_login() {
                if (!inputTestHelper.networkProtocolSupported("smb"))
                    skip("Optional KIO SMB worker is not installed")
                const dialog = findChild(pane, "filePropertiesDialog")
                dialog.close(); tryCompare(dialog, "visible", false)
                pane.currentLocation = "smb://"
                const panel = findChild(pane, "localSambaShares")
                tryCompare(panel, "visible", true)
                const manager = findChild(panel, "localSambaShareManager")
                tryCompare(manager, "busy", false)
                const list = findChild(panel, "localSambaShareList")
                compare(manager.localShares.length, 1)
                compare(manager.localShares[0].name, "Fixture share #1")
                compare(list.count, manager.localShares.length)
                for (const share of manager.localShares) verify(share.url.indexOf("smb://localhost/") === 0)
                pane.refreshDirectory()
                tryCompare(manager, "busy", false)
                compare(list.count, manager.localShares.length)
                pane.currentLocation = String(fileArtworkFixtureUrl)
                tryCompare(panel, "visible", false)
                tryCompare(list, "count", 0)
            }
            function test_closing_properties_dismisses_its_editor_menu() {
                const field = findChild(pane, "permissionModeInput")
                reveal(field); field.forceActiveFocus(); field.selectAll()
                mouseClick(field, 20, field.height / 2, Qt.RightButton)
                const menu = findChild(pane, "propertiesTextEditMenu")
                tryCompare(menu, "opened", true)
                findChild(pane, "filePropertiesDialog").close()
                tryCompare(menu, "visible", false)
            }
        }
    }
}
