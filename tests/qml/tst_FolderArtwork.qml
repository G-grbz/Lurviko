import QtQuick
import QtQuick.Controls
import QtTest
import QtCore
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
            id: checks
            name: "FolderArtwork"
            when: windowShown
            property bool savedSystem
            property bool savedColored
            property bool injectOperations: false
            property var fakeOperations: []
            // Keep this binding dependent on the pane, not every retiring visual delegate.
            readonly property var presentation: page.activePane ? inputTestHelper.objectChild(page.activePane, "operationPresentation") : null
            Binding {
                target: checks.presentation
                property: "operations"
                value: checks.fakeOperations
                when: checks.injectOperations && checks.presentation !== null
                restoreMode: Binding.RestoreBindingOrValue
            }
            function initTestCase() {
                savedSystem = AppTheme.useSystemIcons
                savedColored = AppTheme.useColoredFolderIcons
                tryVerify(function() { return page.activePane !== null && page.activePane.itemCount > 40 })
            }
            function cleanupTestCase() {
                page.activePane.directoryView().forceLayout()
                verify(inputTestHelper.settleQmlCreations(page))
                AppTheme.setSystemIcons(savedSystem)
                AppTheme.setColoredFolderIcons(savedColored)
                wait(40)
                verify(inputTestHelper.settleQmlCreations(page))
                // Finish delegate creation before visibility detaches models.
                page.visible = false
                window.hide()
                wait(50)
                verify(inputTestHelper.settleQmlCreations(page))
            }
            function init() {
                AppTheme.setSystemIcons(false)
                AppTheme.setColoredFolderIcons(true)
                page.activePane.gridMode = true
                page.activePane.showHiddenFiles = true
                tryVerify(function() { return !page.activePane.directory.loading && page.activePane.itemCount > 40 })
                verify(inputTestHelper.settleQmlCreations(page))
                wait(40) // Deferred control polish without requiring an exposed headless window.
            }
            function test_file_names_use_body_font_independent_of_sidebar_and_zoom() {
                const saved = AppTheme.typographySettings.rolesJson
                const pane = page.activePane
                const iconSize = pane.activeIconSize
                try {
                    AppTheme.resetTypography()
                    for (const mode of ["grid", "list", "columns"]) {
                        pane.viewMode = mode
                        wait(80)
                        const objectName = mode === "grid" ? "gridFileName" : mode === "list" ? "listFileName" : "columnFileName"
                        for (const size of mode === "grid" ? [118, 260] : [118]) {
                            pane.setActiveIconSize(size)
                            if (mode !== "columns") pane.directoryView().forceLayout()
                            verify(inputTestHelper.settleQmlCreations(page))
                            let label = null
                            tryVerify(function() { label = findChild(pane, objectName); return label !== null })
                            compare(label.typographyRole, "body")
                            compare(label.font.weight, Font.Normal)
                            AppTheme.setTypography("sidebar", "bold", true)
                            compare(label.font.weight, Font.Normal)
                            AppTheme.setTypography("body", "bold", true)
                            compare(label.font.weight, Font.Bold)
                            AppTheme.setTypography("body", "bold", false)
                            compare(label.font.weight, Font.Normal)
                            AppTheme.setTypography("sidebar", "bold", false)
                        }
                    }
                } finally {
                    AppTheme.typographySettings.rolesJson = saved
                    pane.viewMode = "grid"
                    pane.setActiveIconSize(iconSize)
                    pane.directoryView().forceLayout()
                    verify(inputTestHelper.settleQmlCreations(page))
                }
            }
            function test_view_menu_controls_sidebar_mode_and_persists_it() {
                const saved = AppTheme.sidebarAutoHide
                const savedMode = AppTheme.sidebarRailMode
                const savedWidth = window.width
                const menu = findChild(page, "toolbarViewMenu")
                const toggle = inputTestHelper.objectChild(page, "sidebarAutoHideSwitch")
                const picker = inputTestHelper.objectChild(page, "sidebarRailModePicker")
                const row = inputTestHelper.objectChild(page, "sidebarRailModeSetting")
                try {
                    AppTheme.setSidebarAutoHide(false)
                    menu.open()
                    tryCompare(menu, "opened", true)
                    verify(!row.visible)
                    mouseClick(toggle, toggle.width / 2, toggle.height / 2)
                    compare(AppTheme.sidebarAutoHide, true)
                    verify(row.visible)
                    window.width += 80
                    tryVerify(function() { return Math.abs(page.width - menu.x - menu.width - 8) < 3 })
                    picker.activated(2)
                    compare(AppTheme.sidebarRailMode, "icon")
                    tryVerify(function() { return inputTestHelper.savedAppearanceSetting("sidebarRailMode") === "icon" })
                    mouseClick(toggle, toggle.width / 2, toggle.height / 2)
                    compare(AppTheme.sidebarAutoHide, false)
                    verify(!row.visible)
                } finally {
                    menu.close()
                    AppTheme.setSidebarAutoHide(saved)
                    AppTheme.setSidebarRailMode(savedMode)
                    window.width = savedWidth
                }
            }
            function test_typography_dialog_is_loaded_only_when_opened() {
                const loader = inputTestHelper.objectChild(page, "typographyDialogLoader")
                const action = inputTestHelper.objectChild(page, "typographyMenuItem")
                verify(loader)
                verify(action)
                compare(loader.active, false)
                action.triggered()
                tryVerify(function() { return loader.item && loader.item.opened })
                loader.item.close()
                tryCompare(loader, "active", false)
                compare(loader.item, null)
                verify(inputTestHelper.settleQmlCreations(page))
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
            function test_cancelled_background_selection_data() {
                return [{tag: "grid-disabled", grid: true, disable: true},
                        {tag: "list-disabled", grid: false, disable: true},
                        {tag: "grid-cancelled", grid: true, disable: false},
                        {tag: "list-cancelled", grid: false, disable: false}]
            }
            function test_cancelled_background_selection(data) {
                const pane = page.activePane
                const savedLocation = pane.currentLocation
                pane.gridMode = data.grid
                // Qt 6.12's recursive TestCase.findChild can encounter retired
                // delegates while switching views. The gesture surface is a
                // direct contentItem child; resolve it without traversing them.
                const view = pane.directoryView()
                view.forceLayout()
                verify(inputTestHelper.settleQmlCreations(page))
                const name = data.grid ? "gridBackgroundMouse" : "listBackgroundMouse"
                let background = null
                for (const child of view.contentItem.children)
                    if (child && child.objectName === name) { background = child; break }
                verify(background !== null)
                try {
                    pane.currentLocation = String(fileArtworkFixtureUrl) + "/visible-folder"
                    tryVerify(function() { return !pane.directory.loading && pane.itemCount === 0 })
                    mousePress(background, 30, 30, Qt.LeftButton)
                    tryCompare(pane, "rubberSelecting", true)
                    verify(background.pressed)
                    // Interrupt a real pressed MouseArea by disabling its
                    // owner. Qt does not always deliver cancelled in this case.
                    if (data.disable)
                        background.enabled = false
                    else
                        inputTestHelper.cancelMouseGrab(background)
                    tryCompare(pane, "rubberSelecting", false)
                } finally {
                    background.enabled = true
                    mouseRelease(background, 30, 30, Qt.LeftButton)
                    pane.finishRubberSelection(data.grid ? "grid" : "list")
                    pane.currentLocation = savedLocation
                    tryVerify(function() { return !pane.directory.loading && pane.itemCount > 40 })
                    view.forceLayout()
                    verify(inputTestHelper.settleQmlCreations(page))
                }
            }
            function test_explicit_selection_replaces_interrupted_preview_data() {
                return [{tag: "click", reveal: false}, {tag: "show-in-folder", reveal: true}]
            }
            function test_explicit_selection_replaces_interrupted_preview(data) {
                const pane = page.activePane
                const view = findChild(pane, "fileGridView")
                tryVerify(function() { return !pane.directory.loading && pane.itemCount > 40 })
                const item = pane.directory.itemAt(0)
                try {
                    pane.beginRubberSelection("grid", 0, 0, Qt.NoModifier)
                    compare(pane.rubberSelecting, true)
                    if (data.reveal)
                        pane.revealExternalItem(pane.currentLocation, item.itemUrl, true)
                    else
                        pane.selectIndex(0, Qt.NoModifier)
                    tryCompare(pane, "selectedUrl", item.itemUrl)
                    tryCompare(pane, "rubberSelecting", false)
                    verify(pane.isSelected(item.itemUrl))
                    view.positionViewAtIndex(0, GridView.Beginning)
                    tryVerify(function() { return view.itemAtIndex(0) !== null })
                    const highlight = findChild(view.itemAtIndex(0), "gridItemHighlight")
                    verify(highlight !== null)
                    tryCompare(highlight, "visible", true)
                } finally {
                    pane.finishRubberSelection("grid")
                }
            }
            function test_rectangle_selection_preserves_modifier_behaviour_data() {
                let cases = []
                for (const grid of [true, false])
                    for (const modifier of [Qt.NoModifier, Qt.ControlModifier, Qt.ShiftModifier])
                        cases.push({tag: (grid ? "grid" : "list") + "-" + modifier,
                                    grid: grid, modifier: modifier})
                return cases
            }
            function test_rectangle_selection_preserves_modifier_behaviour(data) {
                const pane = page.activePane
                pane.gridMode = data.grid
                tryVerify(function() { return !pane.directory.loading && pane.itemCount > 40 })
                const view = findChild(pane, data.grid ? "fileGridView" : "fileListView")
                const first = pane.directory.itemAt(0).itemUrl
                const second = pane.directory.itemAt(1).itemUrl
                pane.selectIndex(0, Qt.NoModifier)
                pane.beginRubberSelection(data.grid ? "grid" : "list", 1, 1, data.modifier)
                compare(pane.rubberSelecting, true)
                const rowHeight = Math.round(48 + (pane.activeIconSize - 118) * 0.13)
                pane.updateRubberSelection(data.grid ? view.cellWidth * 2 - 1 : 15,
                                          data.grid ? view.cellHeight - 1 : rowHeight * 2 - 1)
                pane.finishRubberSelection(data.grid ? "grid" : "list")
                compare(pane.rubberSelecting, false)
                compare(pane.rubberView, "")
                compare(pane.rubberPreviewSelection.length, 0)
                compare(pane.selectedCount, data.modifier === Qt.ControlModifier ? 1 : 2)
                compare(pane.isSelected(first), data.modifier !== Qt.ControlModifier)
                verify(pane.isSelected(second))
            }
            function test_navigation_discards_interrupted_selection() {
                const pane = page.activePane
                const location = pane.currentLocation
                try {
                    // Start from a fully created viewport. This test interrupts
                    // rubber selection, not Qt's asynchronous delegate setup.
                    verify(inputTestHelper.settleQmlCreations(page))
                    pane.beginRubberSelection("grid", 1, 1, Qt.NoModifier)
                    pane.currentLocation = String(fileArtworkFixtureUrl) + "/visible-folder"
                    tryVerify(function() { return !pane.directory.loading && pane.itemCount === 0 })
                    compare(pane.rubberSelecting, false)
                    compare(pane.rubberView, "")
                } finally {
                    pane.currentLocation = location
                    tryVerify(function() { return !pane.directory.loading && pane.itemCount > 40 })
                    verify(inputTestHelper.settleQmlCreations(page))
                }
            }
            function test_view_switch_discards_interrupted_selection() {
                const pane = page.activePane
                pane.beginRubberSelection("grid", 1, 1, Qt.NoModifier)
                pane.gridMode = false
                compare(pane.rubberSelecting, false)
                pane.directoryView().forceLayout()
                verify(inputTestHelper.settleQmlCreations(page))
                pane.beginRubberSelection("list", 1, 1, Qt.NoModifier)
                pane.visible = false
                compare(pane.rubberSelecting, false)
                pane.visible = true
                pane.directoryView().forceLayout()
                verify(inputTestHelper.settleQmlCreations(page))
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
            function test_progress_cards_minimize_restore_and_clear_the_status_icon() {
                const native = AppTheme.kioProgressEnabled
                const automatic = AppTheme.operationAutoHide
                try {
                    AppTheme.setKioProgressEnabled(false)
                    AppTheme.setOperationAutoHide(false)
                    fakeOperations = [{id: 90001, kind: "copy", detail: "A long transfer", percent: 20,
                        finished: false, success: false, controllable: false}]
                    injectOperations = true
                    const cards = findChild(page.activePane, "operationCards")
                    const toggle = findChild(page.activePane, "operationProgressToggle")
                    tryCompare(cards, "visible", true)
                    tryCompare(toggle, "visible", true)
                    const minimize = findChild(cards, "operationCardMinimize")
                    verify(minimize !== null)
                    mouseClick(minimize, minimize.width / 2, minimize.height / 2)
                    tryCompare(cards, "visible", false)
                    verify(toggle.visible)
                    mouseClick(toggle, toggle.width / 2, toggle.height / 2)
                    tryCompare(cards, "visible", true)
                    mouseClick(toggle, toggle.width / 2, toggle.height / 2)
                    tryCompare(cards, "visible", false)
                    fakeOperations = [{id: 90001, kind: "copy", percent: 100, finished: true, success: true}]
                    tryCompare(toggle, "visible", false)
                    verify(!cards.visible, "A minimized successful operation must not reopen its toast")
                } finally {
                    injectOperations = false
                    fakeOperations = []
                    AppTheme.setOperationAutoHide(automatic)
                    AppTheme.setKioProgressEnabled(native)
                }
            }
            function test_auto_hide_settings_visibility_editing_and_persistence() {
                const native = AppTheme.kioProgressEnabled
                const automatic = AppTheme.operationAutoHide
                const seconds = AppTheme.operationAutoHideSeconds
                const menu = findChild(page, "toolbarViewMenu")
                try {
                    window.requestActivate()
                    tryCompare(window, "active", true)
                    AppTheme.setKioProgressEnabled(false)
                    AppTheme.setOperationAutoHide(true)
                    AppTheme.setOperationAutoHideSeconds(9)
                    menu.open()
                    tryCompare(menu, "opened", true)
                    const row = findChild(page, "operationAutoHideSettings")
                    const toggle = findChild(page, "operationAutoHideSwitch")
                    const delay = findChild(page, "operationAutoHideDelay")
                    let rowIndex = -1
                    for (let i = 0; i < menu.count; ++i)
                        if (menu.itemAt(i) === row) rowIndex = i
                    verify(rowIndex >= 0)
                    menu.currentIndex = rowIndex
                    wait(100)
                    verify(row.visible)
                    compare(delay.text, "9")
                    mouseClick(delay, delay.width / 2, delay.height / 2)
                    tryCompare(delay, "activeFocus", true)
                    keyClick(Qt.Key_A, Qt.ControlModifier)
                    keyClick(Qt.Key_1)
                    keyClick(Qt.Key_2)
                    keyClick(Qt.Key_Return)
                    compare(delay.text, "12", "The editable delay receives typed digits")
                    tryCompare(AppTheme, "operationAutoHideSeconds", 12)
                    mouseClick(toggle, toggle.width / 2, toggle.height / 2)
                    tryCompare(AppTheme, "operationAutoHide", false)
                    verify(menu.visible, "Embedded preferences keep the view menu open")
                    // QtCore.Settings batches property writes. Wait for that
                    // batch before reading through a fresh native QSettings instance.
                    tryVerify(function() {
                        return String(AppTheme.settings.value("operationAutoHide", true)) === "false"
                                && Number(AppTheme.settings.value("operationAutoHideSeconds", 0)) === 12
                    })
                    AppTheme.settings.sync()
                    compare(inputTestHelper.savedAppearanceSetting("operationAutoHide"), false)
                    compare(inputTestHelper.savedAppearanceSetting("operationAutoHideSeconds"), 12)
                    AppTheme.setKioProgressEnabled(true)
                    verify(!row.visible)
                    AppTheme.setOperationAutoHideSeconds(-20)
                    compare(AppTheme.operationAutoHideSeconds, 1)
                    AppTheme.setOperationAutoHideSeconds(9999)
                    compare(AppTheme.operationAutoHideSeconds, 300)
                } finally {
                    menu.close()
                    tryCompare(menu, "visible", false)
                    AppTheme.setOperationAutoHide(automatic)
                    AppTheme.setOperationAutoHideSeconds(seconds)
                    AppTheme.setKioProgressEnabled(native)
                    AppTheme.settings.sync()
                }
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
