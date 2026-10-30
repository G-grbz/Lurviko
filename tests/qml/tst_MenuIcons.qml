import QtQuick
import QtQuick.Controls
import QtTest
import Lurviko.App

Item {
    width: 640; height: 480
    ApplicationWindow {
        id: window
        width: 640; height: 480; visible: true
        GMenu {
            id: menu
            x: 20; y: 20
            GMenuItem { id: bundled; text: "Copy"; icon.source: AppTheme.icon("edit-copy.svg") }
            GMenuItem { id: application; text: "Application"; icon.name: "application-x-executable" }
            GMenuItem { id: colored; text: "Colored artwork"; icon.source: Qt.resolvedUrl("fixtures/landscape.svg") }
            GMenuItem { id: checkedAction; text: "Checked option"; checkable: true; checked: true; icon.name: "folder" }
            GMenuItem { id: blank; text: "No icon" }
            GMenu { id: childMenu; title: "Actions"; icon.source: AppTheme.icon("archive.svg")
                GMenuItem { text: "Run" }
            }
        }
        TestCase {
            name: "MenuIcons"
            when: windowShown
            property bool savedSystemIcons
            property bool savedQtTheme
            property bool savedDark
            function init() {
                savedSystemIcons = AppTheme.useSystemIcons
                savedQtTheme = AppTheme.useQtTheme
                savedDark = AppTheme.settings.darkMode
                menu.open()
                tryCompare(menu, "opened", true)
            }
            function cleanup() {
                menu.dismiss()
                tryCompare(menu, "visible", false)
                application.enabled = true
                application.icon.source = ""
                AppTheme.setSystemIcons(savedSystemIcons)
                AppTheme.setQtTheme(savedQtTheme)
                AppTheme.settings.darkMode = savedDark
            }
            function actionIcon(entry) { return findChild(entry, "menuActionIcon") }
            function test_bundled_actions_and_real_application_artwork_data() {
                return [{tag: "bundled-light", system: false, qt: false, dark: false},
                    {tag: "bundled-dark", system: false, qt: false, dark: true},
                    {tag: "system-icons", system: true, qt: false, dark: false},
                    {tag: "qt-theme", system: true, qt: true, dark: false}]
            }
            function test_bundled_actions_and_real_application_artwork(data) {
                AppTheme.setSystemIcons(data.system)
                AppTheme.settings.darkMode = data.dark
                AppTheme.setQtTheme(data.qt)
                const glyph = actionIcon(bundled)
                const app = actionIcon(application)
                const artwork = actionIcon(colored)
                compare(decodeURIComponent(String(glyph.source)), decodeURIComponent(AppTheme.icon("edit-copy.svg")))
                compare(String(app.source), AppTheme.systemIcon("application-x-executable"))
                tryCompare(glyph, "status", Image.Ready)
                tryCompare(app, "status", Image.Ready)
                tryCompare(artwork, "status", Image.Ready)
                wait(40)
                // Popup.Item owns a separate content transform. Sample the real
                // window at the mapped icon position, including its letterboxing.
                const pixels = grabImage(window.contentItem)
                // Original application artwork must not be painted a single text color.
                const point = artwork.mapToItem(window.contentItem, artwork.width / 2, artwork.height / 2)
                const x = Math.floor(point.x * pixels.width / window.width)
                const y = Math.floor(point.y * pixels.height / window.height)
                pixels.save("/tmp/lurviko-menu-icons-test.png")
                verify(pixels.blue(x, y) > pixels.red(x, y) && pixels.blue(x, y) > pixels.green(x, y),
                       "Original blue artwork pixel: " + pixels.pixel(x, y) + ", size " + pixels.width + "x" + pixels.height)
                verify(glyph.width <= 22 && glyph.height <= bundled.height)
                compare(findChild(bundled, "menuCheckIndicator").visible, false)
            }
            function test_submenu_icon_is_forwarded_to_its_parent_entry() {
                const entry = menu.itemAt(5)
                compare(entry.subMenu, childMenu)
                const glyph = actionIcon(entry)
                compare(String(glyph.source), AppTheme.icon("archive.svg"))
                tryCompare(glyph, "status", Image.Ready)
            }
            function test_checkmarks_and_blank_slots_keep_alignment() {
                verify(!actionIcon(checkedAction).visible)
                compare(String(actionIcon(checkedAction).source), "")
                verify(findChild(checkedAction, "menuCheckIndicator").visible)
                verify(!actionIcon(blank).visible)
                compare(String(actionIcon(blank).source), "")
                const a = actionIcon(bundled).mapToItem(bundled, 0, 0)
                const b = actionIcon(application).mapToItem(application, 0, 0)
                compare(a.x, b.x)
                compare(bundled.height, application.height)
            }
            function test_disabled_icons_and_explicit_file_sources() {
                const glyph = actionIcon(application)
                application.enabled = false
                verify(glyph.opacity < 1)
                application.enabled = true
                compare(glyph.opacity, 1)
                application.icon.source = Qt.resolvedUrl("fixtures/landscape.svg")
                compare(String(glyph.source), String(application.icon.source))
                tryCompare(glyph, "status", Image.Ready)
            }
        }
    }
}
