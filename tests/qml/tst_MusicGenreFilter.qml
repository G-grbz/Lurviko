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
        property bool renameRegressionActive: false
        Shortcut {
            sequence: "F2"
            enabled: window.renameRegressionActive && pane.renameShortcutAllowed
            context: Qt.WindowShortcut
            onActivated: pane.renameSelection()
        }
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
            property bool playing: true
            property bool opened: false
            property bool visible: false
            property bool backgroundPlayback: true
            property int queueLimit: 50
            property var queue: []
            property int currentIndex: 0
            property int rebuildCount: 0
            function rebuildPlaybackOrder(index) { ++rebuildCount }
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
            property var savedHoverPreferences
            function initTestCase() {
                savedHoverPreferences = [AppTheme.musicHoverEnabled, AppTheme.musicHoverAnimationsEnabled, AppTheme.musicHoverDelayMs]
                AppTheme.setMusicHoverPreferences(false, false, 600)
                language.language = "en"
                pane.navigateTo("category:/music")
            }
            function cleanupTestCase() { AppTheme.setMusicHoverPreferences(...savedHoverPreferences) }
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
            function test_file_rename_works_after_music_hover_and_page_switch() {
                AppTheme.setMusicHoverPreferences(true, false, 120)
                try {
                    pane.musicLibraryTab = 0
                    const view = findChild(pane, "musicLibraryView")
                    verify(view !== null)
                    tryVerify(function() { return view.viewItems.length === 3 })
                    const grid = findChild(view, "musicLibraryGrid")
                    grid.forceLayout()
                    tryVerify(function() { return grid.itemAtIndex(0) !== null })
                    const anchor = findChild(grid.itemAtIndex(0), "musicTrackMouse").parent
                    view.requestHoverPreview(view.viewItems[0].track, anchor)
                    tryVerify(function() { return view.hoverTrack !== null })
                    compare(window.Overlay.overlay.visible, false)
                    const sourceUrl = musicHoverRenameFixtureUrl.toString()
                    const folder = sourceUrl.substring(0, sourceUrl.lastIndexOf("/"))
                    pane.navigateTo(folder)
                    tryVerify(function() { return pane.itemCount > 0 })
                    let found = false
                    for (let i = 0; i < pane.itemCount; ++i) {
                        pane.selectIndex(i, Qt.NoModifier)
                        if (pane.selectedUrl === sourceUrl) { found = true; break }
                    }
                    verify(found)
                    compare(window.Overlay.overlay.visible, false)
                    window.renameRegressionActive = true
                    window.requestActivate()
                    pane.forceActiveFocus()
                    keyClick(Qt.Key_F2)
                    tryCompare(pane, "inlineRenameUrl", sourceUrl)
                    tryVerify(function() { return pane.inlineRenameEditor && pane.inlineRenameEditor.activeFocus })
                    const editor = pane.inlineRenameEditor
                    editor.selectAll()
                    keyClick(Qt.Key_A)
                    compare(editor.text, "a")
                    editor.text = "renamed-after-preview.txt"
                    keyClick(Qt.Key_Return)
                    tryCompare(pane, "inlineRenameUrl", "")
                    tryCompare(pane, "selectedUrl", folder + "/renamed-after-preview.txt")
                } finally {
                    window.renameRegressionActive = false
                    pane.cancelInlineRename()
                    AppTheme.setMusicHoverPreferences(false, false, 600)
                }
            }
            function test_context_menu_rename_keeps_editor_open_data() {
                return [{tag: "grid", list: false}, {tag: "list", list: true}]
            }
            function test_context_menu_rename_keeps_editor_open(data) {
                const sourceUrl = musicHoverRenameFixtureUrl.toString()
                const folder = sourceUrl.substring(0, sourceUrl.lastIndexOf("/"))
                const oldGridMode = pane.gridMode
                try {
                    pane.gridMode = !data.list
                    pane.navigateTo(folder)
                    tryVerify(function() { return pane.itemCount > 0 })
                    for (let i = 0; i < pane.itemCount; ++i) {
                        pane.selectIndex(i, Qt.NoModifier)
                        if (pane.selectedUrl === sourceUrl) break
                    }
                    compare(pane.selectedUrl, sourceUrl)
                    const menu = findChild(pane, "itemContextMenu")
                    const renameAction = findChild(pane, "contextRenameAction")
                    verify(menu !== null && renameAction !== null)
                    window.requestActivate()
                    pane.forceActiveFocus()
                    pane.openItemContextAt(pane, {x: 200, y: 150})
                    tryCompare(menu, "opened", true)
                    for (let i = 0; i < menu.count; ++i)
                        if (menu.itemAt(i) === renameAction) menu.currentIndex = i
                    wait(100)
                    mouseClick(renameAction)
                    tryCompare(menu, "visible", false)
                    tryCompare(pane, "inlineRenameUrl", sourceUrl)
                    tryVerify(function() { return pane.inlineRenameEditor && pane.inlineRenameEditor.activeFocus })
                    wait(250)
                    compare(pane.inlineRenameUrl, sourceUrl, "Menu focus restoration must not finish rename")
                    compare(pane.inlineRenameEditor.activeFocus, true)
                    pane.inlineRenameEditor.selectAll()
                    keyClick(Qt.Key_A)
                    compare(pane.inlineRenameEditor.text, "a")
                } finally {
                    pane.closeContextMenus()
                    pane.cancelInlineRename()
                    pane.gridMode = oldGridMode
                }
            }
            function test_queue_settings_only_affect_future_random_queues() {
                const queue = []
                for (let i = 0; i < 300; ++i)
                    queue.push({itemUrl: "file:///playlist/" + i + ".mp3"})
                player.queue = queue
                player.currentIndex = 125
                player.opened = true
                player.queueLimit = 50
                player.rebuildCount = 0
                const popup = findChild(pane, "musicCategorySettings")
                verify(popup !== null)
                try {
                    popup.open()
                    tryCompare(popup, "visible", true)
                    popup.musicQueueDraft = 10
                    mouseClick(findChild(popup, "saveCategorySettings"))
                    compare(player.queueLimit, 10)
                    compare(player.queue.length, 300)
                    compare(player.currentIndex, 125)
                    compare(player.rebuildCount, 0)
                } finally {
                    popup.close()
                    player.opened = false
                    player.queue = []
                    player.queueLimit = 50
                }
            }
            function test_music_settings_fit_the_modal_data() {
                return [{tag: "narrow-tr", size: 420, language: "tr"},
                        {tag: "normal-tr", size: 1020, language: "tr"},
                        {tag: "narrow-en", size: 420, language: "en"},
                        {tag: "normal-en", size: 1020, language: "en"}]
            }
            function test_music_settings_fit_the_modal(data) {
                const previousWidth = window.width
                const popup = findChild(pane, "musicCategorySettings")
                try {
                    window.width = data.size
                    language.language = data.language
                    popup.open()
                    tryCompare(popup, "visible", true)
                    wait(150)
                    verify(popup.width <= window.width - 24)
                    const scroll = findChild(popup, "musicCategorySettingsScroll")
                    const content = findChild(popup, "musicSettingsScrollContent")
                    const row = findChild(popup, "musicQueueSettingsRow")
                    verify(scroll.width <= popup.width + 1, "Settings viewport expanded outside the modal")
                    verify(content.width <= scroll.availableWidth + 1)
                    verify(row.x + row.width <= content.width + 1, "Queue settings run off the right edge")
                    if (data.tag === "normal-tr") {
                        scroll.contentItem.contentY = Math.max(0, scroll.contentItem.contentHeight - scroll.contentItem.height)
                        wait(100)
                        grabImage(window.contentItem).save("/tmp/lurviko-music-settings.png")
                    }
                    compare(findChild(popup, "musicHoverEnabledCheck").text, language.t("hover_preview"))
                } finally {
                    popup.close()
                    window.width = previousWidth
                    language.language = "en"
                    wait(180)
                }
            }
            function test_music_hover_preferences_save_and_reopen() {
                const popup = findChild(pane, "musicCategorySettings")
                try {
                    popup.open()
                    tryCompare(popup, "visible", true)
                    compare(popup.musicHoverDelayDraft, 600)
                    popup.musicHoverDraft = true
                    popup.musicHoverAnimationsDraft = true
                    const combo = findChild(popup, "musicHoverDelayInput")
                    combo.currentIndex = combo.valueModel.indexOf(1000)
                    combo.activated(combo.currentIndex)
                    mouseClick(findChild(popup, "saveCategorySettings"))
                    compare(AppTheme.musicHoverEnabled, true)
                    compare(AppTheme.musicHoverAnimationsEnabled, true)
                    compare(AppTheme.musicHoverDelayMs, 1000)
                    popup.open()
                    tryCompare(popup, "visible", true)
                    compare(popup.musicHoverDraft, true)
                    compare(popup.musicHoverDelayDraft, 1000)
                    compare(combo.valueModel[combo.currentIndex], 1000)
                    combo.currentIndex = combo.valueModel.indexOf(200)
                    combo.activated(combo.currentIndex)
                    popup.close()
                    compare(AppTheme.musicHoverDelayMs, 1000, "Cancelling must not save draft preferences")
                    popup.open()
                    tryCompare(popup, "visible", true)
                    compare(combo.valueModel[combo.currentIndex], 1000, "Reopening must restore the saved control position")
                } finally {
                    popup.close()
                    AppTheme.setMusicHoverPreferences(false, false, 600)
                }
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
