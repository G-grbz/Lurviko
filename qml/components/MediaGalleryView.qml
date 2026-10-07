import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtCore
import QtQml.Models
import GFile.App
import GFile.Backend

Item {
    id: root
    required property var files
    required property var lang
    required property string categoryKey
    required property string categoryIcon
    required property int gallerySize
    required property var contentIndexModel
    property var hostWindow: null
    property int selectionRevision: 0
    property var isSelected: null
    signal browseRequested(string location)
    signal zoomRequested(int delta)
    signal itemPressed(int modelIndex, int modifiers)

    function itemSelected(item) {
        const revision = root.selectionRevision
        return revision >= 0 && item && root.isSelected
                ? !!root.isSelected(String(item.url || "")) : false
    }

    function pressItem(item, modifiers) {
        if (!item || item.modelIndex === undefined || item.modelIndex === null)
            return
        root.itemPressed(Number(item.modelIndex), modifiers)
    }

    readonly property bool imageMode: categoryKey === "images"
    readonly property bool videoMode: categoryKey === "videos"
    readonly property bool singleClickOpen: {
        if (AppTheme.singleClickOpen)
            return true
        const revision = root.contentIndexModel ? root.contentIndexModel.categoryInteractionRevision : 0
        return revision >= 0 && root.contentIndexModel
                ? !!root.contentIndexModel.categorySingleClickOpen(root.categoryKey) : false
    }
    property var albums: []
    property var albumsByPath: ({})
    property string activeAlbumPath: ""
    readonly property var displayFiles: imageMode && activeAlbumPath.length > 0
                                        ? (albumsByPath[activeAlbumPath] ? albumsByPath[activeAlbumPath].items : [])
                                        : files
    property bool albumScrollResetPending: false
    property var hoverPreviewAnchor: null
    property var hoverPreviewPendingItem: null
    property var hoverPreviewItem: null
    property bool hoverPreviewShowing: false
    property bool hoverPreviewFadingOut: false
    property var tmdbInfo: ({ state: "loading" })
    // Local-video card artwork is lazy: only instantiated/visible gallery cards
    // request TMDB metadata. The indexed video list itself remains untouched.
    property var localArtworkByPath: ({})
    property var localArtworkQueued: ({})
    property var localArtworkInflight: ({})
    property var localArtworkQueue: []
    property int localArtworkInflightCount: 0
    property int localArtworkEpoch: 0
    // Media size follows the category-only zoom slider. Text grows only a
    // little and then caps, so large thumbnails never turn into giant labels.
    readonly property real galleryScale: Math.max(0.70, Math.min(2.15, gallerySize / 168.0))
    readonly property real textScale: Math.max(0.94, Math.min(1.18, 1.0 + (galleryScale - 1.0) * 0.12))
    readonly property real desiredCardWidth: Math.max(104, Math.min(360, gallerySize))
    readonly property real cardGap: 16
    readonly property real rowGap: 14
    readonly property real galleryContentWidth: Math.max(1, width - 30)
    // Keep the date layout, but expose it to a ListView as virtualized rows.
    // The previous nested Repeaters instantiated every media card at once;
    // large indexed libraries therefore blocked page entry, page destruction
    // and every icon-size change even when their thumbnails were cached.
    property var dateGroups: []
    property var rows: []
    property int layoutColumns: 1
    property string resizeAnchorKind: ""
    property string resizeAnchorKey: ""
    property string resizeAnchorPath: ""
    property real resizeAnchorOffset: 0
    property bool resizeGestureActive: false
    property bool resizeAnchorPending: false
    property bool rowsDirty: true
    readonly property int calculatedColumns: Math.max(1, Math.floor(
            (galleryContentWidth + cardGap) / (desiredCardWidth + cardGap)))
    // Fill complete rows up to the scrollbar gutter. Height still follows
    // every slider pixel, including pixels between column-count changes.
    readonly property real rowCardWidth: Math.max(1,
            (galleryContentWidth - (layoutColumns - 1) * cardGap) / layoutColumns)
    readonly property real rowCardHeight: desiredCardWidth * 0.82
    property double wheelBurstLastMs: 0
    property int wheelBurstCount: 0
    property int wheelBurstDirection: 0

    Settings {
        id: imageViewSettings
        category: "imageView"
        property bool hoverEnabled: true
        property bool hoverAnimationsEnabled: true
        property int hoverDelayMs: 420
        property int slideshowIntervalMs: 3500
    }

    TmdbMetadataManager {
        id: tmdbManager
        onMetadataReady: function(filePath, metadata) {
            root.acceptLocalArtworkResult(filePath, metadata)
            if (!root.videoMode || !root.hoverPreviewShowing || !root.hoverPreviewItem)
                return
            const activePath = String(root.hoverPreviewItem.path || "")
            if (activePath === filePath)
                root.tmdbInfo = metadata
        }
        onMetadataFailed: function(filePath, message) {
            root.finishLocalArtworkRequest(filePath)
            if (!root.videoMode || !root.hoverPreviewShowing || !root.hoverPreviewItem)
                return
            const activePath = String(root.hoverPreviewItem.path || "")
            if (activePath === filePath)
                root.tmdbInfo = ({ state: "error", parsedTitle: root.hoverPreviewItem.name || "", message: message })
        }
    }

    function openSettings() {
        if (root.imageMode)
            imageSettingsPopup.open()
        else if (root.videoMode)
            tmdbSettingsPopup.open()
    }

    function isToday(date) {
        const now = new Date()
        return date.getFullYear() === now.getFullYear()
                && date.getMonth() === now.getMonth()
                && date.getDate() === now.getDate()
    }

    function isYesterday(date) {
        const y = new Date()
        y.setDate(y.getDate() - 1)
        return date.getFullYear() === y.getFullYear()
                && date.getMonth() === y.getMonth()
                && date.getDate() === y.getDate()
    }

    function monthName(month) {
        const tr = ["Ocak", "Şubat", "Mart", "Nisan", "Mayıs", "Haziran",
                    "Temmuz", "Ağustos", "Eylül", "Ekim", "Kasım", "Aralık"]
        const en = ["January", "February", "March", "April", "May", "June",
                    "July", "August", "September", "October", "November", "December"]
        return (lang.language === "tr" ? tr : en)[month]
    }

    function groupTitle(ms) {
        const d = new Date(Number(ms || 0))
        if (isNaN(d.getTime()))
            return lang.language === "tr" ? "Tarih bilinmiyor" : "Unknown date"
        if (isToday(d))
            return lang.language === "tr" ? "Bugün" : "Today"
        if (isYesterday(d))
            return lang.language === "tr" ? "Dün" : "Yesterday"
        const dd = String(d.getDate()).padStart(2, "0")
        const mm = String(d.getMonth() + 1).padStart(2, "0")
        return dd + "." + mm + "." + d.getFullYear()
    }

    function groupKey(ms) {
        const d = new Date(Number(ms || 0))
        if (isToday(d)) return "today"
        if (isYesterday(d)) return "yesterday"
        const month = d.getMonth() + 1
        const day = d.getDate()
        return d.getFullYear() + "-" + (month < 10 ? "0" : "") + month
                + "-" + (day < 10 ? "0" : "") + day
    }

    function rebuildDateGroups() {
        const grouped = []
        const positions = ({})
        for (let i = 0; i < displayFiles.length; ++i) {
            const item = displayFiles[i]
            const key = groupKey(item.modifiedMs)
            let index = positions[key]
            if (index === undefined) {
                index = grouped.length
                positions[key] = index
                grouped.push({ key: key, dateMs: item.modifiedMs, items: [] })
            }
            grouped[index].items.push(item)
        }

        dateGroups = grouped
    }

    function parentPath(item) {
        if (item.parentPath) return String(item.parentPath)
        const path = String(item.path || "")
        return path.substring(0, path.lastIndexOf("/")) || "/"
    }

    function rebuildAlbums() {
        const grouped = ({})
        const result = []
        if (imageMode) {
            for (let i = 0; i < files.length; ++i) {
                const item = files[i]
                const path = parentPath(item)
                let album = grouped[path]
                if (!album) {
                    album = { path: path, title: path.substring(path.lastIndexOf("/") + 1) || "/",
                              count: 0, latestMs: 0, items: [], covers: [] }
                    grouped[path] = album
                    result.push(album)
                }
                album.count += 1
                album.items.push(item)
                album.latestMs = Math.max(album.latestMs, Number(item.modifiedMs || 0))
                album.covers.push({ item: item, thumbnailSource: thumbSource(item, "dated") })
                album.covers.sort(function(a, b) { return Number(b.item.modifiedMs || 0) - Number(a.item.modifiedMs || 0) })
                if (album.covers.length > 3) album.covers.pop()
            }
            result.sort(function(a, b) {
                return b.latestMs - a.latestMs || a.path.localeCompare(b.path)
            })
        }
        albumsByPath = grouped
        albums = result
        if (activeAlbumPath.length && !grouped[activeAlbumPath]) activeAlbumPath = ""
    }

    function selectAlbum(path) {
        dismissHoverPreview()
        if (activeAlbumPath !== path) activeAlbumPath = path
    }

    function requestHoverPreview(item, anchor) {
        if ((!imageMode && !videoMode) || !item || !anchor || galleryList.moving
                || wheelScrollAnimation.running || hoverScrollGuard.running)
            return
        if (videoMode && !tmdbManager.hoverEnabled)
            return
        if (imageMode && !imageViewSettings.hoverEnabled)
            return
        if (hoverPreviewAnchor === anchor && hoverPreviewPendingItem
                && hoverPreviewPendingItem.path === item.path) return

        hoverPreviewPendingItem = item
        hoverPreviewAnchor = anchor
        hoverPreviewDelay.stop()

        // Never stack a second card on top of a card that is still visible.
        // Fade the old card out first, then start the normal hover delay for
        // the new candidate. This also makes fast pointer sweeps deterministic.
        if (hoverPreviewShowing) {
            hoverPreviewShowing = false
            hoverPreviewFadingOut = true
            hoverPreviewCloseTimer.restart()
        } else if (!hoverPreviewFadingOut) {
            hoverPreviewDelay.restart()
        }
    }

    function dismissHoverPreview(anchor) {
        if (anchor && anchor !== hoverPreviewAnchor) return
        hoverPreviewDelay.stop()
        hoverPreviewPendingItem = null
        hoverPreviewAnchor = null
        if (hoverPreviewItem !== null) {
            hoverPreviewShowing = false
            hoverPreviewFadingOut = true
            hoverPreviewCloseTimer.restart()
        } else {
            hoverPreviewShowing = false
            hoverPreviewFadingOut = false
            if (videoMode)
                tmdbInfo = ({ state: "loading" })
        }
    }

    function showHoverPreview() {
        if (hoverPreviewFadingOut || !hoverPreviewAnchor || !hoverPreviewPendingItem || !root.visible
                || (videoMode && !tmdbManager.hoverEnabled)
                || (imageMode && !imageViewSettings.hoverEnabled)
                || galleryList.moving || wheelScrollAnimation.running || hoverScrollGuard.running) return
        const point = hoverPreviewAnchor.mapToItem(root, 0, 0)
        const margin = 12
        const right = point.x + hoverPreviewAnchor.width + margin
        const left = point.x - hoverPreview.width - margin
        let x = right + hoverPreview.width <= root.width - margin ? right
                : (left >= margin ? left : (point.x < root.width / 2 ? root.width - hoverPreview.width - margin : margin))
        hoverPreview.x = Math.max(margin, Math.min(root.width - hoverPreview.width - margin, x))
        hoverPreview.y = Math.max(margin, Math.min(root.height - hoverPreview.height - margin,
                                  point.y + (hoverPreviewAnchor.height - hoverPreview.height) / 2))
        hoverPreviewItem = hoverPreviewPendingItem
        if (videoMode && hoverPreviewItem) {
            const sourcePath = String(hoverPreviewItem.path || hoverPreviewItem.url || "")
            tmdbInfo = tmdbManager.lookup(sourcePath, String(hoverPreviewItem.name || ""), String(tmdbManager.preferredLanguage || "tr"))
        }
        hoverPreviewShowing = true
    }

    function rebuildRows() {
        const result = []
        const columns = Math.max(1, layoutColumns)
        for (let groupIndex = 0; groupIndex < dateGroups.length; ++groupIndex) {
            const group = dateGroups[groupIndex]
            result.push({ kind: "header", key: group.key,
                          dateMs: group.dateMs, count: group.items.length })
            for (let offset = 0; offset < group.items.length; offset += columns) {
                result.push({ kind: "cards", key: group.key,
                              items: group.items.slice(offset, offset + columns) })
            }
        }
        rows = result
        // Keep the ListView model alive. Replacing a JS array model resets
        // its viewport to the beginning and can paint that frame before a
        // delayed scroll restoration. Only adjust slots at the end instead.
        while (rowModel.count < result.length)
            rowModel.append({ rowIndex: rowModel.count })
        if (rowModel.count > result.length)
            rowModel.remove(result.length, rowModel.count - result.length)
    }

    function thumbSource(item, tag) {
        if (!item || !item.path)
            return ""
        return "image://gfilethumb/" + encodeURIComponent(item.path)
                + "|gallery-" + (tag || "card") + "-"
                + String(item.modifiedMs || 0) + "-" + String(item.size || 0)
    }

    function localArtworkMetadata(item) {
        if (!item || !item.path)
            return null
        return localArtworkByPath[String(item.path)] || null
    }

    function localArtworkUrl(item) {
        if (!videoMode || !tmdbManager.localArtworkEnabled || !item || !item.path)
            return ""
        const metadata = localArtworkMetadata(item)
        if (!metadata)
            return ""
        // Requested priority for local cards: backdrop -> poster -> existing thumbnail fallback.
        const backdrop = String(metadata.backdropUrl || "")
        if (backdrop.length > 0)
            return backdrop
        return String(metadata.posterUrl || "")
    }

    function localCardImageSource(item) {
        const artwork = localArtworkUrl(item)
        return artwork.length > 0 ? artwork : thumbSource(item, "dated")
    }

    function localCardUsesArtwork(item) {
        return localArtworkUrl(item).length > 0
    }

    function finishLocalArtworkRequest(path) {
        const key = String(path || "")
        if (!key.length || !localArtworkInflight[key])
            return
        const inflight = Object.assign({}, localArtworkInflight)
        delete inflight[key]
        localArtworkInflight = inflight
        localArtworkInflightCount = Math.max(0, localArtworkInflightCount - 1)
        localArtworkPump.restart()
    }

    function acceptLocalArtworkResult(path, metadata) {
        const key = String(path || "")
        if (!key.length)
            return
        if (metadata && String(metadata.state || "") === "ready") {
            const copy = Object.assign({}, localArtworkByPath)
            copy[key] = metadata
            localArtworkByPath = copy
        }
        finishLocalArtworkRequest(key)
    }

    function requestLocalArtwork(item) {
        if (!videoMode || !tmdbManager.localArtworkEnabled || !tmdbManager.configured || !item || !item.path)
            return
        const key = String(item.path)
        if (!key.length || localArtworkByPath[key] || localArtworkQueued[key] || localArtworkInflight[key])
            return

        // Cached hover metadata should paint immediately without entering the network queue.
        const cached = tmdbManager.cachedLookup(key, String(tmdbManager.preferredLanguage || "tr"))
        if (cached && String(cached.state || "") === "ready") {
            const copy = Object.assign({}, localArtworkByPath)
            copy[key] = cached
            localArtworkByPath = copy
            return
        }

        const queued = Object.assign({}, localArtworkQueued)
        queued[key] = true
        localArtworkQueued = queued
        localArtworkQueue = localArtworkQueue.concat([{ path: key, name: String(item.name || "") }])
        localArtworkPump.restart()
    }

    function pumpLocalArtwork() {
        if (!videoMode || !tmdbManager.localArtworkEnabled || !tmdbManager.configured)
            return
        const maximum = Math.max(1, Number(tmdbManager.artworkConcurrency || 6))
        while (localArtworkInflightCount < maximum && localArtworkQueue.length > 0) {
            const request = localArtworkQueue[0]
            localArtworkQueue = localArtworkQueue.slice(1)
            const key = String(request.path || "")
            const queued = Object.assign({}, localArtworkQueued)
            delete queued[key]
            localArtworkQueued = queued
            if (!key.length || localArtworkByPath[key] || localArtworkInflight[key])
                continue

            const inflight = Object.assign({}, localArtworkInflight)
            inflight[key] = true
            localArtworkInflight = inflight
            localArtworkInflightCount += 1

            const result = tmdbManager.lookup(key, String(request.name || ""), String(tmdbManager.preferredLanguage || "tr"))
            const state = String(result && result.state ? result.state : "")
            if (state === "ready") {
                acceptLocalArtworkResult(key, result)
            } else if (state !== "loading") {
                finishLocalArtworkRequest(key)
            }
        }
    }

    function resetLocalArtwork(reRequest) {
        localArtworkByPath = ({})
        localArtworkQueued = ({})
        localArtworkInflight = ({})
        localArtworkQueue = []
        localArtworkInflightCount = 0
        localArtworkEpoch += 1
        if (reRequest)
            localArtworkPump.restart()
    }

    function formatSize(bytes) {
        const n = Number(bytes || 0)
        if (n < 1024) return n + " B"
        if (n < 1024 * 1024) return (n / 1024).toFixed(1) + " KiB"
        if (n < 1024 * 1024 * 1024) return (n / (1024 * 1024)).toFixed(1) + " MiB"
        return (n / (1024 * 1024 * 1024)).toFixed(1) + " GiB"
    }

    function fileIndex(path) {
        for (let i = 0; i < displayFiles.length; ++i) {
            if (displayFiles[i].path === path)
                return i
        }
        return -1
    }

    function activate(item) {
        if (!item)
            return
        dismissHoverPreview()
        const idx = fileIndex(item.path)
        if (idx < 0)
            return
        if (imageMode)
            viewer.openAt(idx)
        else if (videoMode)
            videoViewer.openAt(idx)
        else
            Qt.openUrlExternally(item.url)
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
            return direction * AppTheme.wheelScrollStep * stepFraction * acceleration
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
        dismissHoverPreview()
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
        const rowExtent = Math.max(120, desiredCardWidth * 0.82)
        const limit = Math.max(AppTheme.wheelScrollStep, view.height * 1.8, rowExtent * 6)
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
        property: "contentY"
        easing.type: Easing.OutCubic
    }

    function scheduleRowRebuild() {
        rowRebuildTimer.restart()
    }

    function beginResizeGesture() {
        prepareForResize()
        resizeGestureActive = true
    }

    function endResizeGesture() {
        resizeGestureActive = false
        if (resizeAnchorPending)
            scheduleRowRebuild()
        else
            resizeAnchorKind = ""
    }

    function prepareForResize() {
        if (resizeAnchorPending || (resizeGestureActive && resizeAnchorKind.length > 0)) {
            resizeAnchorPending = true
            return
        }
        wheelScrollAnimation.stop()
        galleryList.cancelFlick()
        galleryList.forceLayout()
        let index = galleryList.indexAt(2, galleryList.contentY + 2)
        if (index < 0)
            index = galleryList.indexAt(2, galleryList.contentY + Math.min(24, galleryList.height / 2))
        if (index < 0 || index >= root.rows.length) {
            resizeAnchorKind = ""
            return
        }
        const row = root.rows[index]
        const delegate = galleryList.itemAtIndex(index)
        resizeAnchorKind = String(row.kind || "")
        resizeAnchorKey = String(row.key || "")
        resizeAnchorPath = row.kind === "cards" && row.items && row.items.length > 0
                ? String(row.items[0].path || "") : ""
        resizeAnchorOffset = delegate ? galleryList.contentY - delegate.y : 0
        resizeAnchorPending = true
    }

    function resizeAnchorIndex() {
        for (let i = 0; i < root.rows.length; ++i) {
            const row = root.rows[i]
            if (resizeAnchorKind === "header" && row.kind === "header"
                    && String(row.key || "") === resizeAnchorKey)
                return i
            if (resizeAnchorKind === "cards" && row.kind === "cards" && row.items) {
                for (let itemIndex = 0; itemIndex < row.items.length; ++itemIndex) {
                    if (String(row.items[itemIndex].path || "") === resizeAnchorPath)
                        return i
                }
            }
        }
        return -1
    }

    function restoreResizeAnchor() {
        const index = resizeAnchorIndex()
        if (index < 0)
            return
        galleryList.positionViewAtIndex(index, ListView.Beginning)
        galleryList.forceLayout()
        const delegate = galleryList.itemAtIndex(index)
        if (!delegate)
            return
        const top = Number(galleryList.originY || 0)
        const bottom = top + Math.max(0, galleryList.contentHeight - galleryList.height)
        galleryList.contentY = Math.max(top, Math.min(bottom,
                                                     delegate.y + resizeAnchorOffset))
    }

    onFilesChanged: {
        dismissHoverPreview()
        rebuildAlbums()
        if (videoMode)
            tmdbManager.pruneMissingFiles()
    }
    onImageModeChanged: rebuildAlbums()
    onDisplayFilesChanged: {
        rowsDirty = true
        scheduleRowRebuild()
    }
    onActiveAlbumPathChanged: {
        dismissHoverPreview()
        resizeAnchorPending = false
        resizeAnchorKind = ""
        albumScrollResetPending = true
        rowsDirty = true
        scheduleRowRebuild()
    }
    onCalculatedColumnsChanged: scheduleRowRebuild()
    onGallerySizeChanged: { dismissHoverPreview(); scheduleRowRebuild() }
    onVisibleChanged: if (!visible) dismissHoverPreview()
    Component.onCompleted: { rebuildAlbums(); scheduleRowRebuild() }

    ListModel { id: rowModel }

    Timer {
        id: rowRebuildTimer
        interval: 0
        repeat: false
        onTriggered: {
            const columnsChanged = root.layoutColumns !== root.calculatedColumns
            root.layoutColumns = root.calculatedColumns
            if (root.rowsDirty)
                root.rebuildDateGroups()
            if (root.rowsDirty || columnsChanged) {
                root.rebuildRows()
                root.rowsDirty = false
            }
            // Finish geometry and anchoring in the same event-loop turn,
            // before the scene graph can draw. No later correction timer.
            galleryList.forceLayout()
            if (root.albumScrollResetPending) {
                galleryList.contentY = galleryList.originY
                root.albumScrollResetPending = false
            } else if (root.resizeAnchorPending)
                root.restoreResizeAnchor()
            root.resizeAnchorPending = false
            if (!root.resizeGestureActive)
                root.resizeAnchorKind = ""
        }
    }

    ListView {
        id: galleryList
        objectName: "categoryGalleryList"
        anchors.fill: parent
        anchors.leftMargin: 6
        anchors.rightMargin: 12
        clip: true
        spacing: root.rowGap
        model: rowModel
        cacheBuffer: Math.max(600, height * 1.5)
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds
        onContentYChanged: { root.dismissHoverPreview(); hoverScrollGuard.restart() }
        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }

        WheelHandler {
            target: null
            blocking: true
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            acceptedModifiers: Qt.ControlModifier
            onWheel: function(event) {
                const dy = event.angleDelta.y !== 0 ? event.angleDelta.y : event.pixelDelta.y
                if (dy !== 0)
                    root.zoomRequested(dy > 0 ? 8 : -8)
                event.accepted = true
            }
        }

        WheelHandler {
            target: null
            blocking: true
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            acceptedModifiers: Qt.NoModifier
            onWheel: function(event) {
                root.scrollFromWheel(event)
                event.accepted = true
            }
        }

        header: Loader {
            width: Math.max(1, galleryList.width - 12)
            active: root.files.length > 0
            sourceComponent: root.imageMode ? albumHeader : videoHighlights
            height: item ? item.implicitHeight : 0
        }

        delegate: Item {
            id: virtualRow
            required property int rowIndex
            readonly property var rowData: root.rows[rowIndex] || ({ kind: "empty", items: [] })
            width: galleryList.width
            height: rowData.kind === "header" ? 58 : root.rowCardHeight + 4

            CategoryDateHeader {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.rightMargin: 12
                anchors.bottomMargin: 5
                visible: virtualRow.rowData.kind === "header"
                title: root.groupTitle(virtualRow.rowData.dateMs)
                count: Number(virtualRow.rowData.count || 0)
                lang: root.lang
                textScale: root.textScale
            }

            Row {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.rightMargin: 12
                anchors.topMargin: 2
                anchors.bottomMargin: 2
                spacing: root.cardGap
                visible: virtualRow.rowData.kind === "cards"

                Repeater {
                    model: virtualRow.rowData.kind === "cards"
                           ? virtualRow.rowData.items.length : 0
                    delegate: MediaGalleryCard {
                        objectName: "galleryMediaCard"
                        required property int index
                        // Keep the card, texture and mask alive when a sync
                        // updates the row's data. An array model destroys every
                        // delegate even if nearly all thumbnails are unchanged.
                        readonly property var modelData: virtualRow.rowData.items
                                ? (virtualRow.rowData.items[index] || null) : null
                        width: root.rowCardWidth
                        height: parent.height
                        itemData: modelData
                        imageSource: root.videoMode ? root.localCardImageSource(modelData) : root.thumbSource(modelData, "dated")
                        artworkMode: root.videoMode && root.localCardUsesArtwork(modelData)
                        videoMode: root.videoMode
                        property int artworkEpoch: root.localArtworkEpoch
                        Component.onCompleted: if (root.videoMode) root.requestLocalArtwork(modelData)
                        onArtworkEpochChanged: if (root.videoMode) root.requestLocalArtwork(modelData)
                        onModelDataChanged: if (root.videoMode) root.requestLocalArtwork(modelData)
                        singleClickOpen: root.singleClickOpen
                        selected: root.itemSelected(itemData)
                        onPressed: function(modifiers) { root.pressItem(itemData, modifiers) }
                        textScale: root.textScale
                        lang: root.lang
                        onPreviewRequested: function(item, anchor) { root.requestHoverPreview(item, anchor) }
                        onPreviewDismissed: function(anchor) { root.dismissHoverPreview(anchor) }
                        onActivated: root.activate(itemData)
                        onBrowseRequested: function(location) { root.browseRequested(location) }
                    }
                }
            }
        }

        footer: Item { width: 1; height: 24 }
    }

    Component {
        id: albumHeader
        Column {
            width: Math.max(1, galleryList.width - 12)
            spacing: 10
            bottomPadding: 22
            RowLayout {
                width: parent.width
                height: 28
                spacing: 10
                Text {
                    text: root.lang.language === "tr" ? "Albümler" : "Albums"
                    color: AppTheme.text
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                }
                Text {
                    text: root.albums.length + (root.lang.language === "tr" ? " klasör · Klasörüne göre keşfet" : " folders · Explore by folder")
                    color: AppTheme.textMuted
                    font.pixelSize: 10
                }
                Item { Layout.fillWidth: true }
                GButton {
                    objectName: "showAllGalleryImages"
                    visible: root.activeAlbumPath.length > 0
                    text: root.lang.language === "tr" ? "Tüm görseller" : "All images"
                    onClicked: root.selectAlbum("")
                    background: Rectangle { radius: 8; color: parent.hovered ? AppTheme.surfaceHover : AppTheme.surface; border.color: AppTheme.border }
                }
                GButton {
                    text: ""
                    display: AbstractButton.IconOnly
                    icon.source: AppTheme.icon("chevron-left.svg")
                    icon.width: 16; icon.height: 16
                    visible: albumStrip.contentWidth > albumStrip.width
                    enabled: albumStrip.contentX > 0
                    onClicked: albumStrip.scrollPage(-1)
                    background: Rectangle { radius: 8; color: parent.hovered ? AppTheme.surfaceHover : AppTheme.surface; border.color: AppTheme.border }
                }
                GButton {
                    text: ""
                    display: AbstractButton.IconOnly
                    icon.source: AppTheme.icon("chevron-right.svg")
                    icon.width: 16; icon.height: 16
                    visible: albumStrip.contentWidth > albumStrip.width
                    enabled: albumStrip.contentX < albumStrip.contentWidth - albumStrip.width
                    onClicked: albumStrip.scrollPage(1)
                    background: Rectangle { radius: 8; color: parent.hovered ? AppTheme.surfaceHover : AppTheme.surface; border.color: AppTheme.border }
                }
            }
            ListView {
                id: albumStrip
                objectName: "galleryAlbumStrip"
                width: parent.width
                height: 164
                orientation: ListView.Horizontal
                spacing: 12
                model: root.albums
                clip: true
                reuseItems: true
                cacheBuffer: 0
                boundsBehavior: Flickable.StopAtBounds
                onContentXChanged: { root.dismissHoverPreview(); hoverScrollGuard.restart() }
                onDraggingChanged: if (dragging) albumScrollAnimation.stop()
                onFlickStarted: albumScrollAnimation.stop()
                onWidthChanged: albumScrollAnimation.stop()
                onModelChanged: albumScrollAnimation.stop()
                function scrollPage(direction) {
                    const base = albumScrollAnimation.running ? albumScrollAnimation.to : contentX
                    albumScrollAnimation.stop()
                    cancelFlick()
                    albumScrollAnimation.from = contentX
                    albumScrollAnimation.to = Math.max(0, Math.min(Math.max(0, contentWidth - width),
                                                                  base + direction * width * 0.8))
                    albumScrollAnimation.start()
                }
                NumberAnimation {
                    id: albumScrollAnimation
                    target: albumStrip
                    property: "contentX"
                    duration: 280
                    easing.type: Easing.OutCubic
                }
                delegate: MediaAlbumCard {
                    objectName: "galleryAlbumCard"
                    required property var modelData
                    width: Math.max(190, Math.min(270, (albumStrip.width - 36) / 4))
                    height: 150
                    album: modelData
                    lang: root.lang
                    selected: root.activeAlbumPath === modelData.path
                    onActivated: root.selectAlbum(album.path)
                    onPreviewRequested: function(item, anchor) { root.requestHoverPreview(item, anchor) }
                    onPreviewDismissed: function(anchor) { root.dismissHoverPreview(anchor) }
                }
                ScrollBar.horizontal: ScrollBar {
                    policy: ScrollBar.AsNeeded
                    onPressedChanged: if (pressed) albumScrollAnimation.stop()
                }
            }
            Text {
                visible: root.activeAlbumPath.length > 0
                width: parent.width
                text: root.activeAlbumPath + " · " + root.displayFiles.length
                      + (root.lang.language === "tr" ? " görsel" : " images")
                elide: Text.ElideMiddle
                color: AppTheme.textMuted
                font.pixelSize: 11
            }
        }
    }
    Component {
        id: videoHighlights
        Column {
            width: Math.max(1, galleryList.width - 12)
            spacing: 10
            bottomPadding: 22
            visible: root.files.length > 0
            height: visible ? implicitHeight : 0

            Column {
                width: parent.width
                spacing: 10

                RowLayout {
                    width: parent.width
                    height: 30
                    spacing: 8
                    Text {
                        text: root.lang.language === "tr" ? "Öne Çıkanlar" : "Highlights"
                        color: AppTheme.text
                        font.pixelSize: Math.round(15 * root.textScale)
                        font.weight: Font.DemiBold
                    }
                    Rectangle {
                        Layout.preferredWidth: 5
                        Layout.preferredHeight: 5
                        Layout.alignment: Qt.AlignVCenter
                        radius: 3
                        color: AppTheme.accent
                    }
                    Text {
                        Layout.alignment: Qt.AlignVCenter
                        text: root.lang.language === "tr"
                              ? "En son eklenenlerden otomatik kolaj"
                              : "Automatic collage from recent media"
                        color: AppTheme.textMuted
                        font.pixelSize: Math.round(9 * root.textScale)
                    }
                    Item { Layout.fillWidth: true }
                }

                Row {
                    id: collage
                    width: parent.width
                    height: Math.min(360, Math.max(210, width * 0.24 * Math.min(1.22, 0.96 + root.galleryScale * 0.12)))
                    spacing: 12

                    MediaGalleryCard {
                        id: heroCard
                        width: Math.max(220, collage.width * 0.56)
                        height: collage.height
                        itemData: root.files.length > 0 ? root.files[0] : null
                        imageSource: itemData
                                     ? (root.videoMode ? root.localCardImageSource(itemData)
                                                       : root.thumbSource(itemData, "hero"))
                                     : ""
                        artworkMode: root.videoMode && itemData && root.localCardUsesArtwork(itemData)
                        videoMode: root.videoMode
                        property int artworkEpoch: root.localArtworkEpoch
                        Component.onCompleted: if (root.videoMode && itemData) root.requestLocalArtwork(itemData)
                        onArtworkEpochChanged: if (root.videoMode && itemData) root.requestLocalArtwork(itemData)
                        onItemDataChanged: if (root.videoMode && itemData) root.requestLocalArtwork(itemData)
                        singleClickOpen: root.singleClickOpen
                        selected: root.itemSelected(itemData)
                        onPressed: function(modifiers) { root.pressItem(itemData, modifiers) }
                        featured: true
                        textScale: root.textScale
                        lang: root.lang
                        onPreviewRequested: function(item, anchor) { root.requestHoverPreview(item, anchor) }
                        onPreviewDismissed: function(anchor) { root.dismissHoverPreview(anchor) }
                        onActivated: root.activate(itemData)
                        onBrowseRequested: function(location) { root.browseRequested(location) }
                    }

                    Grid {
                        width: Math.max(0, collage.width - collage.children[0].width - collage.spacing)
                        height: collage.height
                        columns: 2
                        rows: 2
                        rowSpacing: 12
                        columnSpacing: 12

                        Repeater {
                            model: 4
                            delegate: MediaGalleryCard {
                                required property int index
                                width: (parent.width - parent.columnSpacing) / 2
                                height: (parent.height - parent.rowSpacing) / 2
                                itemData: root.files.length > index + 1 ? root.files[index + 1] : null
                                imageSource: itemData
                                             ? (root.videoMode ? root.localCardImageSource(itemData)
                                                               : root.thumbSource(itemData, "hero-mini"))
                                             : ""
                                artworkMode: root.videoMode && itemData && root.localCardUsesArtwork(itemData)
                                videoMode: root.videoMode
                                property int artworkEpoch: root.localArtworkEpoch
                                Component.onCompleted: if (root.videoMode && itemData) root.requestLocalArtwork(itemData)
                                onArtworkEpochChanged: if (root.videoMode && itemData) root.requestLocalArtwork(itemData)
                                onItemDataChanged: if (root.videoMode && itemData) root.requestLocalArtwork(itemData)
                                singleClickOpen: root.singleClickOpen
                                selected: root.itemSelected(itemData)
                                onPressed: function(modifiers) { root.pressItem(itemData, modifiers) }
                                compact: true
                                textScale: root.textScale
                                lang: root.lang
                                visible: itemData !== null
                                onPreviewRequested: function(item, anchor) { root.requestHoverPreview(item, anchor) }
                                onPreviewDismissed: function(anchor) { root.dismissHoverPreview(anchor) }
                                onActivated: root.activate(itemData)
                                onBrowseRequested: function(location) { root.browseRequested(location) }
                            }
                        }
                    }
                }
            }
        }

    }

    // Scrolling moves delegates beneath a stationary pointer and can emit
    // synthetic hover entries. Wait for fresh pointer movement after settling.
    Timer { id: hoverScrollGuard; interval: 180 }
    Timer {
        id: hoverPreviewDelay
        interval: root.videoMode ? tmdbManager.hoverDelayMs : imageViewSettings.hoverDelayMs
        onTriggered: root.showHoverPreview()
    }
    Timer {
        id: hoverPreviewCloseTimer
        interval: (root.videoMode ? tmdbManager.hoverAnimationsEnabled : imageViewSettings.hoverAnimationsEnabled) ? 155 : 1
        repeat: false
        onTriggered: {
            root.hoverPreviewFadingOut = false
            root.hoverPreviewItem = null
            if (root.videoMode)
                root.tmdbInfo = ({ state: "loading" })
            if (root.hoverPreviewPendingItem && root.hoverPreviewAnchor
                    && (!root.videoMode || tmdbManager.hoverEnabled))
                hoverPreviewDelay.restart()
        }
    }
    Timer {
        id: localArtworkPump
        interval: 0
        repeat: false
        onTriggered: root.pumpLocalArtwork()
    }

    Item {
        id: hoverPreview
        objectName: "galleryHoverPreview"
        z: 100
        visible: root.visible && (root.hoverPreviewShowing || root.hoverPreviewFadingOut) && root.hoverPreviewItem !== null
        opacity: root.hoverPreviewShowing ? 1.0 : 0.0
        scale: root.hoverPreviewShowing ? 1.0 : 0.978
        transformOrigin: Item.Center
        Behavior on opacity { NumberAnimation { duration: (root.videoMode ? tmdbManager.hoverAnimationsEnabled : imageViewSettings.hoverAnimationsEnabled) ? 150 : 0; easing.type: Easing.OutCubic } }
        Behavior on scale { NumberAnimation { duration: (root.videoMode ? tmdbManager.hoverAnimationsEnabled : imageViewSettings.hoverAnimationsEnabled) ? 155 : 0; easing.type: Easing.OutCubic } }
        width: root.videoMode
               ? Math.max(1, Math.min(root.width - 24, Math.max(540, Math.min(700, root.width * 0.62))))
               : Math.max(1, Math.min(root.width - 24, Math.max(320, Math.min(560, root.width * 0.53))))
        height: root.videoMode
                ? Math.max(1, Math.min(root.height - 24, 370))
                : Math.max(1, Math.min(root.height - 24, 480, imageHoverPreview.preferredHeight))
        onHeightChanged: if (root.hoverPreviewShowing) root.showHoverPreview()
        onWidthChanged: if (root.hoverPreviewShowing) root.showHoverPreview()

        MediaHoverPreview {
            id: imageHoverPreview
            anchors.fill: parent
            visible: root.imageMode
            itemData: root.hoverPreviewItem
            thumbnailSource: itemData ? root.thumbSource(itemData, "dated") : ""
            locationLabel: itemData ? root.formatSize(itemData.size) + " · " + root.parentPath(itemData) : ""
        }

        TmdbHoverPreview {
            anchors.fill: parent
            visible: root.videoMode
            shown: true
            animationsEnabled: false
            itemData: root.hoverPreviewItem
            metadata: root.tmdbInfo
            lang: root.lang
        }
    }

    Connections {
        target: tmdbManager
        function onPreferredLanguageChanged() {
            if (root.videoMode)
                root.resetLocalArtwork(tmdbManager.localArtworkEnabled)
            if (root.videoMode && root.hoverPreviewShowing && root.hoverPreviewItem) {
                const sourcePath = String(root.hoverPreviewItem.path || root.hoverPreviewItem.url || "")
                root.tmdbInfo = tmdbManager.lookup(sourcePath, String(root.hoverPreviewItem.name || ""), String(tmdbManager.preferredLanguage || "tr"))
            }
        }
        function onHoverEnabledChanged() {
            if (!tmdbManager.hoverEnabled)
                root.dismissHoverPreview()
        }
        function onLocalArtworkEnabledChanged() {
            root.resetLocalArtwork(tmdbManager.localArtworkEnabled)
        }
    }

    Shortcut {
        sequence: "Escape"
        enabled: hoverPreview.visible
        onActivated: root.dismissHoverPreview()
    }

    GModalPopup {
        id: imageSettingsPopup
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(580, Math.max(400, root.width - 40))
        height: Math.min(620, Math.max(440, root.height - 36))
        padding: 0
        onOpened: {
            imageRootsEditor.reload()
            imageHoverEnabledCheck.checked = imageViewSettings.hoverEnabled
            imageHoverAnimationCheck.checked = imageViewSettings.hoverAnimationsEnabled
            imageHoverDelayInput.currentIndex = Math.max(0, imageHoverDelayInput.valueModel.indexOf(imageViewSettings.hoverDelayMs))
            imageSlideshowInput.currentIndex = Math.max(0, imageSlideshowInput.valueModel.indexOf(imageViewSettings.slideshowIntervalMs))
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.topMargin: 22
                Layout.bottomMargin: 14
                spacing: 7
                Text {
                    Layout.fillWidth: true
                    text: root.lang.language === "tr" ? "Resim Ayarları" : "Image Settings"
                    color: AppTheme.text
                    font.pixelSize: 20
                    font.weight: Font.Bold
                }
                Text {
                    Layout.fillWidth: true
                    text: root.lang.language === "tr"
                          ? "Resim kütüphanesi, hover önizlemesi ve slayt davranışını buradan yönetebilirsin."
                          : "Manage the image library, hover preview and slideshow behavior here."
                    color: AppTheme.textMuted
                    font.pixelSize: 10
                    wrapMode: Text.Wrap
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: AppTheme.border }

            ScrollView {
                id: imageSettingsScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ScrollBar.vertical.policy: ScrollBar.AsNeeded

                ColumnLayout {
                    width: Math.max(1, imageSettingsScroll.availableWidth)
                    spacing: 12
                    Item { Layout.preferredHeight: 4 }

                    MediaRootsEditor {
                        id: imageRootsEditor
                        Layout.fillWidth: true
                        Layout.leftMargin: 24
                        Layout.rightMargin: 24
                        contentIndexModel: root.contentIndexModel
                        categoryKey: "images"
                        lang: root.lang
                        globalSingleClickOpen: AppTheme.singleClickOpen
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.leftMargin: 24
                        Layout.rightMargin: 24
                        Layout.preferredHeight: 1
                        color: AppTheme.border
                    }

                    Text {
                        Layout.fillWidth: true
                        Layout.leftMargin: 24
                        Layout.rightMargin: 24
                        text: root.lang.language === "tr" ? "Önizleme ve slayt" : "Preview and slideshow"
                        color: AppTheme.text
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 24
                        Layout.rightMargin: 24
                        columns: imageSettingsScroll.availableWidth >= 480 ? 2 : 1
                        columnSpacing: 18
                        rowSpacing: 4
                        GCheckBox {
                            id: imageHoverEnabledCheck
                            Layout.fillWidth: true
                            text: root.lang.language === "tr" ? "Hover önizlemesi" : "Hover preview"
                        }
                        GCheckBox {
                            id: imageHoverAnimationCheck
                            Layout.fillWidth: true
                            text: root.lang.language === "tr" ? "Hover animasyonları" : "Hover animations"
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 24
                        Layout.rightMargin: 24
                        columns: 2
                        columnSpacing: 18
                        rowSpacing: 10
                        Text {
                            Layout.fillWidth: true
                            text: root.lang.language === "tr" ? "Hover gecikmesi" : "Hover delay"
                            color: AppTheme.textMuted
                            font.pixelSize: 10
                        }
                        GComboBox {
                            id: imageHoverDelayInput
                            property var valueModel: [200, 420, 600, 800]
                            Layout.fillWidth: true
                            model: ["200 ms", "420 ms", "600 ms", "800 ms"]
                        }
                        Text {
                            Layout.fillWidth: true
                            text: root.lang.language === "tr" ? "Slayt aralığı" : "Slideshow interval"
                            color: AppTheme.textMuted
                            font.pixelSize: 10
                        }
                        GComboBox {
                            id: imageSlideshowInput
                            property var valueModel: [2500, 3500, 5000, 8000]
                            Layout.fillWidth: true
                            model: ["2.5 sn", "3.5 sn", "5 sn", "8 sn"]
                        }
                    }
                    Item { Layout.preferredHeight: 10 }
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: AppTheme.border }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 18
                Layout.rightMargin: 18
                Layout.topMargin: 12
                Layout.bottomMargin: 14
                Item { Layout.fillWidth: true }
                GModalButton {
                    text: root.lang.language === "tr" ? "İptal" : "Cancel"
                    onClicked: imageSettingsPopup.close()
                }
                GModalButton {
                    primary: true
                    text: root.lang.language === "tr" ? "Kaydet" : "Save"
                    onClicked: {
                        imageViewSettings.hoverEnabled = imageHoverEnabledCheck.checked
                        imageViewSettings.hoverAnimationsEnabled = imageHoverAnimationCheck.checked
                        imageViewSettings.hoverDelayMs = imageHoverDelayInput.valueModel[Math.max(0, imageHoverDelayInput.currentIndex)]
                        imageViewSettings.slideshowIntervalMs = imageSlideshowInput.valueModel[Math.max(0, imageSlideshowInput.currentIndex)]
                        imageRootsEditor.save()
                        if (!imageViewSettings.hoverEnabled)
                            root.dismissHoverPreview()
                        imageSettingsPopup.close()
                    }
                }
            }
        }
    }

    GModalPopup {
        id: tmdbSettingsPopup
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(620, Math.max(420, root.width - 40))
        height: Math.min(660, Math.max(460, root.height - 36))
        padding: 0
        onOpened: {
            videoRootsEditor.reload()
            tmdbTokenInput.text = tmdbManager.apiToken
            hoverEnabledCheck.checked = tmdbManager.hoverEnabled
            hoverAnimationCheck.checked = tmdbManager.hoverAnimationsEnabled
            dlnaArtworkCheck.checked = tmdbManager.dlnaArtworkEnabled
            localArtworkCheck.checked = tmdbManager.localArtworkEnabled
            hoverDelayInput.currentIndex = Math.max(0, hoverDelayInput.valueModel.indexOf(tmdbManager.hoverDelayMs))
            artworkConcurrencyInput.currentIndex = Math.max(0, artworkConcurrencyInput.valueModel.indexOf(tmdbManager.artworkConcurrency))
            const languageIndex = tmdbLanguageInput.model.indexOf(String(tmdbManager.preferredLanguage || "tr"))
            tmdbLanguageInput.currentIndex = languageIndex
            if (languageIndex < 0)
                tmdbLanguageInput.editText = String(tmdbManager.preferredLanguage || "tr")
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // Header stays fixed while the settings body scrolls.
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.topMargin: 22
                Layout.bottomMargin: 14
                spacing: 7

                Text {
                    Layout.fillWidth: true
                    text: root.lang.language === "tr" ? "Video Ayarları" : "Video Settings"
                    color: AppTheme.text
                    font.pixelSize: 20
                    font.weight: Font.Bold
                }
                Text {
                    Layout.fillWidth: true
                    text: root.lang.language === "tr"
                          ? "TMDB eşleştirme, hover kartı ve DLNA video görünümünü buradan yönetebilirsin."
                          : "Manage TMDB matching, hover cards and the DLNA video view here."
                    color: AppTheme.textMuted
                    font.pixelSize: 10
                    wrapMode: Text.Wrap
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: AppTheme.border
            }

            ScrollView {
                id: videoSettingsScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ScrollBar.vertical.policy: ScrollBar.AsNeeded

                ColumnLayout {
                    width: Math.max(1, videoSettingsScroll.availableWidth)
                    spacing: 12

                    Item { Layout.preferredHeight: 2 }

                    MediaRootsEditor {
                        id: videoRootsEditor
                        Layout.fillWidth: true
                        Layout.leftMargin: 24
                        Layout.rightMargin: 24
                        contentIndexModel: root.contentIndexModel
                        categoryKey: "videos"
                        lang: root.lang
                        globalSingleClickOpen: AppTheme.singleClickOpen
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.leftMargin: 24
                        Layout.rightMargin: 24
                        Layout.preferredHeight: 1
                        color: AppTheme.border
                    }

                    Text {
                        Layout.fillWidth: true
                        Layout.leftMargin: 24
                        Layout.rightMargin: 24
                        text: root.lang.language === "tr" ? "TMDB" : "TMDB"
                        color: AppTheme.text
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }

                    Text {
                        Layout.fillWidth: true
                        Layout.leftMargin: 24
                        Layout.rightMargin: 24
                        text: "API Read Access Token"
                        color: AppTheme.text
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                    }
                    GTextField {
                        id: tmdbTokenInput
                        Layout.fillWidth: true
                        Layout.leftMargin: 24
                        Layout.rightMargin: 24
                        placeholderText: "eyJhbGciOiJIUzI1NiJ9…"
                        echoMode: TextInput.Password
                        selectByMouse: true
                        color: AppTheme.text
                    }

                    Text {
                        Layout.fillWidth: true
                        Layout.leftMargin: 24
                        Layout.rightMargin: 24
                        text: root.lang.language === "tr" ? "TMDB bilgi dili" : "TMDB metadata language"
                        color: AppTheme.text
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                    }
                    GComboBox {
                        id: tmdbLanguageInput
                        Layout.fillWidth: true
                        Layout.leftMargin: 24
                        Layout.rightMargin: 24
                        editable: true
                        model: ["tr", "en", "de", "fr", "es", "it", "pt", "ru", "ar", "ja", "ko", "zh", "nl", "pl", "sv", "da", "fi", "no", "el", "cs", "hu", "ro", "bg", "uk", "he", "id", "th", "vi", "fa"]
                        currentIndex: 0
                    }
                    Text {
                        Layout.fillWidth: true
                        Layout.leftMargin: 24
                        Layout.rightMargin: 24
                        text: root.lang.language === "tr"
                              ? "Varsayılan tr. Backdrop: dil etiketsiz → seçilen dil → en. Logo/poster: seçilen dil → en → dil etiketsiz."
                              : "Default is tr. Backdrop: no-language → selected language → en. Logo/poster: selected language → en → no-language."
                        color: AppTheme.textFaint
                        font.pixelSize: 9
                        wrapMode: Text.Wrap
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.leftMargin: 24
                        Layout.rightMargin: 24
                        Layout.preferredHeight: 1
                        color: AppTheme.border
                    }

                    Text {
                        Layout.fillWidth: true
                        Layout.leftMargin: 24
                        Layout.rightMargin: 24
                        text: root.lang.language === "tr" ? "Hover ve DLNA" : "Hover and DLNA"
                        color: AppTheme.text
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 24
                        Layout.rightMargin: 24
                        columns: videoSettingsScroll.availableWidth >= 500 ? 2 : 1
                        columnSpacing: 18
                        rowSpacing: 4

                        GCheckBox {
                            id: hoverEnabledCheck
                            Layout.fillWidth: true
                            text: root.lang.language === "tr" ? "TMDB hover kartı" : "TMDB hover card"
                            checked: true
                        }
                        GCheckBox {
                            id: hoverAnimationCheck
                            Layout.fillWidth: true
                            text: root.lang.language === "tr" ? "Hover animasyonları" : "Hover animations"
                            checked: true
                        }
                        GCheckBox {
                            id: localArtworkCheck
                            Layout.fillWidth: true
                            Layout.columnSpan: parent.columns
                            text: root.lang.language === "tr" ? "Yerel video kartlarında TMDB artwork" : "TMDB artwork on local video cards"
                            checked: true
                        }
                        GCheckBox {
                            id: dlnaArtworkCheck
                            Layout.fillWidth: true
                            Layout.columnSpan: parent.columns
                            text: root.lang.language === "tr" ? "DLNA kartlarında TMDB artwork" : "TMDB artwork on DLNA cards"
                            checked: true
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 24
                        Layout.rightMargin: 24
                        columns: 2
                        columnSpacing: 18
                        rowSpacing: 10

                        Text {
                            Layout.fillWidth: true
                            text: root.lang.language === "tr" ? "Hover gecikmesi" : "Hover delay"
                            color: AppTheme.textMuted
                            font.pixelSize: 10
                            verticalAlignment: Text.AlignVCenter
                        }
                        GComboBox {
                            id: hoverDelayInput
                            property var valueModel: [200, 360, 500, 700]
                            Layout.fillWidth: true
                            model: ["200 ms", "360 ms", "500 ms", "700 ms"]
                        }

                        Text {
                            Layout.fillWidth: true
                            text: root.lang.language === "tr" ? "Eşzamanlı TMDB artwork" : "Concurrent TMDB artwork"
                            color: AppTheme.textMuted
                            font.pixelSize: 10
                            verticalAlignment: Text.AlignVCenter
                        }
                        GComboBox {
                            id: artworkConcurrencyInput
                            property var valueModel: [2, 4, 6, 8]
                            Layout.fillWidth: true
                            model: ["2", "4", "6", "8"]
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        Layout.leftMargin: 24
                        Layout.rightMargin: 24
                        text: root.lang.language === "tr"
                              ? "6 eşzamanlı istek varsayılandır. Cache dolduktan sonra tekrar TMDB sorgusu yapılmaz."
                              : "6 concurrent requests is the default. Cached entries are not requested from TMDB again."
                        color: AppTheme.textFaint
                        font.pixelSize: 9
                        wrapMode: Text.Wrap
                    }
                    Text {
                        Layout.fillWidth: true
                        Layout.leftMargin: 24
                        Layout.rightMargin: 24
                        text: (root.lang.language === "tr" ? "Önbellek: " : "Cache: ") + tmdbManager.cacheDirectory
                        color: AppTheme.textFaint
                        font.pixelSize: 9
                        elide: Text.ElideMiddle
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.leftMargin: 24
                        Layout.rightMargin: 24
                        Layout.preferredHeight: 1
                        color: AppTheme.border
                    }

                    Text {
                        Layout.fillWidth: true
                        Layout.leftMargin: 24
                        Layout.rightMargin: 24
                        text: root.lang.language === "tr"
                              ? "Bu ürün TMDB API'sini kullanır ancak TMDB tarafından desteklenmez veya onaylanmaz."
                              : "This product uses the TMDB API but is not endorsed or certified by TMDB."
                        color: AppTheme.textMuted
                        font.pixelSize: 9
                        wrapMode: Text.Wrap
                    }

                    Item { Layout.preferredHeight: 10 }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: AppTheme.border
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 18
                Layout.rightMargin: 18
                Layout.topMargin: 12
                Layout.bottomMargin: 14
                spacing: 8

                GModalButton {
                    text: root.lang.language === "tr" ? "Önbelleği temizle" : "Clear cache"
                    onClicked: { tmdbManager.clearCache(); root.tmdbInfo = ({ state: "loading" }); root.resetLocalArtwork(tmdbManager.localArtworkEnabled) }
                }
                Item { Layout.fillWidth: true }
                GModalButton {
                    text: root.lang.language === "tr" ? "İptal" : "Cancel"
                    onClicked: tmdbSettingsPopup.close()
                }
                GModalButton {
                    primary: true
                    text: root.lang.language === "tr" ? "Kaydet" : "Save"
                    onClicked: {
                        tmdbManager.apiToken = tmdbTokenInput.text.trim()
                        tmdbManager.preferredLanguage = tmdbLanguageInput.currentText.trim().toLowerCase()
                        tmdbManager.hoverEnabled = hoverEnabledCheck.checked
                        tmdbManager.hoverAnimationsEnabled = hoverAnimationCheck.checked
                        tmdbManager.localArtworkEnabled = localArtworkCheck.checked
                        tmdbManager.dlnaArtworkEnabled = dlnaArtworkCheck.checked
                        tmdbManager.hoverDelayMs = hoverDelayInput.valueModel[Math.max(0, hoverDelayInput.currentIndex)]
                        tmdbManager.artworkConcurrency = artworkConcurrencyInput.valueModel[Math.max(0, artworkConcurrencyInput.currentIndex)]
                        videoRootsEditor.save()
                        tmdbSettingsPopup.close()
                        if (root.videoMode && root.hoverPreviewShowing && root.hoverPreviewItem) {
                            const sourcePath = String(root.hoverPreviewItem.path || root.hoverPreviewItem.url || "")
                            root.tmdbInfo = tmdbManager.lookup(sourcePath, String(root.hoverPreviewItem.name || ""), String(tmdbManager.preferredLanguage || "tr"))
                        }
                    }
                }
            }
        }
    }

    PhotoViewer {
        id: viewer
        objectName: "galleryPhotoViewer"
        files: root.imageMode ? root.displayFiles : []
        lang: root.lang
        hostWindow: root.hostWindow
        slideshowIntervalMs: imageViewSettings.slideshowIntervalMs
        onBrowseRequested: function(location) { root.browseRequested(location) }
    }

    VideoViewer {
        id: videoViewer
        objectName: "galleryVideoViewer"
        files: root.videoMode ? root.displayFiles : []
        lang: root.lang
        hostWindow: root.hostWindow
        metadataManager: tmdbManager
        metadataForItem: function(item) {
            if (!item) return null
            return root.localArtworkByPath[String(item.path || "")] || null
        }
        onBrowseRequested: function(location) { root.browseRequested(location) }
    }
}
