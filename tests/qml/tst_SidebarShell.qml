import QtQuick
import QtQuick.Controls
import QtTest
import Lurviko.App
import Lurviko.Backend

Item {
    width: 900; height: 700
    QtObject { id: language; property string language: "en"; function t(key) { return key } }
    FavoritesModel { id: favorites }
    QtObject { id: storage; property var favoriteItems: [] }
    SidebarShell {
        id: shell
        width: implicitWidth
        height: parent.height
        lang: language
        favoritesModel: favorites
        storageModel: storage
    }
    Rectangle { id: workspace; x: shell.width + 8; width: parent.width - x; height: parent.height; color: AppTheme.workspace }
    SignalSpy { id: navigations; target: shell; signalName: "locationRequested" }
    SignalSpy { id: discoveries; target: shell; signalName: "homeRequested" }
    TestCase {
        name: "SidebarShell"
        when: windowShown
        property bool savedAutoHide
        property string savedMode
        function init() {
            savedAutoHide = AppTheme.sidebarAutoHide
            savedMode = AppTheme.sidebarRailMode
            AppTheme.setSidebarAutoHide(false)
            AppTheme.setSidebarRailMode("both")
            shell.closeSection()
            navigations.clear()
            discoveries.clear()
            mouseMove(workspace, workspace.width - 20, workspace.height - 20)
            wait(120)
        }
        function cleanup() {
            shell.closeSection()
            AppTheme.setSidebarAutoHide(savedAutoHide)
            AppTheme.setSidebarRailMode(savedMode)
            wait(150)
        }
        function badge(key) { return findChild(shell, "sidebarBadge_" + key) }
        function collapse() {
            AppTheme.setSidebarAutoHide(true)
            tryCompare(shell, "collapsed", true)
            verify(shell.width < 248)
            wait(50) // Allow the reparented controls and their layouts to polish.
        }
        function test_disabled_keeps_the_existing_complete_sidebar() {
            compare(shell.width, 248)
            compare(shell.panel.sectionFilter, "")
            verify(shell.panel.footer.visible)
            compare(shell.panel.footer.columns, 6)
            for (const key of ["places", "favorites", "network", "disks"])
                verify(findChild(shell.panel, "sidebarSection_" + key).visible)
        }
        function test_hover_opens_only_its_section_without_resizing_the_workspace() {
            collapse()
            const width = workspace.width
            const sharedPanel = shell.panel
            for (const key of ["places", "favorites", "network", "disks"]) {
                mouseMove(badge(key), badge(key).width / 2, badge(key).height / 2)
                tryVerify(function() { return shell.sectionPopup.opened && shell.activeSection === key })
                compare(shell.panel, sharedPanel)
                compare(workspace.width, width)
                for (const section of ["places", "favorites", "network", "disks"])
                    compare(findChild(shell.panel, "sidebarSection_" + section).visible, section === key)
            }
        }
        function test_crossing_from_badge_to_panel_does_not_close_it() {
            collapse()
            mouseMove(badge("places"), 20, 20)
            tryCompare(shell.sectionPopup, "opened", true)
            mouseMove(shell.panel, shell.panel.width / 2, 30)
            wait(500)
            verify(shell.sectionPopup.visible)
            mouseMove(workspace, workspace.width - 20, workspace.height - 20)
            tryCompare(shell.sectionPopup, "visible", false)
        }
        function test_quick_pass_over_a_badge_does_not_open_a_panel() {
            collapse()
            mouseMove(badge("favorites"), 20, 20)
            wait(35)
            mouseMove(workspace, workspace.width - 20, workspace.height - 20)
            wait(250)
            verify(!shell.sectionPopup.visible)
        }
        function test_badge_modes_and_larger_icon_only_artwork() {
            collapse()
            const icon = findChild(badge("places"), "sidebarBadgeIcon_places")
            const title = findChild(badge("places"), "sidebarBadgeTitle_places")
            verify(icon.visible && title.visible)
            compare(icon.width, 18)
            AppTheme.setSidebarRailMode("title")
            verify(!icon.visible && title.visible)
            AppTheme.setSidebarRailMode("icon")
            verify(icon.visible && !title.visible)
            tryCompare(icon, "width", 28)
            compare(shell.width, 54)
            AppTheme.setSidebarRailMode("invalid")
            compare(AppTheme.sidebarRailMode, "icon")
        }
        function test_icon_only_badges_center_the_artwork_in_both_directions() {
            collapse()
            AppTheme.setSidebarRailMode("icon")
            wait(50)
            for (const key of ["dashboard", "places", "favorites", "network", "disks"]) {
                const button = badge(key)
                const icon = findChild(button, "sidebarBadgeIcon_" + key)
                const center = icon.mapToItem(button, icon.width / 2, icon.height / 2)
                fuzzyCompare(center.x, button.width / 2, 0.1)
                fuzzyCompare(center.y, button.height / 2, 0.1)
            }
        }
        function test_vertical_badges_are_evenly_distributed_and_fit_after_resizing() {
            collapse()
            const keys = ["places", "favorites", "network", "disks"]
            const rail = findChild(shell, "sidebarBadges")
            const backdrop = findChild(shell, "sidebarRailBackground")
            compare(backdrop.topLeftRadius, 0)
            compare(backdrop.bottomLeftRadius, 0)
            compare(backdrop.topRightRadius, AppTheme.radiusSmall)
            compare(backdrop.bottomRightRadius, AppTheme.radiusSmall)
            try {
                for (const mode of ["both", "title"]) {
                    AppTheme.setSidebarRailMode(mode)
                    for (const railHeight of [700, 580]) {
                        shell.height = railHeight
                        wait(50)
                        let previousBottom = 0
                        for (let i = 0; i < keys.length; ++i) {
                            const button = badge(keys[i])
                            const title = findChild(button, "sidebarBadgeTitle_" + keys[i])
                            fuzzyCompare(button.y + button.height / 2, rail.height * (i + 0.5) / keys.length, 0.1)
                            verify(button.y >= previousBottom)
                            verify(button.y + button.height <= rail.height)
                            if (mode !== "icon") compare(title.rotation, -90)
                            previousBottom = button.y + button.height
                        }
                    }
                }
            } finally { shell.height = 700 }
        }
        function test_icon_mode_has_a_borderless_dock_centered_above_administrator_controls() {
            collapse()
            AppTheme.setSidebarRailMode("icon")
            const keys = ["dashboard", "places", "favorites", "network", "disks"]
            const admin = findChild(shell, "sidebarRailAdministrator")
            try {
                for (const height of [580, 700, 900]) {
                    shell.height = height
                    wait(50)
                    let previousBottom = -1
                    for (const key of keys) {
                        const button = badge(key)
                        const position = button.mapToItem(shell, 0, 0)
                        fuzzyCompare(position.x + button.width / 2, shell.width / 2, 0.1)
                        compare(button.background.color.a, 0)
                        compare(button.background.border.width, 0)
                        if (previousBottom >= 0) fuzzyCompare(position.y - previousBottom, 6, 0.1)
                        previousBottom = position.y + button.height
                    }
                    const top = badge("dashboard").mapToItem(shell, 0, 0).y
                    const adminTop = admin.mapToItem(shell, 0, 0).y
                    fuzzyCompare((top + previousBottom) / 2, adminTop / 2, 0.1)
                    verify(previousBottom < adminTop)
                    verify((top + previousBottom) / 2 < shell.height / 2 - 40)
                }
                shell.height = 700
                wait(50)
                const favorites = badge("favorites")
                mouseMove(favorites, favorites.width / 2, favorites.height / 2)
                tryCompare(shell.sectionPopup, "opened", true)
                compare(shell.activeSection, "favorites")
                compare(favorites.background.color.a, 0)
                compare(favorites.background.border.width, 0)
            } finally { shell.height = 700 }
        }
        function test_badge_names_fit_at_compact_window_height() {
            collapse()
            try {
                shell.height = 680
                for (const mode of ["both", "title"]) {
                    AppTheme.setSidebarRailMode(mode)
                    wait(50)
                    for (const key of ["dashboard", "places", "favorites", "network", "disks"]) {
                        const title = findChild(badge(key), "sidebarBadgeTitle_" + key)
                        verify(!title.truncated, key + " should fit without an ellipsis")
                    }
                }
            } finally { shell.height = 700 }
        }
        function test_bottom_controls_remain_visible_and_reuse_the_full_sidebar_controls() {
            const footer = shell.panel.footer
            const tr = findChild(footer, "sidebarTurkishButton")
            const en = findChild(footer, "sidebarEnglishButton")
            const theme = findChild(footer, "themeSwitch")
            const savedLanguage = language.language
            const savedQt = AppTheme.useQtTheme
            const savedDark = AppTheme.settings.darkMode
            try {
                collapse()
                tryCompare(footer, "columns", 1)
                tryVerify(function() { return en.y >= tr.y + tr.height })
                compare(footer.parent, findChild(shell, "sidebarRailControlsHost"))
                verify(footer.visible && tr.visible && en.visible && theme.visible)
                verify(!shell.sectionPopup.visible)
                verify(!findChild(shell.panel, "sidebarAdministratorItem").visible)
                mouseClick(tr, tr.width / 2, tr.height / 2)
                compare(language.language, "tr")
                mouseClick(en, en.width / 2, en.height / 2)
                compare(language.language, "en")
                AppTheme.setQtTheme(false)
                const previousDark = AppTheme.dark
                mouseClick(theme, theme.width / 2, theme.height / 2)
                compare(AppTheme.dark, !previousDark)
                AppTheme.setQtTheme(true)
                verify(!theme.enabled)
                const admin = findChild(shell, "sidebarRailAdministrator")
                verify(admin.visible)
                mouseClick(admin, admin.width / 2, admin.height / 2)
                compare(navigations.count, 1)
                compare(navigations.signalArguments[0][0], "admin:///")
                verify(!shell.sectionPopup.visible)
                AppTheme.setSidebarAutoHide(false)
                compare(shell.panel.footer, footer)
                tryCompare(footer, "columns", 6)
                verify(footer.visible)
            } finally {
                language.language = savedLanguage
                AppTheme.settings.darkMode = savedDark
                AppTheme.setQtTheme(savedQt)
            }
        }
        function test_mini_player_stays_available_without_opening_a_section() {
            collapse()
            try {
                shell.musicPlayer = {
                    opened: true, playing: false, currentFavorite: false,
                    trackMeta: {}, currentTitle: "", displayTitle: "", currentArtwork: "",
                    queue: [], currentIndex: 0, position: 0, duration: 0,
                    shuffleMode: false, repeatMode: 0, volume: 0.5, muted: false,
                    showRemainingTime: false, durationText: function() { return "0:00" }
                }
                const button = findChild(shell.panel.footer, "sidebarMiniMusicButton")
                tryCompare(button, "visible", true)
                mouseClick(button, button.width / 2, button.height / 2)
                const popup = inputTestHelper.objectChild(shell.panel, "sidebarMiniMusicPopup")
                tryCompare(popup, "opened", true)
                verify(!shell.sectionPopup.visible)
                verify(popup.contentItem.visible)
                verify(popup.contentItem.mapToItem(shell, 0, 0).x >= shell.width)
                keyClick(Qt.Key_Escape)
                tryCompare(popup, "visible", false)
            } finally { shell.musicPlayer = null }
        }
        function test_badge_click_pins_escape_closes_and_restore_returns_full_sidebar() {
            collapse()
            tryCompare(findChild(shell, "sidebarRestoreIcon"), "status", Image.Ready)
            mouseClick(badge("network"), 20, 20)
            tryCompare(shell.sectionPopup, "opened", true)
            verify(shell.pinnedOpen)
            mouseMove(workspace, workspace.width - 20, workspace.height - 20)
            wait(500)
            verify(shell.sectionPopup.visible)
            keyClick(Qt.Key_Escape)
            tryCompare(shell.sectionPopup, "visible", false)
            mouseClick(findChild(shell, "sidebarRestoreButton"), 20, 20)
            tryCompare(shell, "collapsed", false)
            compare(shell.width, 248)
            compare(shell.panel.sectionFilter, "")
        }
        function test_navigation_uses_the_same_sidebar_action_and_closes_the_panel() {
            collapse()
            mouseMove(badge("places"), 20, 20)
            tryCompare(shell.sectionPopup, "opened", true)
            const home = findChild(shell.panel, "sidebarSystemItem_homefolder")
            verify(home && home.visible)
            mouseClick(home, home.width / 2, home.height / 2)
            compare(navigations.count, 1)
            compare(navigations.signalArguments[0][0], shell.panel.homePath)
            tryCompare(shell.sectionPopup, "visible", false)
        }
        function test_places_click_goes_home_and_hover_still_opens_its_section() {
            collapse()
            const places = badge("places")
            mouseMove(places, places.width / 2, places.height / 2)
            tryCompare(shell.sectionPopup, "opened", true)
            verify(!findChild(shell.panel, "sidebarSystemItem_dashboard").visible)
            mouseClick(places, places.width / 2, places.height / 2)
            compare(navigations.count, 1)
            compare(navigations.signalArguments[0][0], shell.panel.homePath)
            tryCompare(shell.sectionPopup, "visible", false)
            wait(220)
            verify(!shell.sectionPopup.visible)
            mouseMove(workspace, workspace.width - 20, workspace.height - 20)
            mouseMove(places, places.width / 2, places.height / 2)
            tryCompare(shell.sectionPopup, "opened", true)
        }
        function test_discover_is_a_separate_top_action_only_in_collapsed_mode() {
            const dashboard = findChild(shell.panel, "sidebarSystemItem_dashboard")
            verify(dashboard.visible)
            collapse()
            const discover = badge("dashboard")
            verify(discover.visible)
            verify(discover.y + discover.height < findChild(shell, "sidebarBadges").y)
            verify(!dashboard.visible)
            mouseMove(discover, discover.width / 2, discover.height / 2)
            wait(220)
            verify(!shell.sectionPopup.visible)
            mouseClick(discover, discover.width / 2, discover.height / 2)
            compare(discoveries.count, 1)
            compare(navigations.count, 0)
            AppTheme.setSidebarAutoHide(false)
            tryVerify(function() { return dashboard.visible })
            verify(!discover.visible)
        }
        function test_pin_and_hide_are_the_same_button_in_both_layouts_and_languages() {
            const toggle = findChild(shell, "sidebarRestoreButton")
            const savedLanguage = language.language
            try {
                language.language = "en"
                compare(toggle.Accessible.name, "Hide")
                mouseClick(toggle, toggle.width / 2, toggle.height / 2)
                tryCompare(shell, "collapsed", true)
                wait(50)
                compare(toggle.Accessible.name, "Pin")
                language.language = "tr"
                compare(toggle.Accessible.name, "Sabitle")
                mouseClick(toggle, toggle.width / 2, toggle.height / 2)
                tryCompare(shell, "collapsed", false)
                wait(50)
                compare(toggle.Accessible.name, "Gizle")
                compare(toggle, findChild(shell, "sidebarRestoreButton"))
                language.language = "en"
                compare(toggle.Accessible.name, "Hide")
            } finally { language.language = savedLanguage }
        }
        function test_disabling_while_open_reuses_the_panel_and_restores_all_sections() {
            collapse()
            const panel = shell.panel
            mouseMove(badge("network"), 20, 20)
            tryCompare(shell.sectionPopup, "opened", true)
            AppTheme.setSidebarAutoHide(false)
            tryCompare(shell.sectionPopup, "visible", false)
            compare(shell.panel, panel)
            compare(panel.parent, shell)
            verify(panel.visible)
            compare(panel.sectionFilter, "")
        }
        function test_qt_theme_uses_the_existing_popup_surface() {
            const savedQt = AppTheme.useQtTheme
            try {
                AppTheme.setQtTheme(true)
                collapse()
                mouseMove(badge("favorites"), 20, 20)
                tryCompare(shell.sectionPopup, "opened", true)
                verify(shell.sectionPopup.background.nativeTheme)
                verify(!shell.sectionPopup.modal && !shell.sectionPopup.dim)
                verify(shell.panel.drawerPresentation)
            } finally { AppTheme.setQtTheme(savedQt) }
        }
        function test_context_menu_keeps_the_hover_panel_open() {
            collapse()
            mouseMove(badge("places"), 20, 20)
            tryCompare(shell.sectionPopup, "opened", true)
            const home = findChild(shell.panel, "sidebarSystemItem_homefolder")
            mouseClick(home, home.width / 2, home.height / 2, Qt.RightButton)
            tryCompare(shell.panel, "interactionActive", true)
            mouseMove(workspace, workspace.width - 20, workspace.height - 20)
            wait(500)
            verify(shell.sectionPopup.visible)
            keyClick(Qt.Key_Escape)
            tryCompare(shell.panel, "interactionActive", false)
        }
    }
}
