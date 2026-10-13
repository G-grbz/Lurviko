import QtQuick
import QtQuick.Controls
import QtTest
import Lurviko.App
import Lurviko.Backend
import "../../qml/components/MusicArtists.js" as Artists

Item {
    width: 1200
    height: 850
    LanguageManager { id: language }
    FavoritesModel { id: favorites }
    QtObject { id: storage; property var favoriteItems: [] }
    QtObject { id: library; property var tracks: [] }
    QtObject {
        id: contentIndex
        property var tracks: []
        function filesForCategory(category) { return category === "music" ? tracks : [] }
    }
    MusicPlayer {
        id: player
        x: 290; width: 900; height: 360
        lang: language
        contentIndexSource: contentIndex
        property int loadCount: 0
        property bool lastAutoplay: false
        function loadCurrent(autoplay) { ++loadCount; lastAutoplay = !!autoplay }
    }
    AppSidebar {
        id: sidebar
        width: 280; height: 850
        lang: language
        favoritesModel: favorites
        storageModel: storage
        musicPlayer: player
    }
    MusicExpandedView {
        id: expanded
        anchors.fill: parent
        player: player
        lang: language
        visible: false
    }
    MusicLibraryView {
        id: collection
        anchors.fill: parent
        lang: language
        manager: library
        player: player
        visible: false
    }
    TestCase {
        name: "MusicArtistPlaylists"
        when: windowShown
        property bool savedHoverEnabled
        function initTestCase() {
            savedHoverEnabled = AppTheme.musicHoverEnabled
            AppTheme.setMusicHoverPreferences(false, AppTheme.musicHoverAnimationsEnabled, AppTheme.musicHoverDelayMs)
        }
        function cleanupTestCase() {
            AppTheme.setMusicHoverPreferences(savedHoverEnabled, AppTheme.musicHoverAnimationsEnabled, AppTheme.musicHoverDelayMs)
        }
        function init() {
            language.language = "en"
            contentIndex.tracks = []
            player.libraryManager.setItems([])
            const tracks = []
            for (let i = 0; i < 160; ++i)
                tracks.push({itemUrl: "file:///artist-fixture/track-" + i + ".mp3", name: "Track " + i,
                    title: "Track " + i, artist: i % 3 === 0 ? "Mark Eliyahu & Cem Adrian"
                        : i % 3 === 1 ? "Mark Eliyahu" : "Other", album: "Album"})
            library.tracks = tracks
            player.sourceLibrary = tracks
            player.queue = [tracks[0]]
            player.currentIndex = 0
            player.shuffleMode = false
            player.rebuildPlaybackOrder(0)
            player.loadCount = 0
            player.trackMeta = {title: "Song", artist: "Mark Eliyahu, Cem Adrian", album: "Album"}
            player.persistPlaylists([])
            player.opened = true
            player.panelVisible = true
            player.queueOpen = false
            player.drawerTab = 0
            player.expandedPresentation = false
            expanded.visible = false
            collection.visible = false
        }
        function cleanup() {
            findChild(player, "musicPlaylistDialog").close()
            findChild(expanded, "expandedMusicPlaylistDialog").close()
            expanded.visible = false
            collection.visible = false
            player.opened = false
            player.persistPlaylists([])
            wait(200)
        }
        function test_artist_separators_and_exact_membership() {
            for (const value of ["Mark Eliyahu, Cem Adrian", "Mark Eliyahu & Cem Adrian",
                                 "Mark Eliyahu feat. Cem Adrian", "Mark Eliyahu ft. Cem Adrian",
                                 "Mark Eliyahu featuring Cem Adrian", "Mark Eliyahu x Cem Adrian",
                                 "Mark Eliyahu + Cem Adrian", "Mark Eliyahu ve Cem Adrian"])
                compare(JSON.stringify(Artists.split(value)), JSON.stringify(["Mark Eliyahu", "Cem Adrian"]))
            compare(JSON.stringify(Artists.split("AC/DC")), '["AC/DC"]')
            compare(Artists.split("Cem Adrian; cem adrian").length, 1)
            verify(Artists.contains("Mark Eliyahu & Cem Adrian", "Cem Adrian"))
            verify(!Artists.contains("Mark Eliyahu & Cem Adrian", "Cem"))
            compare(player.tracksForArtist("Mark Eliyahu").length, 107,
                    "Artist lookup must cover the collection, not just the one-track queue")
        }
        function test_collection_artist_groups_include_collaborations() {
            collection.tabIndex = 2
            collection.visible = true
            tryVerify(function() { return collection.viewItems.length === 3 })
            const mark = collection.viewItems.find(function(item) { return item.title === "Mark Eliyahu" })
            compare(mark.tracks.length, 107)
            collection.browseGroup(mark)
            tryVerify(function() { return collection.viewItems.length === 107 })
        }
        function test_artist_lookup_initializes_the_whole_native_index() {
            player.sourceLibrary = []
            player.queue = []
            player.currentIndex = -1
            const indexed = [
                {url: "file:///unavailable/Mark%20Eliyahu%20-%20One.mp3", name: "Mark Eliyahu - One.mp3", size: 10, modifiedMs: 1},
                {url: "file:///unavailable/Mark%20Eliyahu%20-%20Two.mp3", name: "Mark Eliyahu - Two.mp3", size: 10, modifiedMs: 1},
                {url: "file:///unavailable/Mark%20Eliyahu%20%26%20Cem%20Adrian%20-%20Duet.mp3", name: "Mark Eliyahu & Cem Adrian - Duet.mp3", size: 10, modifiedMs: 1},
                {url: "file:///unavailable/Other%20-%20Song.mp3", name: "Other - Song.mp3", size: 10, modifiedMs: 1}
            ]
            contentIndex.tracks = indexed
            // Simulate an earlier filtered category: only one indexed track is
            // loaded. Artist lookup must normalize "url" and restore all four.
            player.libraryManager.setItems([Object.assign({}, indexed[0], {itemUrl: indexed[0].url})])
            compare(player.tracksForArtist("Mark Eliyahu").length, 3)
            compare(player.libraryManager.tracks.length, 4)
            compare(player.tracksForArtist("Cem Adrian", false).length, 1)
        }
        function test_artist_links_open_in_each_player_data() {
            return [
                {tag: "normal", kind: "normal", links: "normalMusicArtistLinks"},
                {tag: "mini", kind: "mini", links: "miniMusicArtistLinks"},
                {tag: "expanded", kind: "expanded", links: "expandedMusicArtistLinks"},
                {tag: "detached", kind: "detached", links: "expandedMusicArtistLinks"},
                {tag: "expanded-footer", kind: "expanded", links: "expandedFooterArtistLinks"}
            ]
        }
        function clickArtistLink(label, index) {
            const wanted = "artist:" + index
            for (let y = 2; y < label.height; y += 3)
                for (let x = 2; x < label.width; x += 3)
                    if (label.linkAt(x, y) === wanted) {
                        mouseClick(label, x, y)
                        return
                    }
            fail("Artist link is not independently clickable: " + wanted)
        }
        function test_artist_links_open_in_each_player(data) {
            const large = data.kind === "expanded" || data.kind === "detached"
            if (large) {
                player.expandedPresentation = true
                expanded.detached = data.kind === "detached"
                expanded.visible = true
            } else if (data.kind === "mini") {
                player.panelVisible = false
                sidebar.toggleMiniMusicPopup()
            }
            const host = large ? expanded : data.kind === "mini" ? sidebar : player
            const dialog = findChild(large ? expanded : player, large ? "expandedMusicPlaylistDialog" : "musicPlaylistDialog")
            const links = findChild(host, data.links)
            verify(links !== null)
            wait(200)
            clickArtistLink(links, 0)
            tryCompare(dialog, "visible", true)
            compare(dialog.artistName, "Mark Eliyahu")
            compare(dialog.items.length, 107)
            const field = findChild(dialog, "playlistSuggestedName")
            compare(field.text, "Mark Eliyahu")
            dialog.close()
            wait(220)
            if (data.kind === "mini") sidebar.toggleMiniMusicPopup()
            wait(200)
            clickArtistLink(links, 1)
            tryCompare(dialog, "visible", true)
            compare(dialog.artistName, "Cem Adrian")
            compare(dialog.items.length, 54)
        }
        function test_selection_survives_delegate_reuse_and_toggle_all() {
            player.openArtist("Mark Eliyahu")
            const dialog = findChild(player, "musicPlaylistDialog")
            tryCompare(dialog, "visible", true)
            const view = findChild(dialog, "playlistArtistTracks")
            const first = dialog.items[0].itemUrl
            dialog.toggleItem(first)
            compare(dialog.selectedCount, 1)
            view.positionViewAtIndex(100, ListView.Beginning)
            wait(120)
            compare(dialog.selectedCount, 1)
            view.positionViewAtBeginning()
            wait(120)
            verify(dialog.selectedUrls[first])
            const all = findChild(dialog, "playlistToggleAll")
            mouseClick(all)
            compare(dialog.selectedCount, 107)
            mouseClick(all)
            compare(dialog.selectedCount, 0)
            dialog.toggleItem(first)
            dialog.saveSelection()
            compare(player.playlists.length, 1)
            compare(player.playlists[0].items.length, 1)
            compare(player.playlists[0].items[0].itemUrl, first)
        }
        function test_track_click_plays_temporarily_and_checkbox_only_selects() {
            player.saveCurrentQueueAsPlaylist("Original")
            player.playPlaylist(0)
            // Supply the full catalog without scanning media files in this UI fixture.
            player.sourceLibrary = library.tracks
            const saved = JSON.stringify(player.playlists)
            player.openArtist("Mark Eliyahu")
            const dialog = findChild(player, "musicPlaylistDialog")
            tryCompare(dialog, "visible", true)
            const view = findChild(dialog, "playlistArtistTracks")
            view.forceLayout()
            tryVerify(function() { return view.itemAtIndex(1) !== null })
            const row = view.itemAtIndex(1)
            const selection = findChild(row, "artistTrackSelection")
            const play = findChild(row, "artistTrackPlay")
            verify(selection !== null && play !== null)
            const loads = player.loadCount
            mouseClick(selection)
            compare(dialog.selectedCount, 1)
            compare(player.loadCount, loads, "Selecting for saving must not start playback")
            mouseClick(play)
            compare(player.loadCount, loads + 1)
            verify(player.lastAutoplay)
            compare(player.currentUrl, row.modelData.itemUrl)
            compare(player.queue.length, 2)
            compare(dialog.selectedCount, 1, "Playing must not change the save selection")
            verify(dialog.visible, "Keep the artist browser open for more tracks")
            mouseClick(play)
            compare(player.queue.length, 2)
            compare(JSON.stringify(player.playlists), saved)
            player.reloadPlaylists()
            compare(JSON.stringify(player.playlists), saved)
        }
        function test_long_metadata_stays_inside_its_columns_data() {
            const cases = []
            for (const width of [400, 680, 900])
                for (const lyrics of [false, true])
                    cases.push({tag: width + (lyrics ? "-lyrics" : "-no-lyrics"), size: width, lyrics: lyrics})
            return cases
        }
        function test_long_metadata_stays_inside_its_columns(data) {
            const originalWidth = player.width
            try {
                player.width = data.size
                player.trackMeta = {artist: "Bryan Adams", title: "A very long track title ".repeat(15),
                    album: "A very long album title ".repeat(15), genre: "Rock", year: "1995"}
                player.activeLyricText = data.lyrics ? "A very long lyric line ".repeat(15) : ""
                wait(150)
                const row = findChild(player, "normalMusicMetadataRow")
                const title = findChild(player, "normalMusicArtistLinks")
                verify(title.truncated, "Long artist/title text must elide")
                const labels = [title, findChild(player, "normalMusicDetails")]
                if (data.lyrics) labels.push(findChild(player, "normalMusicLiveLyrics"))
                labels.push(findChild(player, "normalMusicQueuePosition"))
                let previousEnd = 0
                for (const label of labels) {
                    const point = label.mapToItem(row, 0, 0)
                    verify(point.x >= previousEnd - 1, "Metadata columns overlap")
                    verify(point.x + label.width <= row.width + 1, "Metadata runs outside the player")
                    verify(label.paintedWidth <= label.width + 1, "Text paints beyond its column")
                    previousEnd = point.x + label.width
                }
            } finally {
                player.width = originalWidth
                player.activeLyricText = ""
            }
        }
        function test_playlist_name_click_starts_playback_data() {
            return [{tag: "normal", kind: "normal"}, {tag: "expanded", kind: "expanded"},
                    {tag: "detached", kind: "detached"}]
        }
        function test_playlist_name_click_starts_playback(data) {
            player.queueLimit = 3
            verify(player.savePlaylistItems(library.tracks, "Named playlist", ""))
            const large = data.kind !== "normal"
            let host
            if (large) {
                player.expandedPresentation = true
                expanded.detached = data.kind === "detached"
                expanded.sideTab = 2
                expanded.visible = true
                host = findChild(expanded, "musicExpandedSavedPlaylists")
            } else {
                player.queueOpen = true
                player.drawerTab = 1
                host = findChild(player, "musicPlaylistView")
            }
            tryCompare(host, "count", 1)
            host.forceLayout()
            tryVerify(function() { return host.itemAtIndex(0) !== null })
            const label = findChild(host.itemAtIndex(0), large ? "expandedPlaylistName" : "normalPlaylistName")
            verify(label !== null)
            wait(180)
            const loads = player.loadCount
            mouseClick(label, Math.min(40, label.width / 2), label.height / 2)
            compare(player.loadCount, loads + 1)
            verify(player.lastAutoplay)
            compare(player.queue.length, 160, "Clicking the name must play the whole saved playlist")
            compare(player.currentIndex, 0)
        }
        function test_existing_list_append_dedup_names_and_persistence() {
            verify(player.savePlaylistItems(player.queue, "Favorites", ""))
            collection.tabIndex = 4
            collection.visible = true
            tryVerify(function() { return collection.viewItems.length === 1 })
            collection.browseGroup(collection.viewItems[0])
            tryVerify(function() { return collection.viewItems.length === 1 })
            const mark = player.tracksForArtist("Mark Eliyahu")
            verify(player.savePlaylistItems(mark, "ignored", "Favorites"))
            compare(player.playlists.length, 1)
            compare(player.playlists[0].items.length, 107)
            tryVerify(function() { return collection.viewItems.length === 107 }, 5000,
                      "An already open playlist must refresh after appending tracks")
            verify(player.savePlaylistItems(mark, "ignored", "Favorites"))
            compare(player.playlists[0].items.length, 107)
            verify(!player.savePlaylistItems(mark, "ignored", "Deleted list"))
            verify(player.saveCurrentQueueAsPlaylist("Favorites"))
            compare(player.playlists[1].name, "Favorites (2)")
            verify(player.saveCurrentQueueAsPlaylist(""))
            verify(player.playlists[2].name.trim().length > 0)
            player.reloadPlaylists()
            compare(player.playlists.length, 3)
            compare(player.playlists[0].items.length, 107)
        }
        function test_library_click_limits_tracks_but_not_saved_playlist_tracks() {
            player.queueLimit = 3
            collection.visible = true
            collection.tabIndex = 0
            tryVerify(function() { return collection.viewItems.length === 160 })
            collection.activate(collection.viewItems[5])
            compare(player.queue.length, 3)
            verify(player.savePlaylistItems(library.tracks, "Full playlist", ""))
            collection.tabIndex = 4
            tryVerify(function() { return collection.viewItems.length === 1 })
            collection.browseGroup(collection.viewItems[0])
            tryVerify(function() { return collection.viewItems.length === 160 })
            const item = collection.viewItems[5]
            collection.activate(item)
            compare(player.queue.length, 160)
            compare(player.currentUrl, item.track.itemUrl)
            compare(player.playlists[0].items.length, 160)
            collection.closeGroup()
            collection.tabIndex = 0
        }
        function test_dialog_destination_adds_without_replacing() {
            player.saveCurrentQueueAsPlaylist("Existing")
            player.openArtist("Mark Eliyahu")
            const dialog = findChild(player, "musicPlaylistDialog")
            tryCompare(dialog, "visible", true)
            dialog.toggleAll()
            const destination = findChild(dialog, "playlistDestination")
            destination.currentIndex = 1
            destination.activated(1)
            compare(dialog.targetName, "Existing")
            compare(findChild(dialog, "playlistSuggestedName").visible, false)
            mouseClick(findChild(dialog, "playlistSaveSelection"))
            compare(player.playlists.length, 1)
            compare(player.playlists[0].items.length, 107)
        }
        function test_shared_button_label_has_both_languages() {
            const normal = findChild(player, "musicAddPlaylist")
            const large = findChild(expanded, "expandedAddPlaylist")
            language.language = "tr"
            compare(language.t("music_add_playlist"), "+ Çalma listesi")
            compare(normal.text, "+ Çalma listesi")
            compare(large.text, normal.text)
            language.language = "en"
            compare(language.t("music_add_playlist"), "+ Playlist")
            compare(normal.text, "+ Playlist")
            compare(large.text, normal.text)
        }
    }
}
