import QtQuick
import QtTest
import Lurviko.App

Item {
    width: 1000
    height: 700
    QtObject {
        id: language
        property string language: "en"
        function t(key) { return key }
    }
    QtObject {
        id: index
        property var tracks: []
        function filesForCategory(category) { return category === "music" ? tracks : [] }
    }
    MusicPlayer {
        id: player
        lang: language
        contentIndexSource: index
        // Exercise queue construction without opening files or an audio device.
        property int loadCount: 0
        property bool lastAutoplay: false
        function loadCurrent(autoplay) { ++loadCount; lastAutoplay = !!autoplay }
    }
    MusicExpandedView {
        id: expandedView
        width: 1000; height: 700
        visible: false
        player: player
        lang: language
        onMinimizeRequested: player.minimize()
    }
    SignalSpy { id: minimizedSpy; target: expandedView; signalName: "minimizeRequested" }
    SignalSpy { id: closedSpy; target: player; signalName: "requestClose" }
    TestCase {
        name: "MusicPlayerQueue"
        when: windowShown
        function init() {
            const tracks = []
            for (let i = 0; i < 8; ++i)
                tracks.push({url: "file:///collection/track-" + i + ".mp3", name: "Track " + i})
            index.tracks = tracks
            player.queueLimit = 3
            player.sourceLibrary = player.normalizedLibrary(tracks.slice(0, 2))
            player.queue = player.sourceLibrary.slice()
            player.shuffleMode = false
            player.repeatMode = 0
            player.currentIndex = 0
            player.loadCount = 0
            player.persistPlaylists([])
            player.rebuildPlaybackOrder(0)
        }
        function cleanup() { player.persistPlaylists([]) }
        function test_saved_playlist_is_not_limited_or_modified_by_temporary_tracks() {
            const tracks = []
            for (let i = 0; i < 300; ++i)
                tracks.push({itemUrl: "file:///playlist/" + i + ".mp3", name: "Track " + i})
            player.queueLimit = 50
            verify(player.savePlaylistItems(tracks, "Long playlist", ""))
            const saved = JSON.stringify(player.playlists)
            player.playPlaylist(0)
            compare(player.queue.length, 300)
            compare(player.playbackOrder.length, 300)
            player.playTemporaryQueueItem({itemUrl: "file:///extra.mp3", name: "Extra"})
            compare(player.queue.length, 301)
            compare(player.currentIndex, 300)
            verify(player.lastAutoplay)
            compare(JSON.stringify(player.playlists), saved)
            player.reloadPlaylists()
            compare(JSON.stringify(player.playlists), saved, "Temporary additions must not be persisted")
            player.playTemporaryQueueItem(tracks[125])
            compare(player.queue.length, 301, "An existing track must be selected without duplication")
            compare(player.currentIndex, 125)
            verify(player.next(false))
            compare(player.currentIndex, 126)
        }
        function test_starting_from_a_track_respects_queue_limit() {
            player.openUrl(index.tracks[6].url, "Track 6", "", index.tracks)
            compare(player.queue.length, 3)
            compare(player.sourceLibrary.length, 8, "Keep the collection available for future queues")
            compare(player.queue[0].itemUrl, index.tracks[6].url)
            compare(player.queue[1].itemUrl, index.tracks[7].url)
            compare(player.queue[2].itemUrl, index.tracks[0].url)
            verify(player.lastAutoplay)
            player.playTemporaryQueueItem(index.tracks[1])
            compare(player.queue.length, 4, "Later additions must not be limited")
            compare(player.currentUrl, index.tracks[1].url)
        }
        function test_starting_inside_a_saved_playlist_retains_all_tracks() {
            player.openUrl(index.tracks[2].url, "Track 2", "", index.tracks, false)
            compare(player.queue.length, 8)
            compare(player.queue[0].itemUrl, index.tracks[2].url)
        }
        function test_random_queue_can_grow_without_losing_shuffle_order() {
            player.shuffleMode = true
            player.freshQueue(false)
            compare(player.queue.length, 3)
            const order = player.playbackOrder.slice()
            const urls = player.queue.map(function(item) { return item.itemUrl })
            player.playTemporaryQueueItem({itemUrl: "file:///extra.mp3", name: "Extra"})
            compare(player.queue.length, 4)
            compare(JSON.stringify(player.playbackOrder), JSON.stringify(order.concat([3])))
            compare(player.currentIndex, 3)
            verify(player.lastAutoplay)
            player.playTemporaryQueueItem(player.queue[order[0]])
            compare(player.queue.length, 4)
            verify(player.next(false))
            compare(player.currentIndex, order[1])
            for (let i = 0; i < urls.length; ++i) compare(player.queue[i].itemUrl, urls[i])
        }
        function test_selected_track_continues_with_following_queue_item() {
            player.queue = player.normalizedLibrary(index.tracks)
            player.selectQueueIndex(3, false)
            compare(player.currentIndex, 3)
            verify(player.next(false))
            compare(player.currentIndex, 4, "End of media must continue after the selected track")
            player.previous()
            compare(player.currentIndex, 3)
            player.selectQueueIndex(7, false)
            compare(player.next(false), false, "The last track must stop when repeat is off")
            compare(player.currentIndex, 7)
            player.repeatMode = 1
            verify(player.next(false))
            compare(player.currentIndex, 0, "Repeat all must wrap only at the end of the queue")
        }
        function test_queue_selection_preserves_shuffle_order() {
            player.queue = player.normalizedLibrary(index.tracks)
            player.shuffleMode = true
            player.rebuildPlaybackOrder(0)
            const order = player.playbackOrder.slice()
            const selected = order[3]
            player.selectQueueIndex(selected, false)
            compare(JSON.stringify(player.playbackOrder), JSON.stringify(order))
            verify(player.next(false))
            compare(player.currentIndex, order[4])
            player.shuffleMode = false
            player.rebuildPlaybackOrder(player.currentIndex)
            compare(player.playbackOrderPosition, player.currentIndex)
        }
        function test_expanded_and_detached_minimize_preserves_track() {
            player.currentIndex = 1
            player.opened = true
            const track = player.currentItem.itemUrl
            const originalQueue = JSON.stringify(player.queue)
            for (const detached of [false, true]) {
                player.panelVisible = true
                player.queueOpen = true
                expandedView.detached = detached
                expandedView.visible = true
                minimizedSpy.clear()
                closedSpy.clear()
                const button = findChild(expandedView, "minimizeExpandedMusic")
                verify(button !== null)
                mouseClick(button)
                compare(minimizedSpy.count, 1)
                compare(closedSpy.count, 0)
                compare(player.panelVisible, false)
                compare(player.queueOpen, false)
                compare(player.opened, true)
                compare(player.currentItem.itemUrl, track)
                compare(JSON.stringify(player.queue), originalQueue)
                expandedView.visible = false
            }
            player.opened = false
        }
        function test_new_queue_uses_collection_outside_playing_album() {
            player.freshQueue(false)
            compare(player.queue.length, 3)
            const urls = ({})
            for (const track of player.queue) {
                verify(track.itemUrl !== index.tracks[0].url && track.itemUrl !== index.tracks[1].url,
                       "New queue was restricted to the playing album")
                verify(!urls[track.itemUrl], "Queue contains duplicate tracks")
                urls[track.itemUrl] = true
            }
            compare(player.currentIndex, 0)
        }
        function test_expanded_and_detached_controls_share_player_actions() {
            player.currentIndex = 0
            player.opened = true
            player.lyricsText = "Fixture lyrics"
            for (const detached of [false, true]) {
                expandedView.detached = detached
                expandedView.visible = true
                const refresh = findChild(expandedView, "expandedRefreshQueue")
                verify(refresh !== null)
                compare(findChild(expandedView, "expandedShowQueue"), null)
                compare(findChild(expandedView, "expandedShowPlaylists"), null)
                compare(findChild(expandedView, "expandedShowLyrics"), null)
                compare(findChild(expandedView, "expandedFavorite"), null)
                mouseClick(refresh)
                compare(player.queue.length, 3)
                for (const track of player.queue)
                    verify(track.itemUrl.startsWith("file:///collection/"))
                expandedView.visible = false
            }
            player.opened = false
        }
        function test_large_queue_includes_whole_collection_once() {
            player.queueLimit = 50
            player.freshQueue(false)
            compare(player.queue.length, 8)
            for (const track of index.tracks)
                verify(player.findUrl(player.queue, track.url) >= 0)
        }
        function test_album_playback_and_unindexed_fallback() {
            player.buildQueue(player.sourceLibrary[0].itemUrl, false)
            compare(player.queue.length, 2, "Starting an album must retain the album queue")
            index.tracks = []
            player.freshQueue(false)
            compare(player.queue.length, 2)
            verify(player.queue[0].itemUrl.length > 0)
        }
    }
}
