import QtQuick
import QtTest
import Lurviko.Backend

Item {
    DirectoryModel { id: model }

    TestCase {
        name: "CategoryEntries"

        function rowByName(name) {
            for (let row = 0; row < model.count; ++row)
                if (model.itemAt(row).name === name) return row
            return -1
        }

        function test_entries_match_item_at_and_filter() {
            const fixtures = String(Qt.resolvedUrl("fixtures/"))
            if (fixtures.indexOf("file:") !== 0)
                skip("Fixtures are not on the local filesystem")
            model.location = fixtures
            tryVerify(function() { return !model.loading && model.count === 2 }, 5000)

            const all = model.categoryEntries("")
            compare(all.length, model.count)
            for (const entry of all) {
                const item = model.itemAt(entry.modelIndex)
                compare(entry.name, item.name)
                compare(entry.itemUrl, item.itemUrl)
                compare(entry.localPath, item.localPath)
                compare(entry.isDir, item.isDir)
                compare(entry.size, item.size)
                compare(entry.suffix, "svg")
                compare(entry.modifiedMs, new Date(item.modified).getTime())
                verify(entry.createdMs > 0)
                // Same preview revision as the model role, so preview URLs and
                // their caches match the regular folder views.
                compare(entry.previewRevision, String(item.previewRevision))
            }

            // Case-insensitive name filter, as the category search box expects.
            const filtered = model.categoryEntries("LANDSCAPE")
            compare(filtered.length, 1)
            compare(filtered[0].name, "landscape.svg")
            compare(filtered[0].modelIndex, rowByName("landscape.svg"))
            compare(model.categoryEntries("no-such-file").length, 0)
        }
    }
}
