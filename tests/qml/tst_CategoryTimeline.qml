import QtQuick
import QtQuick.Controls
import QtTest
import Lurviko.App

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
    SignalSpy { id: basisSpy; target: grouped; signalName: "timelineBasisRequested" }

    TestCase {
        name: "CategoryTimeline"
        when: windowShown
        readonly property double day: 86400000

        // perDay files on each of the given days before now.
        function makeFiles(daysAgo, perDay) {
            const files = []
            const base = Date.now() - 60000
            let index = 0
            for (const offset of daysAgo) {
                for (let i = 0; i < perDay; ++i, ++index) {
                    files.push({ modelIndex: index, name: "file-" + index + ".pdf",
                                 itemUrl: "file:///tmp/file-" + index + ".pdf",
                                 localPath: "/tmp/file-" + index + ".pdf", suffix: "pdf",
                                 size: 1024 * 1024,
                                 groupDateMs: base - offset * day,
                                 iconSource: "", thumbnailSource: "", metaText: "1 MiB" })
                }
            }
            return files
        }

        function timeline() { return findChild(grouped, "categoryTimeline") }
        function entryY(line, index) { return line.listTop + index * line.itemHeight + line.itemHeight / 2 }

        function init() {
            AppTheme.setCategoryTimelineVisible(true)
            AppTheme.setCategoryTimelineCompact(false)
            grouped.dateAscending = false
            basisSpy.clear()
            // Start each case from an empty timeline, not the previous one.
            grouped.files = []
            tryVerify(function() { return timeline().periods.length === 0 })
        }

        function test_relative_periods_exist_only_for_files() {
            const days = [0, 40, 400, 800]
            grouped.files = makeFiles(days, 12)
            const line = timeline()
            const now = new Date()
            const expected = []
            for (const d of days) {
                const id = line.periodOf(Date.now() - 60000 - d * day, now)[1]
                if (expected.indexOf(id) < 0) expected.push(id)
            }
            tryVerify(function() { return line.periods.length === expected.length })
            verify(line.visible)
            let total = 0
            for (let i = 0; i < line.periods.length; ++i) {
                compare(line.periods[i].id, expected[i])
                verify(line.periods[i].count > 0)
                compare(line.periods[i].bytes, line.periods[i].count * 1024 * 1024)
                total += line.periods[i].count
            }
            compare(total, 48)
            compare(line.periods[0].title, "Bugün")
        }

        function test_old_years_collapse_into_older() {
            const days = []
            for (let y = 0; y < 30; ++y) days.push(400 + y * 366)
            grouped.files = makeFiles(days, 2)
            const line = timeline()
            tryVerify(function() {
                let count = 0
                for (const period of line.periods) count += period.count
                return count === 60
            })
            verify(line.periods.length <= line.capacity)
            const last = line.periods[line.periods.length - 1]
            compare(last.id, "older")
            compare(last.title, "Daha Eski")
        }

        function test_click_scrolls_without_filtering() {
            const files = makeFiles([0, 40, 400, 800, 1200, 1600], 60)
            grouped.files = files
            const line = timeline()
            tryVerify(function() { return line.periods.length >= 5 })
            const view = findChild(grouped, "categoryGroupedList")
            view.positionViewAtBeginning()
            wait(30)
            const index = line.periods.length - 2
            const target = line.periods[index]
            mouseClick(line, line.width / 2, entryY(line, index))
            tryVerify(function() { return view.contentY > 100 })
            const top = view.indexAt(4, view.contentY + 4)
            compare(grouped.rows[top].kind, "header")
            compare(grouped.rows[top].key, target.firstKey)
            tryCompare(line, "currentIndex", index)
            let cards = 0
            for (const row of grouped.rows) if (row.kind === "cards") cards += row.items.length
            compare(cards, files.length)
        }

        function test_hover_card_reports_range_count_and_size() {
            grouped.files = makeFiles([0, 40, 400], 7)
            const line = timeline()
            tryVerify(function() { return line.periods.length === 3 })
            mouseMove(grouped, 20, 20)
            wait(20)
            mouseMove(line, line.width / 2, entryY(line, 1))
            wait(20)
            mouseMove(line, line.width / 2, entryY(line, 1) + 1)
            const tip = findChild(line, "categoryTimelineTooltip")
            tryVerify(function() { return tip.visible })
            compare(tip.period.count, 7)
            verify(line.rangeText(tip.period).length > 0)
            mouseMove(grouped, 20, 20)
            tryVerify(function() { return !tip.visible })
        }

        function test_panel_sits_beside_cards_and_scrollbar() {
            grouped.files = makeFiles([0, 40, 400], 30)
            const line = timeline()
            tryVerify(function() { return line.periods.length === 3 })
            const view = findChild(grouped, "categoryGroupedList")
            tryCompare(view.anchors, "rightMargin", line.width + 20)
            const viewRight = view.mapToItem(grouped, view.width, 0).x
            const lineLeft = line.mapToItem(grouped, 0, 0).x
            verify(viewRight + 12 <= lineLeft + 0.5)
        }

        function test_dots_only_when_chosen_or_narrow() {
            grouped.files = makeFiles([0, 40, 400], 3)
            const line = timeline()
            tryVerify(function() { return line.periods.length === 3 })
            verify(!line.dotsOnly)
            const full = line.width
            AppTheme.setCategoryTimelineCompact(true)
            tryVerify(function() { return line.dotsOnly && line.width < full / 2 })
            compare(AppTheme.browserInteractionSettings.categoryTimelineCompact, true)
            AppTheme.setCategoryTimelineCompact(false)
            tryVerify(function() { return !line.dotsOnly })
            // A narrow view switches by itself.
            line.compactBelow = grouped.width + 1
            tryVerify(function() { return line.dotsOnly })
            line.compactBelow = 760
            AppTheme.setCategoryTimelineVisible(false)
            verify(!line.visible)
        }

        function test_basis_request_reaches_host() {
            grouped.files = makeFiles([0, 400], 3)
            const line = timeline()
            tryVerify(function() { return line.periods.length === 2 })
            line.basisRequested("added")
            compare(basisSpy.count, 1)
            compare(basisSpy.signalArguments[0][0], "added")
        }

        function cleanupTestCase() {
            AppTheme.setCategoryTimelineVisible(true)
            AppTheme.setCategoryTimelineCompact(false)
        }
    }
}
