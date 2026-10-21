import QtQuick
import QtQuick.Controls
import QtTest
import Lurviko.App
import Lurviko.Backend

Item {
    width: 1100; height: 720
    ApplicationWindow {
        id: appearanceWindow
        width: 1100; height: 720
        visible: true
        // A locally overridden window palette must not feed back into Qt mode.
        palette.window: "#aaddbb"
        color: AppTheme.useQtTheme ? "transparent" : AppTheme.background
        background: Rectangle { color: AppTheme.background }
        DesktopWindowEffects { id: effects; window: appearanceWindow; enabled: AppTheme.translucentWindow }
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
            anchors.leftMargin: 80
            anchors.topMargin: 25
            initialLocation: String(fileArtworkFixtureUrl)
            lang: language; contentIndexModel: null; musicPlayer: null
            favoritesModel: favorites; quickAccessModel: shortcuts; adminEditor: admin
            cloudAuth: auth; cloudIntegrationPreferences: prefs
            googleDrive: cloud; oneDrive: cloud
        }
        GButton { id: primary; primary: true; text: "Primary"; visible: false }
        GTextField { id: field; visible: false }
        VideoViewer { id: video; lang: language }
        QtObject { id: storage; property var favoriteItems: [] }
        AppSidebar {
            id: sidebar
            width: 250; height: 700; visible: false
            lang: language; favoritesModel: favorites; storageModel: storage
        }
        KeyboardShortcutsDialog { id: keyboard; lang: language }
        PhotoViewer { id: photo; lang: language }
        property int submenuInvocations: 0
        GMenu {
            id: parentMenu
            parent: appearanceWindow.contentItem
            x: 40; y: 90
            GMenu {
                id: childMenu
                title: "Submenu"
                GMenuItem { id: childAction; text: "Run"; onTriggered: appearanceWindow.submenuInvocations++ }
            }
        }
        TestCase {
            name: "DesktopAppearance"
            when: windowShown
            property bool savedDark
            property bool savedQt
            function initTestCase() {
                savedDark = AppTheme.settings.darkMode
                savedQt = AppTheme.useQtTheme
            }
            function cleanupTestCase() {
                inputTestHelper.restoreSystemThemeTestAppearance()
                AppTheme.settings.darkMode = savedDark
                AppTheme.setQtTheme(savedQt)
            }
            function init() {
                parentMenu.dismiss()
                findChild(page, "toolbarViewMenu").dismiss()
                findChild(page.activePane, "itemContextMenu").dismiss()
                AppTheme.setQtTheme(false)
                AppTheme.settings.darkMode = false
                inputTestHelper.setSystemThemeTestAppearance(false, false)
                AppTheme.desktopAppearance.refresh()
                language.language = "en"
            }
            function test_positioned_context_menu_stays_in_host_window() {
                AppTheme.setQtTheme(true)
                const pane = page.activePane
                const menu = findChild(pane, "itemContextMenu")
                for (let repeat = 0; repeat < 3; ++repeat) {
                    const point = Qt.point(90 + repeat * 35, 140 + repeat * 20)
                    pane.showContextMenu(menu, pane, point.x, point.y)
                    tryCompare(menu, "opened", true)
                    wait(40) // selection/service entries finish their first layout
                    compare(menu.popupType, Popup.Item)
                    const position = menu.contentItem.parent.mapToItem(pane, 0, 0)
                    const expectedX = Math.max(8, Math.min(pane.width - menu.implicitWidth - 8, point.x))
                    const expectedY = Math.max(8, Math.min(pane.height - menu.implicitHeight - 8, point.y))
                    verify(Math.abs(position.x - expectedX) < 2)
                    verify(Math.abs(position.y - expectedY) < 2,
                           "actual=" + position.y + " expected=" + expectedY + " y=" + menu.y
                           + " height=" + menu.height + " implicit=" + menu.implicitHeight)
                    menu.dismiss()
                    tryCompare(menu, "visible", false)
                }
            }
            function test_modals_and_toast_have_native_backing() {
                inputTestHelper.setTransparentSystemThemeTestAppearance()
                AppTheme.setQtTheme(true)
                compare(AppTheme.surface.a, 1)
                keyboard.open()
                tryCompare(keyboard, "opened", true)
                const surface = findChild(keyboard.background, "nativeOverlaySurface")
                verify(surface !== null && surface.nativeTheme)
                verify(surface.sourceItem !== null)
                if (!surface.blurActive) compare(surface.color.a, 1)
                keyboard.close()
                tryCompare(keyboard, "visible", false)
                const category = findChild(page.activePane, "musicCategorySettings")
                category.open()
                tryCompare(category, "opened", true)
                verify(findChild(category.background, "nativeOverlaySurface").nativeTheme)
                category.close()
                tryCompare(category, "visible", false)
                page.showToast("Theme test")
                const toast = findChild(page, "browserToast")
                tryCompare(toast, "opened", true)
                verify(toast.background.nativeTheme)
                verify(toast.background.sourceItem !== null)
                toast.close()
                tryCompare(toast, "visible", false)
            }
            function test_native_menu_uses_menu_style_not_toolbar_tint() {
                inputTestHelper.setSystemThemeTestAppearance(false, true)
                AppTheme.setQtTheme(true)
                const menu = findChild(page, "toolbarViewMenu")
                menu.open()
                tryCompare(menu, "opened", true)
                compare(menu.popupType, Popup.Item)
                const surface = menu.background
                verify(surface.nativeTheme)
                // Native raster samples may differ by one quantization step
                // when unpremultiplied; compare channels, not decimal strings.
                verify(Math.abs(surface.nativeColor.r - 203 / 255) < 2 / 255)
                verify(Math.abs(surface.nativeColor.g - 220 / 255) < 2 / 255)
                verify(Math.abs(surface.nativeColor.b - 235 / 255) < 2 / 255)
                verify(surface.nativeColor.a >= 0.79)
                verify(surface.nativeColor.a > AppTheme.commandBar.a)
                verify(surface.sourceItem !== null)
                if (!surface.blurActive) compare(surface.color.a, 1)
                menu.dismiss()
                tryCompare(menu, "visible", false)
                AppTheme.setQtTheme(false)
                compare(menu.popupType, Popup.Item)
                compare(surface.color, AppTheme.commandBar)
            }
            function test_native_submenu_hover_and_action_close_the_chain() {
                AppTheme.setQtTheme(true)
                parentMenu.open()
                tryCompare(parentMenu, "opened", true)
                const entry = parentMenu.itemAt(0)
                verify(entry && entry.subMenu === childMenu)
                mouseMove(entry, entry.width / 2, entry.height / 2)
                tryCompare(childMenu, "opened", true)
                compare(childMenu.popupType, Popup.Item)
                const previous = appearanceWindow.submenuInvocations
                mouseClick(childAction)
                compare(appearanceWindow.submenuInvocations, previous + 1)
                tryCompare(parentMenu, "visible", false)
                tryCompare(childMenu, "visible", false)
            }
            function test_native_toolbar_menus_switch_on_one_click_and_close_outside() {
                AppTheme.setQtTheme(true)
                const viewButton = findChild(page, "viewButton")
                const hiddenButton = findChild(page, "hiddenFilesButton")
                const menu = findChild(page, "toolbarViewMenu")
                mouseClick(viewButton)
                tryCompare(menu, "opened", true)
                mousePress(hiddenButton)
                const nextMenu = page.toolbarPressedMenu
                verify(nextMenu !== null && nextMenu !== menu)
                mouseRelease(hiddenButton)
                tryCompare(nextMenu, "opened", true)
                tryCompare(menu, "visible", false)
                mouseClick(page, 20, page.height - 20)
                tryCompare(nextMenu, "visible", false)
            }
            function test_menu_is_in_view_menu_and_localized() {
                const menu = findChild(page, "toolbarViewMenu")
                const entry = findChild(menu, "qtThemeMenuItem")
                verify(entry !== null)
                compare(entry.text, "Use Qt theme")
                entry.triggered()
                compare(AppTheme.useQtTheme, true)
                compare(AppTheme.settings.qtTheme, true)
                compare(entry.checked, true)
                language.language = "tr"
                compare(entry.text, "Qt temasını kullan")
                entry.triggered()
                compare(AppTheme.useQtTheme, false)
            }
            function test_qt_colors_ignore_overridden_window_palette_data() {
                return [{tag: "light", dark: false}, {tag: "dark", dark: true}]
            }
            function test_qt_colors_ignore_overridden_window_palette(data) {
                inputTestHelper.setSystemThemeTestAppearance(data.dark, false)
                AppTheme.setQtTheme(true)
                compare(AppTheme.dark, data.dark)
                compare(AppTheme.background, data.dark ? "#20272d" : "#e3e7ed")
                compare(AppTheme.surface, data.dark ? "#10171d" : "#ffffff")
                compare(AppTheme.accent, "#e0c35c")
                compare(AppTheme.highlightedText, "#151515")
                compare(primary.palette.buttonText, AppTheme.highlightedText)
                compare(field.selectedTextColor, AppTheme.highlightedText)
                compare(AppTheme.translucentWindow, false)
                compare(effects.enabled, false)
            }
            function test_live_palette_changes_and_custom_mode_restoration() {
                AppTheme.settings.darkMode = true
                AppTheme.setQtTheme(true)
                compare(AppTheme.dark, false)
                AppTheme.toggle()
                compare(AppTheme.settings.darkMode, true)
                inputTestHelper.setSystemThemeTestAppearance(true, false)
                tryCompare(AppTheme, "background", "#20272d")
                compare(AppTheme.dark, true)
                inputTestHelper.setSystemThemeTestAppearance(false, false)
                tryCompare(AppTheme, "background", "#e3e7ed")
                AppTheme.setQtTheme(false)
                compare(AppTheme.dark, true)
                compare(AppTheme.background, "#080c16")
                AppTheme.toggle()
                compare(AppTheme.background, "#e9edf4")
            }
            function test_video_chrome_follows_qt_but_canvas_stays_black() {
                AppTheme.setQtTheme(true)
                compare(AppTheme.dark, false)
                compare(video.palette.window, AppTheme.viewerGlassStrong)
                compare(video.background.color, "#080b11")
                inputTestHelper.setSystemThemeTestAppearance(true, false)
                tryCompare(AppTheme, "dark", true)
                compare(video.palette.window, AppTheme.viewerGlassStrong)
                compare(video.background.color, "#080b11")
                AppTheme.setQtTheme(false)
                compare(video.palette.window, "#18212e")
            }
            function test_manual_theme_switch_is_disabled_only_in_qt_mode() {
                const toggle = findChild(sidebar, "themeSwitch")
                verify(toggle !== null)
                compare(toggle.enabled, true)
                AppTheme.setQtTheme(true)
                compare(toggle.enabled, false)
                compare(AppTheme.settings.darkMode, false)
                AppTheme.setQtTheme(false)
                compare(toggle.enabled, true)
            }
            function test_native_translucency_is_sampled_once_and_not_stacked() {
                inputTestHelper.setSystemThemeTestAppearance(false, true)
                AppTheme.setQtTheme(true)
                verify(Math.abs(AppTheme.background.a - 0.6) < 0.01)
                compare(AppTheme.translucentWindow, true)
                compare(AppTheme.workspace.a, 0)
                compare(AppTheme.sidebar.a, 0)
                compare(page.activePane.color.a, 0)
                verify(AppTheme.cardSurface.a < 0.1)
                verify(AppTheme.panelSurface.a < 0.1)
                verify(AppTheme.border.a < 0.2)
                verify(AppTheme.borderStrong.a < 0.3)
                compare(effects.enabled, true)
                wait(30)
                const paints = inputTestHelper.systemThemeProbePaints()
                wait(150)
                compare(inputTestHelper.systemThemeProbePaints(), paints)
                AppTheme.setQtTheme(false)
                compare(effects.enabled, false)
                compare(AppTheme.background.a, 1)
            }
            function test_theme_setting_can_be_reloaded() {
                AppTheme.setQtTheme(true)
                // AppTheme is a singleton; read its actual Settings category
                // using another Settings object, as on the next launch.
                const settings = Qt.createQmlObject('import QtCore; Settings { category: "Appearance"; property bool qtTheme: false }', appearanceWindow)
                compare(settings.qtTheme, true)
                settings.destroy()
            }
        }
    }
}
