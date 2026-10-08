import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import Lurviko.App

Item {
    id: root
    required property var lang
    required property var manager
    required property var player
    property int tabIndex: 0
    property string query: ""
    property string genreFilter: ""
    property string yearFilter: ""
    property int cardSize: 168
    property int wheelStep: AppTheme.wheelScrollStep
    property string sortMode: "name"
    property bool sortAscending: true
    property double wheelBurstLastMs: 0
    property int wheelBurstCount: 0
    property int wheelBurstDirection: 0
    property var viewItems: []
    property var stats: ({})
    property var groupSelection: null
    property int groupReturnIndex: 0
    property real groupReturnOffset: 0
    property bool groupBackPending: false

    readonly property var allTracks: visible && manager ? manager.tracks : []
    readonly property var genreOptions: {
        const groups = ({})
        const tracks = allTracks || []
        for (const track of tracks) {
            const value = genreName(track.genre)
            const key = genreKey(value)
            if (!key.length) continue
            if (!groups[key]) groups[key] = {value: value, count: 0}
            groups[key].count += 1
        }
        const result = [{value: "", text: tr("Tüm türler", "All genres") + " (" + tracks.length + ")"}]
        const keys = Object.keys(groups).sort(function(a, b) { return a.localeCompare(b) })
        for (const key of keys) {
            const genre = groups[key]
            result.push({value: genre.value, text: genre.value + " (" + genre.count + ")"})
        }
        return result
    }
    // Music has its own density: tracks are short rows, collections are covers.
    readonly property bool trackTab: !!groupSelection || tabIndex === 0 || tabIndex === 3 || tabIndex === 5 || tabIndex === 6
    readonly property real zoom: Math.max(0, Math.min(1, (cardSize - 104) / 256.0))
    readonly property real textScale: 1.0 + zoom * 0.12
    readonly property var layoutMetrics: calculateLayout(cardSize, tabIndex, !!groupSelection, libraryGrid.width)
    readonly property real desiredCardWidth: layoutMetrics.desired
    readonly property real cellGap: layoutMetrics.gap
    readonly property int calculatedColumns: layoutMetrics.columns
    readonly property real gridCellWidth: layoutMetrics.cellWidth
    readonly property real cardWidth: Math.max(1, gridCellWidth - cellGap)
    readonly property real artworkSize: layoutMetrics.art
    readonly property real cardHeight: layoutMetrics.height

    function calculateLayout(size, tab, grouped, availableWidth) {
        const isTrack = grouped || tab === 0 || tab === 3 || tab === 5 || tab === 6
        const amount = Math.max(0, Math.min(1, (size - 104) / 256.0))
        const desired = isTrack ? Math.round(236 + amount * 92) : Math.round(146 + amount * 112)
        const columns = Math.max(1, Math.floor(availableWidth / desired))
        const cellWidth = Math.max(1, Math.floor(availableWidth / columns))
        const gap = isTrack ? 8 : 14
        const art = isTrack ? Math.round(40 + amount * 36)
                            : Math.max(1, Math.min(Math.round(120 + amount * 108), cellWidth - gap - 20))
        const height = Math.round(art + (isTrack ? 24 : (tab === 1 ? 84 : 66)))
        return {desired: desired, columns: columns, cellWidth: cellWidth, gap: gap, art: art, height: height}
    }
    property int resizeAnchorIndex: -1
    property real resizeAnchorOffset: 0
    property bool resizeAnchorPending: false

    function prepareForResize() {
        if (resizeAnchorPending || !viewItems.length) return
        const idx = libraryGrid.indexAt(1, libraryGrid.contentY + 1)
        resizeAnchorIndex = Math.max(0, idx)
        const item = libraryGrid.itemAtIndex(resizeAnchorIndex)
        resizeAnchorOffset = item ? libraryGrid.contentY - item.y : 0
        resizeAnchorPending = true
        wheelScrollAnimation.stop()
    }

    function updateGridLayout() {
        // Calculate from the inputs directly: property change handlers can run
        // before the dependent zoom bindings have caught up with a slider move.
        const metrics = calculateLayout(cardSize, tabIndex, !!groupSelection, libraryGrid.width)
        libraryGrid.cellWidth = metrics.cellWidth
        libraryGrid.cellHeight = metrics.height + metrics.gap
        libraryGrid.forceLayout()
        if (resizeAnchorPending) {
            libraryGrid.positionViewAtIndex(resizeAnchorIndex, GridView.Beginning)
            const top = libraryGrid.originY
            libraryGrid.contentY = Math.max(top, Math.min(top + Math.max(0, libraryGrid.contentHeight - libraryGrid.height),
                                                        libraryGrid.contentY + resizeAnchorOffset))
            resizeAnchorPending = false
        }
    }

    onCardSizeChanged: updateGridLayout()
    onTabIndexChanged: { groupBackPending = false; groupSelection = null; updateGridLayout(); rebuildTimer.restart() }
    onGroupSelectionChanged: { updateGridLayout(); rebuildTimer.restart() }

    function tr(trText, enText) { return lang.language === "tr" ? trText : enText }

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
            return direction * root.wheelStep * stepFraction * (1.0 + wheelBurstCount * 0.08)
        }
        if (pixelY !== 0) {
            wheelBurstCount = 0
            wheelBurstDirection = 0
            wheelBurstLastMs = 0
            return pixelY * 2.0
        }
        return 0
    }

    function scrollFromWheel(event, view, extent) {
        const delta = normalizedWheelDelta(event)
        if (delta === 0 || !view)
            return
        const top = Number(view.originY || 0)
        const bottom = top + Math.max(0, view.contentHeight - view.height)
        const continuing = wheelScrollAnimation.running && wheelScrollAnimation.target === view
        const previousTarget = continuing ? wheelScrollAnimation.to : view.contentY
        const sameDirection = !continuing || Math.sign(previousTarget - view.contentY) === -Math.sign(delta)
        const base = sameDirection ? previousTarget : view.contentY
        const limit = Math.max(root.wheelStep, view.height * 1.8, Number(extent || 100) * 6)
        const destination = Math.max(top, Math.min(bottom,
                                Math.max(view.contentY - limit,
                                         Math.min(view.contentY + limit, base - delta))))
        wheelScrollAnimation.stop()
        if (Math.abs(destination - view.contentY) < 0.5)
            return
        wheelScrollAnimation.target = view
        wheelScrollAnimation.from = view.contentY
        wheelScrollAnimation.to = destination
        wheelScrollAnimation.duration = Math.min(400, Math.max(220, Math.abs(destination - view.contentY) * 0.55))
        wheelScrollAnimation.start()
    }

    NumberAnimation {
        id: wheelScrollAnimation
        property: "contentY"
        easing.type: Easing.OutCubic
    }

    function normalized(value) { return String(value || "").trim() }
    function genreName(value) { return normalized(value).normalize("NFC").replace(/\s+/g, " ") }
    function genreKey(value) { return genreName(value).toLowerCase() }

    function trackTitle(track) {
        const value = normalized(track.title)
        return value.length ? value : normalized(track.name)
    }

    function artistTitle(track) {
        const value = normalized(track.artist)
        return value.length ? value : tr("Bilinmeyen sanatçı", "Unknown artist")
    }

    function albumTitle(track) {
        const value = normalized(track.album)
        return value.length ? value : tr("Albüm bilgisi yok", "Unknown album")
    }

    function compareText(a, b) {
        const left = normalized(a).toLocaleLowerCase()
        const right = normalized(b).toLocaleLowerCase()
        const cmp = left.localeCompare(right)
        return sortAscending ? cmp : -cmp
    }

    function sortTrackItemsByName(items) {
        items.sort(function(a, b) {
            return root.compareText(root.trackTitle(a), root.trackTitle(b))
        })
        return items
    }

    function sortViewItemsByName(items) {
        items.sort(function(a, b) {
            return root.compareText(a.title, b.title)
        })
        return items
    }

    function passesFilters(track) {
        const genre = normalized(track.genre)
        const year = normalized(track.year)
        if (genreFilter.length && genreKey(genre) !== genreKey(genreFilter))
            return false
        if (yearFilter.length && year !== yearFilter)
            return false
        const needle = normalized(query).toLocaleLowerCase()
        if (!needle.length)
            return true
        const haystack = [trackTitle(track), artistTitle(track), albumTitle(track), genre, year,
                          normalized(track.name)].join(" ").toLocaleLowerCase()
        return haystack.indexOf(needle) >= 0
    }

    function filteredTracks() {
        const source = allTracks || []
        const result = []
        for (let i = 0; i < source.length; ++i) {
            const track = source[i]
            if (passesFilters(track))
                result.push(track)
        }
        return result
    }

    function firstArtwork(tracks) {
        for (let i = 0; i < tracks.length; ++i) {
            const art = normalized(tracks[i].thumbnailSource)
            if (art.length)
                return art
        }
        return ""
    }

    function buildAlbums(tracks) {
        const map = ({})
        const result = []
        for (let i = 0; i < tracks.length; ++i) {
            const track = tracks[i]
            const album = albumTitle(track)
            const artist = artistTitle(track)
            const key = artist.toLocaleLowerCase() + "\u0001" + album.toLocaleLowerCase()
            if (map[key] === undefined) {
                map[key] = result.length
                result.push({kind: "album", title: album, subtitle: artist, tracks: [], artwork: "", year: normalized(track.year)})
            }
            const group = result[map[key]]
            group.tracks.push(track)
            if (!group.artwork.length && normalized(track.thumbnailSource).length)
                group.artwork = normalized(track.thumbnailSource)
        }
        return result
    }

    function buildArtists(tracks) {
        const map = ({})
        const result = []
        for (let i = 0; i < tracks.length; ++i) {
            const track = tracks[i]
            const artist = artistTitle(track)
            const key = artist.toLocaleLowerCase()
            if (map[key] === undefined) {
                map[key] = result.length
                result.push({kind: "artist", title: artist, tracks: [], artwork: "", albums: ({})})
            }
            const group = result[map[key]]
            group.tracks.push(track)
            group.albums[albumTitle(track)] = true
            if (!group.artwork.length && normalized(track.thumbnailSource).length)
                group.artwork = normalized(track.thumbnailSource)
        }
        for (let i = 0; i < result.length; ++i) {
            const albumCount = Object.keys(result[i].albums).length
            result[i].subtitle = result[i].tracks.length + tr(" parça · ", " tracks · ")
                               + albumCount + tr(" albüm", " albums")
        }
        return result
    }

    function buildPlaylists() {
        const lists = player ? (player.playlists || []) : []
        const result = []
        for (let i = 0; i < lists.length; ++i) {
            const list = lists[i]
            const items = list.items || []
            result.push({kind: "playlist", title: normalized(list.name),
                         subtitle: items.length + tr(" parça", " tracks"),
                         playlistIndex: i, tracks: items, artwork: firstArtwork(items)})
        }
        return result
    }

    function calculateStats(tracks) {
        const albums = ({})
        const artists = ({})
        const genres = ({})
        let favorites = 0
        let plays = 0
        let recent = 0
        for (let i = 0; i < tracks.length; ++i) {
            const track = tracks[i]
            albums[artistTitle(track) + "\u0001" + albumTitle(track)] = true
            artists[artistTitle(track)] = true
            const genre = genreKey(track.genre)
            if (genre.length) genres[genre] = true
            if (!!track.favorite) favorites += 1
            plays += Number(track.playCount || 0)
            if (Number(track.lastPlayed || 0) > 0) recent += 1
        }
        return {
            tracks: tracks.length,
            albums: Object.keys(albums).length,
            artists: Object.keys(artists).length,
            genres: Object.keys(genres).length,
            favorites: favorites,
            plays: plays,
            recent: recent
        }
    }

    function rebuild() {
        if (!visible) { viewItems = []; return }
        const oldIndex = libraryGrid.indexAt(1, libraryGrid.contentY + 1)
        const oldItem = oldIndex >= 0 ? viewItems[oldIndex] : null
        const anchorKey = oldItem ? viewIdentity(oldItem) : ""
        const oldDelegate = libraryGrid.itemAtIndex(oldIndex)
        const offset = oldDelegate ? libraryGrid.contentY - oldDelegate.y : 0
        let tracks = filteredTracks()
        stats = calculateStats(tracks)
        if (groupSelection) {
            if (groupSelection.kind === "playlist") {
                const byUrl = ({})
                for (const track of allTracks) byUrl[String(track.itemUrl || "")] = track
                tracks = (groupSelection.tracks || []).map(function(item) {
                    return byUrl[String(item.itemUrl || "")] || item
                }).filter(function(item) { return root.passesFilters(item) })
            } else {
                tracks = tracks.filter(function(track) {
                    return root.artistTitle(track).toLocaleLowerCase() === root.groupSelection.artist.toLocaleLowerCase()
                        && (root.groupSelection.kind === "artist"
                            || root.albumTitle(track).toLocaleLowerCase() === root.groupSelection.title.toLocaleLowerCase())
                })
            }
        }
        if (sortMode === "name" && (tabIndex === 0 || tabIndex === 3 || groupSelection))
            tracks = sortTrackItemsByName(tracks)
        if (tabIndex === 0 || groupSelection) {
            const result = []
            for (let i = 0; i < tracks.length; ++i) {
                const track = tracks[i]
                result.push({kind: "track", track: track, title: trackTitle(track),
                             subtitle: artistTitle(track), artwork: normalized(track.thumbnailSource),
                             favorite: !!track.favorite, tracks: [track]})
            }
            viewItems = result
        } else if (tabIndex === 1) {
            const albums = buildAlbums(tracks)
            viewItems = sortMode === "name" ? sortViewItemsByName(albums) : albums
        } else if (tabIndex === 2) {
            const artists = buildArtists(tracks)
            viewItems = sortMode === "name" ? sortViewItemsByName(artists) : artists
        } else if (tabIndex === 3) {
            const result = []
            for (let i = 0; i < tracks.length; ++i) {
                const track = tracks[i]
                if (!!track.favorite)
                    result.push({kind: "track", track: track, title: trackTitle(track), subtitle: artistTitle(track),
                                 artwork: normalized(track.thumbnailSource), favorite: true, tracks: [track]})
            }
            viewItems = result
        } else if (tabIndex === 4) {
            const lists = buildPlaylists()
            viewItems = sortMode === "name" ? sortViewItemsByName(lists) : lists
        } else if (tabIndex === 5) {
            const recent = tracks.filter(function(track) { return Number(track.lastPlayed || 0) > 0 })
            recent.sort(function(a, b) { return Number(b.lastPlayed || 0) - Number(a.lastPlayed || 0) })
            viewItems = recent.map(function(track) {
                const stamp = Number(track.lastPlayed || 0)
                const when = stamp > 0 ? new Date(stamp).toLocaleString() : ""
                return {kind: "track", track: track, title: trackTitle(track),
                        subtitle: artistTitle(track) + (when.length ? " · " + when : ""),
                        artwork: normalized(track.thumbnailSource), favorite: !!track.favorite, tracks: [track]}
            })
        } else if (tabIndex === 6) {
            const top = tracks.filter(function(track) { return Number(track.playCount || 0) > 0 })
            top.sort(function(a, b) {
                const diff = Number(b.playCount || 0) - Number(a.playCount || 0)
                return diff !== 0 ? diff : Number(b.lastPlayed || 0) - Number(a.lastPlayed || 0)
            })
            viewItems = top.map(function(track) {
                return {kind: "track", track: track, title: trackTitle(track),
                        subtitle: Number(track.playCount || 0) + tr(" dinleme · ", " plays · ") + artistTitle(track),
                        artwork: normalized(track.thumbnailSource), favorite: !!track.favorite, tracks: [track]}
            })
        } else {
            viewItems = []
        }
        if (groupBackPending) {
            resizeAnchorIndex = Math.min(groupReturnIndex, Math.max(0, viewItems.length - 1))
            resizeAnchorOffset = groupReturnOffset
            resizeAnchorPending = true
            groupBackPending = false
            updateGridLayout()
            return
        }
        // Metadata/favorite updates replace the JS model. Keep the visible
        // track or album anchored instead of jumping while scanning continues.
        if (anchorKey.length) {
            for (let i = 0; i < viewItems.length; ++i) {
                if (viewIdentity(viewItems[i]) !== anchorKey) continue
                resizeAnchorIndex = i
                resizeAnchorOffset = offset
                resizeAnchorPending = true
                updateGridLayout()
                break
            }
        }
    }

    function viewIdentity(item) {
        return item.kind + ":" + (item.track ? String(item.track.itemUrl || "")
                                            : String(item.title || "") + ":" + String(item.subtitle || ""))
    }

    function browseGroup(item) {
        groupReturnIndex = Math.max(0, libraryGrid.indexAt(1, libraryGrid.contentY + 1))
        const delegate = libraryGrid.itemAtIndex(groupReturnIndex)
        groupReturnOffset = delegate ? libraryGrid.contentY - delegate.y : 0
        groupSelection = {kind: item.kind, title: item.title,
                          artist: item.kind === "artist" ? item.title : item.subtitle,
                          tracks: item.kind === "playlist" ? (item.tracks || []) : []}
        libraryGrid.positionViewAtBeginning()
    }

    function closeGroup() {
        groupBackPending = true
        groupSelection = null
    }

    function activate(item) {
        if (!item || !player)
            return
        if (item.kind === "playlist") {
            player.playPlaylist(Number(item.playlistIndex))
            return
        }
        let items = item.tracks || []
        let first = items.length ? items[0] : null
        // Track-oriented tabs should continue through the currently visible
        // result set instead of turning every click into a one-song queue.
        if (item.kind === "track") {
            items = []
            for (let i = 0; i < viewItems.length; ++i) {
                if (viewItems[i].kind === "track" && viewItems[i].track)
                    items.push(viewItems[i].track)
            }
            first = item.track || first
        }
        if (!first || !items.length)
            return
        player.openUrl(String(first.itemUrl || ""), String(first.name || item.title || ""),
                       String(first.thumbnailSource || item.artwork || ""), items)
    }

    onAllTracksChanged: if (visible && !rebuildTimer.running) rebuildTimer.start()
    onQueryChanged: rebuildTimer.restart()
    onGenreFilterChanged: rebuildTimer.restart()
    onYearFilterChanged: rebuildTimer.restart()
    onSortModeChanged: rebuildTimer.restart()
    onSortAscendingChanged: rebuildTimer.restart()
    Component.onCompleted: { updateGridLayout(); rebuildTimer.restart() }
    onVisibleChanged: { if (!visible) viewItems = []; else rebuildTimer.restart() }

    Connections {
        target: player
        function onPlaylistsChanged() { rebuildTimer.restart() }
    }

    Timer { id: rebuildTimer; interval: 100; repeat: false; onTriggered: root.rebuild() }

    RowLayout {
        id: groupHeader
        objectName: "musicGroupHeader"
        visible: !!root.groupSelection
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: visible ? 36 : 0
        spacing: 10
        GToolButton {
            width: 30; height: 30
            icon.source: AppTheme.icon("nav-back.svg")
            icon.width: 14; icon.height: 14
            onClicked: root.closeGroup()
            ToolTip.visible: hovered
            ToolTip.text: root.tr("Kütüphaneye dön", "Back to library")
        }
        Text {
            Layout.fillWidth: true
            text: root.groupSelection ? root.groupSelection.title : ""
            color: AppTheme.text
            font.pixelSize: 13
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        Text {
            text: root.viewItems.length + root.tr(" parça", " tracks")
            font.pixelSize: 10
            color: AppTheme.textMuted
        }
        GButton {
            text: root.tr("Tümünü çal", "Play all")
            implicitHeight: 30
            topPadding: 4; bottomPadding: 4
            enabled: root.viewItems.length > 0
            onClicked: root.activate(root.viewItems[0])
        }
    }

    GridView {
        id: libraryGrid
        objectName: "musicLibraryGrid"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.top: groupHeader.bottom
        anchors.topMargin: groupHeader.visible ? 6 : 0
        anchors.rightMargin: 14
        visible: root.tabIndex !== 7
        clip: true
        onWidthChanged: { root.prepareForResize(); root.updateGridLayout() }
        model: root.viewItems
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds
        cacheBuffer: Math.max(0, Math.min(600, height))
        onDraggingChanged: if (dragging) wheelScrollAnimation.stop()

        WheelHandler {
            target: null
            blocking: true
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            acceptedModifiers: Qt.NoModifier
            onWheel: function(event) {
                root.scrollFromWheel(event, libraryGrid, libraryGrid.cellHeight)
                event.accepted = true
            }
        }

        delegate: Loader {
            id: cardCell
            required property var modelData
            property var entry: modelData
            width: libraryGrid.cellWidth
            height: libraryGrid.cellHeight
            sourceComponent: root.trackTab ? trackCard : collectionCard
        }

        ScrollBar.vertical: ScrollBar {
            id: libraryScrollBar
            objectName: "musicLibraryScrollBar"
            parent: root
            anchors.top: parent.top
            anchors.topMargin: groupHeader.visible ? groupHeader.height + 6 : 0
            anchors.bottom: parent.bottom
            anchors.right: parent.right
            width: 10
            padding: 0
            policy: ScrollBar.AsNeeded
            visible: libraryGrid.visible && size < 0.999
            active: pressed || hovered || libraryGrid.moving
                    || (wheelScrollAnimation.running && wheelScrollAnimation.target === libraryGrid)
            onPressedChanged: if (pressed) wheelScrollAnimation.stop()
            contentItem: Rectangle {
                implicitWidth: 6
                radius: 3
                color: libraryScrollBar.pressed ? AppTheme.accent : AppTheme.textMuted
                opacity: libraryScrollBar.active ? 0.82 : 0
                Behavior on opacity { NumberAnimation { duration: 160 } }
            }
            background: null
        }
    }

    Component {
        id: trackCard
        Item {
            id: trackCell
            readonly property var entry: parent && parent.entry ? parent.entry : ({})
            readonly property var track: entry.track || ({})
            readonly property bool nowPlaying: !!root.player
                && String(root.player.currentUrl || "") === String(track.itemUrl || "") && !!track.itemUrl
            Rectangle {
                id: trackCardSurface
                anchors.fill: parent
                anchors.margins: root.cellGap / 2
                radius: 12
                color: trackCell.nowPlaying ? AppTheme.accentSoft
                       : (trackMouse.containsMouse ? AppTheme.surfaceHover : AppTheme.surface)
                border.width: 1
                border.color: trackCell.nowPlaying ? AppTheme.accentBorder : AppTheme.border
                MouseArea {
                    id: trackMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.activate(trackCell.entry)
                }
                Rectangle {
                    id: trackArtwork
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    width: root.artworkSize
                    height: width
                    radius: 8
                    color: AppTheme.surfaceRaised
                    layer.enabled: trackCover.status === Image.Ready
                    layer.smooth: true
                    layer.samples: 4
                    layer.effect: MultiEffect { maskEnabled: true; maskSource: trackMask }
                    Image {
                        id: trackCover
                        anchors.fill: parent
                        source: String(trackCell.entry.artwork || "")
                        sourceSize: Qt.size(256, 256)
                        asynchronous: true
                        cache: true
                        fillMode: Image.PreserveAspectCrop
                        visible: status === Image.Ready
                    }
                    CrispIcon {
                        anchors.centerIn: parent
                        width: Math.min(24, parent.width * 0.45)
                        height: width
                        source: AppTheme.discoveryIcon(AppTheme.categoryDefaultIcon("music"), AppTheme.categoryDefaultIcon("music"))
                        visible: trackCover.status !== Image.Ready
                    }
                    Rectangle {
                        anchors.fill: parent
                        radius: parent.radius
                        visible: trackMouse.containsMouse
                        color: "#66000000"
                        CrispIcon {
                            anchors.centerIn: parent
                            width: 20; height: 20
                            source: AppTheme.icon("viewer-play.svg", "#ffffff")
                        }
                    }
                }
                Rectangle {
                    id: trackMask
                    width: trackArtwork.width; height: trackArtwork.height
                    radius: trackArtwork.radius
                    color: "white"; antialiasing: true
                    visible: false
                    layer.enabled: trackArtwork.layer.enabled
                    layer.samples: 4
                }
                Column {
                    anchors.left: trackArtwork.right
                    anchors.right: trackFavorite.left
                    anchors.leftMargin: 11
                    anchors.rightMargin: 5
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4
                    Text {
                        width: parent.width
                        text: String(trackCell.entry.title || "")
                        font.pixelSize: Math.round(12 * root.textScale)
                        font.weight: Font.DemiBold
                        color: trackCell.nowPlaying ? AppTheme.accent : AppTheme.text
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        text: String(trackCell.entry.subtitle || "")
                        font.pixelSize: 10
                        color: AppTheme.textMuted
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        visible: root.artworkSize >= 60 && root.tabIndex !== 5 && root.tabIndex !== 6
                        text: String(trackCell.track.album || "")
                              + (trackCell.track.year ? " · " + trackCell.track.year : "")
                        font.pixelSize: 9
                        color: AppTheme.textFaint
                        elide: Text.ElideRight
                    }
                }
                GToolButton {
                    id: trackFavorite
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    width: 28; height: 28
                    checked: !!trackCell.entry.favorite
                    opacity: checked || trackMouse.containsMouse || hovered ? 1 : 0.35
                    icon.source: AppTheme.icon(checked ? "music-heart-filled.svg" : "music-heart.svg")
                    icon.width: 14; icon.height: 14
                    icon.color: checked ? AppTheme.accent : AppTheme.textMuted
                    onClicked: root.manager.toggleFavorite(String(trackCell.track.itemUrl || ""))
                    ToolTip.visible: hovered
                    ToolTip.text: checked ? root.tr("Favorilerden çıkar", "Remove from favorites")
                                          : root.tr("Favorilere ekle", "Add to favorites")
                }
                ToolTip.visible: trackMouse.containsMouse && !trackFavorite.hovered
                ToolTip.delay: 800
                ToolTip.text: trackCell.entry.title + "\n" + trackCell.entry.subtitle
            }
        }
    }

    Component {
        id: collectionCard
        Item {
            id: collectionCell
            readonly property var entry: parent && parent.entry ? parent.entry : ({})
            Rectangle {
                id: collectionSurface
                anchors.fill: parent
                anchors.margins: root.cellGap / 2
                radius: 14
                color: collectionMouse.containsMouse ? AppTheme.surfaceHover : AppTheme.surface
                border.width: 1
                border.color: collectionMouse.containsMouse ? AppTheme.accentBorder : AppTheme.border
                MouseArea {
                    id: collectionMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.browseGroup(collectionCell.entry)
                }
                Rectangle {
                    id: collectionArtwork
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    anchors.topMargin: 10
                    width: root.artworkSize
                    height: width
                    radius: collectionCell.entry.kind === "artist" ? width / 2 : 10
                    color: AppTheme.surfaceRaised
                    layer.enabled: collectionCover.status === Image.Ready
                    layer.smooth: true
                    layer.samples: 4
                    layer.effect: MultiEffect { maskEnabled: true; maskSource: collectionMask }
                    Image {
                        id: collectionCover
                        anchors.fill: parent
                        source: String(collectionCell.entry.artwork || "")
                        sourceSize: Qt.size(384, 384)
                        asynchronous: true
                        cache: true
                        fillMode: Image.PreserveAspectCrop
                        visible: status === Image.Ready
                    }
                    CrispIcon {
                        anchors.centerIn: parent
                        width: 32; height: 32
                        source: AppTheme.icon(collectionCell.entry.kind === "playlist" ? "music-playlist.svg" : "audio.svg")
                        visible: collectionCover.status !== Image.Ready
                    }
                }
                Rectangle {
                    id: collectionMask
                    width: collectionArtwork.width; height: collectionArtwork.height
                    radius: collectionArtwork.radius
                    color: "white"; antialiasing: true
                    visible: false
                    layer.enabled: collectionArtwork.layer.enabled
                    layer.samples: 4
                }
                GToolButton {
                    anchors.right: collectionArtwork.right
                    anchors.bottom: collectionArtwork.bottom
                    anchors.margins: 8
                    width: 32; height: 32
                    visible: collectionMouse.containsMouse || hovered
                    icon.source: AppTheme.icon("viewer-play.svg")
                    icon.width: 16; icon.height: 16
                    onClicked: root.activate(collectionCell.entry)
                    ToolTip.visible: hovered
                    ToolTip.text: root.tr("Çal", "Play")
                    background: Rectangle { radius: 16; color: AppTheme.accentSoft; border.color: AppTheme.accentBorder }
                }
                Column {
                    anchors.top: collectionArtwork.bottom
                    anchors.topMargin: 10
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: 10
                    spacing: 4
                    Text {
                        width: parent.width
                        text: String(collectionCell.entry.title || "")
                        horizontalAlignment: collectionCell.entry.kind === "artist" ? Text.AlignHCenter : Text.AlignLeft
                        font.pixelSize: Math.round(12 * root.textScale)
                        font.weight: Font.DemiBold
                        color: AppTheme.text
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        text: String(collectionCell.entry.subtitle || "")
                        horizontalAlignment: collectionCell.entry.kind === "artist" ? Text.AlignHCenter : Text.AlignLeft
                        font.pixelSize: 10
                        color: AppTheme.textMuted
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        visible: collectionCell.entry.kind === "album"
                        text: (collectionCell.entry.tracks || []).length + root.tr(" parça", " tracks")
                              + (collectionCell.entry.year ? " · " + collectionCell.entry.year : "")
                        font.pixelSize: 9
                        color: AppTheme.textFaint
                        elide: Text.ElideRight
                    }
                }
                ToolTip.visible: collectionMouse.containsMouse
                ToolTip.delay: 800
                ToolTip.text: collectionCell.entry.title + "\n" + collectionCell.entry.subtitle
            }
        }
    }

    Flickable {
        id: statsFlick
        anchors.fill: parent
        anchors.rightMargin: 14
        visible: root.tabIndex === 7
        clip: true
        contentWidth: width
        contentHeight: statsColumn.implicitHeight + 24
        boundsBehavior: Flickable.StopAtBounds
        onDraggingChanged: if (dragging) wheelScrollAnimation.stop()

        WheelHandler {
            target: null
            blocking: true
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            acceptedModifiers: Qt.NoModifier
            onWheel: function(event) {
                root.scrollFromWheel(event, statsFlick, 92)
                event.accepted = true
            }
        }

        ScrollBar.vertical: ScrollBar {
            id: statsScrollBar
            objectName: "musicStatsScrollBar"
            parent: root
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.right: parent.right
            width: 10
            padding: 0
            policy: ScrollBar.AsNeeded
            visible: statsFlick.visible && size < 0.999
            active: pressed || hovered || statsFlick.moving
                    || (wheelScrollAnimation.running && wheelScrollAnimation.target === statsFlick)
            onPressedChanged: if (pressed) wheelScrollAnimation.stop()
            contentItem: Rectangle {
                implicitWidth: 6
                radius: 3
                color: statsScrollBar.pressed ? AppTheme.accent : AppTheme.textMuted
                opacity: statsScrollBar.active ? 0.82 : 0
                Behavior on opacity { NumberAnimation { duration: 160 } }
            }
            background: null
        }

        ColumnLayout {
            id: statsColumn
            width: statsFlick.width
            spacing: 14

            Text {
                Layout.leftMargin: 8
                Layout.topMargin: 8
                text: root.tr("Müzik kütüphanesi özeti", "Music library overview")
                color: AppTheme.text
                font.pixelSize: 16
                font.weight: Font.DemiBold
            }

            GridLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 8
                Layout.rightMargin: 16
                columns: Math.max(2, Math.min(4, Math.floor(width / 210)))
                columnSpacing: 10
                rowSpacing: 10

                Repeater {
                    model: [
                        {label: root.tr("Parçalar", "Tracks"), value: Number(root.stats.tracks || 0)},
                        {label: root.tr("Albümler", "Albums"), value: Number(root.stats.albums || 0)},
                        {label: root.tr("Sanatçılar", "Artists"), value: Number(root.stats.artists || 0)},
                        {label: root.tr("Favoriler", "Favorites"), value: Number(root.stats.favorites || 0)},
                        {label: root.tr("Toplam dinleme", "Total plays"), value: Number(root.stats.plays || 0)},
                        {label: root.tr("Dinlenen parçalar", "Played tracks"), value: Number(root.stats.recent || 0)},
                        {label: root.tr("Türler", "Genres"), value: Number(root.stats.genres || 0)}
                    ]
                    delegate: Rectangle {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: 92
                        radius: 13
                        color: AppTheme.surface
                        border.width: 1
                        border.color: AppTheme.border
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 4
                            Text {
                                text: String(modelData.value)
                                color: AppTheme.accent
                                font.pixelSize: 24
                                font.weight: Font.Bold
                            }
                            Text {
                                text: String(modelData.label)
                                color: AppTheme.textMuted
                                font.pixelSize: 10
                            }
                        }
                    }
                }
            }

            Text {
                Layout.leftMargin: 8
                Layout.rightMargin: 16
                Layout.fillWidth: true
                text: root.tr("İstatistikler dinleme geçmişi ve favorilerle birlikte cihazda saklanır.",
                              "Statistics are stored on this device together with play history and favorites.")
                color: AppTheme.textFaint
                font.pixelSize: 9
                wrapMode: Text.WordWrap
            }
        }
    }

    Column {
        anchors.centerIn: parent
        spacing: 7
        visible: root.tabIndex !== 7 && root.viewItems.length === 0
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "♪"
            color: AppTheme.textFaint
            font.pixelSize: 30
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.tabIndex === 3 ? root.tr("Henüz favori parça yok", "No favorite tracks yet")
                  : root.tabIndex === 4 ? root.tr("Henüz çalma listesi yok", "No playlists yet")
                  : root.tabIndex === 5 ? root.tr("Henüz dinleme geçmişi yok", "No listening history yet")
                  : root.tabIndex === 6 ? root.tr("Henüz dinleme sayısı oluşmadı", "No play counts yet")
                  : root.tr("Bu filtreyle eşleşen müzik yok", "No music matches these filters")
            color: AppTheme.textMuted
            font.pixelSize: 11
        }
    }

}
