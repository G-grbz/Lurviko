import QtQuick
import QtTest
import Lurviko.App

Item {
    width: 200; height: 200
    Image {
        id: probe
        width: 64; height: 64
        sourceSize: Qt.size(64, 64)
        fillMode: Image.PreserveAspectFit
        asynchronous: true
    }
    CrispIcon {
        id: folder
        width: 128; height: 128
        retainWhileLoading: false
    }
    Component {
        id: unsettledIcon
        CrispIcon { width: 0; height: 0; source: AppTheme.icon("drive.svg") }
    }
    TestCase {
        name: "BundledIconResources"
        when: windowShown
        function test_unsettled_folder_provider_keeps_its_cheap_placeholder() {
            folder.source = AppTheme.icon("folder.svg")
            verify(String(folder.source).startsWith("image://folderart/"))
            folder.width = 0
            folder.height = 0
            compare(folder.sourceSize.width, 1)
            compare(folder.sourceSize.height, 1)
            compare(folder.status, Image.Ready)
            folder.width = 128
            folder.height = 128
            compare(folder.status, Image.Ready)
        }
        function test_filtered_icon_survives_unsettled_layout_geometry() {
            const icon = createTemporaryObject(unsettledIcon, parent)
            verify(icon !== null)
            compare(icon.width, 0)
            verify(icon.sourceSize.width >= 16 && icon.sourceSize.height >= 16,
                   "Unsettled geometry must not decode filtered SVGs at one pixel")
            tryCompare(icon, "status", Image.Ready)
            icon.width = 1
            icon.height = 1
            tryCompare(icon, "status", Image.Ready)
            verify(icon.sourceSize.width >= 16 && icon.sourceSize.height >= 16)
            icon.width = 13
            icon.height = 13
            tryCompare(icon, "status", Image.Ready)
            compare(icon.width, 13, "The visible icon keeps the requested layout size")
            icon.width = 24
            icon.height = 24
            tryCompare(icon, "status", Image.Ready)
            compare(icon.sourceSize.width, 24)
            compare(icon.sourceSize.height, 24)
        }
        function test_folder_art_is_ready_without_an_async_gap_data() {
            return bundledIconNames.filter(function(name) {
                return /^(?:folder|(?:blue|green|orange|pink|purple|red|turquoise|yellow)-(?:folder|documents|download|music|picture|video))\.svg$/.test(name)
            }).map(function(name) { return {tag: name, name: name} })
        }
        function test_folder_art_is_ready_without_an_async_gap(data) {
            folder.source = AppTheme.icon(data.name)
            compare(folder.asynchronous, false)
            compare(folder.status, Image.Ready)
            folder.width = 143
            compare(folder.status, Image.Ready)
            folder.width = 128
            compare(folder.status, Image.Ready)
            // Navigation/recycled delegates keep the correct cache entry.
            folder.source = ""
            folder.source = AppTheme.icon(data.name)
            compare(folder.status, Image.Ready)
        }
        function test_all_grouped_assets_render_at_public_resource_names_data() {
            verify(bundledIconNames.length > 0)
            return bundledIconNames.map(function(name) { return {tag: name, name: name} })
        }
        function test_all_grouped_assets_render_at_public_resource_names(data) {
            // Native icons, logos and some viewers address the resources directly.
            probe.source = "qrc:/qt/qml/Lurviko/App/assets/icons/" + data.name
            tryCompare(probe, "status", Image.Ready)
            verify(probe.sourceSize.width > 0 && probe.sourceSize.height > 0)
            // Theme-aware glyphs use the asynchronous bundled icon provider;
            // colored artwork and PNGs use their original resource colors.
            probe.source = AppTheme.icon(data.name)
            tryCompare(probe, "status", Image.Ready)
        }
    }
}
