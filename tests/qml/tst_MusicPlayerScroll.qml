import QtQuick
import QtQuick.Controls
import QtTest
import Lurviko.App

Item {
    width: 1200
    height: 850
    QtObject {
        id: language
        property string language: "en"
        function t(key) { return key }
    }
    Rectangle { id: desktopContent; anchors.fill: parent; color: "#74889a" }
    MusicPlayer {
        id: player
        width: 1200
        height: 360
        lang: language
        function loadCurrent(autoplay) {}
    }
    MusicExpandedView {
        id: expanded
        anchors.fill: parent
        player: player
        lang: language
        visible: false
    }
    TestCase {
        name: "MusicPlayerScroll"
        when: windowShown
        property int savedWheelStep
        function initTestCase() {
            savedWheelStep = AppTheme.wheelScrollStep
            AppTheme.setWheelScrollStep(180)
        }
        function cleanupTestCase() { AppTheme.setWheelScrollStep(savedWheelStep) }
        function init() {
            AppTheme.setWheelScrollStep(180)
            const tracks = []
            const playlists = []
            const lines = []
            for (let i = 0; i < 100; ++i) {
                tracks.push({itemUrl: "file:///scroll-fixture/track-" + i + ".mp3", name: "Track " + i})
                playlists.push({name: "Playlist " + i, items: []})
                lines.push({time: i * 1000, text: "Fixture lyric line " + i})
            }
            player.queue = tracks
            player.playlists = playlists
            player.currentIndex = 0
            player.lyricsLines = lines
            player.lyricsText = lines.map(function(line) { return line.text }).join("\n")
            player.activeLyricIndex = -1
            player.lyricsSynced = false
            player.panelVisible = true
            player.expandedPresentation = false
            player.opened = true
            player.queueOpen = false
            expanded.visible = false
        }
        function cleanup() {
            findChild(player, "musicLyricsPopup").close()
            expanded.visible = false
            player.opened = false
            wait(60)
        }
        function test_expanded_qt_theme_has_a_safe_backdrop_in_both_modes() {
            const oldQt = AppTheme.useQtTheme
            try {
                inputTestHelper.setSystemThemeTestAppearance(false, true)
                AppTheme.desktopAppearance.refresh()
                AppTheme.setQtTheme(true)
                expanded.backdropSource = desktopContent
                expanded.visible = true
                const backdrop = findChild(expanded, "musicExpandedQtBackdrop")
                verify(backdrop !== null)
                verify(backdrop.visible)
                compare(backdrop.sourceItem, desktopContent)
                // Software rendering must provide a readable native fill;
                // GPU rendering can blur the sibling beneath the music view.
                if (expanded.GraphicsInfo.api !== GraphicsInfo.Software && AppTheme.background.a < 1) {
                    verify(backdrop.blurActive)
                    verify(findChild(backdrop, "surfaceBlurLoader").active)
                } else compare(backdrop.color.a, 1)
                expanded.detached = true
                verify(!backdrop.visible, "Detached window retains its own native/KWin background")
            } finally {
                expanded.detached = false
                expanded.backdropSource = null
                AppTheme.setQtTheme(oldQt)
                inputTestHelper.restoreSystemThemeTestAppearance()
                AppTheme.desktopAppearance.refresh()
            }
        }

        function test_player_wheel_uses_shared_speed_data() {
            const rows = [
                {tag: "normal-queue", view: "musicQueueList", tab: 0},
                {tag: "normal-playlists", view: "musicPlaylistView", tab: 1},
                {tag: "normal-synced-lyrics", view: "musicLyricList", lyrics: true, synced: true},
                {tag: "normal-plain-lyrics", view: "musicPlainLyricsScroll", lyrics: true, synced: false}
            ]
            for (const detached of [false, true]) {
                const prefix = detached ? "detached-" : "expanded-"
                for (const entry of [
                    {view: "musicExpandedTrackList", tab: 0},
                    {view: "musicExpandedSyncedLyrics", tab: 0, synced: true},
                    {view: "musicExpandedPlainLyricsScroll", tab: 0, synced: false},
                    {view: "musicExpandedSecondaryQueue", tab: 1},
                    {view: "musicExpandedSavedPlaylists", tab: 2}
                ]) rows.push(Object.assign({tag: prefix + entry.view, expanded: true, detached: detached}, entry))
            }
            return rows
        }
        function test_player_wheel_uses_shared_speed(data) {
            let host = player
            if (data.expanded) {
                player.expandedPresentation = true
                expanded.detached = data.detached
                expanded.sideTab = data.tab
                player.lyricsSynced = !!data.synced
                expanded.visible = true
                host = expanded
            } else if (data.lyrics) {
                player.lyricsSynced = !!data.synced
                findChild(player, "musicLyricsPopup").open()
            } else {
                player.drawerTab = data.tab
                player.queueOpen = true
            }
            const area = findChild(host, data.view)
            verify(area !== null)
            tryCompare(area, "visible", true)
            // ScrollView owns a Flickable; ListView is already one.
            const view = data.view.endsWith("Scroll") ? area.contentItem : area
            tryVerify(function() { return view.contentHeight > view.height + 1000 && view.height > 50 })
            wait(220)
            const top = view.originY || 0
            view.contentY = top
            const track = player.currentUrl
            mouseWheel(area, area.width / 2, area.height / 2, 0, -120, Qt.NoButton, Qt.NoModifier, 0)
            wait(180)
            verify(Math.abs(view.contentY - top - 180) < 3, "A notch must use the shared 180 px setting")
            mouseWheel(area, area.width / 2, area.height / 2, 0, 120, Qt.NoButton, Qt.NoModifier, 0)
            wait(180)
            verify(Math.abs(view.contentY - top) < 3, "Reversing the wheel must return to the top")
            mouseWheel(area, area.width / 2, area.height / 2, 0, -120, Qt.NoButton, Qt.NoModifier, 0)
            mouseWheel(area, area.width / 2, area.height / 2, 0, -120, Qt.NoButton, Qt.NoModifier, 0)
            wait(180)
            verify(view.contentY - top > 350 && view.contentY - top < 380,
                   "Fast wheel events must accumulate with the shared mild acceleration")
            compare(player.currentUrl, track, "Scrolling must not activate a track or seek lyrics")
            AppTheme.setWheelScrollStep(300)
            wait(320)
            const before = view.contentY
            mouseWheel(area, area.width / 2, area.height / 2, 0, -120, Qt.NoButton, Qt.NoModifier, 0)
            wait(180)
            verify(Math.abs(view.contentY - before - 300) < 3, "Player must follow changes to the shared speed setting")
            AppTheme.setWheelScrollStep(180)
        }
    }
}
