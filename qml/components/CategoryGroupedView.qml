import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQml.Models
import Lurviko.App

Item {
    id: root
    required property var files
    required property var lang
    required property int cardSize
    required property int selectionRevision
    property var isSelected: function(url) { return false }
    property string inlineRenameUrl: ""
    property bool dateAscending: false
    property bool singleClickOpen: false
    property int wheelStep: AppTheme.wheelScrollStep

    signal itemPressed(int modelIndex, int modifiers)
    signal itemActivated(int modelIndex)
    signal contextRequested(var sourceItem, int modelIndex, real x, real y)
    signal renameCommit(string itemUrl, string value, bool backwards, bool advance)
    signal renameCancel()
    signal renamePrepare(var editor, string itemName, bool itemIsDir)
    signal zoomRequested(int delta)

    property var groups: []
    property double wheelBurstLastMs: 0
    property int wheelBurstCount: 0
    property int wheelBurstDirection: 0
    property var rows: []
    property var modelRows: ({})
    property bool groupsDirty: true
    property int layoutColumns: 1
    property string resizeAnchorKey: ""
    property string resizeAnchorUrl: ""
    property real resizeAnchorOffset: 0
    property bool resizeAnchorPending: false

    readonly property real scaleFactor: Math.max(0.70, Math.min(2.15, cardSize / 168.0))
    readonly property real textScale: Math.max(0.96, Math.min(1.16, 1.0 + (scaleFactor - 1.0) * 0.10))
    readonly property real desiredCardWidth: Math.max(104, cardSize)
    readonly property real cellGap: Math.max(6, Math.round(8 * scaleFactor))
    readonly property int calculatedColumns: Math.max(1, Math.floor(galleryList.width / desiredCardWidth))
    readonly property real rowCardWidth: Math.max(1, (galleryList.width - (layoutColumns - 1) * cellGap) / layoutColumns)
    readonly property real rowCardHeight: Math.round(Math.max(58, Math.round(cardSize * 0.68)) + 62)

    function dateKey(ms) {
        const d = new Date(Number(ms || 0))
        if (isNaN(d.getTime())) return "0000-00-00"
        const m = d.getMonth() + 1
        const day = d.getDate()
        return d.getFullYear() + "-" + (m < 10 ? "0" : "") + m + "-" + (day < 10 ? "0" : "") + day
    }

    function sameDay(a, b) {
        return a.getFullYear() === b.getFullYear()
                && a.getMonth() === b.getMonth()
                && a.getDate() === b.getDate()
    }

    function dateTitle(ms) {
        const d = new Date(Number(ms || 0))
        if (isNaN(d.getTime()))
            return lang.language === "tr" ? "Tarih bilinmiyor" : "Unknown date"
        const now = new Date()
        const yesterday = new Date(now)
        yesterday.setDate(now.getDate() - 1)
        if (sameDay(d, now))
            return lang.language === "tr" ? "Bugün" : "Today"
        if (sameDay(d, yesterday))
            return lang.language === "tr" ? "Dün" : "Yesterday"
        const dd = String(d.getDate()).padStart(2, "0")
        const mm = String(d.getMonth() + 1).padStart(2, "0")
        return dd + "." + mm + "." + d.getFullYear()
    }

    function rebuildGroups() {
        const source = files || []
        const byKey = ({})
        const result = []
        for (let i = 0; i < source.length; ++i) {
            const item = source[i]
            const ms = Number(item.groupDateMs || item.modifiedMs || 0)
            const key = dateKey(ms)
            if (byKey[key] === undefined) {
                byKey[key] = result.length
                result.push({ key: key, dateMs: ms, title: dateTitle(ms), items: [] })
            }
            result[byKey[key]].items.push(item)
        }
        result.sort(function(a, b) {
            const av = Number(a.dateMs || 0)
            const bv = Number(b.dateMs || 0)
            return root.dateAscending ? av - bv : bv - av
        })
        groups = result
    }

    function normalizedWheelDelta(event) {
        const angleY = event.angleDelta.y
        const pixelY = event.pixelDelta.y
        const mousePixelOnly = angleY === 0 && pixelY !== 0
                               && (event.phase === Qt.NoScrollPhase
                                   || (event.device && event.device.type === PointerDevice.Mouse))
        if (angleY !== 0 || mousePixelOnly) {
            const now = Date.now()
            const direction = (angleY !== 0 ? angleY : pixelY) > 0 ? 1 : -1
            const gap = wheelBurstLastMs > 0 ? now - wheelBurstLastMs : 9999
            if (direction !== wheelBurstDirection || gap > 300)
                wheelBurstCount = 0
            else
                wheelBurstCount = Math.min(5, wheelBurstCount + 1)
            wheelBurstLastMs = now
            wheelBurstDirection = direction
            const rawFraction = angleY !== 0 ? Math.abs(angleY) / 120.0 : Math.abs(pixelY) / 30.0
            const stepFraction = Math.max(0.125, Math.min(2.0, rawFraction))
            const acceleration = 1.0 + wheelBurstCount * 0.08
            return direction * root.wheelStep * stepFraction * acceleration
        }
        if (pixelY !== 0) {
            wheelBurstCount = 0
            wheelBurstDirection = 0
            wheelBurstLastMs = 0
            return pixelY * 2.0
        }
        return 0
    }

    function scrollFromWheel(event) {
        const view = galleryList
        const delta = normalizedWheelDelta(event)
        if (delta === 0)
            return
        const top = Number(view.originY || 0)
        const bottom = top + Math.max(0, view.contentHeight - view.height)
        const continuing = wheelScrollAnimation.running && wheelScrollAnimation.target === view
        const previousTarget = continuing ? wheelScrollAnimation.to : view.contentY
        const sameDirection = !continuing || Math.sign(previousTarget - view.contentY) === -Math.sign(delta)
        const base = sameDirection ? previousTarget : view.contentY
        const rowExtent = root.rowCardHeight
        const limit = Math.max(root.wheelStep, view.height * 1.8, rowExtent * 6)
        const destination = Math.max(top, Math.min(bottom,
                                Math.max(view.contentY - limit,
                                         Math.min(view.contentY + limit, base - delta))))
        wheelScrollAnimation.stop()
        if (Math.abs(destination - view.contentY) < 0.5)
            return
        wheelScrollAnimation.target = view
        wheelScrollAnimation.from = view.contentY
        wheelScrollAnimation.to = destination
        wheelScrollAnimation.duration = Math.min(400, Math.max(220,
                                        Math.abs(destination - view.contentY) * 0.55))
        wheelScrollAnimation.start()
    }

    NumberAnimation {
        id: wheelScrollAnimation
        objectName: "categoryGroupedWheelAnimation"
        property: "contentY"
        easing.type: Easing.OutCubic
    }


    function rebuildRows() {
        const result = []
        const itemRows = ({})
        for (let g = 0; g < groups.length; ++g) {
            const group = groups[g]
            result.push({ kind: "header", key: group.key, title: group.title, count: group.items.length })
            for (let offset = 0; offset < group.items.length; offset += layoutColumns) {
                const items = group.items.slice(offset, offset + layoutColumns)
                for (let i = 0; i < items.length; ++i) itemRows[items[i].modelIndex] = result.length
                result.push({ kind: "cards", key: group.key, items: items })
            }
        }
        rows = result
        modelRows = itemRows
        while (rowModel.count < result.length) rowModel.append({ rowIndex: rowModel.count })
        if (rowModel.count > result.length) rowModel.remove(result.length, rowModel.count - result.length)
    }

    function prepareForResize() {
        if (resizeAnchorPending) return
        wheelScrollAnimation.stop()
        galleryList.cancelFlick()
        galleryList.forceLayout()
        let index = galleryList.indexAt(2, galleryList.contentY + 2)
        if (index < 0) index = galleryList.indexAt(2, galleryList.contentY + Math.min(24, galleryList.height / 2))
        if (index < 0 || index >= rows.length) return
        const row = rows[index]
        const item = galleryList.itemAtIndex(index)
        resizeAnchorKey = row.key
        resizeAnchorUrl = row.kind === "cards" ? String(row.items[0].itemUrl || "") : ""
        resizeAnchorOffset = item ? galleryList.contentY - item.y : 0
        resizeAnchorPending = true
    }

    function restoreResizeAnchor() {
        let index = -1
        for (let i = 0; i < rows.length; ++i) {
            const row = rows[i]
            if (!resizeAnchorUrl && row.kind === "header" && row.key === resizeAnchorKey) index = i
            if (resizeAnchorUrl && row.kind === "cards") {
                for (let j = 0; j < row.items.length; ++j)
                    if (String(row.items[j].itemUrl || "") === resizeAnchorUrl) index = i
            }
            if (index >= 0) break
        }
        if (index < 0) return
        galleryList.positionViewAtIndex(index, ListView.Beginning)
        galleryList.forceLayout()
        const item = galleryList.itemAtIndex(index)
        if (item) {
            const top = galleryList.originY
            const bottom = top + Math.max(0, galleryList.contentHeight - galleryList.height)
            galleryList.contentY = Math.max(top, Math.min(bottom, item.y + resizeAnchorOffset))
        }
    }

    function positionModelIndex(index, mode) {
        rowRebuildTimer.stop()
        updateRows()
        const row = modelRows[index]
        if (row !== undefined) galleryList.positionViewAtIndex(row, mode)
    }

    function updateRows() {
        const columnsChanged = layoutColumns !== calculatedColumns
        layoutColumns = calculatedColumns
        if (groupsDirty) rebuildGroups()
        if (groupsDirty || columnsChanged) rebuildRows()
        groupsDirty = false
        galleryList.forceLayout()
        if (resizeAnchorPending) restoreResizeAnchor()
        resizeAnchorPending = false
    }

    onFilesChanged: { groupsDirty = true; rowRebuildTimer.restart() }
    onDateAscendingChanged: { groupsDirty = true; rowRebuildTimer.restart() }
    onCalculatedColumnsChanged: rowRebuildTimer.restart()
    onCardSizeChanged: rowRebuildTimer.restart()
    onVisibleChanged: if (!visible) wheelScrollAnimation.stop()
    Component.onCompleted: rowRebuildTimer.restart()

    ListModel { id: rowModel }
    Timer { id: rowRebuildTimer; interval: 0; onTriggered: root.updateRows() }

    ListView {
        id: galleryList
        objectName: "categoryGroupedList"
        anchors.fill: parent
        anchors.rightMargin: 12
        clip: true
        spacing: root.cellGap
        model: rowModel
        cacheBuffer: Math.max(0, Math.min(400, height * 0.5))
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds
        onDraggingChanged: if (dragging) wheelScrollAnimation.stop()

        WheelHandler {
            target: null
            blocking: true
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            acceptedModifiers: Qt.ControlModifier
            onWheel: function(event) {
                const dy = event.angleDelta.y !== 0 ? event.angleDelta.y : event.pixelDelta.y
                if (dy !== 0) root.zoomRequested(dy > 0 ? 8 : -8)
                event.accepted = true
            }
        }
        WheelHandler {
            target: null
            blocking: true
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            acceptedModifiers: Qt.NoModifier
            onWheel: function(event) { root.scrollFromWheel(event); event.accepted = true }
        }

        delegate: Item {
            id: rowDelegate
            required property int rowIndex
            readonly property var rowData: root.rows[rowIndex] || null
            width: galleryList.width
            height: !rowData ? 0 : (rowData.kind === "header" ? 58 + (rowIndex > 0 ? Math.max(0, 12 - root.cellGap) : 0) : root.rowCardHeight)
            CategoryDateHeader {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 6
                visible: rowDelegate.rowData !== null && rowDelegate.rowData.kind === "header"
                title: rowDelegate.rowData && rowDelegate.rowData.kind === "header" ? rowDelegate.rowData.title : ""
                count: rowDelegate.rowData && rowDelegate.rowData.kind === "header" ? Number(rowDelegate.rowData.count || 0) : 0
                lang: root.lang
                textScale: root.textScale
            }
            Row {
                width: parent.width
                height: parent.height
                spacing: root.cellGap
                Repeater {
                    model: rowDelegate.rowData && rowDelegate.rowData.kind === "cards" ? rowDelegate.rowData.items : []
                    delegate: Item {
                        id: cell
                        objectName: "categoryGroupedCard"
                        required property var modelData
                        readonly property bool selected: {
                            const revision = root.selectionRevision
                            return root.isSelected ? root.isSelected(String(modelData.itemUrl || "")) : false
                        }
                        readonly property real iconExtent: Math.max(58, Math.round(root.cardSize * 0.68))
                        width: root.rowCardWidth
                        height: Math.round(iconExtent + 62)

                        Rectangle {
                            anchors.fill: parent
                            anchors.leftMargin: 7
                            anchors.rightMargin: 7
                            anchors.topMargin: 3
                            anchors.bottomMargin: 7
                            radius: 12
                            visible: cell.selected
                            color: AppTheme.accentSoft
                            border.width: 1
                            border.color: AppTheme.accent
                        }

                        ColumnLayout {
                            z: root.inlineRenameUrl === String(cell.modelData.itemUrl || "") ? 1 : 0
                            anchors.fill: parent
                            anchors.leftMargin: 7
                            anchors.rightMargin: 7
                            anchors.topMargin: 7
                            anchors.bottomMargin: 9
                            spacing: 4

                            Item {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.preferredWidth: Math.min(cell.iconExtent, Math.max(36, cell.width - 20))
                                Layout.preferredHeight: Layout.preferredWidth

                                CrispIcon {
                                    anchors.fill: parent
                                    source: AppTheme.systemIconAtSize(String(cell.modelData.iconSource || ""), width)
                                    visible: !cell.modelData.thumbnailSource || preview.status !== Image.Ready
                                }

                                // These categories contain documents, audio,
                                // archives and apps. They need no video-settle
                                // timers; a stable image size shares the cache
                                // across resizing and row reuse.
                                Image {
                                    id: preview
                                    anchors.fill: parent
                                    sourceSize.width: 512
                                    sourceSize.height: 512
                                    source: String(cell.modelData.thumbnailSource || "")
                                    asynchronous: true
                                    cache: true
                                    fillMode: Image.PreserveAspectFit
                                    visible: source.toString().length > 0 && status === Image.Ready
                                }
                            }

                            Item {
                                Layout.fillWidth: true
                                Layout.preferredHeight: root.inlineRenameUrl === String(cell.modelData.itemUrl || "") ? 26 : 22

                                Text {
                                    anchors.fill: parent
                                    visible: root.inlineRenameUrl !== String(cell.modelData.itemUrl || "")
                                    text: String(cell.modelData.name || "")
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideMiddle
                                    color: AppTheme.text
                                    font.pixelSize: Math.max(12, Math.min(15, Math.round(12 * root.textScale)))
                                    font.bold: !!cell.modelData.isDir
                                }

                                GTextField {
                                    id: renameField
                                    objectName: "categoryGroupedRenameField"
                                    text: String(cell.modelData.name || "")
                                    anchors.fill: parent
                                    visible: root.inlineRenameUrl === String(cell.modelData.itemUrl || "")
                                    selectByMouse: true
                                    HoverHandler { cursorShape: Qt.IBeamCursor }
                                    horizontalAlignment: TextInput.AlignHCenter
                                    font.pixelSize: Math.max(12, Math.min(15, Math.round(12 * root.textScale)))
                                    leftPadding: 7
                                    rightPadding: 7
                                    topPadding: 1
                                    bottomPadding: 1
                                    function prepareEditor() {
                                        if (!visible)
                                            return
                                        root.renamePrepare(renameField, String(cell.modelData.name || ""), !!cell.modelData.isDir)
                                        if (!activeFocus) {
                                            forceActiveFocus()
                                            selectAll()
                                        }
                                    }
                                    onVisibleChanged: {
                                        if (visible)
                                            Qt.callLater(renameField.prepareEditor)
                                    }
                                    Component.onCompleted: {
                                        if (visible)
                                            Qt.callLater(renameField.prepareEditor)
                                    }
                                    onAccepted: root.renameCommit(String(cell.modelData.itemUrl || ""), text, false, false)
                                    Keys.onPressed: function(event) {
                                        if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) {
                                            root.renameCommit(String(cell.modelData.itemUrl || ""), text,
                                                              event.key === Qt.Key_Backtab || (event.modifiers & Qt.ShiftModifier), true)
                                            event.accepted = true
                                        } else if (event.key === Qt.Key_Escape) {
                                            root.renameCancel()
                                            event.accepted = true
                                        }
                                    }
                                    background: Rectangle {
                                        radius: 7
                                        color: AppTheme.surface
                                        border.width: 2
                                        border.color: AppTheme.accent
                                    }
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                text: String(cell.modelData.metaText || "")
                                horizontalAlignment: Text.AlignHCenter
                                elide: Text.ElideRight
                                color: AppTheme.textFaint
                                font.pixelSize: Math.max(9, Math.min(11, Math.round(10 * root.textScale)))
                            }
                        }

                        MouseArea {
                            id: cellMouse
                            anchors.fill: parent
                            enabled: root.inlineRenameUrl !== String(cell.modelData.itemUrl || "")
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton | Qt.RightButton

                            function isContentHit(mouseX) {
                                const hitWidth = Math.min(cell.width - 10, Math.max(84, cell.iconExtent + 42))
                                const leftEdge = (cell.width - hitWidth) / 2
                                return mouseX >= leftEdge && mouseX <= leftEdge + hitWidth
                            }

                            cursorShape: isContentHit(mouseX) ? Qt.PointingHandCursor : Qt.ArrowCursor

                            onPressed: function(mouse) {
                                if (!isContentHit(mouse.x)) {
                                    mouse.accepted = false
                                    return
                                }
                                if (mouse.button === Qt.RightButton) {
                                    root.itemPressed(Number(cell.modelData.modelIndex), Qt.NoModifier)
                                    root.contextRequested(cellMouse, Number(cell.modelData.modelIndex), mouse.x, mouse.y)
                                } else {
                                    root.itemPressed(Number(cell.modelData.modelIndex), mouse.modifiers)
                                }
                            }
                            onClicked: function(mouse) {
                                if (root.singleClickOpen && mouse.button === Qt.LeftButton && isContentHit(mouse.x))
                                    root.itemActivated(Number(cell.modelData.modelIndex))
                            }
                            onDoubleClicked: function(mouse) {
                                if (!root.singleClickOpen && mouse.button === Qt.LeftButton && isContentHit(mouse.x))
                                    root.itemActivated(Number(cell.modelData.modelIndex))
                            }
                        }
                    }
                }
            }
        }
        ScrollBar.vertical: ScrollBar {
            id: scrollBar
            parent: root
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.right: parent.right
            width: 10
            policy: ScrollBar.AsNeeded
            active: pressed || hovered || galleryList.moving || wheelScrollAnimation.running
            onPressedChanged: if (pressed) wheelScrollAnimation.stop()
            contentItem: Rectangle {
                implicitWidth: 6
                radius: 3
                color: scrollBar.pressed ? AppTheme.accent : AppTheme.textMuted
                opacity: scrollBar.active ? 0.82 : 0.0
                Behavior on opacity { NumberAnimation { duration: 180 } }
            }
            background: Item {}
        }
    }
}
