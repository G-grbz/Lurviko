import QtQuick
import QtQuick.Controls
import QtTest
import Lurviko.App

Item {
    width: 1020
    height: 700
    QtObject {
        id: language
        property string language: "tr"
        function t(key) { return key }
    }
    MediaGalleryView {
        id: gallery
        anchors.fill: parent
        files: []
        lang: language
        categoryKey: "images"
        categoryIcon: "image.svg"
        gallerySize: 230
        contentIndexModel: null
    }
    TestCase {
        id: testCase
        name: "MediaGalleryResize"
        when: windowShown
        property bool guardPosition: false
        property real minimumObservedY: Infinity
        property int observedSamples: 0
        // Sample every event-loop turn as well as the final state. The old
        // 16 ms restoration painted the beginning of the page in between.
        Timer {
            interval: 1
            running: testCase.guardPosition
            repeat: true
            onTriggered: {
                const view = testCase.findChild(gallery, "categoryGalleryList")
                testCase.minimumObservedY = Math.min(testCase.minimumObservedY, view.contentY)
                testCase.observedSamples += 1
            }
        }

        function initTestCase() {
            const entries = []
            for (let i = 0; i < 300; ++i) {
                entries.push({ path: "/tmp/gallery-test-" + i + ".jpg",
                               name: "image-" + i, url: "file:///tmp/gallery-test-" + i + ".jpg",
                               modifiedMs: 1700000000000 - Math.floor(i / 60) * 86400000,
                               size: 12000 })
            }
            gallery.files = entries
            tryCompare(gallery, "layoutColumns", 4)
            tryVerify(function() { return gallery.rows.length > 70 })
        }

        function test_click_slider_without_moving_releases_anchor() {
            const view = findChild(gallery, "categoryGalleryList")
            view.positionViewAtIndex(30, ListView.Beginning)
            wait(100)
            gallery.beginResizeGesture()
            gallery.endResizeGesture()
            wait(50)
            verify(!gallery.resizeAnchorPending)
            compare(gallery.resizeAnchorKind, "")
        }

        function test_full_rows_fill_available_width() {
            const used = gallery.layoutColumns * gallery.rowCardWidth
                         + (gallery.layoutColumns - 1) * gallery.cardGap
            verify(Math.abs(used - gallery.galleryContentWidth) < 1)
        }

        function test_preserve_file_across_column_changes() {
            const view = findChild(gallery, "categoryGalleryList")
            verify(view !== null)
            view.positionViewAtIndex(38, ListView.Beginning)
            wait(100)
            verify(view.contentY > 500)
            const sizes = [300, 180, 230, 300, 230, 180]
            const columns = [3, 5, 4, 3, 4, 5]
            for (let i = 0; i < sizes.length; ++i) {
                gallery.prepareForResize()
                const path = gallery.resizeAnchorPath
                const key = gallery.resizeAnchorKey
                const kind = gallery.resizeAnchorKind
                const offset = gallery.resizeAnchorOffset
                verify(kind.length > 0, "No visible anchor before resizing")
                minimumObservedY = Infinity
                observedSamples = 0
                guardPosition = true
                gallery.gallerySize = sizes[i]
                tryCompare(gallery, "layoutColumns", columns[i])
                wait(120)
                guardPosition = false
                verify(observedSamples > 0, "No resize samples were observed")
                verify(minimumObservedY > 500, "The viewport jumped to the top before restoration")
                verify(view.contentY > 500, "Resize jumped to the top")
                let rowIndex = -1
                for (let r = 0; r < gallery.rows.length; ++r) {
                    const row = gallery.rows[r]
                    if (kind === "header" && row.kind === kind && row.key === key)
                        rowIndex = r
                    if (kind === "cards" && row.kind === "cards") {
                        for (let c = 0; c < row.items.length; ++c)
                            if (row.items[c].path === path) rowIndex = r
                    }
                }
                verify(rowIndex >= 0)
                const item = view.itemAtIndex(rowIndex)
                verify(item !== null, "The anchored file was not kept visible")
                verify(Math.abs((view.contentY - item.y) - offset) < 2,
                       "The file moved relative to the viewport after a column change")
            }
        }

        function test_continuous_drag_across_column_boundaries() {
            const view = findChild(gallery, "categoryGalleryList")
            view.positionViewAtIndex(30, ListView.Beginning)
            wait(100)
            gallery.beginResizeGesture()
            const path = gallery.resizeAnchorPath
            verify(path.length > 0)
            minimumObservedY = Infinity
            observedSamples = 0
            guardPosition = true
            const sizes = [250, 255, 260, 270, 300, 285, 230, 200, 180]
            for (let i = 0; i < sizes.length; ++i) {
                gallery.prepareForResize()
                gallery.gallerySize = sizes[i]
                wait(2)
            }
            wait(120)
            gallery.endResizeGesture()
            guardPosition = false
            verify(observedSamples > 0, "No dragging samples were observed")
            verify(minimumObservedY > 500, "Rapid dragging briefly jumped to the beginning of the page")
            verify(view.contentY > 500)
            let rowIndex = -1
            for (let r = 0; r < gallery.rows.length; ++r) {
                const row = gallery.rows[r]
                if (row.kind === "cards") {
                    for (let c = 0; c < row.items.length; ++c)
                        if (row.items[c].path === path) rowIndex = r
                }
            }
            verify(rowIndex >= 0)
            const item = view.itemAtIndex(rowIndex)
            verify(item !== null)
            verify(Math.abs(view.contentY - item.y) < 2,
                   "Rapid dragging lost the original visible file")
        }
    }
}
