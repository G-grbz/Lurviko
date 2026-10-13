import QtQuick
import QtQuick.Controls
import QtTest
import Lurviko.App
import Lurviko.Backend

Item {
    width: 600; height: 400
    ContentCategoryTile {
        id: tile
        width: 400
        categoryKey: "images"
        iconName: String(Qt.resolvedUrl("fixtures/landscape.svg"))
    }
    QuickAccessModel { id: shortcuts }
    QuickAccessTile {
        id: recentTile
        y: 200; width: 400; height: 84
        fixedShortcut: true
        title: "Last Location"
    }
    Component { id: savedShortcuts; QuickAccessModel {} }
    Image {
        id: artwork
        width: 64; height: 64
        source: AppTheme.discoveryIcon(tile.iconName, "image.svg")
    }
    TestCase {
        name: "CustomIcons"
        when: windowShown
        property bool previousMode: false
        function init() { previousMode = AppTheme.useSystemIcons }
        function cleanup() { AppTheme.setSystemIcons(previousMode) }

        function test_theme_selections_stay_mode_specific() {
            compare(AppTheme.iconSelection("folder-pictures"), "system:folder-pictures")
            AppTheme.setSystemIcons(true)
            compare(AppTheme.discoveryIcon("system:folder-pictures", "image.svg"), AppTheme.systemIcon("folder-pictures"))
            AppTheme.setSystemIcons(false)
            compare(AppTheme.discoveryIcon("system:folder-pictures", "image.svg"), AppTheme.icon("image.svg"))
        }

        function test_missing_last_location_artwork_uses_bundled_last_location_fallback() {
            const visual = findChild(recentTile, "quickAccessIconVisual")
            const fallback = findChild(recentTile, "quickAccessIconFallback")
            for (const mode of [false, true]) {
                AppTheme.setSystemIcons(mode)
                recentTile.iconSource = AppTheme.icon("folder.svg")
                tryCompare(visual, "status", Image.Ready)
                ignoreWarning(/.*Cannot open: file:.*missing-last-location-icon.svg.*/)
                recentTile.iconSource = "file:///tmp/missing-last-location-icon.svg"
                tryCompare(visual, "status", Image.Error)
                tryCompare(fallback, "status", Image.Ready)
                verify(fallback.visible)
                verify(!visual.visible)
                verify(fallback.width > 0 && fallback.height > 0)
                compare(String(fallback.source), AppTheme.icon("last-location.svg"))
                recentTile.iconSource = AppTheme.icon("folder.svg")
                tryCompare(visual, "status", Image.Ready)
                verify(visual.visible)
                verify(!fallback.visible)
            }
        }

        function test_external_paths_are_encoded_without_losing_characters() {
            const path = "/tmp/ikon Türkçe #? 100%.svg"
            const source = "file:///tmp/ikon%20T%C3%BCrk%C3%A7e%20%23%3F%20100%25.svg"
            compare(AppTheme.iconSelection(path), source)
            compare(AppTheme.iconSelection("system:" + path), source)
            compare(AppTheme.iconSelection(source), source)
            compare(AppTheme.iconSelection(""), "")
            for (const mode of [true, false]) {
                AppTheme.setSystemIcons(mode)
                compare(AppTheme.discoveryIcon("system:" + path, "image.svg"), source)
                compare(AppTheme.customIcon(path), source)
                compare(AppTheme.systemIcon(path), source)
                compare(AppTheme.icon(path), source)
            }
        }

        function test_custom_icons_reject_nonlocal_or_ambiguous_file_addresses() {
            const invalid = ["file://remote/tmp/icon.svg", "file://user@localhost/tmp/icon.svg",
                "file://localhost:22/tmp/icon.svg", "file:relative.svg", "file:///tmp/icon.svg?secret",
                "file:///tmp/icon.svg#fragment", "file:///tmp/icon%00.svg", "file:///tmp/icon%zz.svg",
                "https://example.com/icon.svg", "/tmp/icon\u0000.svg"]
            for (const candidate of invalid) {
                compare(AppTheme.localIconSource(candidate), "")
                compare(AppTheme.localIconSource("system:" + candidate), "")
                compare(AppTheme.iconSelection(candidate), "")
                for (const mode of [false, true]) {
                    AppTheme.setSystemIcons(mode)
                    verify(String(AppTheme.discoveryIcon(candidate, "image.svg")).indexOf(candidate) === -1)
                    verify(String(AppTheme.customIcon(candidate)).indexOf(candidate) === -1)
                }
            }
            compare(AppTheme.localIconSource("file://localhost/tmp/icon.svg"), "file://localhost/tmp/icon.svg")
        }

        function test_file_artwork_survives_mode_switch_and_settings_reload() {
            const source = String(Qt.resolvedUrl("fixtures/landscape.svg"))
            verify(shortcuts.updateShortcut(0, "External icon", "", AppTheme.iconSelection(source)))
            const restored = savedShortcuts.createObject(null)
            verify(restored !== null)
            const restoredIcon = restored.data(restored.index(0, 0), 259) // QuickAccessModel::IconRole
            compare(restoredIcon, source)
            tile.iconName = restoredIcon
            restored.destroy()
            for (const mode of [true, false, true]) {
                AppTheme.setSystemIcons(mode)
                compare(AppTheme.discoveryIcon(tile.iconName, "image.svg"), source)
                tryCompare(artwork, "status", Image.Ready)
                // Inspect the real Discover card's image, not just the resolver.
                const visual = findChild(tile, "categoryIconVisual")
                verify(visual !== null)
                compare(String(visual.source), source)
                tryCompare(visual, "status", Image.Ready)
            }
        }
    }
}
