import QtQuick
import QtQuick.Window
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
            property int scanCompleted: 0
            property int scanTotal: 0
            function setItems(items) {}
        }
        QtObject {
            id: player
            property var libraryManager: library
            property var playlists: []
            property bool playing: false
            property bool opened: false
            property bool visible: false
            property bool backgroundPlayback: true
            property int queueLimit: 50
            property var queue: []
            property int currentIndex: 0
            property string openedUrl: ""
            property var openedItems: []
            function rebuildPlaybackOrder(index) {}
            function openUrl(url, title, artwork, items) { openedUrl = url; openedItems = items }
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

        TestCase {
            name: "InternalViewers"
            when: windowShown
            property bool savedImages
            property bool savedVideos
            property bool savedMusic

            function initTestCase() {
                savedImages = AppTheme.openImagesInternally
                savedVideos = AppTheme.openVideosInternally
                savedMusic = AppTheme.openMusicInternally
            }
            function cleanupTestCase() {
                AppTheme.setOpenImagesInternally(savedImages)
                AppTheme.setOpenVideosInternally(savedVideos)
                AppTheme.setOpenMusicInternally(savedMusic)
            }
            function photoViewer() { return pane.internalPhotoViewer }
            function fixtures() {
                const location = String(Qt.resolvedUrl("fixtures/"))
                if (location.indexOf("file:") !== 0)
                    skip("Fixtures are not on the local filesystem")
                return location
            }

            function test_switches_default_off_and_pick_the_viewer() {
                AppTheme.setOpenImagesInternally(false)
                AppTheme.setOpenVideosInternally(false)
                AppTheme.setOpenMusicInternally(false)
                compare(pane.internalMediaKind("photo.jpg", "image/jpeg"), "")
                compare(pane.internalMediaKind("movie.mkv", "video/x-matroska"), "")
                compare(pane.internalMediaKind("song.mp3", "audio/mpeg"), "")
                AppTheme.setOpenImagesInternally(true)
                AppTheme.setOpenVideosInternally(true)
                AppTheme.setOpenMusicInternally(true)
                compare(pane.internalMediaKind("photo.jpg", "image/jpeg"), "image")
                compare(pane.internalMediaKind("icon.svg", "image/svg+xml"), "image")
                compare(pane.internalMediaKind("movie.mkv", "video/x-matroska"), "video")
                compare(pane.internalMediaKind("song.mp3", "audio/mpeg"), "audio")
                compare(pane.internalMediaKind("notes.txt", "text/plain"), "")
                compare(pane.internalMediaKind("playlist.m3u", "audio/x-mpegurl"), "")
            }

            function test_image_opens_with_folder_images_and_unloads_on_close() {
                const location = fixtures()
                pane.navigateTo(location)
                tryVerify(function() { return !pane.directory.loading && pane.directory.count === 2 }, 5000,
                          "location " + pane.directory.location + " count " + pane.directory.count + " loading " + pane.directory.loading)
                const entries = pane.directory.categoryEntries("")
                let portrait = null
                for (const entry of entries) if (entry.name === "portrait.svg") portrait = entry

                AppTheme.setOpenImagesInternally(false)
                verify(!pane.openMediaInternally(portrait.itemUrl, portrait.name, "image/svg+xml"))
                compare(photoViewer(), null, "Nothing is created while the switch is off")

                AppTheme.setOpenImagesInternally(true)
                verify(pane.openMediaInternally(portrait.itemUrl, portrait.name, "image/svg+xml"))
                const viewer = photoViewer()
                verify(viewer !== null)
                tryCompare(viewer, "visible", true)
                compare(viewer.files.length, 2)
                compare(viewer.currentItem.name, "portrait.svg")
                compare(viewer.currentItem.path, portrait.localPath)
                verify(viewer.currentItem.parentPath.length > 1)

                // Controls fade after an idle moment in the window too, not
                // only in full screen, and come back on interaction.
                verify(!viewer.viewerFullScreen)
                compare(viewer.controlsVisible, true)
                mouseMove(window.contentItem, window.width / 2, window.height / 2)
                tryCompare(viewer, "controlsVisible", false, 5000)
                compare(viewer.controlsOpacity, 0)
                viewer.next()
                compare(viewer.controlsVisible, true)
                compare(viewer.currentItem.name, "landscape.svg")
                // The picture on screen follows the title on every step; it
                // used to stay one photo behind (stale currentItem binding).
                function shownSource() {
                    return String(viewer.showingImageA ? findChild(viewer.contentItem, "photoImageA").source
                                                       : findChild(viewer.contentItem, "photoImageB").source)
                }
                tryVerify(function() { return viewer.pendingImageSlot < 0 && shownSource().endsWith("/landscape.svg") }, 3000,
                          "Shown " + shownSource())
                viewer.next()
                compare(viewer.currentItem.name, "portrait.svg")
                tryVerify(function() { return viewer.pendingImageSlot < 0 && shownSource().endsWith("/portrait.svg") }, 3000,
                          "Shown " + shownSource())
                viewer.close()
                tryVerify(function() { return photoViewer() === null }, 2000)
            }

            function test_photo_fills_stage_and_zooms_around_a_fixed_point() {
                AppTheme.setOpenImagesInternally(true)
                const location = fixtures()
                pane.navigateTo(location)
                tryVerify(function() { return !pane.directory.loading && pane.directory.count === 2 }, 5000)
                let landscape = null
                for (const entry of pane.directory.categoryEntries("")) if (entry.name === "landscape.svg") landscape = entry
                verify(pane.openMediaInternally(landscape.itemUrl, landscape.name, "image/svg+xml"))
                const photo = photoViewer()
                tryCompare(photo, "visible", true)
                const flick = findChild(photo.contentItem, "photoFlick")
                tryVerify(function() { return photo.pendingImageSlot < 0 && photo.displayWidth > 0 }, 3000)
                // Fit uses the whole stage: one side touches the stage edges.
                compare(flick.height, photo.contentItem.height)
                verify(Math.abs(photo.displayWidth - flick.width) < 2 || Math.abs(photo.displayHeight - flick.height) < 2,
                       photo.displayWidth + "x" + photo.displayHeight + " in " + flick.width + "x" + flick.height)
                verify(!photo.zoomedIn)
                verify(!findChild(photo.contentItem, "photoNavigator").visible)

                // The picture point in the stage centre stays there.
                function centreFraction() {
                    return Qt.point((flick.contentX + flick.width / 2 - (flick.contentWidth - photo.displayWidth) / 2) / photo.displayWidth,
                                    (flick.contentY + flick.height / 2 - (flick.contentHeight - photo.displayHeight) / 2) / photo.displayHeight)
                }
                for (let i = 0; i < 6; ++i) photo.zoomBy(1.15)
                verify(photo.zoomedIn)
                fuzzyCompare(centreFraction().x, 0.5, 0.01)
                fuzzyCompare(centreFraction().y, 0.5, 0.01)
                const navigator = findChild(photo.contentItem, "photoNavigator")
                tryCompare(navigator, "visible", true)

                // Zooming at a point keeps that picture point under it.
                const px = flick.width * 0.25, py = flick.height * 0.3
                const before = Qt.point((flick.contentX + px - (flick.contentWidth - photo.displayWidth) / 2) / photo.displayWidth,
                                        (flick.contentY + py - (flick.contentHeight - photo.displayHeight) / 2) / photo.displayHeight)
                photo.zoomBy(1.5, px, py)
                fuzzyCompare((flick.contentX + px - (flick.contentWidth - photo.displayWidth) / 2) / photo.displayWidth, before.x, 0.01)
                fuzzyCompare((flick.contentY + py - (flick.contentHeight - photo.displayHeight) / 2) / photo.displayHeight, before.y, 0.01)

                // Up to 2000%, held buttons repeat.
                photo.setZoom(100)
                compare(photo.zoom, 20)
                verify(findChild(photo.contentItem, "photoZoomIn").autoRepeat)
                verify(findChild(photo.contentItem, "photoZoomOut").autoRepeat)

                // The navigator moves the view; closing it fits the picture again.
                // Pressing and dragging in the navigator pans continuously;
                // the frame is carried from where it was grabbed.
                const area = findChild(photo.contentItem, "photoNavigatorArea")
                const frame = findChild(photo.contentItem, "photoNavigatorFrame")
                tryVerify(function() { return area.width > 20 && area.height > 20 })
                photo.centerOn(0, 0)
                const grabX = frame.x + frame.width / 2, grabY = frame.y + frame.height / 2
                mousePress(area, grabX, grabY)
                compare(flick.contentX, 0, "grabbing the frame does not jump")
                for (let step = 1; step <= 8; ++step)
                    mouseMove(area, grabX + 6 * step, grabY + 4 * step, -1, Qt.LeftButton)
                fuzzyCompare(frame.x + frame.width / 2, grabX + 48, 1.5)
                verify(flick.contentX > 0)
                mouseRelease(area, grabX + 48, grabY + 32)
                // Compact thumbnail strip, centred and only as wide as needed.
                const strip = findChild(photo.contentItem, "photoStripPanel")
                compare(strip.height, 54)
                verify(strip.width <= photo.contentItem.width - 36)
                fuzzyCompare(strip.x + strip.width / 2, photo.contentItem.width / 2, 1)
                photo.centerOn(0, 0)
                compare(flick.contentX, 0)
                photo.centerOn(photo.displayWidth, photo.displayHeight)
                fuzzyCompare(flick.contentX, flick.contentWidth - flick.width, 1)
                mouseClick(findChild(photo.contentItem, "photoNavigatorClose"))
                compare(photo.zoom, 1)
                verify(!photo.zoomedIn)
                tryCompare(navigator, "visible", false)
                photo.close()
                tryVerify(function() { return photoViewer() === null }, 2000)
            }

            function test_vector_render_size_and_unreadable_image() {
                // Logical size: Qt Quick multiplies by the device pixel ratio
                // itself; doing it twice made Qt reject large SVGs at 3x.
                AppTheme.setOpenImagesInternally(true)
                const location = fixtures()
                pane.navigateTo(location)
                tryVerify(function() { return !pane.directory.loading && pane.directory.count === 2 }, 5000)
                const entry = pane.directory.categoryEntries("")[0]
                verify(pane.openMediaInternally(entry.itemUrl, entry.name, "image/svg+xml"))
                const photo = photoViewer()
                tryCompare(photo, "visible", true)
                const dpr = Math.max(1, Screen.devicePixelRatio)
                const small = photo.vectorRenderSize(400, 300)
                compare(small.width, Math.round(400 * Math.min(1, 4096 / (400 * dpr))))
                const big = photo.vectorRenderSize(6000, 2000)
                verify(big.width * dpr <= 4097, "Device-pixel long side is capped: " + big.width + " at " + dpr + "x")
                compare(Math.round(big.width / big.height), 3)

                // A file that cannot be decoded is reported, never left
                // showing the previous picture under its name.
                photo.files = photo.files.concat([{ name: "broken.png", path: "/nonexistent/broken.png",
                                                     url: "data:image/png;base64,AAAA", size: 4 }])
                ignoreWarning(new RegExp("Error decoding: data:image/png;base64,AAAA"))
                photo.setIndex(photo.files.length - 1)
                tryVerify(function() { return photo.imageFailed }, 3000)
                verify(findChild(photo.contentItem, "photoErrorPanel").visible)
                photo.close()
                tryVerify(function() { return photoViewer() === null }, 2000)
            }

            function test_multi_selection_open_uses_the_viewer() {
                const location = fixtures()
                pane.navigateTo(location)
                tryVerify(function() { return !pane.directory.loading && pane.directory.count === 2 }, 5000)
                const urls = pane.directory.categoryEntries("").map(function(entry) { return entry.itemUrl })
                pane.setSelectedUrls(urls, 0, 0)
                compare(pane.selectedCount, 2)

                AppTheme.setOpenImagesInternally(false)
                compare(pane.selectionInternalKind(), "")
                AppTheme.setOpenImagesInternally(true)
                // The Open entry names Lurviko's viewer, not the default app.
                compare(pane.selectionInternalKind(), "image")

                pane.openSelected()
                const viewer = photoViewer()
                verify(viewer !== null, "Multi-selection Open skipped Lurviko's viewer")
                tryCompare(viewer, "visible", true)
                compare(viewer.files.length, 2)
                viewer.close()
                tryVerify(function() { return photoViewer() === null }, 2000)
                pane.clearSelection()
            }
            function test_multi_selection_menu_keeps_desktop_default() {
                const location = fixtures()
                pane.navigateTo(location)
                tryVerify(function() { return !pane.directory.loading && pane.directory.count === 2 }, 5000)
                const urls = pane.directory.categoryEntries("").map(function(entry) { return entry.itemUrl })
                const menu = findChild(pane, "itemContextMenu")
                const chooser = findChild(pane, "openWithModel")
                function externalAction() { return findChild(menu, "contextOpenExternalDefaultAction") }
                function showMenu() {
                    pane.setSelectedUrls(urls, 0, 0)
                    compare(pane.selectedCount, 2)
                    pane.showContextMenu(menu, pane, 200, 150)
                    tryCompare(menu, "opened", true)
                }

                // Lurviko's viewer takes the Open entry; the desktop default
                // stays one click away for the whole selection too.
                AppTheme.setOpenImagesInternally(true)
                showMenu()
                const names = chooser.defaultApplicationNames || []
                if (names.length === 0) {
                    verify(!externalAction().visible, "no desktop default, no entry")
                } else {
                    verify(externalAction().visible, "multi-selection lost the desktop default entry")
                    const expected = chooser.defaultApplicationName.length > 0
                        ? (language.language === "tr" ? chooser.defaultApplicationName + " ile aç"
                                                      : "Open with " + chooser.defaultApplicationName)
                        : (language.language === "tr" ? "Varsayılan uygulamalarla aç" : "Open with default applications")
                    verify(externalAction().text.indexOf(expected) === 0, externalAction().text)
                }
                menu.close()
                tryCompare(menu, "visible", false)

                // Without the internal viewer the main Open entry already names
                // the desktop default; no duplicate entry.
                AppTheme.setOpenImagesInternally(false)
                showMenu()
                verify(!externalAction().visible)
                menu.close()
                tryCompare(menu, "visible", false)
                AppTheme.setOpenImagesInternally(true)
                pane.clearSelection()
            }

            function test_music_queues_the_folder_songs() {
                AppTheme.setOpenMusicInternally(true)
                const url = "file:///tmp/lurviko-internal-test/song.mp3"
                verify(pane.openMediaInternally(url, "song.mp3", "audio/mpeg"))
                compare(player.openedUrl, url)
                compare(player.openedItems.length, 1)
                compare(player.openedItems[0].itemUrl, url)
                compare(player.openedItems[0].path, "/tmp/lurviko-internal-test/song.mp3")
                AppTheme.setOpenMusicInternally(false)
                verify(!pane.openMediaInternally(url, "song.mp3", "audio/mpeg"))
            }
        }
    }
}
