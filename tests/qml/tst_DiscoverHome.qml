import QtQuick
import QtQuick.Controls
import QtTest
import QtCore
import Lurviko.App
import Lurviko.Backend

Item {
    width: 1400; height: 1000
    ApplicationWindow {
        id: window
        width: 1400; height: 1000
        visible: true
        color: AppTheme.background
        StorageModel { id: storageModel }
        FavoritesModel { id: favoritesModel }
        // The real model points at the user's home folders, whose FolderStats
        // scans kept running after this test and slowed the next ones.
        ListModel {
            id: quickAccessModel
            property var recentLocations: [{title: "Fixture", location: "file:///nonexistent-lurviko-fixture/recent"}]
            ListElement { shortcutTitle: ""; shortcutLocation: "file:///nonexistent-lurviko-fixture/recent"; shortcutIcon: "last-location.svg"; shortcutFixed: true }
            ListElement { shortcutTitle: "Fixture A"; shortcutLocation: "file:///nonexistent-lurviko-fixture/a"; shortcutIcon: "folder.svg"; shortcutFixed: false }
            ListElement { shortcutTitle: "Fixture B"; shortcutLocation: "file:///nonexistent-lurviko-fixture/b"; shortcutIcon: "folder.svg"; shortcutFixed: false }
            function addShortcut(url, title, icon) {}
            function moveShortcut(from, to) {}
            function removeShortcut(index) {}
            function updateShortcut() {}
            function pruneRecentLocations() {}
        }
        // A real ContentIndexModel would index the actual home folder and slow
        // every later test; this fixture provides the API Discover uses.
        ListModel {
            id: contentIndexModel
            property bool indexing: false
            property bool scanProgressVisible: false
            property int totalIndexed: 3
            property int scanProcessedDirectories: 0
            property int scanPendingDirectories: 0
            property int scanVisitedEntries: 0
            property int scanMatchedFiles: 0
            property int scanProgressPercent: 0
            property int categoryInteractionRevision: 0
            property int recentWindowDays: 7
            property int refreshes: 0
            ListElement { categoryKey: "recent"; categoryTitleTr: "Son Dosyalar"; categoryTitleEn: "Recent"; categoryIcon: "recent.svg"; categoryCount: 2; categoryVisible: true; categoryIndexHidden: false }
            ListElement { categoryKey: "images"; categoryTitleTr: "Resimler"; categoryTitleEn: "Pictures"; categoryIcon: "image.svg"; categoryCount: 1; categoryVisible: true; categoryIndexHidden: false }
            readonly property var fixtureFiles: [
                {path: "/fixture/a.png", url: "file:///fixture/a.png", name: "a.png", parentPath: "/fixture", suffix: "png", size: 2048, modifiedMs: 1700000000000},
                {path: "/fixture/b.txt", url: "file:///fixture/b.txt", name: "b.txt", parentPath: "/fixture", suffix: "txt", size: 10, modifiedMs: 1690000000000}
            ]
            function visibleCategories() {
                const list = []
                for (let i = 0; i < count; ++i) {
                    const row = get(i)
                    list.push({key: row.categoryKey, titleTr: row.categoryTitleTr, titleEn: row.categoryTitleEn,
                               icon: row.categoryIcon, count: row.categoryCount})
                }
                return list
            }
            function latestFilesForCategory(key, limit) { return key === "recent" ? fixtureFiles.slice(0, limit) : fixtureFiles.slice(0, 1) }
            function countForCategory(key) { return key === "recent" ? 2 : 1 }
            function bytesForCategory(key) { return key === "recent" ? 2058 : 2048 }
            function homeStorageTotalBytes() { return 100 * 1024 * 1024 * 1024 }
            function homeStorageAvailableBytes() { return 40 * 1024 * 1024 * 1024 }
            function titleForCategory(key, lang) { return key }
            function moveCategory(from, to) {}
            property var singleClickKeys: []
            function categorySingleClickOpen(key) { return singleClickKeys.indexOf(key) >= 0 }
            function refresh() { refreshes += 1 }
        }
        LanguageManager { id: language; language: "tr" }
        // The real managers talk to KWallet over D-Bus; their pending calls
        // outlived this test and stalled the next test file by ~50 s.
        QtObject {
            id: cloudAuth
            property string googleAuthState: ""
            property string oneDriveAuthState: ""
            property bool googleConfigured: false
            property bool oneDriveConfigured: false
            property bool googleConnected: false
            property bool oneDriveConnected: false
            property string googleClientId: ""
            property string googleClientSecret: ""
            property string oneDriveClientId: ""
            property string oneDriveTenant: ""
            property string oneDriveRedirectUri: ""
            property string googleLastError: ""
            property string oneDriveLastError: ""
            signal authError(string provider, string message)
            function connectGoogle() {}
            function connectOneDrive() {}
            function resetOneDriveConfiguration() {}
        }
        QtObject {
            id: privateVault
            property bool busy: false
            property bool exists: false
            property bool unlocked: false
            property bool kwalletEnabled: false
            property string lastError: ""
            property int passwordWaitSeconds: 0
            property bool passwordProtectionEnabled: false
            property int failedPasswordAttempts: 0
            property int maxPasswordAttempts: 0
            property bool passwordLockedOut: false
            property int totalItemCount: 0
            function clearStatus() {}
            function lock() {}
            function unlock() { return false }
            function quickUnlock() { return false }
        }
        QtObject {
            id: prefs
            property bool showGoogle: true
            property bool showOneDrive: true
            property bool showDropbox: false
            property bool showNextcloud: false
            property bool showOwnCloud: false
            property bool showMega: true
            property bool showPCloud: false
            property bool showWebDav: false
            property string cloudOrder: "google,onedrive,dropbox,nextcloud,owncloud,mega,pcloud,webdav"
            property string dropboxLocation: ""
            property string nextcloudLocation: ""
            property string ownCloudLocation: ""
            property string megaLocation: ""
            property string pCloudLocation: ""
            property string webDavLocation: ""
            property string googleCustomName: ""
            property string googleCustomIcon: ""
            property string oneDriveCustomName: ""
            property string oneDriveCustomIcon: ""
            property string dropboxCustomName: ""
            property string dropboxCustomIcon: ""
            property string nextcloudCustomName: ""
            property string nextcloudCustomIcon: ""
            property string ownCloudCustomName: ""
            property string ownCloudCustomIcon: ""
            property string megaCustomName: ""
            property string megaCustomIcon: ""
            property string pCloudCustomName: ""
            property string pCloudCustomIcon: ""
            property string webDavCustomName: ""
            property string webDavCustomIcon: ""
        }
        HomePage {
            id: home
            anchors.fill: parent
            storageModel: storageModel; favoritesModel: favoritesModel
            quickAccessModel: quickAccessModel; contentIndexModel: contentIndexModel
            lang: language; cloudAuth: cloudAuth; cloudIntegrationPreferences: prefs
            privateVault: privateVault
        }

        TestCase {
            name: "DiscoverHome"
            when: windowShown
            property var views

            function initTestCase() { views = home.discoverViewSettings }
            function cleanup() {
                views.categories = "grid"; views.clouds = "grid"; views.quickAccess = "grid"; views.disks = "cards"
                home.infoPanelOpen = false
                wait(50)
            }
            function visibleCloudCards() {
                const grid = findChild(home, "cloudGrid")
                let count = 0
                for (let i = 0; i < grid.children.length; ++i) {
                    const child = grid.children[i]
                    if (child.cloudKey !== undefined && child.visible) ++count
                }
                return count
            }

            function test_every_section_uses_the_shared_header() {
                for (const name of ["categoriesHeader", "cloudsHeader", "quickAccessHeader", "disksHeader"])
                    verify(findChild(home, name) !== null, name)
                verify(findChild(home, "refreshDisksButton") !== null)
                verify(findChild(home, "manageCloudIntegrationsButton") !== null)
                verify(findChild(home, "addShortcutButton") !== null)
            }

            function test_category_view_modes() {
                const switcher = findChild(home, "categoriesViewSwitcher")
                verify(switcher.visible)
                mouseClick(findChild(switcher, "viewMode_list"))
                compare(views.categories, "list")
                verify(findChild(home, "categoryTabView").visible === false)
                mouseClick(findChild(switcher, "viewMode_tab"))
                compare(views.categories, "tab")
                tryVerify(function() { return findChild(home, "categoryTabView").visible })
                const panel = findChild(home, "categoryTabPanel")
                compare(panel.key, "recent")
                compare(panel.files.length, 2, "Tab view lists the newest files of the selected category")
                mouseClick(findChild(home, "discoverTab_images"))
                compare(panel.key, "images")
                compare(panel.files.length, 1)
                mouseClick(findChild(switcher, "viewMode_grid"))
                compare(views.categories, "grid")
                verify(!findChild(home, "categoryTabView").visible)
            }

            function test_cloud_layouts() {
                const switcher = findChild(home, "cloudsViewSwitcher")
                compare(findChild(switcher, "viewMode_tab"), null, "Clouds offer Compact instead of Tabs")
                compare(visibleCloudCards(), 3)
                mouseClick(findChild(switcher, "viewMode_compact"))
                compare(views.clouds, "compact")
                const grid = findChild(home, "cloudGrid")
                verify(grid.compactLayout)
                compare(visibleCloudCards(), 3, "Compact keeps every provider")
                for (let i = 0; i < grid.children.length; ++i) {
                    const card = grid.children[i]
                    if (card.cloudKey !== undefined && card.visible) {
                        verify(card.compact)
                        tryCompare(card, "width", 152)
                    }
                }
                views.clouds = "list"
                compare(grid.columns, 1)
            }

            function test_quick_access_compact_keeps_card_behaviour() {
                const switcher = findChild(home, "quickAccessViewSwitcher")
                compare(findChild(switcher, "viewMode_tab"), null, "Quick Access offers Compact instead of Tabs")
                mouseClick(findChild(switcher, "viewMode_compact"))
                compare(views.quickAccess, "compact")
                const grid = findChild(home, "quickAccessGrid")
                let tiles = 0
                let recentButton = null
                for (let i = 0; i < grid.children.length; ++i) {
                    const tile = grid.children[i]
                    if (tile.shortcutIndex === undefined || !tile.visible) continue
                    ++tiles
                    verify(tile.compact)
                    verify(tile.quickAccessReorder, "Compact tiles still reorder by drag")
                    if (tile.fixedShortcut) recentButton = findChild(tile, "quickAccessCompactRecent")
                }
                verify(tiles > 0)
                verify(recentButton !== null && recentButton.visible, "Last Location keeps its recent-locations menu")
            }

            function test_disk_layouts() {
                const switcher = findChild(home, "disksViewSwitcher")
                verify(findChild(switcher, "viewMode_cards") !== null)
                verify(findChild(switcher, "viewMode_compact") !== null)
                compare(findChild(switcher, "viewMode_list"), null, "Disks offer only cards and compact")
                const grid = findChild(home, "diskGrid")
                const cardColumns = grid.columns
                mouseClick(findChild(switcher, "viewMode_compact"))
                compare(views.disks, "compact")
                verify(grid.compactLayout)
                verify(grid.columns > cardColumns)
            }

            function test_category_tab_click_follows_single_click_setting() {
                const savedGlobal = AppTheme.singleClickOpen
                try {
                    AppTheme.setSingleClickOpen(false)
                    contentIndexModel.singleClickKeys = []
                    contentIndexModel.categoryInteractionRevision += 1
                    verify(!home.categoryOpensOnSingleClick("images"))
                    contentIndexModel.singleClickKeys = ["images"]
                    contentIndexModel.categoryInteractionRevision += 1
                    verify(home.categoryOpensOnSingleClick("images"))
                    verify(!home.categoryOpensOnSingleClick("recent"))
                    AppTheme.setSingleClickOpen(true)
                    verify(home.categoryOpensOnSingleClick("recent"), "The global setting opens with one click everywhere")
                } finally {
                    AppTheme.setSingleClickOpen(savedGlobal)
                    contentIndexModel.singleClickKeys = []
                }
            }

            function test_info_panel_is_live_status() {
                home.infoPanelOpen = true
                const panel = findChild(home, "discoverInfoPanel")
                tryVerify(function() { return panel.visible })
                for (const name of ["infoStorageCard", "infoHousekeepingCard", "infoIndexCard", "infoConnectionsCard"])
                    verify(findChild(panel, name).visible, name)
                verify(!findChild(panel, "infoOperationsCard").visible, "No running operations in the fixture")
                tryVerify(function() { return findChild(panel, "infoTrashUsage").text.indexOf("…") < 0 }, 5000)
                const gauge = findChild(panel, "infoStorageGauge")
                if (storageModel.totalBytes > 0)
                    compare(gauge.value, storageModel.usedBytes / storageModel.totalBytes)
                verify(gauge.centerText.indexOf("%") === 0, "Turkish puts the percent sign first")
            }

            function test_clock_area_has_greeting_and_anchor() {
                verify(findChild(home, "homeGreeting").text.length > 0)
                verify(findChild(home, "homeRecentChip").visible)
                verify(findChild(home, "homeFreeSpaceChip").visible)
                verify(findChild(home, "homeClockTime").text.length === 5)
                verify(home.clockAnchorY > 0)
            }
        }
    }
}
