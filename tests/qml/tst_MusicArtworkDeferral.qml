import QtQuick
import QtTest
import Lurviko.App

Item {
    width: 200
    height: 200
    MusicArtwork { id: art; width: 64; height: 64; artworkSource: "image://gfilethumb/a.mp3|1" }

    TestCase {
        name: "MusicArtworkDeferral"
        when: windowShown

        function test_fast_scroll_defers_new_covers() {
            // Requested at once when created normally.
            compare(art.committedSource, "image://gfilethumb/a.mp3|1")
            // While scrolling fast, a recycled card does not request its new
            // cover, and never shows the previous track's one.
            art.deferred = true
            art.artworkSource = "image://gfilethumb/b.mp3|1"
            compare(art.committedSource, "")
            // Neither the old cover nor the fallback: the card stays plain.
            compare(String(art.source), "")
            art.artworkSource = "image://gfilethumb/c.mp3|1"
            compare(art.committedSource, "")
            // Settled: only the cover now on the card is requested.
            art.deferred = false
            compare(art.committedSource, "image://gfilethumb/c.mp3|1")
            verify(String(art.source).indexOf("c.mp3") >= 0)
            // Without a cover, the fallback is shown again once settled.
            art.artworkSource = ""
            compare(String(art.source), String(art.fallbackSource))
        }
    }
}
