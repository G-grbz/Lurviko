import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import GFile.App
import GFile.Backend

Item {
    id: root
    required property var manager
    required property var lang
    property bool singleClickOpen: false
    // Shares the category:/videos zoom level with the local media gallery.
    // The toolbar slider therefore resizes DLNA cards as well.
    property int gallerySize: 168
    readonly property real cardScale: Math.max(0.62, Math.min(2.15, gallerySize / 168.0))
    readonly property real desiredCellWidth: Math.max(150, Math.min(430, gallerySize * 1.24))
    readonly property real desiredCellHeight: Math.max(122, Math.min(330, gallerySize * 0.98))
    property string selectedObjectId: ""
    property var hostWindow: null
    property string activeSection: "movies"
    property var navigationStack: []
    // DLNA lives inside category:/videos, so it needs its own virtual browser
    // history. The outer BrowsePage only sees category:/videos and must not
    // consume Back/Forward while the user is traversing media containers.
    property var navigationBackStack: []
    property var navigationForwardStack: []
    readonly property bool canNavigateBack: navigationBackStack.length > 0
    readonly property bool canNavigateForward: navigationForwardStack.length > 0
    property var hoverCandidate: null
    property var hoverItem: null
    property string hoverSource: ""
    property var tmdbInfo: ({ state: "loading" })
    property bool hoverShowing: false
    property bool hoverFadingOut: false
    property int wheelBurstCount: 0
    property int wheelBurstDirection: 0
    property double wheelBurstLastMs: 0
    property var artworkByObjectId: ({})
    property var artworkSourceMap: ({})
    property var artworkRequested: ({})
    property var artworkQueue: []
    property var artworkQueued: ({})
    property var artworkInflightSources: ({})
    property int artworkInflightCount: 0
    readonly property int artworkMaxInflight: Math.max(1, Number(tmdbManager.artworkConcurrency || 6))
    property string searchText: ""
    property string sortMode: "name"
    property bool sortAscending: true
    property var viewerFiles: []

    readonly property var currentEntries: manager && manager.displayEntries ? manager.displayEntries : []

    function syncViewOptions() {
        if (!manager)
            return
        manager.setViewOptions(String(root.searchText || ""),
                               String(root.sortMode || "name"),
                               !!root.sortAscending)
    }

    onSearchTextChanged: syncViewOptions()
    onSortModeChanged: syncViewOptions()
    onSortAscendingChanged: syncViewOptions()

    readonly property int visibleEntryCount: currentEntries.length
    function buildPlayableEntries() {
        const out = []
        const list = root.currentEntries || []
        for (let i = 0; i < list.length; ++i) {
            const item = list[i]
            if (!item || !item.playable || !String(item.url || "").length)
                continue
            out.push({
                name: String(item.title || ""),
                url: String(item.url || ""),
                path: "dlna://" + encodeURIComponent(root.manager.selectedServerId || "server") + "/" + encodeURIComponent(String(item.objectId || i)),
                tmdbSource: root.tmdbSourceFor(item),
                objectId: String(item.objectId || ""),
                tmdbId: Number(item.tmdbId || 0),
                year: Number(item.year || 0),
                mediaType: String(item.sourceMediaType || (root.activeSection === "shows" ? "tv" : "movie")),
                sourceMediaType: String(item.sourceMediaType || ""),
                size: Number(item.size || 0),
                modifiedMs: 0,
                parentPath: "",
                suffix: "",
                thumbnailUrl: String(item.artUrl || "")
            })
        }
        return out
    }

    function sectionLabel(key) {
        if (key === "movies") return lang.language === "tr" ? "Filmler" : "Movies"
        if (key === "shows") return lang.language === "tr" ? "Diziler" : "TV Shows"
        if (key === "music") return lang.language === "tr" ? "Müzik" : "Music"
        return lang.language === "tr" ? "Tümü" : "All"
    }

    function resetNavigationForSection() {
        if (!manager || !manager.selectedServerId)
            return
        if (activeSection === "all") {
            navigationStack = [{ objectId: "0", title: manager.selectedServerName || "DLNA" }]
            return
        }
        const id = String(manager.sectionObjectId(activeSection) || "")
        if (!id.length)
            return
        navigationStack = [{ objectId: id, title: String(manager.sectionTitle(activeSection) || sectionLabel(activeSection)) }]
    }

    function navigationSnapshot() {
        return {
            section: String(activeSection || "movies"),
            stack: navigationStack ? navigationStack.slice() : []
        }
    }

    function pushVirtualHistory() {
        if (!manager || !manager.selectedServerId || navigationStack.length === 0)
            return
        navigationBackStack = navigationBackStack.concat([navigationSnapshot()])
        navigationForwardStack = []
    }

    function restoreNavigationSnapshot(snapshot) {
        if (!snapshot || !manager || !manager.selectedServerId)
            return false
        dismissHover()
        activeSection = String(snapshot.section || "movies")
        const restored = snapshot.stack ? snapshot.stack.slice() : []
        navigationStack = restored
        if (restored.length === 0) {
            if (activeSection === "all") {
                navigationStack = [{ objectId: "0", title: manager.selectedServerName || "DLNA" }]
                manager.browseRoot()
            } else {
                resetNavigationForSection()
                manager.openSection(activeSection)
            }
            return true
        }
        manager.browse(String(restored[restored.length - 1].objectId))
        return true
    }

    function chooseSection(key) {
        if (String(key) === String(activeSection) && navigationStack.length > 0)
            return
        dismissHover()
        pushVirtualHistory()
        activeSection = key
        navigationStack = []
        if (!manager || !manager.selectedServerId)
            return
        if (key === "all") {
            navigationStack = [{ objectId: "0", title: manager.selectedServerName || "DLNA" }]
            manager.browseRoot()
        } else {
            resetNavigationForSection()
            manager.openSection(key)
        }
    }

    function openContainer(item) {
        if (!item || !String(item.objectId || "").length)
            return
        dismissHover()
        pushVirtualHistory()
        if (activeSection === "shows" && navigationStack.length === 1)
            requestSeriesArtwork(item)
        navigationStack = navigationStack.concat([{
            objectId: String(item.objectId),
            title: String(item.title || ""),
            year: Number(item.year || 0),
            kind: String(item.kind || "container"),
            tmdbId: Number(item.tmdbId || 0),
            sourceMediaType: String(item.sourceMediaType || "")
        }])
        manager.browse(String(item.objectId))
    }

    function navigateBack() {
        if (!canNavigateBack)
            return false
        const back = navigationBackStack.slice()
        const target = back.pop()
        navigationBackStack = back
        navigationForwardStack = navigationForwardStack.concat([navigationSnapshot()])
        return restoreNavigationSnapshot(target)
    }

    function navigateForward() {
        if (!canNavigateForward)
            return false
        const forward = navigationForwardStack.slice()
        const target = forward.pop()
        navigationForwardStack = forward
        navigationBackStack = navigationBackStack.concat([navigationSnapshot()])
        return restoreNavigationSnapshot(target)
    }

    function navigateUp() {
        if (!manager || !manager.selectedServerId)
            return false
        // Inside a container, Up means its DLNA parent. At a section root,
        // the BrowserPane handles Up by returning to the local Videos source.
        if (navigationStack.length <= 1)
            return false
        dismissHover()
        pushVirtualHistory()
        const next = navigationStack.slice(0, navigationStack.length - 1)
        navigationStack = next
        manager.browse(String(next[next.length - 1].objectId))
        return true
    }

    function navigateToBreadcrumb(index) {
        if (index < 0 || index >= navigationStack.length || index === navigationStack.length - 1)
            return
        dismissHover()
        pushVirtualHistory()
        const next = navigationStack.slice(0, index + 1)
        navigationStack = next
        manager.browse(String(next[next.length - 1].objectId))
    }


    function artworkKey(item) {
        if (!item)
            return ""
        const id = String(item.objectId || "")
        return id.length ? id : String(item.title || "")
    }

    function seriesDisplayName(item) {
        if (!item)
            return ""
        const year = Number(item.year || 0)
        return String(item.title || "") + (year > 0 ? " (" + year + ")" : "") + " S01"
    }

    function seasonNumberFromTitle(title) {
        const text = String(title || "")
        let match = text.match(/(?:^|[\s._-])S\s*0*(\d{1,2})(?:$|[\s._-])/i)
        if (!match)
            match = text.match(/(?:season|sezon)\s*0*(\d{1,2})/i)
        if (match)
            return Number(match[1])
        if (/\b(?:specials?|özel(?:ler)?)\b/i.test(text))
            return 0
        return -1
    }

    function artworkSourceFor(item, kind, extra) {
        if (!item || !manager)
            return ""
        return "dlna://" + encodeURIComponent(String(manager.selectedServerId || "server"))
                + "/artwork/" + kind + "/"
                + encodeURIComponent(String(extra || artworkKey(item)))
    }

    function rememberArtworkRequest(source, item, kind) {
        if (!source.length || !item)
            return
        const sourceMap = Object.assign({}, artworkSourceMap)
        sourceMap[source] = ({ objectId: artworkKey(item), kind: kind, item: item })
        artworkSourceMap = sourceMap
        const requested = Object.assign({}, artworkRequested)
        requested[source] = true
        artworkRequested = requested
    }

    function storeArtwork(item, metadata) {
        if (!item || !metadata)
            return
        const key = artworkKey(item)
        if (!key.length)
            return
        const copy = Object.assign({}, artworkByObjectId)
        copy[key] = metadata
        artworkByObjectId = copy
    }

    function seriesMetadata() {
        const series = seriesRootEntry()
        if (!series)
            return null
        return artworkByObjectId[artworkKey(series)] || null
    }

    function artworkUrlFor(item) {
        if (!item)
            return ""
        // Prefer G-File's cached TMDB artwork over low-resolution DLNA
        // albumArtURI thumbnails. Native DLNA art remains the instant fallback.
        const metadata = tmdbManager.dlnaArtworkEnabled ? (artworkByObjectId[artworkKey(item)] || null) : null
        if (metadata) {
            const direct = String(metadata.artworkUrl || metadata.backdropUrl || metadata.posterUrl || "")
            if (direct.length)
                return direct
        }
        const nativeArt = String(item.artUrl || "")
        if (nativeArt.length)
            return nativeArt
        if (activeSection === "shows" && navigationStack.length >= 2) {
            const parentMetadata = seriesMetadata()
            if (parentMetadata) {
                const fallback = String(parentMetadata.backdropUrl || parentMetadata.posterUrl || "")
                if (fallback.length)
                    return fallback
            }
        }
        return ""
    }

    function artworkRequestDescriptor(item) {
        if (!item || !tmdbManager.configured || !tmdbManager.dlnaArtworkEnabled)
            return null
        if (activeSection === "movies" && item.playable) {
            const source = tmdbSourceFor(item)
            return source.length ? ({ source: source, kind: "movie", item: item,
                                      title: String(item.title || ""), year: Number(item.year || 0),
                                      mediaType: "movie", tmdbId: Number(item.tmdbId || 0) }) : null
        }
        if (activeSection !== "shows" || String(item.kind || "") !== "container")
            return null
        if (navigationStack.length <= 1) {
            const source = tmdbSourceFor(item)
            return source.length ? ({ source: source, kind: "show", item: item,
                                      title: String(item.title || ""), year: Number(item.year || 0),
                                      mediaType: "tv", tmdbId: Number(item.tmdbId || 0) }) : null
        }
        const seasonNumber = seasonNumberFromTitle(item.title)
        if (seasonNumber < 0)
            return null
        const series = seriesRootEntry()
        if (!series)
            return null
        const seriesInfo = artworkByObjectId[artworkKey(series)] || null
        const seriesId = Number(series.tmdbId || (seriesInfo && seriesInfo.tmdbId ? seriesInfo.tmdbId : 0))
        if (seriesId <= 0) {
            const source = tmdbSourceFor(series)
            return source.length ? ({ source: source, kind: "show", item: series,
                                      title: String(series.title || ""), year: Number(series.year || 0),
                                      mediaType: "tv", tmdbId: Number(series.tmdbId || 0) }) : null
        }
        const source = artworkSourceFor(item, "season",
                                        seriesId + "/" + seasonNumber + "/" + artworkKey(item))
        return source.length
                ? ({ source: source, kind: "season", item: item,
                     seriesId: seriesId, seasonNumber: seasonNumber,
                     seriesTitle: String(series.title || "") })
                : null
    }

    function enqueueArtworkDescriptor(descriptor) {
        if (!descriptor || !descriptor.source || !String(descriptor.source).length)
            return
        const source = String(descriptor.source)
        if (artworkRequested[source] || artworkQueued[source] || artworkInflightSources[source])
            return

        // Disk-cache hits must be instant. Do not make cached artwork wait
        // behind the two-wide network queue every time the user re-enters a
        // large DLNA section. Only real cache misses are queued for TMDB.
        const cached = tmdbManager.cachedLookup(source, String(tmdbManager.preferredLanguage || "tr"))
        const cachedState = String(cached && cached.state ? cached.state : "")
        if (cachedState.length) {
            const requested = Object.assign({}, artworkRequested)
            requested[source] = true
            artworkRequested = requested
            if (cachedState === "ready")
                storeArtwork(descriptor.item, cached)
            return
        }

        const queued = Object.assign({}, artworkQueued)
        queued[source] = true
        artworkQueued = queued
        artworkQueue = artworkQueue.concat([descriptor])
        artworkPump.restart()
    }

    function enqueueCardArtwork(item) {
        enqueueArtworkDescriptor(artworkRequestDescriptor(item))
    }

    function markArtworkInflight(source, value) {
        if (!source || !source.length)
            return
        const copy = Object.assign({}, artworkInflightSources)
        const existed = !!copy[source]
        if (value)
            copy[source] = true
        else
            delete copy[source]
        artworkInflightSources = copy
        if (value && !existed)
            artworkInflightCount += 1
        else if (!value && existed)
            artworkInflightCount = Math.max(0, artworkInflightCount - 1)
    }

    function finishArtworkRequest(source) {
        markArtworkInflight(source, false)
        artworkPump.restart()
    }

    function runArtworkRequest(descriptor) {
        if (!descriptor || !descriptor.item)
            return
        const item = descriptor.item
        const source = String(descriptor.source || "")
        if (!source.length)
            return

        if (!artworkRequested[source])
            rememberArtworkRequest(source, item, String(descriptor.kind || "movie"))

        let result = null
        if (descriptor.kind === "season") {
            result = tmdbManager.lookupSeasonArtwork(
                        source, Number(descriptor.seriesId || 0), Number(descriptor.seasonNumber || 0),
                        String(descriptor.seriesTitle || ""), String(tmdbManager.preferredLanguage || "tr"))
        } else {
            const knownTmdbId = Number(descriptor.tmdbId || item.tmdbId || 0)
            if (knownTmdbId > 0) {
                result = tmdbManager.lookupById(
                            source, knownTmdbId, String(descriptor.title || item.title || ""),
                            Number(descriptor.year || item.year || 0),
                            String(descriptor.mediaType || (descriptor.kind === "show" ? "tv" : "movie")),
                            String(tmdbManager.preferredLanguage || "tr"))
            } else {
                result = tmdbManager.lookupTyped(
                            source, String(descriptor.title || item.title || ""),
                            Number(descriptor.year || item.year || 0),
                            String(descriptor.mediaType || (descriptor.kind === "show" ? "tv" : "movie")),
                            String(tmdbManager.preferredLanguage || "tr"))
            }
        }

        const state = String(result && result.state ? result.state : "")
        if (state === "ready") {
            storeArtwork(item, result)
            return
        }
        if (state === "loading") {
            markArtworkInflight(source, true)
            return
        }
    }

    function pumpArtworkQueue() {
        if (!tmdbManager.dlnaArtworkEnabled)
            return
        // Fill all available network slots in one pass. The older pump started
        // one request every 90 ms and was hard-capped at two inflight jobs,
        // which made large libraries visibly populate in pairs.
        while (artworkInflightCount < artworkMaxInflight && artworkQueue.length > 0) {
            const descriptor = artworkQueue[0]
            artworkQueue = artworkQueue.slice(1)
            const queued = Object.assign({}, artworkQueued)
            delete queued[String(descriptor.source || "")]
            artworkQueued = queued
            runArtworkRequest(descriptor)
        }
    }

    function requestSeriesArtwork(item) {
        if (!item || String(item.kind || "") !== "container" || !tmdbManager.configured || !tmdbManager.dlnaArtworkEnabled)
            return
        const source = tmdbSourceFor(item)
        if (!source.length)
            return
        enqueueArtworkDescriptor(({ source: source, kind: "show", item: item,
                                    title: String(item.title || ""), year: Number(item.year || 0),
                                    mediaType: "tv", tmdbId: Number(item.tmdbId || 0) }))
    }

    function requestSeasonArtwork(item) {
        if (!item || String(item.kind || "") !== "container" || !tmdbManager.configured || !tmdbManager.dlnaArtworkEnabled)
            return
        const seasonNumber = seasonNumberFromTitle(item.title)
        const series = seriesRootEntry()
        if (seasonNumber < 0 || !series)
            return
        const seriesInfo = artworkByObjectId[artworkKey(series)] || null
        const seriesId = Number(series.tmdbId || (seriesInfo && seriesInfo.tmdbId ? seriesInfo.tmdbId : 0))
        if (seriesId <= 0) {
            requestSeriesArtwork(series)
            return
        }
        const source = artworkSourceFor(item, "season",
                                        seriesId + "/" + seasonNumber + "/" + artworkKey(item))
        if (!source.length)
            return
        enqueueArtworkDescriptor(({ source: source, kind: "season", item: item,
                                    seriesId: seriesId, seasonNumber: seasonNumber,
                                    seriesTitle: String(series.title || "") }))
    }

    function mediaDisplayName(item) {
        if (!item)
            return ""
        return titleWithYear(String(item.title || ""), Number(item.year || 0))
    }

    function requestMovieArtwork(item) {
        if (!item || !item.playable || !tmdbManager.configured || !tmdbManager.dlnaArtworkEnabled)
            return
        const source = tmdbSourceFor(item)
        if (!source.length)
            return
        enqueueArtworkDescriptor(({ source: source, kind: "movie", item: item,
                                    title: String(item.title || ""), year: Number(item.year || 0),
                                    mediaType: "movie", tmdbId: Number(item.tmdbId || 0) }))
    }

    function requestCardArtwork(item) {
        enqueueCardArtwork(item)
    }

    function requestCurrentSeasonArtwork() {
        if (activeSection !== "shows" || navigationStack.length < 2)
            return
        const list = currentEntries || []
        for (let i = 0; i < list.length; ++i)
            enqueueCardArtwork(list[i])
    }

    function viewerMetadataFor(item) {
        if (!item)
            return null
        const mediaType = String(item.mediaType || item.sourceMediaType || "").toLowerCase()
        if (root.activeSection === "shows" || mediaType === "tv" || mediaType === "show" || mediaType === "series") {
            const series = root.seriesMetadata()
            if (series && String(series.state || "") === "ready")
                return series
        }
        const direct = root.artworkByObjectId[String(item.objectId || "")] || null
        if (direct && String(direct.state || "") === "ready")
            return direct
        return null
    }

    function viewerIndexFor(item) {
        const url = String(item && item.url ? item.url : "")
        const files = root.viewerFiles || []
        for (let i = 0; i < files.length; ++i) {
            if (String(files[i].url || "") === url)
                return i
        }
        return -1
    }

    function activate(item) {
        if (!item)
            return
        if (String(item.kind || "") === "container") {
            openContainer(item)
            return
        }
        if (!item.playable)
            return
        dismissHover()
        root.viewerFiles = root.buildPlayableEntries()
        const index = viewerIndexFor(item)
        if (index >= 0)
            videoViewer.openAt(index)
    }

    function seriesRootEntry() {
        if (activeSection !== "shows")
            return null
        if (navigationStack.length >= 2)
            return navigationStack[1]
        return null
    }

    function titleWithYear(title, year) {
        const raw = String(title || "").trim()
        const y = Number(year || 0)
        if (y <= 0)
            return raw
        // GiG already publishes clean DLNA titles as "Title (YYYY)".  Do not
        // append the exact same year a second time for hover/footer text.
        const escaped = String(y).replace(/[.*+?^${}()|[\]\\]/g, "\\$&")
        const trailingYear = new RegExp("(?:\\s*[\\(\\[]?" + escaped + "[\\)\\]]?)+\\s*$")
        if (trailingYear.test(raw))
            return raw.replace(trailingYear, " (" + y + ")").trim()
        return raw + " (" + y + ")"
    }

    function tmdbDisplayName(item) {
        if (!item)
            return ""
        if (activeSection === "shows") {
            const series = seriesRootEntry()
            const raw = series ? String(series.title || "") : String(item.title || "")
            const year = Number(series && series.year ? series.year : (item.year || 0))
            // Force the existing TMDB filename parser into TV mode while it
            // still strips the artificial season marker back to the show name.
            return titleWithYear(raw, year) + " S01"
        }
        return titleWithYear(String(item.title || ""), Number(item.year || 0))
    }

    function tmdbSourceFor(item) {
        if (!item)
            return ""
        let objectId = String(item.objectId || "")
        if (activeSection === "shows") {
            const series = seriesRootEntry()
            if (series && String(series.objectId || "").length)
                objectId = String(series.objectId)
        }
        return "dlna://" + encodeURIComponent(String(manager.selectedServerId || "server"))
                + "/" + encodeURIComponent(objectId || String(item.title || "item"))
    }

    function tmdbEligible(item) {
        if (!item || !manager || !manager.selectedServerId)
            return false
        if (activeSection === "movies" || activeSection === "shows")
            return true
        if (activeSection === "all") {
            const c = String(item.className || "").toLowerCase()
            return c.indexOf("video") >= 0
        }
        return false
    }

    function scheduleHover(item) {
        if (!tmdbManager.hoverEnabled || !tmdbEligible(item))
            return
        if (hoverCandidate && String(hoverCandidate.objectId || "") === String(item.objectId || "")
                && (hoverShowing || hoverFadingOut))
            return
        hoverCandidate = item
        hoverTimer.stop()
        if (hoverShowing) {
            hoverShowing = false
            hoverFadingOut = true
            hoverCloseTimer.restart()
        } else if (!hoverFadingOut) {
            hoverTimer.restart()
        }
    }

    function dismissHover() {
        hoverTimer.stop()
        hoverCandidate = null
        if (hoverItem !== null) {
            hoverShowing = false
            hoverFadingOut = true
            hoverCloseTimer.restart()
        } else {
            hoverShowing = false
            hoverFadingOut = false
            hoverSource = ""
            tmdbInfo = ({ state: "loading" })
        }
    }

    function showHover(item) {
        if (hoverFadingOut || !tmdbManager.hoverEnabled || !item || !hoverCandidate
                || String(item.objectId || "") !== String(hoverCandidate.objectId || ""))
            return
        hoverItem = item
        hoverSource = tmdbSourceFor(item)
        let target = item
        let mediaType = "movie"
        if (activeSection === "shows") {
            const series = seriesRootEntry()
            if (series)
                target = series
            mediaType = "tv"
        } else if (activeSection === "all") {
            const cls = String(item.className || "").toLowerCase()
            mediaType = cls.indexOf("movie") >= 0 ? "movie" : "tv"
        }
        const knownTmdbId = Number(target.tmdbId || 0)
        if (knownTmdbId > 0) {
            tmdbInfo = tmdbManager.lookupById(
                        hoverSource, knownTmdbId, String(target.title || ""), Number(target.year || 0), mediaType,
                        String(tmdbManager.preferredLanguage || "tr"))
        } else {
            tmdbInfo = tmdbManager.lookupTyped(
                        hoverSource, String(target.title || ""), Number(target.year || 0), mediaType,
                        String(tmdbManager.preferredLanguage || "tr"))
        }
        hoverShowing = true
    }

    function formatSize(bytes) {
        const n = Number(bytes || 0)
        if (n <= 0) return ""
        if (n < 1024 * 1024) return (n / 1024).toFixed(1) + " KiB"
        if (n < 1024 * 1024 * 1024) return (n / (1024 * 1024)).toFixed(1) + " MiB"
        return (n / (1024 * 1024 * 1024)).toFixed(1) + " GiB"
    }

    function formatDuration(ms) {
        const total = Math.floor(Number(ms || 0) / 1000)
        if (total <= 0) return ""
        const h = Math.floor(total / 3600)
        const m = Math.floor((total % 3600) / 60)
        return h > 0 ? h + "s " + m + "dk" : m + "dk"
    }

    function normalizedWheelDelta(event) {
        const angleY = Number(event.angleDelta.y || 0)
        const pixelY = Number(event.pixelDelta.y || 0)
        if (angleY !== 0) {
            const now = Date.now()
            const direction = angleY > 0 ? 1 : -1
            const gap = wheelBurstLastMs > 0 ? now - wheelBurstLastMs : 9999
            if (direction !== wheelBurstDirection || gap > 300)
                wheelBurstCount = 0
            else
                wheelBurstCount = Math.min(5, wheelBurstCount + 1)
            wheelBurstLastMs = now
            wheelBurstDirection = direction
            const fraction = Math.max(0.125, Math.min(2.0, Math.abs(angleY) / 120.0))
            return direction * AppTheme.wheelScrollStep * fraction * (1.0 + wheelBurstCount * 0.08)
        }
        if (pixelY !== 0) {
            wheelBurstCount = 0
            wheelBurstDirection = 0
            wheelBurstLastMs = 0
            return pixelY * 2.0
        }
        return 0
    }

    function scrollGridFromWheel(event) {
        root.dismissHover()
        const delta = root.normalizedWheelDelta(event)
        if (delta === 0)
            return
        const top = Number(grid.originY || 0)
        const bottom = top + Math.max(0, grid.contentHeight - grid.height)
        const continuing = gridScrollAnimation.running
        const base = continuing ? gridScrollAnimation.to : grid.contentY
        const limit = Math.max(AppTheme.wheelScrollStep, grid.height * 1.8, grid.cellHeight * 6)
        const destination = Math.max(top, Math.min(bottom,
                                Math.max(grid.contentY - limit,
                                         Math.min(grid.contentY + limit, base - delta))))
        gridScrollAnimation.stop()
        if (Math.abs(destination - grid.contentY) < 0.5)
            return
        gridScrollAnimation.from = grid.contentY
        gridScrollAnimation.to = destination
        gridScrollAnimation.start()
    }

    TmdbMetadataManager {
        id: tmdbManager
        onMetadataReady: function(sourcePath, metadata) {
            if (root.hoverShowing && sourcePath === root.hoverSource)
                root.tmdbInfo = metadata

            const request = root.artworkSourceMap[String(sourcePath)] || null
            if (request) {
                root.finishArtworkRequest(String(sourcePath))
                let target = request.item || null
                if (!target) {
                    const entries = root.manager && root.manager.entries ? root.manager.entries : []
                    for (let i = 0; i < entries.length; ++i) {
                        if (root.artworkKey(entries[i]) === String(request.objectId || "")) {
                            target = entries[i]
                            break
                        }
                    }
                }
                if (!target && request.kind === "show") {
                    const series = root.seriesRootEntry()
                    if (series && root.artworkKey(series) === String(request.objectId || ""))
                        target = series
                }
                if (target)
                    root.storeArtwork(target, metadata)

                if (request.kind === "show") {
                    const currentSeries = root.seriesRootEntry()
                    if (currentSeries && root.artworkKey(currentSeries) === String(request.objectId || ""))
                        root.requestCurrentSeasonArtwork()
                }
            }
        }
        onMetadataFailed: function(sourcePath, message) {
            if (root.artworkSourceMap[String(sourcePath)])
                root.finishArtworkRequest(String(sourcePath))
            if (root.hoverShowing && sourcePath === root.hoverSource)
                root.tmdbInfo = ({ state: "error", parsedTitle: root.tmdbDisplayName(root.hoverItem), message: message })
        }
    }

    Connections {
        target: tmdbManager
        function onHoverEnabledChanged() {
            if (!tmdbManager.hoverEnabled)
                root.dismissHover()
        }
        function onDlnaArtworkEnabledChanged() {
            if (!tmdbManager.dlnaArtworkEnabled) {
                root.artworkQueue = []
                root.artworkQueued = ({})
            } else {
                const list = root.currentEntries || []
                for (let i = 0; i < list.length; ++i)
                    root.enqueueCardArtwork(list[i])
            }
        }
        function onArtworkConcurrencyChanged() {
            artworkPump.restart()
        }
    }

    Timer {
        id: artworkPump
        interval: 90
        repeat: false
        onTriggered: root.pumpArtworkQueue()
    }

    Timer {
        id: hoverTimer
        interval: tmdbManager.hoverDelayMs
        repeat: false
        onTriggered: root.showHover(root.hoverCandidate)
    }

    Timer {
        id: hoverCloseTimer
        interval: tmdbManager.hoverAnimationsEnabled ? 155 : 1
        repeat: false
        onTriggered: {
            root.hoverFadingOut = false
            root.hoverItem = null
            root.hoverSource = ""
            root.tmdbInfo = ({ state: "loading" })
            if (root.hoverCandidate && tmdbManager.hoverEnabled)
                hoverTimer.restart()
        }
    }

    Connections {
        target: manager
        function onSelectedServerChanged() {
            root.navigationStack = []
            root.navigationBackStack = []
            root.navigationForwardStack = []
            root.artworkByObjectId = ({})
            root.artworkSourceMap = ({})
            root.artworkRequested = ({})
            root.artworkQueue = []
            root.artworkQueued = ({})
            root.artworkInflightSources = ({})
            root.artworkInflightCount = 0
            root.dismissHover()
            if (manager.selectedServerId)
                manager.openSection(root.activeSection)
        }
        function onRootEntriesChanged() {
            if (!manager.selectedServerId)
                return
            if (root.activeSection !== "all" && manager.hasSection(root.activeSection))
                root.resetNavigationForSection()
        }
        function onBrowseFinished(objectId) {
            if (root.navigationStack.length === 0) {
                if (root.activeSection === "all")
                    root.navigationStack = [{ objectId: "0", title: manager.selectedServerName || "DLNA" }]
                else
                    root.resetNavigationForSection()
            }
            if (root.activeSection === "shows") {
                if (root.navigationStack.length <= 1) {
                    const list = root.currentEntries || []
                    for (let i = 0; i < list.length; ++i)
                        root.enqueueCardArtwork(list[i])
                } else {
                    const series = root.seriesRootEntry()
                    if (series)
                        root.requestSeriesArtwork(series)
                    root.requestCurrentSeasonArtwork()
                }
            }
        }
    }

    Component.onCompleted: {
        root.syncViewOptions()
        manager.discover()
        if (manager.selectedServerId)
            manager.openSection(activeSection)
    }

    Rectangle {
        anchors.fill: parent
        color: "transparent"
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10
        visible: !!manager.selectedServerId

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            spacing: 8

            Rectangle {
                Layout.preferredWidth: Math.min(390, Math.max(270, root.width * 0.38))
                Layout.preferredHeight: 38
                radius: 12
                color: AppTheme.surface
                border.color: AppTheme.border

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 3
                    spacing: 3

                    Repeater {
                        model: ["movies", "shows", "music", "all"]
                        delegate: GToolButton {
                            required property string modelData
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            enabled: modelData === "all" || manager.rootEntries.length === 0 || manager.hasSection(modelData)
                            onClicked: root.chooseSection(modelData)
                            background: Rectangle {
                                radius: 9
                                color: root.activeSection === modelData
                                       ? AppTheme.accentSoft
                                       : (parent.hovered ? AppTheme.surfaceHover : "transparent")
                                border.width: root.activeSection === modelData ? 1 : 0
                                border.color: AppTheme.accentBorder
                            }
                            contentItem: Text {
                                text: root.sectionLabel(modelData)
                                color: parent.enabled
                                       ? (root.activeSection === modelData ? AppTheme.accent : AppTheme.text)
                                       : AppTheme.textFaint
                                font.pixelSize: 10
                                font.weight: root.activeSection === modelData ? Font.DemiBold : Font.Medium
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }
                }
            }

            GToolButton {
                id: backButton
                Layout.preferredWidth: 38
                Layout.preferredHeight: 38
                enabled: root.canNavigateBack
                onClicked: root.navigateBack()
                background: Rectangle {
                    radius: 10
                    color: backButton.hovered ? AppTheme.surfaceHover : AppTheme.surface
                    border.color: AppTheme.border
                }
                text: ""
                display: AbstractButton.IconOnly
                icon.source: AppTheme.icon("nav-back.svg")
                icon.width: 16
                icon.height: 16
            }

            Flickable {
                Layout.fillWidth: true
                Layout.preferredHeight: 38
                contentWidth: breadcrumbRow.implicitWidth
                contentHeight: height
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                Row {
                    id: breadcrumbRow
                    height: parent.height
                    spacing: 4
                    Repeater {
                        model: root.navigationStack
                        delegate: Row {
                            required property var modelData
                            required property int index
                            height: breadcrumbRow.height
                            spacing: 4
                            GToolButton {
                                height: 34
                                text: String(modelData.title || "")
                                onClicked: root.navigateToBreadcrumb(index)
                                background: Rectangle {
                                    radius: 9
                                    color: parent.hovered ? AppTheme.surfaceHover : "transparent"
                                }
                                contentItem: Text {
                                    text: parent.text
                                    color: index === root.navigationStack.length - 1 ? AppTheme.text : AppTheme.textMuted
                                    font.pixelSize: 10
                                    font.weight: index === root.navigationStack.length - 1 ? Font.DemiBold : Font.Normal
                                    elide: Text.ElideRight
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                visible: index < root.navigationStack.length - 1
                                text: "›"
                                color: AppTheme.textFaint
                                font.pixelSize: 15
                            }
                        }
                    }
                }
            }

            Rectangle {
                visible: root.width >= 850
                Layout.preferredWidth: providerText.implicitWidth + 24
                Layout.preferredHeight: 30
                radius: 15
                color: AppTheme.accentSoft
                border.color: AppTheme.accentBorder
                Text {
                    id: providerText
                    anchors.centerIn: parent
                    text: manager.selectedProvider || "DLNA"
                    color: AppTheme.accent
                    font.pixelSize: 9
                    font.weight: Font.DemiBold
                }
            }

            GToolButton {
                id: serverButton
                Layout.preferredWidth: root.width < 820 ? 96 : 112
                Layout.preferredHeight: 38
                onClicked: serverPopup.open()
                background: Rectangle {
                    radius: 10
                    color: serverButton.hovered ? AppTheme.surfaceHover : AppTheme.surface
                    border.color: AppTheme.border
                }
                contentItem: Text {
                    text: lang.language === "tr" ? "Sunucu seç" : "Choose server"
                    color: AppTheme.text
                    font.pixelSize: 10
                    font.weight: Font.Medium
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            GBusyIndicator {
                anchors.centerIn: parent
                running: manager.loading
                visible: manager.loading && root.currentEntries.length === 0
            }

            Text {
                anchors.centerIn: parent
                visible: !manager.loading && String(manager.errorString || "").length > 0
                width: Math.min(parent.width - 80, 560)
                text: manager.errorString
                color: AppTheme.textMuted
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: 11
            }

            GridView {
                id: grid
                anchors.fill: parent
                visible: String(manager.errorString || "").length === 0
                clip: true
                model: root.currentEntries
                readonly property int columnCount: Math.max(1, Math.floor(width / root.desiredCellWidth))
                cellWidth: width > 0 ? Math.max(1, Math.floor(width / columnCount)) : root.desiredCellWidth
                cellHeight: root.desiredCellHeight
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                WheelHandler {
                    target: null
                    blocking: true
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    acceptedModifiers: Qt.NoModifier
                    onWheel: function(event) {
                        root.scrollGridFromWheel(event)
                        event.accepted = true
                    }
                }

                delegate: Item {
                    id: cardRoot
                    required property var modelData
                    width: grid.cellWidth
                    height: grid.cellHeight

                    Component.onCompleted: root.enqueueCardArtwork(cardRoot.modelData)
                    onModelDataChanged: root.enqueueCardArtwork(cardRoot.modelData)

                    Rectangle {
                        id: cardSurface
                        anchors.fill: parent
                        anchors.margins: 6
                        radius: 15
                        color: String(cardRoot.modelData.objectId || "") === root.selectedObjectId
                               ? AppTheme.accentSoft
                               : (cardMouse.containsMouse ? AppTheme.surfaceHover : AppTheme.surface)
                        antialiasing: true

                        Rectangle {
                            id: cardMask
                            anchors.fill: parent
                            radius: 15
                            color: "white"
                            antialiasing: true
                            layer.enabled: true
                            z: -100
                        }

                        Item {
                            id: artworkLayer
                            anchors.fill: parent
                            layer.enabled: true
                            layer.effect: MultiEffect {
                                maskEnabled: true
                                maskSource: cardMask
                                maskThresholdMin: 0.01
                                maskThresholdMax: 1.0
                                maskSpreadAtMin: 0.0
                                maskSpreadAtMax: 0.0
                                autoPaddingEnabled: false
                            }

                            Image {
                                id: artwork
                                anchors.fill: parent
                                source: root.artworkUrlFor(cardRoot.modelData)
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                cache: true
                                opacity: status === Image.Ready ? 1.0 : 0.0
                            }

                            Rectangle {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: 72
                                visible: artwork.status === Image.Ready
                                gradient: Gradient {
                                    orientation: Gradient.Vertical
                                    GradientStop { position: 0.0; color: "#00000000" }
                                    GradientStop { position: 1.0; color: "#E5090D14" }
                                }
                            }
                        }

                        Rectangle {
                            anchors.fill: parent
                            radius: 15
                            visible: artwork.status !== Image.Ready
                            color: cardMouse.containsMouse ? AppTheme.surfaceHover : AppTheme.surfaceRaised
                            antialiasing: true

                            CrispIcon {
                                anchors.centerIn: parent
                                anchors.verticalCenterOffset: -12
                                width: Math.max(32, Math.min(64, 44 * root.cardScale))
                                height: width
                                opacity: 0.78
                                source: AppTheme.customIcon(String(cardRoot.modelData.kind || "") === "container"
                                                            ? "folder.svg"
                                                            : (String(cardRoot.modelData.mediaKind || "") === "audio"
                                                               ? "audio.svg" : "video.svg"))
                            }
                        }

                        Column {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.margins: 10
                            spacing: 3
                            Text {
                                width: parent.width
                                text: String(cardRoot.modelData.title || "")
                                color: artwork.status === Image.Ready ? "#F7FAFF" : AppTheme.text
                                font.pixelSize: Math.max(9, Math.min(14, 11 * root.cardScale))
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }
                            Text {
                                width: parent.width
                                text: {
                                    const bits = []
                                    if (String(cardRoot.modelData.mediaKind || "") === "audio") {
                                        const artist = String(cardRoot.modelData.artist || "")
                                        const album = String(cardRoot.modelData.album || "")
                                        if (artist.length) bits.push(artist)
                                        if (album.length) bits.push(album)
                                    }
                                    const duration = root.formatDuration(cardRoot.modelData.durationMs)
                                    const size = root.formatSize(cardRoot.modelData.size)
                                    if (duration.length) bits.push(duration)
                                    if (size.length) bits.push(size)
                                    if (String(cardRoot.modelData.resolution || "").length) bits.push(String(cardRoot.modelData.resolution))
                                    if (String(cardRoot.modelData.kind || "") === "container" && Number(cardRoot.modelData.childCount || 0) > 0)
                                        bits.push(Number(cardRoot.modelData.childCount) + " " + (lang.language === "tr" ? "öğe" : "items"))
                                    return bits.join(" · ")
                                }
                                color: artwork.status === Image.Ready ? "#C8D2E2" : AppTheme.textMuted
                                font.pixelSize: Math.max(7, Math.min(11, 8 * root.cardScale))
                                elide: Text.ElideRight
                            }
                        }

                        Rectangle {
                            anchors.centerIn: parent
                            width: Math.max(32, Math.min(54, 42 * root.cardScale))
                            height: width
                            radius: width / 2
                            visible: cardRoot.modelData.playable
                            color: "#85060B12"
                            border.width: 1
                            border.color: "#A5FFFFFF"
                            Text {
                                anchors.centerIn: parent
                                anchors.horizontalCenterOffset: 1
                                text: "▶"
                                color: "white"
                                font.pixelSize: Math.max(13, Math.min(21, 17 * root.cardScale))
                            }
                        }

                        Rectangle {
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: 9
                            width: Math.max(22, Math.min(36, 28 * root.cardScale))
                            height: width
                            radius: width / 2
                            visible: String(cardRoot.modelData.kind || "") === "container"
                            color: artwork.status === Image.Ready ? "#7D060B12" : AppTheme.accentSoft
                            Text {
                                anchors.centerIn: parent
                                text: "›"
                                color: artwork.status === Image.Ready ? "white" : AppTheme.accent
                                font.pixelSize: 19
                            }
                        }

                        Rectangle {
                            anchors.fill: parent
                            radius: 15
                            color: "transparent"
                            border.width: String(cardRoot.modelData.objectId || "") === root.selectedObjectId ? 2 : 1
                            border.color: String(cardRoot.modelData.objectId || "") === root.selectedObjectId
                                          ? AppTheme.accent
                                          : (cardMouse.containsMouse ? AppTheme.accentBorder : AppTheme.border)
                            antialiasing: true
                            z: 50
                        }

                        MouseArea {
                            id: cardMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            z: 60
                            onEntered: root.scheduleHover(cardRoot.modelData)
                            onExited: {
                                if (root.hoverCandidate
                                        && String(root.hoverCandidate.objectId || "") === String(cardRoot.modelData.objectId || ""))
                                    root.dismissHover()
                            }
                            onClicked: function(mouse) {
                                if (mouse.button !== Qt.LeftButton)
                                    return
                                root.selectedObjectId = String(cardRoot.modelData.objectId || "")
                                if (root.singleClickOpen)
                                    root.activate(cardRoot.modelData)
                            }
                            onDoubleClicked: function(mouse) {
                                if (!root.singleClickOpen && mouse.button === Qt.LeftButton)
                                    root.activate(cardRoot.modelData)
                            }
                        }
                    }
                }
            }

            NumberAnimation {
                id: gridScrollAnimation
                target: grid
                property: "contentY"
                duration: 230
                easing.type: Easing.OutCubic
            }

            TmdbHoverPreview {
                id: tmdbPreview
                anchors.centerIn: parent
                width: Math.max(1, Math.min(700, parent.width - 42))
                height: Math.max(1, Math.min(370, parent.height - 28))
                visible: (root.hoverShowing || root.hoverFadingOut) && root.hoverItem !== null
                shown: root.hoverShowing
                animationsEnabled: tmdbManager.hoverAnimationsEnabled
                metadata: root.tmdbInfo
                itemData: root.hoverItem ? ({ name: root.tmdbDisplayName(root.hoverItem) }) : null
                lang: root.lang
                z: 200
            }
        }
    }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 80, 620)
        spacing: 14
        visible: !manager.selectedServerId

        Text {
            Layout.fillWidth: true
            text: lang.language === "tr" ? "DLNA medya sunucusu seç" : "Choose a DLNA media server"
            color: AppTheme.text
            font.pixelSize: 22
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
        }
        Text {
            Layout.fillWidth: true
            text: lang.language === "tr"
                  ? "GiG, Jellyfin, Emby veya başka bir UPnP/DLNA MediaServer aynı ağda otomatik bulunur."
                  : "GiG, Jellyfin, Emby, or another UPnP/DLNA MediaServer on the same LAN is discovered automatically."
            color: AppTheme.textMuted
            font.pixelSize: 11
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
        }
        GBusyIndicator {
            Layout.alignment: Qt.AlignHCenter
            running: manager.discovering
            visible: manager.discovering
        }
        Repeater {
            model: manager.servers
            delegate: GButton {
                id: discoveredServerDelegate
                required property var modelData
                Layout.fillWidth: true
                Layout.preferredHeight: 58
                onClicked: manager.selectServer(String(discoveredServerDelegate.modelData.id || ""))
                background: Rectangle {
                    radius: 13
                    color: parent.hovered ? AppTheme.surfaceHover : AppTheme.surface
                    border.color: parent.hovered ? AppTheme.accentBorder : AppTheme.border
                }
                contentItem: RowLayout {
                    spacing: 12
                    Rectangle {
                        Layout.preferredWidth: 40
                        Layout.preferredHeight: 40
                        radius: 12
                        color: AppTheme.accentSoft
                        Text {
                            anchors.centerIn: parent
                            text: String(discoveredServerDelegate.modelData.provider || "DLNA").substring(0, 1)
                            color: AppTheme.accent
                            font.pixelSize: 16
                            font.weight: Font.Bold
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text { text: String(discoveredServerDelegate.modelData.name || "DLNA"); color: AppTheme.text; font.pixelSize: 12; font.weight: Font.DemiBold; Layout.fillWidth: true; elide: Text.ElideRight }
                        Text { text: String(discoveredServerDelegate.modelData.provider || "DLNA") + " · " + String(discoveredServerDelegate.modelData.location || ""); color: AppTheme.textMuted; font.pixelSize: 9; Layout.fillWidth: true; elide: Text.ElideMiddle }
                    }
                }
            }
        }
        GButton {
            Layout.alignment: Qt.AlignHCenter
            text: manager.discovering
                  ? (lang.language === "tr" ? "Aranıyor…" : "Searching…")
                  : (lang.language === "tr" ? "Ağı yeniden tara" : "Scan network again")
            enabled: !manager.discovering
            onClicked: manager.discover()
        }
    }

    Popup {
        id: serverPopup
        parent: Overlay.overlay
        width: Math.min(520, root.width - 60)
        height: Math.min(520, Math.max(220, 110 + serverList.contentHeight))
        x: parent ? Math.max(20, (parent.width - width) / 2) : 0
        y: parent ? Math.max(20, (parent.height - height) / 2) : 0
        modal: true
        focus: true
        padding: 14
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            radius: 16
            color: AppTheme.surfaceRaised
            border.color: AppTheme.border
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: lang.language === "tr" ? "DLNA medya sunucuları" : "DLNA media servers"
                    color: AppTheme.text
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                }
                GButton {
                    text: lang.language === "tr" ? "Tara" : "Scan"
                    enabled: !manager.discovering
                    onClicked: manager.discover()
                }
            }
            ListView {
                id: serverList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: manager.servers
                spacing: 6
                delegate: GButton {
                    id: serverDelegate
                    required property var modelData
                    width: serverList.width
                    height: 58
                    padding: 8
                    onClicked: {
                        manager.selectServer(String(serverDelegate.modelData.id || ""))
                        serverPopup.close()
                    }
                    background: Rectangle {
                        radius: 11
                        color: String(serverDelegate.modelData.id || "") === manager.selectedServerId
                               ? AppTheme.accentSoft
                               : (parent.hovered ? AppTheme.surfaceHover : AppTheme.surface)
                        border.color: String(serverDelegate.modelData.id || "") === manager.selectedServerId
                                      ? AppTheme.accentBorder : AppTheme.border
                    }
                    contentItem: ColumnLayout {
                        spacing: 3
                        Text { Layout.fillWidth: true; text: String(serverDelegate.modelData.name || "DLNA"); color: AppTheme.text; font.pixelSize: 11; font.weight: Font.DemiBold; elide: Text.ElideRight }
                        Text { Layout.fillWidth: true; text: String(serverDelegate.modelData.provider || "DLNA") + " · " + String(serverDelegate.modelData.location || ""); color: AppTheme.textMuted; font.pixelSize: 8; elide: Text.ElideMiddle }
                    }
                }
            }
        }
    }

    VideoViewer {
        id: videoViewer
        objectName: "dlnaVideoViewer"
        files: root.viewerFiles
        lang: root.lang
        hostWindow: root.hostWindow
        metadataManager: tmdbManager
        metadataForItem: function(item) { return root.viewerMetadataFor(item) }
    }
}
