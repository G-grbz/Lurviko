import QtQuick
import QtQuick.Controls
import QtTest
import Lurviko.App

Item {
    width: 1020
    height: 700
    QtObject { id: language; property string language: "tr"; function t(key) { return key } }
    MediaGalleryView {
        id: gallery
        anchors.fill: parent
        files: []
        lang: language
        categoryKey: "images"
        categoryIcon: "image.svg"
        gallerySize: 230
        contentIndexModel: null
    }
    SignalSpy { id: selectionSpy; target: gallery; signalName: "toggleItems" }
    SignalSpy { id: revealSpy; target: gallery; signalName: "revealRequested" }
    TestCase {
        id: tests
        name: "MediaGalleryExplore"
        when: windowShown
        function init() {
            gallery.categoryKey = "images"
            AppTheme.setSingleClickOpen(true)
            mouseMove(gallery, 1, gallery.height - 2)
            gallery.dismissHoverPreview()
            gallery.selectAlbum("")
            gallery.gallerySize = 230
            const files = []
            for (let i = 0; i < 40; ++i) {
                const folder = "/pictures/album-" + (i % 3)
                files.push({ path: folder + "/photo-" + i + ".svg", parentPath: folder,
                             name: "photo-" + i, size: 23000,
                             url: Qt.resolvedUrl("fixtures/landscape.svg").toString(),
                             modifiedMs: 1700000000000 + i * 1000 })
            }
            gallery.files = files
            tryCompare(gallery, "layoutColumns", 4)
            tryVerify(function() { return gallery.rows.length > 2 })
            findChild(gallery, "categoryGalleryList").positionViewAtBeginning()
            wait(220)
        }
        function cleanup() {
            AppTheme.setSingleClickOpen(false)
            gallery.dismissHoverPreview()
            findChild(gallery, "galleryPhotoViewer").close()
            findChild(gallery, "galleryVideoViewer").close()
        }
        function firstCard() {
            const view = findChild(gallery, "categoryGalleryList")
            view.positionViewAtIndex(1, ListView.Beginning)
            wait(220)
            return findChild(view.itemAtIndex(1), "galleryMediaCard")
        }
        function test_real_video_hover_receives_episode_metadata() {
            gallery.categoryKey = "videos"
            const item = {name: "Show.S01E03.mkv", path: "/videos/Show.S01E03.mkv", url: "file:///videos/Show.S01E03.mkv"}
            const metadata = {state: "ready", mediaType: "tv", tmdbId: 123, title: "Dizi", overview: "Genel konu",
                episodeChecked: true, episodeSeriesId: 123, seasonNumber: 1, episodeNumber: 3,
                episodeTitle: "Bölüm adı", episodeOverview: "Bölüm konusu", episodeRuntime: 47}
            gallery.hoverPreviewItem = item
            gallery.tmdbInfo = gallery.resolveHoverMetadata(metadata, item)
            const card = findChild(gallery, "galleryVideoHoverMetadata")
            verify(card !== null)
            compare(card.episodeHeading, "S01E03 · Bölüm adı")
            compare(card.overviewText, "Bölüm konusu")
            compare(card.runtimeMinutes, 47)
            gallery.hoverPreviewItem = null
        }

        function test_video_strip_uses_natural_order_and_cached_tmdb_artwork() {
            gallery.categoryKey = "videos"
            gallery.files = [10, 2, 1].map(function(number) {
                return {name: "Episode " + number + ".mp4", path: "/videos/" + number + ".mp4",
                        url: "file:///videos/" + number + ".mp4", modifiedMs: 1700000000000}
            })
            compare(gallery.orderedVideoFiles.map(function(item) { return item.name }).join(","),
                    "Episode 1.mp4,Episode 2.mp4,Episode 10.mp4")
            compare(gallery.fileIndex("/videos/10.mp4"), 2)
            const video = findChild(gallery, "galleryVideoViewer")
            compare(video.files[0].path, "/videos/1.mp4")
            const backdrop = Qt.resolvedUrl("fixtures/landscape.svg").toString()
            const poster = Qt.resolvedUrl("fixtures/portrait.svg").toString()
            gallery.localArtworkByPath = ({
                "/videos/1.mp4": {backdropUrl: backdrop, posterUrl: poster, title: "Dizi", mediaType: "tv", seasonNumber: 1, episodeNumber: 3, episodeTitle: "Bölüm adı"},
                "/videos/2.mp4": {posterUrl: poster}
            })
            compare(video.filmstripSource(video.files[0]), backdrop)
            compare(video.filmstripSource(video.files[1]), poster)
            compare(video.filmstripSource(video.files[2]), video.thumbSource(video.files[2]))
            compare(video.filmstripTitle(video.files[0]), "Dizi · S01E03 · Bölüm adı")
            compare(video.filmstripTitle(video.files[2]), "Episode 10.mp4")
            const oldQt = AppTheme.useQtTheme
            try {
                AppTheme.setQtTheme(true)
                video.normalOverlayParent = gallery
                video.open()
                tryVerify(function() { return findChild(video.contentItem, "videoFilmstripPlayIcon") !== null })
                const glyph = findChild(video.contentItem, "videoFilmstripPlayIcon")
                compare(glyph.tintColor, "#ffffff")
                const tooltip = glyph.parent.parent.hoverTooltip
                verify(tooltip !== null)
                verify(tooltip.text === "Dizi · S01E03 · Bölüm adı" || tooltip.text === "Episode 2.mp4" || tooltip.text === "Episode 10.mp4")
                compare(tooltip.delay, 420)
            } finally { video.close(); AppTheme.setQtTheme(oldQt) }
        }

        function test_media_checkbox_and_date_scope_do_not_open_the_viewer() {
            gallery.selectionEnabled = true
            selectionSpy.clear()
            const card = firstCard()
            const check = findChild(card, "mediaSelectionCheck")
            verify(check !== null)
            mouseClick(check)
            compare(selectionSpy.count, 1)
            compare(selectionSpy.signalArguments[0][0][0].path, card.itemData.path)
            verify(!findChild(gallery, "galleryPhotoViewer").visible)
            const view = findChild(gallery, "categoryGalleryList")
            view.positionViewAtIndex(0, ListView.Beginning)
            wait(40)
            const header = findChild(view.itemAtIndex(0), "dateGroupSelectAll")
            verify(header !== null)
            mouseClick(header)
            compare(selectionSpy.count, 2)
            compare(selectionSpy.signalArguments[1][0].length, 40)
            gallery.selectionEnabled = false
        }

        function test_open_containing_folder_carries_the_target_item() {
            const card = firstCard()
            verify(card !== null)
            const action = findChild(card, "mediaOpenContainingFolder")
            verify(action !== null)
            revealSpy.clear()
            action.triggered()
            compare(revealSpy.count, 1)
            compare(revealSpy.signalArguments[0][0], card.itemData.parentPath)
            compare(revealSpy.signalArguments[0][1], card.itemData.url)
        }
        function test_albums_group_by_parent_and_choose_recent_covers() {
            compare(gallery.albums.length, 3)
            compare(gallery.albums[0].path, "/pictures/album-0")
            compare(gallery.albums[0].count, 14)
            compare(gallery.albums[0].covers.length, 3)
            compare(gallery.albums[0].covers[0].item.name, "photo-39")
            compare(gallery.albums[0].covers[1].item.name, "photo-36")
        }
        function test_album_cover_preview_and_click_filter() {
            const strip = findChild(gallery, "galleryAlbumStrip")
            const album = strip.itemAtIndex(0)
            verify(album !== null)
            const cover = findChild(album, "galleryAlbumCover")
            mouseMove(cover, cover.width / 2, cover.height / 2)
            tryCompare(gallery, "hoverPreviewShowing", true)
            compare(gallery.hoverPreviewItem.name, "photo-39")
            mouseClick(cover, cover.width / 2, cover.height / 2)
            tryCompare(gallery, "activeAlbumPath", "/pictures/album-0")
            compare(gallery.displayFiles.length, 14)
            verify(!gallery.hoverPreviewShowing)
            verify(!findChild(gallery, "galleryPhotoViewer").visible)
        }
        function test_filter_and_return_to_all() {
            gallery.selectAlbum("/pictures/album-1")
            tryCompare(gallery, "activeAlbumPath", "/pictures/album-1")
            compare(gallery.displayFiles.length, 13)
            tryVerify(function() { return gallery.dateGroups.length > 0 && gallery.dateGroups[0].items.length === 13 })
            for (let i = 0; i < gallery.rows.length; ++i) {
                const row = gallery.rows[i]
                if (row.kind !== "cards") continue
                for (let j = 0; j < row.items.length; ++j)
                    compare(row.items[j].parentPath, "/pictures/album-1")
            }
            gallery.selectAlbum("")
            tryCompare(gallery, "activeAlbumPath", "")
            compare(gallery.displayFiles.length, 40)
        }
        function test_hover_is_delayed_large_and_clicks_reach_card() {
            const card = firstCard()
            verify(card !== null)
            mouseMove(card, card.width / 2, card.height / 2)
            verify(!gallery.hoverPreviewShowing)
            wait(200)
            verify(!gallery.hoverPreviewShowing)
            tryCompare(gallery, "hoverPreviewShowing", true)
            // The animated container now wraps the passive image preview.
            const image = findChild(gallery, "hoverPreviewImage")
            const preview = image.parent.parent.parent
            verify(preview.width > card.width * 2)
            verify(!preview.enabled)
            compare(preview.itemData.name, card.itemData.name)
            tryCompare(image, "status", Image.Ready)
            mouseClick(card, card.width / 2, card.height / 2)
            tryCompare(gallery, "hoverPreviewShowing", false)
            tryCompare(findChild(gallery, "galleryPhotoViewer"), "visible", true)
        }
        function test_scroll_cancels_preview_and_leaves_no_original_loading() {
            const card = firstCard()
            mouseMove(card, card.width / 2, card.height / 2)
            tryCompare(gallery, "hoverPreviewShowing", true)
            const view = findChild(gallery, "categoryGalleryList")
            view.contentY += 30
            tryCompare(gallery, "hoverPreviewShowing", false)
            // Keep the original alive through the 155 ms exit animation,
            // then ensure it is released rather than decoding in the background.
            tryVerify(function() {
                return findChild(gallery, "hoverPreviewImage").source.toString() === ""
            })
            wait(500)
            verify(!gallery.hoverPreviewShowing)
        }
        function test_preview_stays_in_bounds_at_bottom_right() {
            const anchor = Qt.createQmlObject('import QtQuick; Item { width: 40; height: 40 }', gallery)
            anchor.x = gallery.width - 45
            anchor.y = gallery.height - 45
            gallery.requestHoverPreview(gallery.files[0], anchor)
            tryCompare(gallery, "hoverPreviewShowing", true)
            const preview = findChild(gallery, "galleryHoverPreview")
            verify(preview.x >= 0 && preview.y >= 0)
            verify(preview.x + preview.width <= gallery.width)
            verify(preview.y + preview.height <= gallery.height)
            gallery.dismissHoverPreview()
            anchor.destroy()
        }
    }
}
