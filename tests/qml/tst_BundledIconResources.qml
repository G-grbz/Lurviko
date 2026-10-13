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
    TestCase {
        name: "BundledIconResources"
        when: windowShown
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
