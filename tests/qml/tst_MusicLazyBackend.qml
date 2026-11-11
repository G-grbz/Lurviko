import QtQuick
import QtTest
import QtMultimedia
import Lurviko.App

Item {
    width: 800
    height: 600
    QtObject {
        id: language
        property string language: "en"
        function t(key) { return key }
    }
    QtObject {
        id: index
        property var tracks: []
        function filesForCategory(category) { return [] }
    }
    MusicPlayer {
        id: player
        lang: language
        contentIndexSource: index
    }
    TestCase {
        name: "MusicLazyBackend"
        when: windowShown

        function backend() { return findChild(player, "musicPlayerBackend") }

        function test_backendIsCreatedOnlyForPlayback() {
            verify(backend() !== null)
            // An idle global player must not initialise the multimedia stack.
            compare(backend().active, false)
            compare(player.playing, false)
            compare(player.paused, false)
            compare(player.position, 0)
            compare(player.duration, 0)
            player.pause()
            player.toggle()   // no current track: still idle
            compare(backend().active, false)
        }

        function test_muteStateDoesNotNeedBackend() {
            compare(player.muted, false)
            player.toggleMute()
            compare(player.muted, true)
            compare(backend().active, false)
            player.toggleMute()
            compare(player.muted, false)
        }

        function test_playCreatesBackendOnDemand() {
            compare(backend().active, false)
            // Playing through the idle stand-in creates the real backend and
            // hands over its state (no media, so nothing is opened here).
            player.player.play()
            tryVerify(function() { return backend().active && backend().item !== null })
            compare(player.player, backend().item)
            compare(String(backend().item.source), "")
            player.muted = true
            compare(backend().item.audioOutput.muted, true)
            player.muted = false
            compare(backend().item.audioOutput.muted, false)
            player.pause()
        }
    }
}
