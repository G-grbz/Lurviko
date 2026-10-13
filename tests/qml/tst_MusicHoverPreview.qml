import QtQuick
import QtQuick.Controls
import QtTest
import Lurviko.App
import Lurviko.Backend

Item {
    width: 900
    height: 700
    ApplicationWindow {
        id: window
        width: 900
        height: 700
        visible: true
        LanguageManager { id: language }
        QtObject { id: library; property var tracks: [] }
        QtObject {
            id: player
            property bool playing: true
            property real volume: 0.35
            property string currentUrl: "file:///current.mp3"
            property var queue: [{itemUrl: currentUrl}]
            property var playlists: []
            property int playCount: 0
            function openUrl(url, title, artwork, items, limited) { ++playCount }
        }
        MusicLibraryView {
            id: music
            anchors.fill: parent
            lang: language
            manager: library
            player: player
        }
        Item { id: cornerAnchor; x: 840; y: 630; width: 40; height: 30 }
        Rectangle { id: maskTestStage; anchors.fill: parent; color: "#4cc198"; visible: false; z: 20000 }
        Component {
            id: audioPreview
            MusicHoverPreview {
                track: ({itemUrl: musicHoverFixtureUrl.toString(), title: "Fixture tone", artist: "Artist"})
                lang: language
                anchorItem: cornerAnchor
                viewport: window.contentItem
                silent: true
            }
        }
        Component { id: coverFixture; MusicArtwork { width: 64; height: 64 } }
        Component { id: realPlayerFixture; MusicPlayer { lang: language; width: 600 } }
        TestCase {
            name: "MusicHoverPreview"
            when: windowShown
            property var originalSettings
            function initTestCase() {
                originalSettings = [AppTheme.musicHoverEnabled, AppTheme.musicHoverAnimationsEnabled, AppTheme.musicHoverDelayMs]
            }
            function cleanupTestCase() { AppTheme.setMusicHoverPreferences(...originalSettings) }
            function init() {
                language.language = "en"
                player.playing = true
                player.playCount = 0
                AppTheme.setMusicHoverPreferences(true, false, 600)
                music.visible = true
                music.tabIndex = 0
                const tracks = []
                for (let i = 0; i < 45; ++i)
                    tracks.push({itemUrl: i === 0 ? musicHoverFixtureUrl.toString() : "file:///unused-" + i + ".mp3",
                        title: "Track " + String(i).padStart(2, "0"), artist: "Artist", album: "Album", year: "2026", genre: "Rock"})
                library.tracks = tracks
                tryVerify(function() { return music.viewItems.length === 45 })
            }
            function cleanup() {
                maskTestStage.visible = false
                music.dismissHoverPreview()
                mouseMove(window.contentItem, 895, 695)
                music.visible = false
                wait(200)
            }
            function loader() { return findChild(music, "musicHoverLoader") }
            function firstMouse() {
                const grid = findChild(music, "musicLibraryGrid")
                grid.positionViewAtBeginning()
                grid.forceLayout()
                tryVerify(function() { return grid.itemAtIndex(0) !== null })
                return findChild(grid.itemAtIndex(0), "musicTrackMouse")
            }
            function enterTrack() {
                const target = firstMouse()
                verify(target !== null)
                mouseMove(window.contentItem, 895, 695)
                wait(20)
                mouseMove(target, 40, target.height / 2)
            }
            function test_music_artwork_uses_shared_png_and_switches_to_real_cover() {
                const cover = createTemporaryObject(coverFixture, window.contentItem)
                verify(cover !== null)
                tryCompare(cover, "status", Image.Ready)
                compare(cover.source.toString(), AppTheme.icon("music-cover.png"))
                cover.artworkSource = AppTheme.icon("logo.png")
                tryCompare(cover, "status", Image.Ready)
                compare(cover.source.toString(), AppTheme.icon("logo.png"))
                ignoreWarning(/.*Cannot open: qrc:\/missing-music-cover.png/)
                cover.artworkSource = "qrc:/missing-music-cover.png"
                tryCompare(cover, "coverFailed", true)
                tryCompare(cover, "status", Image.Ready)
                compare(cover.source.toString(), AppTheme.icon("music-cover.png"))
                cover.artworkSource = AppTheme.icon("logo.png")
                tryCompare(cover, "status", Image.Ready)
                compare(cover.coverFailed, false)
                compare(cover.source.toString(), AppTheme.icon("logo.png"))
                cover.artworkSource = ""
                tryCompare(cover, "status", Image.Ready)
                compare(cover.source.toString(), AppTheme.icon("music-cover.png"))
            }
            function test_albums_without_covers_use_same_fallback() {
                music.tabIndex = 1
                tryVerify(function() { return music.viewItems.length === 1 })
                const grid = findChild(music, "musicLibraryGrid")
                grid.forceLayout()
                tryVerify(function() { return grid.itemAtIndex(0) !== null })
                const cover = findChild(grid.itemAtIndex(0), "musicCollectionCover")
                verify(cover !== null)
                tryCompare(cover, "status", Image.Ready)
                compare(cover.source.toString(), AppTheme.icon("music-cover.png"))
            }
            function test_inactive_preview_does_not_keep_modal_overlay_visible() {
                music.dismissHoverPreview()
                wait(50)
                compare(window.Overlay.overlay.visible, false,
                        "A hidden music preview must not keep the application popup overlay alive")
            }
            function test_delay_and_cancelled_hover_create_no_decoder() {
                AppTheme.setMusicHoverPreferences(true, false, 1000)
                enterTrack()
                wait(250)
                compare(loader().active, false)
                mouseMove(window.contentItem, 895, 695)
                wait(1050)
                compare(loader().active, false)
                AppTheme.setMusicHoverPreferences(true, false, 200)
                enterTrack()
                tryCompare(loader(), "active", true, 800)
                verify(loader().item !== null)
                compare(loader().item.decoderActive, false, "Playing music must suppress preview audio")
                compare(findChild(loader().item, "musicHoverMediaPlayer"), null)
            }
            function test_preview_is_passive_and_unloads_on_leaving_the_track() {
                enterTrack()
                tryCompare(loader(), "active", true, 1200)
                const card = loader().item
                compare(card.width, 340)
                compare(findChild(card, "musicHoverPlay"), null, "Hover must not invoke the main player")
                const artwork = findChild(card, "musicHoverCover")
                tryCompare(artwork, "status", Image.Ready)
                compare(artwork.source.toString(), AppTheme.icon("music-cover.png"))
                const point = card.mapToItem(window.contentItem, 0, 0)
                verify(point.x >= 0 && point.x + card.width <= window.width)
                verify(point.y >= 0 && point.y + card.height <= window.height)
                compare(card.enabled, false, "A preview must never intercept file clicks or focus")
                compare(window.Overlay.overlay.visible, false)
                wait(350)
                compare(loader().active, true)
                const snapshot = grabImage(card)
                verify(snapshot.red(20, 20) > 25 && snapshot.red(20, 20) < 130,
                       "The cover background must remain visible with software rendering")
                snapshot.save("/tmp/lurviko-music-hover-preview.png")
                mouseMove(window.contentItem, 895, 695)
                tryCompare(loader(), "active", false)
                compare(loader().item, null)
            }
            function test_scroll_disable_and_page_switch_cancel_hover() {
                enterTrack()
                tryCompare(loader(), "active", true, 1200)
                const grid = findChild(music, "musicLibraryGrid")
                grid.contentY += 90
                compare(loader().active, false)
                enterTrack()
                tryCompare(loader(), "active", true, 1200)
                AppTheme.setMusicHoverPreferences(false, false, 600)
                compare(loader().active, false)
                enterTrack()
                wait(700)
                compare(loader().active, false)
                AppTheme.setMusicHoverPreferences(true, false, 200)
                enterTrack()
                tryCompare(loader(), "active", true, 800)
                music.visible = false
                compare(loader().active, false)
            }
            function test_main_playback_unloads_preview_without_changing_queue() {
                enterTrack()
                tryCompare(loader(), "active", true, 1200)
                const card = loader().item
                card.silent = true
                const queue = JSON.stringify(player.queue)
                const url = player.currentUrl
                player.playing = false
                tryVerify(function() { return card.previewPlaying }, 4000)
                const media = findChild(card, "musicHoverMediaPlayer")
                verify(media !== null)
                compare(media.duration, 100000)
                compare(media.startPosition, 45000)
                compare(media.activeVideoTrack, -1)
                compare(media.activeSubtitleTrack, -1)
                verify(media.position >= 44500, "Preview must seek into the track before playing")
                player.playing = true
                tryCompare(card, "decoderActive", false)
                tryVerify(function() { return findChild(card, "musicHoverMediaPlayer") === null }, 1000)
                compare(JSON.stringify(player.queue), queue)
                compare(player.currentUrl, url)
                compare(player.playCount, 0)
            }
            function test_embedded_cover_audio_rapid_lifecycle_keeps_ui_responsive() {
                for (let i = 0; i < 6; ++i) {
                    const card = createTemporaryObject(audioPreview, window.contentItem, {
                        track: {itemUrl: musicHoverCoveredFixtureUrl.toString(), title: "MP3 with cover"},
                        silent: !musicHoverUseAudioDevice, previewVolume: 0
                    })
                    verify(card !== null)
                    tryVerify(function() { return card.previewPlaying }, 4000)
                    const media = findChild(card, "musicHoverMediaPlayer")
                    verify(media.duration > 99000 && media.duration < 101000)
                    compare(media.startPosition, card.sampleStart(media.duration))
                    compare(media.activeVideoTrack, -1)
                    // Qt may omit attached pictures from its exposed videoTracks;
                    // the runner verifies that this MP3 really embeds a cover.
                    verify(media.audioTracks.length > 0)
                    // Destroy during active playback, then immediately start another.
                    card.destroy()
                    wait(30)
                }
            }
            function test_external_track_with_main_player_and_real_audio() {
                if (!musicHoverExternalFixtureUrl.toString().length) {
                    skip("Set LURVIKO_QML_EXTERNAL_AUDIO_FIXTURE to exercise a real collection track")
                    return
                }
                const mainPlayer = createTemporaryObject(realPlayerFixture, window.contentItem)
                verify(mainPlayer !== null)
                for (let i = 0; i < 8; ++i) {
                    const card = createTemporaryObject(audioPreview, window.contentItem, {
                        track: {itemUrl: musicHoverExternalFixtureUrl.toString(), title: "Collection track"},
                        silent: !musicHoverUseAudioDevice, previewVolume: 0
                    })
                    tryVerify(function() { return card.previewPlaying }, 10000)
                    wait(250)
                    card.destroy()
                    wait(30)
                }
            }
            function test_cover_and_gradient_stay_inside_rounded_frame() {
                maskTestStage.visible = true
                const card = createTemporaryObject(audioPreview, maskTestStage, {audioAllowed: false})
                verify(card !== null)
                tryCompare(findChild(card, "musicHoverCover"), "status", Image.Ready)
                wait(200)
                const snapshot = grabImage(card)
                snapshot.save("/tmp/lurviko-music-hover-rounded.png")
                // Quick Test grabs the composited window region. Outside the
                // rounded shape all four corners must show the stage beneath.
                // The captured image uses physical pixels (e.g. 2.5x on the
                // user's Wayland display), not the card's logical dimensions.
                const corners = [[0, 0], [snapshot.width - 1, 0],
                                 [0, snapshot.height - 1], [snapshot.width - 1, snapshot.height - 1]]
                for (const point of corners) {
                    compare(snapshot.red(point[0], point[1]), 76, "Rounded corner must show the underlying stage")
                    compare(snapshot.green(point[0], point[1]), 193)
                    compare(snapshot.blue(point[0], point[1]), 152)
                }
                verify(snapshot.green(20, 20) !== 193, "Mask must preserve the actual cover inside the frame")
                card.destroy()
                wait(20)
            }
            function test_real_decoder_stops_at_thirty_second_boundary_and_is_released() {
                const card = createTemporaryObject(audioPreview, window.contentItem)
                verify(card !== null)
                compare(card.sampleStart(8000), 0)
                compare(card.sampleStart(20000), 0)
                compare(card.sampleStart(40000), 10000)
                compare(card.sampleStart(120000), 54000)
                tryVerify(function() { return card.previewPlaying }, 4000)
                compare(card.sampleDurationMs, 30000)
                const media = findChild(card, "musicHoverMediaPlayer")
                compare(media.startPosition, 45000)
                compare(findChild(card, "musicHoverExpiry").interval, 30000)
                // Exercise the real end boundary without waiting thirty seconds.
                media.position = media.startPosition + 29800
                tryCompare(card, "sampleFinished", true, 4000)
                compare(card.decoderActive, false)
                tryVerify(function() { return findChild(card, "musicHoverMediaPlayer") === null }, 1000)
            }
        }
    }
}
