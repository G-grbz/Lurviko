import QtQuick
import QtQuick.Controls
import Lurviko.App

// Timeline panel for category pages, beside the view (it never covers
// cards). Files are grouped into relative periods - Today, Yesterday, This
// week, This month, Last month, Last 3 months, This year, then each older
// year - and a period only appears when it holds files. Each entry shows
// its name and file count; hovering adds the exact date range and total
// size, clicking scrolls the view to the period (nothing is filtered).
//
// When the view is narrow, or "Dots only" is chosen, only the dots remain
// and the name moves into the hover card.
Item {
    id: timeline
    objectName: "categoryTimeline"

    // The view's date groups in display order: [{ key, dateMs, count, bytes }].
    property var groups: []
    // Group shown at the top of the viewport.
    property string currentKey: ""
    property var lang: null
    // "modified" | "created" | "added"
    property string basis: "modified"
    // Width of the hosting view; below compactBelow only dots are shown.
    property real hostWidth: 1200
    property real compactBelow: 760
    signal periodActivated(string groupKey)
    signal basisRequested(string basis)

    readonly property bool tr: !!lang && lang.language === "tr"
    readonly property bool dotsOnly: AppTheme.categoryTimelineCompact || hostWidth < compactBelow
    // Width the host gives the panel.
    readonly property real gutterWidth: dotsOnly ? 34 : 156
    readonly property real lineX: 17
    readonly property real listTop: 34
    readonly property real itemHeight: dotsOnly ? 26 : 54
    readonly property real availableHeight: Math.max(0, height - listTop - 8)
    readonly property int capacity: Math.max(2, Math.floor(availableHeight / itemHeight))

    property var periods: []
    property var periodByKey: ({})
    property int hoveredIndex: -1
    readonly property int currentIndex: {
        const index = periodByKey[currentKey]
        return index === undefined ? (periods.length ? 0 : -1) : index
    }

    width: gutterWidth
    visible: periods.length > 1

    onGroupsChanged: rebuildTimer.restart()
    onCapacityChanged: rebuildTimer.restart()
    onTrChanged: rebuildTimer.restart()
    Timer { id: rebuildTimer; interval: 0; onTriggered: timeline.rebuild() }
    // "Today" and "This week" move with the clock.
    Timer { interval: 30 * 60 * 1000; repeat: true; running: timeline.visible; onTriggered: timeline.rebuild() }

    readonly property var shortMonthsTr: ["Oca", "Şub", "Mar", "Nis", "May", "Haz",
                                          "Tem", "Ağu", "Eyl", "Eki", "Kas", "Ara"]
    readonly property var shortMonthsEn: ["Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"]

    function validDate(ms) {
        const n = Number(ms || 0)
        return n > 0 && !isNaN(new Date(n).getTime())
    }

    // Relative period of a date: [order, id, title]. Lower order = newer.
    function periodOf(ms, now) {
        if (!validDate(ms))
            return [100000, "undated", tr ? "Tarihsiz" : "Undated"]
        const n = Number(ms)
        const y = now.getFullYear(), m = now.getMonth(), d = now.getDate()
        const today = new Date(y, m, d).getTime()
        const yesterday = new Date(y, m, d - 1).getTime()
        const weekStart = new Date(y, m, d - ((now.getDay() + 6) % 7)).getTime()  // Monday
        const monthStart = new Date(y, m, 1).getTime()
        const lastMonthStart = new Date(y, m - 1, 1).getTime()
        const threeMonthsStart = new Date(y, m - 3, 1).getTime()
        const yearStart = new Date(y, 0, 1).getTime()
        if (n >= today) return [0, "today", tr ? "Bugün" : "Today"]
        if (n >= yesterday) return [1, "yesterday", tr ? "Dün" : "Yesterday"]
        if (n >= weekStart) return [2, "week", tr ? "Bu Hafta" : "This Week"]
        if (n >= monthStart) return [3, "month", tr ? "Bu Ay" : "This Month"]
        if (n >= lastMonthStart) return [4, "lastMonth", tr ? "Geçen Ay" : "Last Month"]
        if (n >= threeMonthsStart) return [5, "threeMonths", tr ? "Son 3 Ay" : "Last 3 Months"]
        if (n >= yearStart) return [6, "year", tr ? "Bu Yıl" : "This Year"]
        const year = new Date(n).getFullYear()
        return [10 + (y - year), "y" + year, String(year)]
    }

    function rebuild() {
        const source = groups || []
        const now = new Date()
        const result = []
        const byId = ({})
        for (const group of source) {
            const ms = Number(group.dateMs || 0)
            const info = periodOf(ms, now)
            let period = byId[info[1]]
            if (!period) {
                period = { id: info[1], order: info[0], title: info[2], firstKey: String(group.key),
                           keys: [], count: 0, bytes: 0, minMs: 0, maxMs: 0 }
                byId[info[1]] = period
                result.push(period)
            }
            period.keys.push(String(group.key))
            period.count += Number(group.count || 0)
            period.bytes += Number(group.bytes || 0)
            if (validDate(ms)) {
                period.minMs = period.minMs > 0 ? Math.min(period.minMs, ms) : ms
                period.maxMs = Math.max(period.maxMs, ms)
            }
        }
        // Years beyond the space join into one "Older" entry at the old end.
        const years = result.filter(function(p) { return p.order >= 10 && p.order < 100000 })
        if (result.length > capacity && years.length > 1) {
            const keepYears = Math.max(0, years.length - (result.length - capacity) - 1)
            years.sort(function(a, b) { return a.order - b.order })
            const merged = years.slice(keepYears)
            const older = { id: "older", order: merged[0].order, title: tr ? "Daha Eski" : "Older",
                            firstKey: "", keys: [], count: 0, bytes: 0, minMs: 0, maxMs: 0 }
            for (const p of merged) {
                older.keys = older.keys.concat(p.keys)
                older.count += p.count
                older.bytes += p.bytes
                if (p.minMs > 0) older.minMs = older.minMs > 0 ? Math.min(older.minMs, p.minMs) : p.minMs
                older.maxMs = Math.max(older.maxMs, p.maxMs)
            }
            // First key in display order: the merged period met first.
            const firstIndex = Math.min.apply(null, merged.map(function(p) { return result.indexOf(p) }))
            older.firstKey = result[firstIndex].firstKey
            const kept = []
            let placed = false
            for (const p of result) {
                if (merged.indexOf(p) >= 0) {
                    if (!placed) { kept.push(older); placed = true }
                } else {
                    kept.push(p)
                }
            }
            result.length = 0
            for (const p of kept) result.push(p)
        }
        const byKey = ({})
        for (let i = 0; i < result.length; ++i)
            for (const key of result[i].keys)
                byKey[key] = i
        hoveredIndex = -1
        periodByKey = byKey
        periods = result
    }

    function shortDate(ms) {
        const d = new Date(Number(ms))
        const months = tr ? shortMonthsTr : shortMonthsEn
        return tr ? d.getDate() + " " + months[d.getMonth()] + " " + d.getFullYear()
                  : months[d.getMonth()] + " " + d.getDate() + ", " + d.getFullYear()
    }

    function rangeText(period) {
        if (!period)
            return ""
        if (!(period.minMs > 0))
            return tr ? "Tarihi bilinmeyen dosyalar" : "Files without a date"
        const first = shortDate(period.minMs)
        const last = shortDate(period.maxMs)
        return first === last ? first : first + " – " + last
    }

    function countText(count) {
        return count + (tr ? " öğe" : (count === 1 ? " item" : " items"))
    }

    function formatSize(bytes) {
        const n = Number(bytes || 0)
        if (n < 1024) return n + " B"
        if (n < 1024 * 1024) return (n / 1024).toFixed(1) + " KB"
        if (n < 1024 * 1024 * 1024) return (n / (1024 * 1024)).toFixed(1) + " MB"
        if (n < 1024 * 1024 * 1024 * 1024) return (n / (1024 * 1024 * 1024)).toFixed(1) + " GB"
        return (n / (1024 * 1024 * 1024 * 1024)).toFixed(2) + " TB"
    }

    function basisText(value) {
        if (value === "created")
            return tr ? "Oluşturulma tarihi" : "Date created"
        if (value === "added")
            return tr ? "Eklenme tarihi" : "Date added"
        return tr ? "Değiştirilme tarihi" : "Date modified"
    }

    // --- Panel -----------------------------------------------------------
    // Sized to its entries rather than the whole view height.
    Rectangle {
        y: 2
        width: parent.width
        height: Math.min(parent.height - 4,
                         timeline.listTop + timeline.periods.length * timeline.itemHeight + 8)
        radius: 14
        color: AppTheme.surfaceRaised
        border.width: 1
        border.color: AppTheme.border
        opacity: 0.85
    }

    GToolButton {
        id: optionsButton
        objectName: "categoryTimelineOptions"
        x: timeline.lineX - 11
        y: 7
        width: 22
        height: 22
        padding: 0
        hoverEnabled: true
        HoverHandler { cursorShape: Qt.PointingHandCursor }
        onClicked: optionsMenu.popup(optionsButton, 0, optionsButton.height + 4)
        Accessible.name: timeline.tr ? "Zaman çizelgesi seçenekleri" : "Timeline options"
        background: Rectangle {
            radius: width / 2
            color: optionsButton.hovered || optionsMenu.opened ? AppTheme.accentSoft : "transparent"
            border.width: optionsButton.hovered || optionsMenu.opened ? 1 : 0
            border.color: AppTheme.accentBorder
        }
        contentItem: Item {
            CrispIcon {
                anchors.centerIn: parent
                width: 14
                height: 14
                source: AppTheme.icon("history.svg")
            }
        }
    }

    GText {
        visible: !timeline.dotsOnly
        anchors.left: optionsButton.right
        anchors.leftMargin: 8
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.verticalCenter: optionsButton.verticalCenter
        text: timeline.tr ? "Zaman Çizelgesi" : "Timeline"
        baseColor: AppTheme.textMuted
        basePixelSize: 10
        baseFontWeight: Font.DemiBold
        elide: Text.ElideRight
    }

    // Line through the first and last dot.
    Rectangle {
        x: timeline.lineX - 1
        y: timeline.listTop + timeline.itemHeight / 2
        width: 2
        height: Math.max(0, (timeline.periods.length - 1) * timeline.itemHeight)
        radius: 1
        color: AppTheme.borderStrong
    }

    Repeater {
        model: timeline.periods
        delegate: Item {
            id: entry
            objectName: "categoryTimelineEntry"
            required property var modelData
            required property int index
            readonly property bool active: index === timeline.currentIndex
            readonly property bool hovered: index === timeline.hoveredIndex
            x: 0
            y: timeline.listTop + index * timeline.itemHeight
            width: timeline.width
            height: timeline.itemHeight

            // Highlight behind the label (full mode).
            Rectangle {
                visible: !timeline.dotsOnly && (entry.active || entry.hovered)
                x: timeline.lineX + 12
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - x - 8
                height: parent.height - 6
                radius: 10
                color: entry.active ? AppTheme.accentSoft : AppTheme.surfaceHover
                border.width: entry.active ? 1 : 0
                border.color: AppTheme.accentBorder
            }

            // Soft halo around the active dot.
            Rectangle {
                visible: entry.active
                x: timeline.lineX - width / 2
                anchors.verticalCenter: parent.verticalCenter
                width: 24
                height: 24
                radius: 12
                color: AppTheme.accent
                opacity: 0.22
            }
            Rectangle {
                x: timeline.lineX - width / 2
                anchors.verticalCenter: parent.verticalCenter
                width: entry.active ? 14 : (entry.hovered ? 11 : 9)
                height: width
                radius: width / 2
                color: entry.active || entry.hovered ? AppTheme.accent : AppTheme.textMuted
                border.width: 2
                border.color: AppTheme.surfaceRaised
                Behavior on width { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
            }

            Column {
                visible: !timeline.dotsOnly
                x: timeline.lineX + 22
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - x - 14
                spacing: 1
                GText {
                    width: parent.width
                    text: entry.modelData.title
                    baseColor: entry.active ? AppTheme.accent : AppTheme.text
                    basePixelSize: 12
                    baseFontWeight: entry.active ? Font.Bold : Font.DemiBold
                    elide: Text.ElideRight
                }
                GText {
                    width: parent.width
                    text: timeline.countText(entry.modelData.count)
                    baseColor: AppTheme.textMuted
                    basePixelSize: 10
                    elide: Text.ElideRight
                }
            }

            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                cursorShape: Qt.PointingHandCursor
                onEntered: timeline.hoveredIndex = entry.index
                onExited: if (timeline.hoveredIndex === entry.index) timeline.hoveredIndex = -1
                onClicked: function(mouse) {
                    if (mouse.button === Qt.RightButton) {
                        optionsMenu.popup()
                        return
                    }
                    timeline.periodActivated(entry.modelData.firstKey)
                }
            }
        }
    }

    // Hover card beside the panel: exact range, count and size (and the
    // period name when only dots are shown).
    Rectangle {
        id: tip
        objectName: "categoryTimelineTooltip"
        readonly property var period: timeline.hoveredIndex >= 0 ? timeline.periods[timeline.hoveredIndex] : null
        visible: period !== null && !optionsMenu.opened
        z: 10
        width: Math.min(320, Math.max(150, tipColumn.implicitWidth) + 26)
        height: tipColumn.implicitHeight + 18
        x: -width - 8
        y: period ? Math.max(0, Math.min(timeline.height - height,
                    timeline.listTop + timeline.hoveredIndex * timeline.itemHeight
                    + timeline.itemHeight / 2 - height / 2)) : 0
        radius: 10
        color: AppTheme.surfaceRaised
        border.width: 1
        border.color: AppTheme.borderStrong

        Column {
            id: tipColumn
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 13
            spacing: 3
            GText {
                visible: timeline.dotsOnly
                text: tip.period ? tip.period.title : ""
                baseColor: AppTheme.text
                basePixelSize: 12
                baseFontWeight: Font.Bold
            }
            GText {
                text: timeline.rangeText(tip.period)
                baseColor: AppTheme.text
                basePixelSize: 11
                baseFontWeight: Font.DemiBold
            }
            GText {
                text: tip.period ? timeline.countText(tip.period.count) + "  •  " + timeline.formatSize(tip.period.bytes) : ""
                baseColor: AppTheme.textMuted
                basePixelSize: 10
            }
            GText {
                text: timeline.basisText(timeline.basis)
                baseColor: AppTheme.textFaint
                basePixelSize: 9
            }
        }
    }

    GMenu {
        id: optionsMenu
        GMenuItem {
            objectName: "categoryTimelineCompactItem"
            text: timeline.tr ? "Yalnızca noktalar" : "Dots only"
            checkable: true
            checked: AppTheme.categoryTimelineCompact
            onTriggered: AppTheme.setCategoryTimelineCompact(!AppTheme.categoryTimelineCompact)
        }
        GMenuSeparator {}
        GMenuItem {
            text: timeline.basisText("modified")
            checkable: true
            checked: timeline.basis === "modified"
            onTriggered: timeline.basisRequested("modified")
        }
        GMenuItem {
            text: timeline.basisText("created")
            checkable: true
            checked: timeline.basis === "created"
            onTriggered: timeline.basisRequested("created")
        }
        GMenuItem {
            objectName: "categoryTimelineAddedItem"
            text: timeline.basisText("added")
            checkable: true
            checked: timeline.basis === "added"
            onTriggered: timeline.basisRequested("added")
        }
        GMenuSeparator {}
        GMenuItem {
            text: timeline.tr ? "Zaman çizelgesini gizle" : "Hide timeline"
            onTriggered: AppTheme.setCategoryTimelineVisible(false)
        }
    }
}
