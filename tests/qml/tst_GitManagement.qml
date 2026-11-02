import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtTest
import Lurviko.App
import Lurviko.Backend

Item {
    width: 1280; height: 900
    ApplicationWindow {
        id: window
        width: 1280; height: 900; visible: true
        LanguageManager { id: language }
        FavoritesModel { id: favorites }
        QuickAccessModel { id: shortcuts }
        AdminEditManager { id: admin }
        CloudAuthManager { id: auth }
        QtObject { id: prefs; property bool showGoogle: false; property bool showOneDrive: false; property bool showDropbox: false; property bool showNextcloud: false; property bool showOwnCloud: false; property bool showMega: false; property bool showPCloud: false; property bool showWebDav: false }
        QtObject { id: cloud; property string accessToken: ""; signal refreshRequested() }
        BrowsePage {
            id: page
            anchors.fill: parent
            initialLocation: String(gitManagementFixtureUrl)
            contentIndexModel: null; musicPlayer: null
            lang: language; favoritesModel: favorites; quickAccessModel: shortcuts
            adminEditor: admin; cloudAuth: auth; cloudIntegrationPreferences: prefs
            googleDrive: cloud; oneDrive: cloud
        }
        TestCase {
            name: "GitManagement"
            when: windowShown
            property string savedView
            property bool savedQt
            function init() {
                savedQt = AppTheme.useQtTheme
                page.setSplitEnabled(false)
                page.activePane.currentLocation = String(gitManagementFixtureUrl)
                savedView = page.activePane.viewMode
                tryVerify(function() { return page.activePane.directory.count >= 3 })
                tryVerify(function() { return findChild(page.activePane, "paneGitOperations").available })
            }
            function cleanup() {
                const pane = page.activePane
                pane.closeContextMenus()
                const dialog = findChild(pane, "gitDialog")
                dialog.close()
                tryCompare(dialog, "visible", false)
                page.setSplitEnabled(false)
                page.activePane.viewMode = savedView
                AppTheme.setQtTheme(savedQt)
                wait(180)
            }
            function submenu(menu) {
                for (let i = 0; i < menu.count; ++i) {
                    const entry = menu.itemAt(i)
                    if (entry && entry.subMenu && entry.subMenu.objectName === "gitActionsMenu") return entry.subMenu
                }
                return null
            }
            function test_context_actions_in_all_views_data() {
                return [{tag: "grid-file", view: "grid", name: "tracked.txt"},
                    {tag: "list-file", view: "list", name: "tracked.txt"},
                    {tag: "columns-file", view: "columns", name: "tracked.txt"},
                    {tag: "grid-folder", view: "grid", name: "child"},
                    {tag: "list-folder", view: "list", name: "child"},
                    {tag: "columns-folder", view: "columns", name: "child"}]
            }
            function test_context_actions_in_all_views(data) {
                const pane = page.activePane
                pane.viewMode = data.view
                const url = String(gitManagementFixtureUrl) + "/" + data.name
                tryVerify(function() { return pane.directory.indexOfUrl(url) >= 0 })
                pane.selectIndex(pane.directory.indexOfUrl(url), Qt.NoModifier)
                const menu = findChild(pane, "itemContextMenu")
                pane.showContextMenu(menu, pane, 300, 180)
                tryCompare(menu, "opened", true)
                const git = submenu(menu)
                verify(git !== null)
                tryCompare(git, "menuEntryVisible", true)
                compare(git.count, 10)
                verify(findChild(git, "gitCommitAction").enabled)
                menu.close()
            }
            function test_blank_inside_and_outside_repository() {
                const pane = page.activePane
                const menu = findChild(pane, "backgroundContextMenu")
                pane.showContextMenu(menu, pane, 250, 150)
                tryCompare(menu, "opened", true)
                const git = submenu(menu)
                verify(git !== null)
                tryCompare(git, "menuEntryVisible", true)
                menu.close(); tryCompare(menu, "visible", false)
                pane.currentLocation = String(fileArtworkFixtureUrl)
                tryCompare(findChild(pane, "paneGitOperations"), "loading", false)
                pane.showContextMenu(menu, pane, 250, 150)
                tryCompare(menu, "opened", true)
                tryCompare(findChild(pane, "contextGitOperations"), "loading", false)
                compare(git.menuEntryVisible, false)
            }
            function test_indicator_and_commit_modal_qt_palette() {
                const pane = page.activePane
                AppTheme.setQtTheme(true)
                const indicator = findChild(pane, "gitRepositoryIndicator")
                tryCompare(indicator, "visible", true)
                tryVerify(function() { return indicator.summary.indexOf("main") >= 0 && indicator.summary.indexOf("upstream") >= 0 })
                mouseClick(indicator)
                const dialog = findChild(pane, "gitDialog")
                tryCompare(dialog, "opened", true)
                compare(dialog.mode, "info")
                grabImage(window.contentItem).save("/tmp/lurviko-git-information-dialog.png")
                dialog.changeMode("commit")
                const operations = findChild(pane, "paneGitOperations")
                tryCompare(operations, "loading", false)
                tryVerify(function() { return operations.changes.length >= 2 })
                compare(findChild(dialog, "gitPushAfterCommit").checked, false)
                compare(findChild(dialog, "gitCommitSubmit").enabled, false)
                dialog.choose(operations.changes[0].id, true)
                findChild(dialog, "gitCommitMessage").text = "Selected changes"
                compare(dialog.selectedCount, 1)
                verify(findChild(dialog, "gitCommitSubmit").enabled)
                wait(40)
                grabImage(window.contentItem).save("/tmp/lurviko-git-commit-dialog.png")
                dialog.changeMode("history")
                tryCompare(operations, "loading", false)
                compare(operations.history.length, 1)
                dialog.changeMode("diff")
                tryCompare(operations, "loading", false)
                verify(findChild(dialog, "gitDiffText").text.indexOf("changed") >= 0)
                dialog.close(); tryCompare(dialog, "visible", false)
            }
            function test_tabs_and_split_use_each_pane_repository() {
                const primary = page.activePane
                page.setSplitEnabled(true)
                tryVerify(function() { return findChild(page, "browserRightPane") !== null })
                const secondary = findChild(page, "browserRightPane")
                wait(80)
                secondary.currentLocation = String(fileArtworkFixtureUrl)
                page.activePane = secondary
                tryCompare(findChild(secondary, "paneGitOperations"), "loading", false)
                compare(findChild(secondary, "gitRepositoryIndicator").visible, false)
                page.activePane = primary
                tryCompare(findChild(primary, "paneGitOperations"), "available", true)
                verify(findChild(primary, "gitRepositoryIndicator").visible)
                page.addTab(String(fileArtworkFixtureUrl))
                tryVerify(function() { return page.activePane.currentLocation === String(fileArtworkFixtureUrl) })
                tryCompare(findChild(page.activePane, "paneGitOperations"), "loading", false)
                compare(findChild(page.activePane, "gitRepositoryIndicator").visible, false)
                page.activePane.currentLocation = String(gitManagementFixtureUrl)
            }
        }
    }
}
