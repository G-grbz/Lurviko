import QtQuick
import QtTest
import Lurviko.App

Item {
    width: 1000
    height: 200
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
        function loadCurrent(autoplay) {}
    }
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
