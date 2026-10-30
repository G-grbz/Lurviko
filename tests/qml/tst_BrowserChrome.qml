import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtTest
import Lurviko.App
import Lurviko.Backend

Item {
    width: 1280; height: 800
    ApplicationWindow {
        id: window
        width: 1280; height: 800; visible: true
        color: AppTheme.background
        property bool showingBrowser: true
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
        QtObject { id: storage; property var favoriteItems: [] }
        ContentIndexModel { id: contentIndex }
        Rectangle {
            visible: window.showingBrowser
            x: 10; y: 10 + page.tabStripHeight
            width: window.width - 20; height: window.height - y - 10
            topLeftRadius: 12; topRightRadius: 12
            bottomLeftRadius: AppTheme.panelRadius; bottomRightRadius: AppTheme.panelRadius
            color: page.navigationColor
        }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 0
            Item {
                id: topHost
                Layout.fillWidth: true
                Layout.preferredHeight: window.showingBrowser ? page.headerHeight : 0
                visible: window.showingBrowser
            }
            RowLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; spacing: window.showingBrowser ? 0 : 4
                SidebarShell {
                    id: sidebar
                    unifiedSurface: window.showingBrowser
                    Layout.preferredWidth: implicitWidth; Layout.fillHeight: true
                    lang: language; favoritesModel: favorites; storageModel: storage
                    currentLocation: window.showingBrowser ? page.visibleLocation : ""
                }
                TopSearchBar {
                    id: homeSearch
                    visible: !window.showingBrowser
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    lang: language
                }
                BrowsePage {
                    id: page
                    Layout.fillWidth: true; Layout.fillHeight: true
                    visible: window.showingBrowser
                    headerHost: topHost
                    initialLocation: String(fileArtworkFixtureUrl)
                    lang: language; contentIndexModel: contentIndex; musicPlayer: null
                    favoritesModel: favorites; quickAccessModel: shortcuts; adminEditor: admin
                    cloudAuth: auth; cloudIntegrationPreferences: prefs
                    googleDrive: cloud; oneDrive: cloud
                }
            }
        }
        GTextField { id: clipboardProbe; visible: false }
        TestCase {
            name: "BrowserChrome"
            when: windowShown
            property bool savedAutoHide
            function init() {
                savedAutoHide = AppTheme.sidebarAutoHide
                AppTheme.setSidebarAutoHide(false)
                window.showingBrowser = true
                window.requestActivate()
                tryCompare(window, "active", true)
                page.setSplitEnabled(false)
                page.activePane.currentLocation = String(fileArtworkFixtureUrl)
                page.activePane.closeSearch()
                wait(80)
            }
            function cleanup() {
                const menu = findChild(page, "browserTextEditMenu")
                if (menu) menu.close()
                page.activePane.closeSearch()
                page.activePane.forceActiveFocus()
                page.activePane.leaveLocationEdit()
                window.showingBrowser = true
                AppTheme.setSidebarAutoHide(savedAutoHide)
                wait(180)
            }
            function test_three_rows_span_the_sidebar_and_keep_explore_separate() {
                const tabs = findChild(page, "browserTabsBar")
                const navigation = findChild(page, "browserNavigationHost")
                const command = findChild(page, "browserCommandBar")
                const explore = findChild(sidebar, "sidebarExploreItem")
                const places = findChild(sidebar, "sidebarSection_places")
                const tabsPoint = tabs.mapToItem(window.contentItem, 0, 0)
                const navPoint = navigation.mapToItem(window.contentItem, 0, 0)
                const commandPoint = command.mapToItem(window.contentItem, 0, 0)
                compare(tabs.parent.parent, topHost)
                compare(tabs.width, topHost.width)
                verify(tabsPoint.x < page.x)
                verify(navPoint.y >= tabsPoint.y + tabs.height)
                verify(commandPoint.y >= navPoint.y + navigation.height)
                const explorePoint = explore.mapToItem(window.contentItem, 0, 0)
                verify(Math.abs(explorePoint.y - commandPoint.y) <= 2)
                verify(places.mapToItem(window.contentItem, 0, 0).y >= explorePoint.y + explore.height)
                verify(findChild(places, "sidebarSystemItem_dashboard") === null)
                verify(findChild(places, "sidebarSystemItem_homefolder").visible)
                grabImage(window.contentItem).save("/tmp/lurviko-browser-chrome.png")
            }
            function visualTab(index) {
                const row = findChild(page, "browserTabRow")
                for (let i = 0; i < row.children.length; ++i)
                    if (row.children[i].objectName === "browserTab" + index) return row.children[i]
                return null
            }
            function test_tab_buttons_are_centered_and_inside_the_contour() {
                const savedTabs = page.tabs
                const savedCurrent = page.currentTab
                try {
                    page.tabs = [page.makeTab(String(fileArtworkFixtureUrl)), page.makeTab(String(fileArtworkFixtureUrl))]
                    page.switchToTab(0, true)
                    wait(80)
                    const tab = visualTab(0)
                    const surface = findChild(tab, "fluidTabSurface")
                    const close = findChild(tab, "browserTabClose")
                    const plus = findChild(page, "browserNewTabButton")
                    verify(plus.width <= 28 && plus.height <= 28)
                    const faceCenter = tab.mapToItem(window.contentItem, 0, (surface.capTop + tab.height) / 2)
                    fuzzyCompare(plus.mapToItem(window.contentItem, 0, plus.height / 2).y, faceCenter.y, 0.5)
                    mouseMove(close, close.width / 2, close.height / 2)
                    verify(close.mapToItem(tab, close.width, close.height / 2).x <= tab.width - surface.wing - 4,
                        "Close hover stays inside the curved right border")
                } finally {
                    page.tabs = savedTabs
                    page.switchToTab(savedCurrent, true)
                }
            }
            function test_native_translucent_tab_occlusion_tracks_selection() {
                const savedTabs = page.tabs
                const savedCurrent = page.currentTab
                const savedQt = AppTheme.useQtTheme
                try {
                    inputTestHelper.setSystemThemeTestAppearance(false, true)
                    AppTheme.setQtTheme(true)
                    verify(AppTheme.translucentWindow)
                    const location = String(fileArtworkFixtureUrl)
                    page.tabs = [page.makeTab(location), page.makeTab(location), page.makeTab(location)]
                    page.switchToTab(1, true)
                    verify(inputTestHelper.settleQmlCreations(page))
                    const left = findChild(visualTab(0), "fluidTabSurface")
                    const active = findChild(visualTab(1), "fluidTabSurface")
                    const right = findChild(visualTab(2), "fluidTabSurface")
                    compare(active.tabOverlap, -findChild(page, "browserTabRow").spacing)
                    verify(!active.cutLeft && !active.cutRight)
                    verify(left.cutRight && right.cutLeft)
                    verify(visualTab(1).z > visualTab(0).z && visualTab(1).z > visualTab(2).z)
                    page.switchToTab(0, true)
                    verify(!left.cutLeft && !left.cutRight)
                    verify(active.cutLeft && active.cutRight)
                    verify(!right.cutLeft && !right.cutRight)
                    page.tabDragFrom = 0
                    verify(!active.cutLeft && !active.cutRight)
                    page.tabDragFrom = -1
                    AppTheme.setQtTheme(false)
                    verify(!active.cutLeft && !active.cutRight)
                } finally {
                    page.tabDragFrom = -1
                    page.tabs = savedTabs
                    page.switchToTab(savedCurrent, true)
                    AppTheme.setQtTheme(savedQt)
                    inputTestHelper.restoreSystemThemeTestAppearance()
                }
            }
            function test_view_session_preferences_are_shared_and_reversible() {
                const savedResume = BrowserSession.resumeSession
                const savedWarn = BrowserSession.warnOnMultipleTabs
                try {
                    const resume = findChild(page, "resumeSessionMenuItem")
                    const warn = findChild(page, "warnOnMultipleTabsMenuItem")
                    verify(resume.checkable && warn.checkable)
                    resume.triggered()
                    compare(BrowserSession.resumeSession, !savedResume)
                    compare(resume.checked, !savedResume)
                    warn.triggered()
                    compare(BrowserSession.warnOnMultipleTabs, !savedWarn)
                    compare(warn.checked, !savedWarn)
                } finally {
                    BrowserSession.setResumeSession(savedResume)
                    BrowserSession.setWarnOnMultipleTabs(savedWarn)
                }
            }
            function test_fluid_tabs_join_navigation_and_preserve_switching() {
                const savedTabs = page.tabs
                const savedCurrent = page.currentTab
                try {
                    const location = String(fileArtworkFixtureUrl)
                    page.tabs = [page.makeTab(location), page.makeTab(location + "/Mor"), page.makeTab(location)]
                    page.switchToTab(0, true)
                    verify(inputTestHelper.settleQmlCreations(page))
                    wait(60)
                    const activeTab = visualTab(0)
                    const inactiveTab = visualTab(1)
                    const active = findChild(activeTab, "fluidTabSurface")
                    const inactive = findChild(inactiveTab, "fluidTabSurface")
                    const navigation = findChild(page, "browserNavigationSurface")
                    grabImage(window.contentItem).save("/tmp/lurviko-bordered-tabs.png")
                    verify(active !== null && inactive !== null,
                        "Tab surfaces must be instantiated: " + activeTab + " / " + inactiveTab
                        + " (" + page.tabs.length + " tabs)")
                    verify(active.selected && !inactive.selected)
                    compare(active.fillColor, navigation.color)
                    fuzzyCompare(active.mapToItem(window.contentItem, 0, active.height).y,
                        navigation.mapToItem(window.contentItem, 0, 0).y, 0.1)
                    compare(active.base, active.height)
                    compare(inactive.base, inactive.height)
                    verify(inactive.lowerRadius <= 6 && inactive.corner <= 8)
                    compare(inactive.capTop, active.capTop)
                    verify(findChild(active, "fluidTabGlow") === null,
                        "Tabs must not allocate glow effects or offscreen textures")
                    verify(!active.selectionAnimating && !inactive.selectionAnimating)
                    verify(page.headerHeight <= 90)
                    verify(findChild(page, "browserCommandBar").height <= 40)
                    grabImage(window.contentItem).save("/tmp/lurviko-bordered-tabs.png")
                    const title = findChild(inactiveTab, "browserTabTitle")
                    const titleBefore = title.mapToItem(window.contentItem, 0, 0)
                    const fontBefore = title.font.weight
                    const titleWidth = title.width
                    const tabBefore = inactiveTab.mapToItem(window.contentItem, 0, 0)
                    verify(findChild(page, "browserTabRow").spacing <= -14)
                    verify(activeTab.z > inactiveTab.z)
                    mouseClick(inactiveTab, 45, 22)
                    compare(page.currentTab, 1)
                    compare(visualTab(1), inactiveTab)
                    verify(findChild(inactiveTab, "fluidTabSurface").selectionAnimating,
                        "A tab change starts the short selection transition")
                    verify(inputTestHelper.settleQmlCreations(page))
                    const selectedTab = visualTab(1)
                    const selectedTitle = findChild(selectedTab, "browserTabTitle")
                    const titleAfter = selectedTitle.mapToItem(window.contentItem, 0, 0)
                    const tabAfter = selectedTab.mapToItem(window.contentItem, 0, 0)
                    fuzzyCompare(titleAfter.y, titleBefore.y, 0.1)
                    fuzzyCompare(titleAfter.x - tabAfter.x, titleBefore.x - tabBefore.x, 0.1)
                    compare(selectedTitle.font.weight, fontBefore)
                    fuzzyCompare(selectedTitle.width, titleWidth, 0.1)
                    const selectedSurface = findChild(selectedTab, "fluidTabSurface")
                    tryCompare(selectedSurface, "selectionProgress", 1)
                    tryCompare(selectedSurface, "selectionAnimating", false)
                    const previousSurface = findChild(visualTab(0), "fluidTabSurface")
                    tryCompare(previousSurface, "selectionProgress", 0)
                    verify(findChild(selectedSurface, "fluidTabGlow") === null)
                    verify(findChild(visualTab(1), "fluidTabSurface").selected)
                    mouseClick(visualTab(2), 45, 15, Qt.MiddleButton)
                    compare(page.tabs.length, 2)
                    const closeButton = findChild(visualTab(1), "browserTabClose")
                    verify(closeButton !== null && closeButton.visible)
                    verify(closeButton.width <= 22 && closeButton.height <= 22)
                    compare(closeButton.icon.width, 12)
                    compare(closeButton.icon.height, 12)
                    mouseClick(closeButton, closeButton.width / 2, closeButton.height / 2)
                    compare(page.tabs.length, 1)
                    compare(page.currentTab, 0)
                } finally {
                    page.tabs = savedTabs
                    page.switchToTab(savedCurrent, true)
                }
            }
            function test_context_menu_opens_encryption_for_selected_local_items() {
                const pane = page.activePane
                pane.selectAll()
                const urls = pane.compressionSelectionUrls()
                verify(urls.length > 1)
                const action = findChild(pane, "compressEncryptedAction")
                const dialog = findChild(pane, "encryptedArchiveDialog")
                verify(action !== null && dialog !== null)
                try {
                    action.triggered()
                    tryCompare(dialog, "opened", true)
                    compare(dialog.sourceUrls.length, urls.length)
                    compare(dialog.sourceUrls[0], urls[0])
                    verify(!findChild(dialog, "encryptedArchiveCreate").enabled)
                    mouseClick(findChild(dialog, "encryptedArchiveCancel"))
                    tryCompare(dialog, "visible", false)
                    compare(dialog.sourceUrls.length, 0)
                } finally {
                    dialog.close()
                    pane.clearSelection()
                }
            }
            function test_compact_controls_stay_inset_from_their_backdrops() {
                const navigation = findChild(page, "browserNavigationHost")
                const commands = findChild(page, "browserCommandBar")
                for (const entry of [
                    {row: findChild(page, "paneNavigationBar"), bounds: navigation},
                    {row: findChild(page, "browserCommandControls"), bounds: commands}
                ]) {
                    for (let i = 0; i < entry.row.children.length; ++i) {
                        const control = entry.row.children[i]
                        if (!control.visible || control.width <= 0 || control.height <= 0) continue
                        const point = control.mapToItem(entry.bounds, 0, 0)
                        verify(point.y >= 3, control + " must have top padding")
                        verify(point.y + control.height <= entry.bounds.height - 3,
                            control + " must have bottom padding")
                    }
                }
            }
            function test_sidebar_selection_is_centered_in_its_panel() {
                const explore = findChild(sidebar, "sidebarExploreItem")
                const point = explore.mapToItem(sidebar.panel, 0, 0)
                fuzzyCompare(point.x, sidebar.panel.width - point.x - explore.width, 0.1)
                compare(sidebar.panel.color.a, 0)
                compare(page.activePane.color.a, 0)
                compare(page.activePane.border.width, 0)
            }
            function test_command_icons_use_editable_bundled_artwork() {
                const savedIcons = AppTheme.useSystemIcons
                try {
                    const artwork = {cut: "edit-cut.svg", copy: "edit-copy.svg", paste: "edit-paste.svg",
                        rename: "edit-rename.svg", trash: "delete.svg", properties: "properties.svg"}
                    for (const mode of [false, true]) {
                        AppTheme.setSystemIcons(mode)
                        for (const key in artwork) {
                            const button = findChild(page, "command_" + key)
                            compare(decodeURIComponent(String(button.icon.source)), decodeURIComponent(AppTheme.icon(artwork[key])))
                            verify(!String(button.icon.source).startsWith("image://systemicon/"))
                        }
                        if (!mode)
                            verify(String(findChild(page, "browserRefreshButton").icon.source).endsWith("/refresh.svg"))
                    }
                } finally { AppTheme.setSystemIcons(savedIcons) }
            }
            function test_discover_has_no_browser_header() {
                window.showingBrowser = false
                tryCompare(topHost, "visible", false)
                tryVerify(function() { return sidebar.y === 0 })
                verify(!findChild(page, "browserTabsBar").visible)
                verify(findChild(sidebar, "sidebarExploreItem").visible)
            }
            function test_category_tabs_keep_the_same_header() {
                const savedTabs = page.tabs
                try {
                    page.tabs = [page.makeTab("category:/videos"), page.makeTab("category:/music")]
                    wait(40)
                    verify(findChild(page, "browserTabsBar").visible)
                    verify(findChild(page, "browserNavigationHost").visible)
                    verify(topHost.height >= 82 && topHost.height <= 90)
                } finally { page.tabs = savedTabs }
            }
            function test_split_switches_one_address_row_without_changing_pane_geometry() {
                const left = page.activePane
                page.setSplitEnabled(true)
                tryVerify(function() { return findChild(page, "browserRightPane") !== null })
                const other = findChild(page, "browserRightPane")
                compare(left.navigationHost, other.navigationHost)
                const leftY = left.mapToItem(window.contentItem, 0, 0).y
                compare(other.mapToItem(window.contentItem, 0, 0).y, leftY)
                page.activePane = other
                verify(!findChild(left, "paneNavigationBar").visible)
                verify(findChild(other, "paneNavigationBar").visible)
                compare(other.mapToItem(window.contentItem, 0, 0).y, leftY)
            }
            function test_home_search_context_menu_keeps_the_selection() {
                window.showingBrowser = false
                const field = findChild(homeSearch, "homeSearchField")
                field.text = "Search your home"
                field.forceActiveFocus()
                field.select(0, 6)
                mouseClick(field, field.width - 15, field.height / 2, Qt.RightButton)
                const menu = findChild(homeSearch, "homeSearchTextEditMenu")
                tryCompare(menu, "opened", true)
                compare(field.selectedText, "Search")
                verify(menu.itemAt(4).enabled)
                menu.close()
                wait(180)
            }
            function test_context_menu_preserves_selected_text_data() {
                return [{tag: "address", address: true}, {tag: "search", address: false}]
            }
            function test_context_menu_preserves_selected_text(data) {
                const pane = page.activePane
                if (data.address) pane.focusLocationField()
                else pane.showSearch()
                const field = findChild(pane, data.address ? "browserLocationField" : "browserSearchField")
                tryCompare(field, "activeFocus", true)
                // Focus is assigned on callLater; allow the newly visible
                // search row to finish its layout before pointer input.
                wait(50)
                if (!data.address) field.text = "Selected search text"
                const text = field.text
                field.select(1, 7)
                const selected = field.selectedText
                mouseClick(field, field.width - 15, field.height / 2, Qt.RightButton)
                const menu = findChild(pane, "browserTextEditMenu")
                tryCompare(menu, "opened", true)
                compare(field.text, text)
                compare(field.selectedText, selected)
                if (data.address) verify(pane.locationEditMode)
                const position = menu.parent.mapToItem(window.contentItem, menu.x, menu.y)
                verify(position.x >= 0 && position.y >= 0)
                verify(position.x + menu.width <= window.width)
                const copyAction = menu.itemAt(4)
                verify(copyAction.enabled)
                mouseClick(copyAction, copyAction.width / 2, copyAction.height / 2)
                tryCompare(menu, "visible", false)
                clipboardProbe.text = ""
                clipboardProbe.paste()
                compare(clipboardProbe.text, selected)
            }
        }
    }
}
