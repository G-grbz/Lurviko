import QtQuick
import QtTest
import Lurviko.App

Item {
    id: fixture
    width: 1020
    height: 700
    QtObject { id: language; property string language: "en"; function t(key) { return key } }
    QtObject { id: library; property var tracks: [] }
    QtObject { id: player; property var playlists: []; property string currentUrl: "" }
    MusicLibraryView {
        id: music
        anchors.fill: parent
        lang: language
        manager: library
        player: player
    }
    SignalSpy { id: revealSpy; target: music; signalName: "revealRequested" }
    TestCase {
        name: "MusicLibraryView"
        when: windowShown
        property bool savedHoverEnabled
        function initTestCase() {
            savedHoverEnabled = AppTheme.musicHoverEnabled
            AppTheme.setMusicHoverPreferences(false, AppTheme.musicHoverAnimationsEnabled, AppTheme.musicHoverDelayMs)
            const tracks = []
            for (let i = 0; i < 2836; ++i)
                tracks.push({name: "Track " + i, title: "Track " + String(i).padStart(4, "0"),
                             artist: "Artist", album: "Album", itemUrl: "file:///tmp/music-" + i + ".mp3",
                             thumbnailSource: "qrc:/qt/qml/Lurviko/App/assets/icons/logo.png"})
            library.tracks = tracks
        }
        function cleanupTestCase() {
            AppTheme.setMusicHoverPreferences(savedHoverEnabled, AppTheme.musicHoverAnimationsEnabled, AppTheme.musicHoverDelayMs)
        }
        function init() {
            music.visible = true
            music.groupSelection = null
            music.tabIndex = 0
            music.cardSize = 168
            tryVerify(function() { return music.viewItems.length === 2836 })
            const grid = findChild(music, "musicLibraryGrid")
            grid.positionViewAtBeginning()
            wait(30)
        }
        function test_resize_keeps_visible_track_across_column_changes() {
            const grid = findChild(music, "musicLibraryGrid")
            grid.positionViewAtIndex(1100, GridView.Beginning)
            wait(50)
            for (const size of [250, 300, 200, 104, 360, 168]) {
                music.prepareForResize()
                const anchor = music.resizeAnchorIndex
                const offset = music.resizeAnchorOffset
                music.cardSize = size
                compare(grid.cellWidth, Math.floor(grid.width / Math.floor(grid.width / music.desiredCardWidth)))
                verify(grid.contentY > 500, "Resize briefly reset scrolling to the top")
                wait(35)
                const item = grid.itemAtIndex(anchor)
                verify(item !== null)
                verify(Math.abs(grid.contentY - item.y - offset) < 2, "Visible track moved on column change")
            }
        }
        function test_pixel_steps_change_height_without_column_jump() {
            const grid = findChild(music, "musicLibraryGrid")
            music.cardSize = 250
            const before = grid.cellHeight
            music.prepareForResize()
            music.cardSize = 251
            verify(grid.cellHeight >= before && grid.cellHeight - before <= 2)
            music.prepareForResize()
            music.cardSize = 266
            verify(grid.cellHeight > before)
        }
        function test_hidden_library_releases_derived_lists() {
            music.visible = false
            tryCompare(music, "viewItems", [])
            compare(music.allTracks.length, 0)
            music.visible = true
            tryVerify(function() { return music.viewItems.length === 2836 })
        }
        function test_tracks_fit_multiple_rows_above_an_open_player() {
            const grid = findChild(music, "musicLibraryGrid")
            fixture.height = 300
            for (const size of [104, 168, 250, 360]) {
                music.prepareForResize()
                music.cardSize = size
                grid.forceLayout()
                verify(grid.cellHeight * 2 <= grid.height, "Music was squeezed into a single row")
                const columns = Math.floor(grid.width / grid.cellWidth)
                verify(columns * Math.floor(grid.height / grid.cellHeight) >= 6,
                       "Open player leaves too few usable track rows")
            }
            fixture.height = 700
        }
        function test_albums_artists_and_tracks_use_their_own_layout() {
            const grid = findChild(music, "musicLibraryGrid")
            const trackHeight = grid.cellHeight
            music.tabIndex = 1
            tryVerify(function() { return music.viewItems.length === 1 })
            verify(grid.cellHeight > trackHeight * 2, "Albums need cover-oriented cards")
            music.browseGroup(music.viewItems[0])
            tryVerify(function() { return music.viewItems.length === 2836 })
            verify(music.trackTab, "Opening an album must show its tracks")
            verify(grid.cellHeight < 110, "Album tracks should use compact rows")
            music.closeGroup()
            tryVerify(function() { return music.viewItems.length === 1 })
            music.tabIndex = 2
            tryVerify(function() { return music.viewItems.length === 1 && music.viewItems[0].kind === "artist" })
            music.browseGroup(music.viewItems[0])
            tryVerify(function() { return music.viewItems.length === 2836 })
            music.closeGroup()
        }
        function test_loaded_covers_survive_repeated_tab_and_density_changes() {
            const grid = findChild(music, "musicLibraryGrid")
            tryVerify(function() {
                const cover = findChild(music, "musicTrackCover")
                return cover && cover.status === Image.Ready
            })
            for (let cycle = 0; cycle < 3; ++cycle) {
                for (const tab of [1, 2, 0, 5, 6, 3, 4, 7, 0]) {
                    music.tabIndex = tab
                    wait(120)
                    music.cardSize = cycle % 2 ? 360 : 104
                    wait(30)
                    if (tab === 1 || tab === 2) {
                        tryVerify(function() {
                            return music.viewItems.length === 1
                                && music.viewItems[0].kind === (tab === 1 ? "album" : "artist")
                        })
                        tryVerify(function() {
                            const cover = findChild(music, "musicCollectionCover")
                            return cover && cover.status === Image.Ready
                        })
                        music.browseGroup(music.viewItems[0])
                        tryCompare(grid, "count", 2836)
                        wait(100)
                        music.closeGroup()
                        tryVerify(function() { return music.viewItems.length === 1 })
                    }
                }
            }
            compare(music.tabIndex, 0)
            tryCompare(grid, "count", 2836)
        }
        function test_right_click_track_has_folder_and_external_actions() {
            const grid = findChild(music, "musicLibraryGrid")
            grid.forceLayout()
            tryVerify(function() { return grid.itemAtIndex(0) !== null })
            const area = findChild(grid.itemAtIndex(0), "musicTrackMouse")
            verify(area !== null)
            mouseClick(area, 18, 18, Qt.RightButton)
            const menu = findChild(music, "musicTrackMenu")
            tryCompare(menu, "visible", true)
            verify(music.contextTrack.itemUrl.length > 0)
            const target = music.contextTrack.itemUrl
            const folderAction = findChild(music, "musicOpenContainingFolder")
            const externalAction = findChild(music, "musicOpenExternally")
            verify(folderAction !== null && externalAction !== null)
            revealSpy.clear()
            folderAction.triggered()
            compare(revealSpy.count, 1)
            compare(revealSpy.signalArguments[0][0], target)
            menu.close()
        }
        function test_only_the_active_tab_draws_a_scrollbar() {
            const tracksBar = findChild(music, "musicLibraryScrollBar")
            const statsBar = findChild(music, "musicStatsScrollBar")
            tryCompare(tracksBar, "visible", true)
            compare(statsBar.visible, false, "Hidden statistics must not draw a second thumb")
            music.scrollFromWheel({angleDelta: Qt.point(0, -120), pixelDelta: Qt.point(0, 0)},
                                  findChild(music, "musicLibraryGrid"), 150)
            tryCompare(tracksBar, "active", true)
            compare(statsBar.active, false, "Scrolling tracks must not activate the statistics thumb")
            compare(statsBar.visible, false)
            music.tabIndex = 7
            wait(150)
            compare(tracksBar.visible, false)
        }
    }
}
