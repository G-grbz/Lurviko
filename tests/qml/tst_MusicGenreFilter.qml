import QtQuick
import QtQuick.Controls
import QtTest
import Lurviko.App
import Lurviko.Backend

Item {
    width: 1020
    height: 700
    ApplicationWindow {
        id: window
        width: 1020
        height: 700
        visible: true
        LanguageManager { id: language }
        QtObject {
            id: library
            property var tracks: []
            property bool loading: false
            property bool scanPaused: false
            property real scanProgress: 1
            property int scanCompleted: tracks.length
            property int scanTotal: tracks.length
            function setItems(items) {}
        }
        QtObject {
            id: player
            property var libraryManager: library
            property var playlists: []
            property bool opened: false
            property bool visible: false
        }
        QtObject { id: auth; property string googleAccessToken: ""; property string oneDriveAccessToken: "" }
        QtObject {
            id: prefs
            property bool showGoogle: false
            property bool showOneDrive: false
            property bool showDropbox: false
            property bool showNextcloud: false
            property bool showOwnCloud: false
            property bool showMega: false
            property bool showPCloud: false
            property bool showWebDav: false
        }
        QtObject { id: cloud; property string accessToken: ""; signal refreshRequested() }
        FavoritesModel { id: favorites }
        AdminEditManager { id: admin }
        BrowserPane {
            id: pane
            anchors.fill: parent
            lang: language
            musicPlayer: player
            favoritesModel: favorites
            adminEditor: admin
            cloudAuth: auth
            cloudIntegrationPreferences: prefs
            googleDrive: cloud
            oneDrive: cloud
        }
        Component {
            id: extraPaneComponent
            BrowserPane {
                width: 1020
                height: 700
                visible: false
                initialLocation: "category:/videos"
                lang: language
                musicPlayer: player
                favoritesModel: favorites
                adminEditor: admin
                cloudAuth: auth
                cloudIntegrationPreferences: prefs
                googleDrive: cloud
                oneDrive: cloud
            }
        }
        TestCase {
            name: "MusicGenreFilter"
            when: windowShown
            function initTestCase() { language.language = "en"; pane.navigateTo("category:/music") }
            function init() {
                pane.navigateTo("category:/music")
                library.tracks = [
                    {itemUrl: "file:///music/amo.mp3", name: "Amo988 - Sar Zamanımızı Geriye.mp3",
                     genre: "Turkish Pop", year: "2025", lastPlayed: 0},
                    {itemUrl: "file:///music/recent.mp3", name: "Recent", genre: " Turkish  Pop ",
                     year: "2024", lastPlayed: 123},
                    {itemUrl: "file:///music/rock.mp3", name: "Rock", genre: "Rock", year: "2025"}
                ]
                pane.musicGenreFilter = ""
                pane.musicYearFilter = ""
                pane.musicLibraryFilter = ""
                pane.musicLibraryTab = 5
                tryCompare(pane, "categoryKey", "music")
            }
            function test_genre_selection_lists_unplayed_tracks_and_counts() {
                const combo = findChild(pane, "musicGenreCombo")
                verify(combo !== null)
                const genreIndex = pane.musicGenreOptions.findIndex(function(option) { return option.value === "Turkish Pop" })
                compare(pane.musicGenreOptions[genreIndex].text, "Turkish Pop (2)")
                combo.currentIndex = genreIndex
                combo.activated(genreIndex)
                compare(pane.musicLibraryTab, 0, "Genre browsing must leave the recent-only tab")
                compare(pane.musicGenreFilter, "Turkish Pop", "The count must never become part of the filter")
                const grid = findChild(pane, "musicLibraryGrid")
                tryCompare(grid, "count", 2)
                verify(grid.model.some(function(item) { return item.track.itemUrl === "file:///music/amo.mp3" }),
                       "Unplayed example track must be included")
            }
            function test_metadata_updates_keep_the_selected_genre() {
                pane.selectMusicGenre("Turkish Pop")
                const combo = findChild(pane, "musicGenreCombo")
                library.tracks = library.tracks.concat([{itemUrl: "file:///music/alternative.mp3", name: "Alternative",
                                                        genre: "Alternative"},
                                                       {itemUrl: "file:///music/pop.mp3", name: "Pop", genre: "turkish pop"}])
                tryCompare(combo, "currentValue", "Turkish Pop")
                compare(combo.currentText, "Turkish Pop (3)")
                compare(pane.musicGenreFilter, "Turkish Pop")
                const grid = findChild(pane, "musicLibraryGrid")
                tryCompare(grid, "count", 3)
            }
            function test_music_zoom_does_not_resize_other_categories() {
                const oldMusic = pane.musicIconSize
                const oldCategory = pane.categoryIconSize
                const oldDirectory = pane.iconSize
                pane.setActiveIconSize(231)
                compare(pane.musicIconSize, 231)
                compare(pane.activeIconSize, 231)
                compare(pane.categoryIconSize, oldCategory)
                pane.navigateTo("category:/images")
                pane.setActiveIconSize(300)
                compare(pane.categoryIconSize, 300)
                compare(pane.musicIconSize, 231)
                compare(pane.iconSize, oldDirectory)
                pane.navigateTo("category:/music")
                compare(pane.activeIconSize, 231)
                AppTheme.musicIconSize = oldMusic
                AppTheme.categoryIconSize = oldCategory
            }
            function test_category_zoom_survives_page_switches_and_recreation() {
                const oldMusic = AppTheme.musicIconSize
                const oldCategory = AppTheme.categoryIconSize
                let peer = extraPaneComponent.createObject(window.contentItem)
                verify(peer !== null)
                pane.navigateTo("category:/images")
                const slider = findChild(pane, "iconSizeSlider")
                verify(slider !== null)
                // Move the actual control rather than assigning a test value.
                mouseClick(slider, slider.width - slider.rightPadding, slider.height / 2)
                tryCompare(pane, "categoryIconSize", 360)
                tryCompare(peer, "activeIconSize", 360, 1000)
                peer.navigateTo("category:/images")
                compare(findChild(peer, "iconSizeSlider").value, 360)
                pane.navigateTo("category:/music")
                pane.setActiveIconSize(215)
                tryCompare(slider, "value", 215)
                pane.navigateTo("category:/videos")
                tryCompare(slider, "value", 360)
                mouseClick(slider, slider.leftPadding, slider.height / 2)
                tryCompare(pane, "categoryIconSize", 104)
                tryCompare(peer, "activeIconSize", 104)
                peer.destroy()
                wait(250)
                peer = extraPaneComponent.createObject(window.contentItem)
                compare(peer.activeIconSize, 104, "Reopened pages must use the latest size")
                peer.setActiveIconSize(360)
                peer.destroy()
                // Closing a page during the save debounce must keep its final value.
                pane.navigateTo("category:/images")
                tryCompare(slider, "value", 360)
                wait(250)
                compare(AppTheme.browserInteractionSettings.categoryIconSize, 360)
                compare(AppTheme.browserInteractionSettings.musicIconSize, 215)
                AppTheme.musicIconSize = oldMusic
                AppTheme.categoryIconSize = oldCategory
            }
            function test_back_and_up_leave_the_album_inside_music() {
                const music = findChild(pane, "musicLibraryView")
                verify(music !== null)
                pane.musicLibraryTab = 1
                tryVerify(function() { return music.viewItems.length === 1 && music.viewItems[0].kind === "album" })
                music.browseGroup(music.viewItems[0])
                tryVerify(function() { return music.viewItems.length === 3 })
                pane.handleBackNavigation()
                compare(pane.currentLocation, "category:/music")
                compare(music.groupSelection, null)
                tryVerify(function() { return music.viewItems.length === 1 })
                music.browseGroup(music.viewItems[0])
                tryVerify(function() { return music.viewItems.length === 3 })
                pane.handleUpNavigation()
                compare(pane.currentLocation, "category:/music")
                compare(music.groupSelection, null)
            }
            function test_large_category_slider_matches_size_after_directory_navigation() {
                const oldSystemIcons = AppTheme.useSystemIcons
                const oldCategory = AppTheme.categoryIconSize
                const oldMusic = AppTheme.musicIconSize
                try {
                    AppTheme.setSystemIcons(true)
                    const slider = findChild(pane, "iconSizeSlider")
                    verify(slider !== null)
                    const sizes = [104, 180, 255, 256, 257, 300, 360]
                    for (const category of ["images", "videos", "music"]) {
                        for (const size of sizes) {
                            pane.navigateTo("category:/" + category)
                            pane.setActiveIconSize(size)
                            tryCompare(slider, "value", size)
                            if (size > 256) {
                                const reopened = extraPaneComponent.createObject(window.contentItem,
                                                        {initialLocation: "category:/" + category})
                                verify(reopened !== null)
                                const reopenedSlider = findChild(reopened, "iconSizeSlider")
                                tryCompare(reopenedSlider, "value", size, 500,
                                           "New page restored the icons but not the slider")
                                // Let the pane's asynchronous share discovery
                                // finish before tearing down this short-lived fixture.
                                wait(250)
                                reopened.destroy()
                            }
                            pane.navigateTo("file:///")
                            tryCompare(slider, "to", pane.directorySystemIconMaximum())
                            pane.navigateTo("category:/" + category)
                            tryCompare(slider, "to", 360)
                            compare(pane.activeIconSize, size)
                            tryCompare(slider, "value", size, 500,
                                       "Slider lost " + size + "px on reopening " + category)
                            compare(slider.position, (size - slider.from) / (slider.to - slider.from))
                        }
                    }
                } finally {
                    AppTheme.categoryIconSize = oldCategory
                    AppTheme.musicIconSize = oldMusic
                    AppTheme.setSystemIcons(oldSystemIcons)
                }
            }
        }
    }
}
