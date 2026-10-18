import QtQuick
import QtQuick.Controls
import QtTest
import Lurviko.App
import Lurviko.Backend

Item {
    width: 1020; height: 700
    ApplicationWindow {
        id: window
        width: 1020; height: 700
        visible: true
        LanguageManager { id: language; language: "en" }
        FavoritesModel { id: favorites }
        QuickAccessModel { id: shortcuts }
        AdminEditManager { id: admin }
        QtObject { id: auth; property string googleAccessToken: ""; property string oneDriveAccessToken: "" }
        QtObject {
            id: prefs
            property bool showGoogle: false
            property bool showOneDrive: false
            property bool showDropbox: false
            property bool showNextcloud: false
            property bool showOwnCloud: false
            property bool showMega: false
            property bool showPCloud: false
            property bool showWebDav: false
        }
        QtObject { id: cloud; property string accessToken: ""; signal refreshRequested() }
        BrowsePage {
            id: page
            anchors.fill: parent
            initialLocation: String(fileArtworkFixtureUrl)
            lang: language
            contentIndexModel: null
            musicPlayer: null
            favoritesModel: favorites
            quickAccessModel: shortcuts
            adminEditor: admin
            cloudAuth: auth
            cloudIntegrationPreferences: prefs
            googleDrive: cloud
            oneDrive: cloud
        }
        DeferredThumbnail {
            id: preview
            width: 64; height: 64
            visible: false
            fileSuffix: "png"
        }
        SignalSpy { id: newTabSpy; target: page.activePane; signalName: "requestNewTab" }
        TestCase {
            name: "FolderArtwork"
            when: windowShown
            property bool savedSystem
            property bool savedColored
            function initTestCase() {
                savedSystem = AppTheme.useSystemIcons
                savedColored = AppTheme.useColoredFolderIcons
                tryVerify(function() { return page.activePane !== null && page.activePane.itemCount > 40 })
            }
            function cleanupTestCase() {
                AppTheme.setSystemIcons(savedSystem)
                AppTheme.setColoredFolderIcons(savedColored)
            }
            function init() {
                AppTheme.setSystemIcons(false)
                AppTheme.setColoredFolderIcons(true)
                page.activePane.gridMode = true
                page.activePane.showHiddenFiles = true
            }
            function test_multilingual_keywords_data() {
                return [
                    {tag: "Turkish-color", name: "Mor", icon: "purple-folder.svg"},
                    {tag: "English-color", name: "GREEN", icon: "green-folder.svg"},
                    {tag: "Turkish-pictures", name: "Mavi Fotoğraflar", icon: "blue-picture.svg"},
                    {tag: "Turkish-possessive", name: "Pembe Resimlerimiz 2026", icon: "pink-picture.svg"},
                    {tag: "English-camel", name: "PurplePicturesBackup", icon: "purple-picture.svg"},
                    {tag: "screenshots", name: "Turkuaz Ekran Görüntülerim", icon: "turquoise-picture.svg"},
                    {tag: "music", name: "Sarı Şarkılarım", icon: "yellow-music.svg"},
                    {tag: "video", name: "Red TV Shows", icon: "red-video.svg"},
                    {tag: "documents", name: "Yeşil Belgelerim", icon: "green-documents.svg"},
                    {tag: "downloads", name: "Turuncu İndirilenler", icon: "orange-download.svg"},
                    {tag: "specific-media", name: "Blue Download Photos", icon: "blue-picture.svg"},
                    {tag: "joined-Turkish", name: "morresimarsivi", icon: "purple-picture.svg"},
                    {tag: "joined-English", name: "summerphotosbackup", kind: "picture"},
                    {tag: "word-boundary", name: "topic", kind: "folder"},
                    {tag: "prototype-safe", name: "constructor", kind: "folder"}
                ]
            }
            function test_multilingual_keywords(data) {
                const source = String(AppTheme.bundledFolderIcon(data.name, "file:///test/" + data.name))
                if (data.icon) verify(source.endsWith("/" + data.icon), source)
                else verify(new RegExp("/(blue|green|orange|pink|purple|red|turquoise|yellow)-" + data.kind + "\\.svg$").test(source), source)
                const icon = findChild(page.activePane, "gridFileIcon")
                verify(icon !== null)
                verify(source.indexOf("image://bundledicon/") === -1, "Ready-made colors must not be tinted")
            }
            function test_colors_are_stable_and_can_be_disabled() {
                const first = AppTheme.bundledFolderIcon("Projects")
                for (let i = 0; i < 2000; ++i) AppTheme.bundledFolderIcon("Different folder " + i)
                compare(AppTheme.bundledFolderIcon("Projects"), first)
                compare(AppTheme.bundledFolderIcon("Projects", "different parent"), first)
                AppTheme.setColoredFolderIcons(false)
                compare(AppTheme.bundledFolderIcon("Mor"), AppTheme.icon("folder.svg"))
                AppTheme.setColoredFolderIcons(true)
                compare(AppTheme.bundledFolderIcon("Mor"), AppTheme.icon("purple-folder.svg"))
                const external = "file:///tmp/my-folder-art.svg"
                compare(page.activePane.iconForItem(true, "", external, "Mor"), external)
            }
            function test_archive_file_icons_data() {
                return ["pkg.tar.zst", "tar.lz4", "tar.bz2", "tar.xz", "tgz", "tbz2", "tzst", "gzip", "cpio", "cab", "rpm"]
                    .map(function(extension) { return {tag: extension, extension: extension} })
            }
            function test_archive_file_icons(data) {
                const suffix = data.extension.split(".").pop()
                compare(page.activePane.iconForItem(false, suffix, "text-x-generic", "package." + data.extension),
                        AppTheme.icon("archive.svg"))
                compare(page.activePane.iconForItem(false, "dat", "text-x-generic", "unknown.dat", "", "application/zip"),
                        AppTheme.icon("archive.svg"))
            }
            function test_switch_is_shown_only_for_bundled_icons() {
                const row = findChild(page, "coloredFolderIconsSetting")
                const toggle = findChild(page, "coloredFolderIconsSwitch")
                const menu = findChild(page, "toolbarViewMenu")
                verify(row !== null && toggle !== null)
                menu.open()
                tryVerify(function() { return row.visible })
                compare(row.implicitHeight, 42)
                AppTheme.setSystemIcons(true)
                compare(row.implicitHeight, 0)
                verify(!row.visible)
                AppTheme.setSystemIcons(false)
                compare(row.implicitHeight, 42)
                toggle.checked = false
                toggle.toggled()
                compare(AppTheme.useColoredFolderIcons, false)
                menu.close()
            }
            function test_grid_spacing_matches_at_the_same_visual_icon_size_data() {
                return [{tag: "small", size: 72}, {tag: "medium", size: 114},
                        {tag: "large", size: 168}, {tag: "maximum", size: 245}]
            }
            function test_grid_spacing_matches_at_the_same_visual_icon_size(data) {
                const pane = page.activePane
                const grid = findChild(pane, "fileGridView")
                const oldSize = pane.iconSize
                pane.iconSize = Math.round(data.size / 0.68)
                wait(0)
                compare(grid.iconExtent, data.size)
                const bundled = {width: grid.cellWidth, height: grid.cellHeight,
                    columns: grid.columnCount, font: grid.itemNameFontPixels}
                AppTheme.setSystemIcons(true)
                pane.iconSize = data.size
                wait(0)
                compare(grid.iconExtent, data.size)
                compare(grid.columnCount, bundled.columns)
                compare(grid.cellWidth, bundled.width)
                compare(grid.cellHeight, bundled.height)
                compare(grid.itemNameFontPixels, bundled.font)
                AppTheme.setSystemIcons(false)
                pane.iconSize = oldSize
            }
            function test_active_tabs_reveal_neighbours_data() {
                return [{tag: "narrow", width: 900}, {tag: "normal", width: 1020}, {tag: "wide", width: 1600}]
            }
            function test_active_tabs_reveal_neighbours(data) {
                const savedTabs = page.tabs.slice()
                const savedCurrent = page.currentTab
                const savedWidth = window.width
                const scroll = findChild(page, "browserTabScrollView")
                const row = findChild(page, "browserTabRow")
                verify(scroll !== null && row !== null)
                let tabs = []
                for (let i = 0; i < 12; ++i)
                    tabs.push(page.makeTab(String(fileArtworkFixtureUrl)))
                try {
                    window.width = data.width
                    wait(0)
                    page.tabs = tabs
                    page.switchToTab(0, true)
                    const flick = scroll.contentItem
                    tryVerify(function() { return flick.contentWidth > flick.width })
                    tryVerify(function() { return Math.abs(flick.contentX) < 1 })
                    const first = findChild(page, "browserTab0")
                    verify(first !== null)
                    const step = first.width + row.spacing
                    // Select an edge tab by mouse, not a hidden tab by index.
                    // Wide strips also reveal neighbours from the penultimate tab.
                    const wide = flick.width >= first.width + 4 * step + 12
                    const target = Math.max(1, Math.floor((flick.width + row.spacing) / step) - (wide ? 2 : 1))
                    const clicked = findChild(page, "browserTab" + target)
                    verify(clicked !== null)
                    mouseClick(clicked, 25, 17)
                    tryCompare(page, "currentTab", target)
                    tryVerify(function() { return flick.contentX > 1 })
                    wait(200)
                    const next = findChild(page, "browserTab" + (target + 1))
                    verify(next.x + next.width <= flick.contentX + flick.width + 1,
                           "The neighbouring tab must be fully visible")
                    if (flick.width >= first.width + 4 * step + 12) {
                        const second = findChild(page, "browserTab" + (target + 2))
                        verify(second.x + second.width <= flick.contentX + flick.width + 1,
                               "Wide tab strips reveal two neighbours")
                    }
                    page.switchToTab(6)
                    wait(220)
                    flick.contentX = 6 * step
                    const before = flick.contentX
                    mouseClick(findChild(page, "browserTab6"), 25, 17)
                    tryVerify(function() { return flick.contentX < before - 1 })
                    wait(200)
                    const previous = findChild(page, "browserTab5")
                    verify(previous.x >= flick.contentX - 1,
                           "Clicking an active edge tab also reveals left neighbours")
                    page.switchToTab(11)
                    tryVerify(function() { return Math.abs(flick.contentX - (flick.contentWidth - flick.width)) < 1 })
                    page.switchToTab(0)
                    tryVerify(function() { return Math.abs(flick.contentX) < 1 })
                } finally {
                    window.width = savedWidth
                    page.tabs = savedTabs
                    page.switchToTab(savedCurrent, true)
                }
            }
            function test_hidden_files_and_folder_opacity_data() {
                return [{tag: "grid", grid: true}, {tag: "list", grid: false}]
            }
            function test_hidden_files_and_folder_opacity(data) {
                const pane = page.activePane
                pane.gridMode = data.grid
                const view = findChild(pane, data.grid ? "fileGridView" : "fileListView")
                tryVerify(function() { return pane.itemCount >= 54 })
                const names = [".hidden-folder", ".hidden-file.txt", "visible-folder", "visible-file.txt"]
                for (const name of names) {
                    let index = -1
                    for (let i = 0; i < view.model.count; ++i)
                        if (view.model.itemAt(i).name === name) { index = i; break }
                    verify(index >= 0, "Missing fixture " + name)
                    view.positionViewAtIndex(index, ListView.Beginning)
                    tryVerify(function() { return view.itemAtIndex(index) !== null })
                    const card = findChild(view.itemAtIndex(index), data.grid ? "gridFileCard" : "listFileCard")
                    verify(card !== null)
                    const expected = name.startsWith(".") ? AppTheme.hiddenItemOpacity : 1
                    tryVerify(function() { return Math.abs(card.opacity - expected) < 0.005 })
                    const icon = findChild(card, data.grid ? "gridFileIcon" : "listFileIcon")
                    verify(icon !== null)
                    compare(icon.retainWhileLoading, false)
                    if (name.endsWith("folder")) {
                        compare(icon.asynchronous, false)
                        compare(icon.status, Image.Ready)
                    }
                }
            }
            function test_preview_does_not_show_the_previous_file_on_delegate_reuse() {
                preview.fileIdentity = "first-file"
                preview.candidateSource = AppTheme.icon("music-cover.png")
                tryCompare(preview, "displayReady", true)
                preview.fileIdentity = "second-file"
                preview.candidateSource = AppTheme.icon("logo.png")
                compare(preview.displayReady, false)
                tryCompare(preview, "displayReady", true)
                compare(preview.trackedIdentity, "second-file")
            }
            function test_transfer_counts_speed_and_eta() {
                const pane = page.activePane
                const operation = {kind: "copy", totalItems: 10, processedItems: 2,
                    totalBytes: 10000, processedBytes: 2000, speed: 1000, percent: 20, paused: false}
                verify(pane.transferMetrics(operation).startsWith("2 / 10 items"))
                compare(pane.transferRemaining(operation), "Remaining 8 sec")
                compare(pane.transferPercent(operation), 20)
                operation.paused = true
                compare(pane.transferRemaining(operation), "Paused")
                operation.paused = false
                operation.speed = 0
                compare(pane.transferRemaining(operation), "Calculating remaining time")
            }
            function selectNamedFixture(name, modifiers) {
                const pane = page.activePane
                const model = findChild(pane, "fileGridView").model
                let index = -1
                tryVerify(function() {
                    for (let i = 0; i < model.count; ++i)
                        if (model.itemAt(i).name === name) { index = i; return true }
                    return false
                })
                pane.selectIndex(index, modifiers || Qt.NoModifier)
                return model.itemAt(index)
            }
            function test_context_menu_selection_types_data() {
                return [
                    {tag: "folder", first: "visible-folder", openWith: false, newTab: true},
                    {tag: "file", first: "visible-file.txt", openWith: true, newTab: false},
                    {tag: "folders", first: "visible-folder", second: "Mor", openWith: false, newTab: true},
                    {tag: "mixed", first: "visible-folder", second: "visible-file.txt", openWith: false, newTab: false}
                ]
            }
            function test_context_menu_selection_types(data) {
                const pane = page.activePane
                selectNamedFixture(data.first)
                if (data.second) selectNamedFixture(data.second, Qt.ControlModifier)
                const menu = findChild(pane, "itemContextMenu")
                const submenu = findChild(pane, "openWithContextMenu")
                const newTab = findChild(pane, "openFoldersInNewTabAction")
                verify(menu && submenu && newTab)
                menu.open()
                tryCompare(menu, "opened", true)
                let entry = null
                for (let i = 0; i < menu.count; ++i)
                    if (menu.itemAt(i).subMenu === submenu) entry = menu.itemAt(i)
                verify(entry !== null, "Actual submenu entry exists")
                compare(entry.visible, data.openWith)
                compare(newTab.visible, data.newTab)
                if (!data.openWith) compare(entry.implicitHeight, 0)
                menu.dismiss()
                tryCompare(menu, "visible", false)
                pane.clearSelection()
            }
            function test_folder_new_tab_action() {
                const pane = page.activePane
                const folder = selectNamedFixture("visible-folder")
                const menu = findChild(pane, "itemContextMenu")
                const action = findChild(pane, "openFoldersInNewTabAction")
                menu.open()
                tryCompare(menu, "opened", true)
                const before = page.tabs.length
                newTabSpy.clear()
                action.triggered()
                tryCompare(newTabSpy, "count", 1)
                compare(String(newTabSpy.signalArguments[0][0]), String(folder.itemUrl))
                tryCompare(menu, "visible", false)
                tryVerify(function() { return page.tabs.length === before + 1 })
                page.switchToTab(0, false)
                page.closeTab(before)
                tryVerify(function() { return pane.itemCount > 40 })
            }
            function test_svgz_uses_image_preview_pipeline() {
                const pane = page.activePane
                verify(pane.isPreviewable("svgz"))
                verify(pane.isPreviewable("SVGZ"))
                verify(pane.thumbSource("/fixture/example.svgz", "svgz", null, 100, "revision").startsWith("image://gfilethumb/"))
                compare(String(pane.iconForItem(false, "svgz", "", "example.svgz", "", "image/svg+xml-compressed")),
                        String(AppTheme.icon("image.svg")))
            }
        }
    }
}
