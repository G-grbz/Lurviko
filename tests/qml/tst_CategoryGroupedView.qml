import QtQuick
import QtQuick.Controls
import QtTest
import GFile.App

Item {
    width: 1020
    height: 700
    QtObject { id: language; property string language: "tr"; function t(key) { return key } }
    CategoryGroupedView {
        id: grouped
        anchors.fill: parent
        files: []
        lang: language
        cardSize: 168
        selectionRevision: 0
    }
    MediaGalleryView {
        id: media
        visible: false
        files: []
        lang: language
        categoryKey: "images"
        categoryIcon: "image.svg"
        gallerySize: 168
        contentIndexModel: null
    }
    SignalSpy { id: pressed; target: grouped; signalName: "itemPressed" }
    SignalSpy { id: activated; target: grouped; signalName: "itemActivated" }
    SignalSpy { id: context; target: grouped; signalName: "contextRequested" }
    SignalSpy { id: rename; target: grouped; signalName: "renameCommit" }
    TestCase {
        name: "CategoryGroupedView"
        when: windowShown
        property var fixture: []
        function initTestCase() {
            const files = []
            for (let i = 0; i < 2836; ++i) {
                files.push({ modelIndex: i, name: "Music " + i + ".mp3",
                             itemUrl: "file:///tmp/music-" + i + ".mp3",
                             localPath: "/tmp/music-" + i + ".mp3", suffix: "mp3",
                             groupDateMs: 1700000000000 - Math.floor(i / 100) * 86400000,
                             iconSource: "", thumbnailSource: "", metaText: "4 MiB" })
            }
            fixture = files
        }
        function init() {
            grouped.inlineRenameUrl = ""
            grouped.cardSize = 168
            grouped.dateAscending = false
            grouped.files = fixture
            AppTheme.setWheelScrollStep(250)
            tryVerify(function() { return grouped.rows.length > 500 })
            tryCompare(grouped, "layoutColumns", 6)
            const view = findChild(grouped, "categoryGroupedList")
            view.positionViewAtBeginning()
            wait(40)
            pressed.clear(); activated.clear(); context.clear(); rename.clear()
        }
        function countCards(item) {
            let count = item.objectName === "categoryGroupedCard" ? 1 : 0
            const children = item.children || []
            for (let i = 0; i < children.length; ++i) count += countCards(children[i])
            return count
        }
        function test_large_category_only_creates_viewport_cards_and_reopens() {
            const view = findChild(grouped, "categoryGroupedList")
            verify(countCards(grouped) > 10)
            verify(countCards(grouped) < 150, "Offscreen files instantiated their own cards")
            grouped.positionModelIndex(2835, ListView.Contain)
            wait(80)
            verify(view.contentY > 5000)
            verify(view.itemAtIndex(grouped.modelRows[2835]) !== null)
            verify(countCards(grouped) < 200)
            grouped.files = []
            tryCompare(grouped, "rows", [])
            grouped.files = fixture
            tryVerify(function() { return grouped.rows.length > 500 })
            wait(40)
            verify(countCards(grouped) < 150)
        }
        function test_all_files_keep_group_order_and_original_model_indices() {
            const found = ({})
            let count = 0
            for (let r = 0; r < grouped.rows.length; ++r) {
                const row = grouped.rows[r]
                if (row.kind !== "cards") continue
                for (let i = 0; i < row.items.length; ++i) {
                    const index = row.items[i].modelIndex
                    verify(!found[index])
                    found[index] = true
                    compare(grouped.modelRows[index], r)
                    ++count
                }
            }
            compare(count, 2836)
            const latest = grouped.groups[0].dateMs
            grouped.dateAscending = true
            tryVerify(function() { return grouped.groups[0].dateMs < latest })
        }
        function test_wheel_matches_media_and_accumulates_bursts() {
            const event = { angleDelta: { y: -120 }, pixelDelta: { y: 0 }, phase: Qt.NoScrollPhase }
            grouped.wheelBurstLastMs = 0; media.wheelBurstLastMs = 0
            compare(grouped.normalizedWheelDelta(event), media.normalizedWheelDelta(event))
            compare(grouped.normalizedWheelDelta(event), media.normalizedWheelDelta(event))
            AppTheme.setWheelScrollStep(100)
            grouped.wheelBurstLastMs = 0; media.wheelBurstLastMs = 0
            const pixelMouse = { angleDelta: { y: 0 }, pixelDelta: { y: -30 }, phase: Qt.NoScrollPhase }
            compare(grouped.normalizedWheelDelta(pixelMouse), media.normalizedWheelDelta(pixelMouse))
            grouped.wheelBurstLastMs = 0
            const view = findChild(grouped, "categoryGroupedList")
            const start = view.contentY
            grouped.scrollFromWheel(event)
            grouped.scrollFromWheel(event)
            const animation = findChild(grouped, "categoryGroupedWheelAnimation")
            verify(animation.to - start > 200, "Fast wheel events lost the previous destination")
            tryVerify(function() { return !animation.running })
            verify(view.contentY - start > 200)
        }
        function test_resize_preserves_visible_file() {
            const view = findChild(grouped, "categoryGroupedList")
            grouped.positionModelIndex(1100, ListView.Beginning)
            wait(80)
            grouped.prepareForResize()
            const url = grouped.resizeAnchorUrl
            verify(url.length > 0)
            const offset = grouped.resizeAnchorOffset
            grouped.cardSize = 250
            tryCompare(grouped, "layoutColumns", 4)
            wait(40)
            const index = Number(url.match(/music-(\d+)/)[1])
            const row = view.itemAtIndex(grouped.modelRows[index])
            verify(row !== null)
            verify(Math.abs(view.contentY - row.y - offset) < 2)
            verify(view.contentY > 500)
        }
        function test_press_context_activation_and_inline_rename() {
            const view = findChild(grouped, "categoryGroupedList")
            const card = findChild(view.itemAtIndex(1), "categoryGroupedCard")
            verify(card !== null)
            mouseClick(card, card.width / 2, 30)
            compare(pressed.count, 1)
            compare(pressed.signalArguments[0][0], card.modelData.modelIndex)
            mouseClick(card, card.width / 2, 30, Qt.RightButton)
            compare(context.count, 1)
            mouseDoubleClickSequence(card, card.width / 2, 30)
            compare(activated.count, 1)
            grouped.inlineRenameUrl = card.modelData.itemUrl
            const field = findChild(card, "categoryGroupedRenameField")
            tryCompare(field, "activeFocus", true)
            compare(field.text, card.modelData.name)
            field.text = "Renamed.mp3"
            keyClick(Qt.Key_Return)
            tryCompare(rename, "count", 1)
            compare(rename.signalArguments[0][0], card.modelData.itemUrl)
            compare(rename.signalArguments[0][1], "Renamed.mp3")
        }
    }
}
