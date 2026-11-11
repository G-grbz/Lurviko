import QtQuick
import QtQuick.Controls
import QtTest
import Lurviko.App
import Lurviko.Backend

Item {
    width: 900
    height: 640
    ApplicationWindow {
    id: window
    width: 900
    height: 640
    visible: true
    LanguageManager { id: language }
    VideoViewer { id: viewer; lang: language }

    TestCase {
        name: "VideoRotation"
        when: windowShown

        function cleanup() {
            viewer.nativeVideoScale = false
            viewer.close()
        }

        function viewport() {
            return findChild(viewer.contentItem, "videoOutput").parent
        }

        // rotated-portrait.mp4 stores 160×90 frames tagged "rotate 90", like a
        // phone video: it must be laid out as 90×160, never stretched wide.
        function test_rotated_video_uses_displayed_size() {
            const source = String(Qt.resolvedUrl("media-fixtures/rotated-portrait.mp4"))
            if (source.indexOf("file:") !== 0)
                skip("Fixtures are not on the local filesystem")
            const path = decodeURIComponent(source.replace(/^file:\/\//, ""))
            viewer.files = [{ name: "rotated-portrait.mp4", path: path, url: source, size: 2856,
                              modifiedMs: 0, parentPath: path.substring(0, path.lastIndexOf("/")) }]
            viewer.openAt(0)
            tryVerify(function() { return viewer.firstVideoFrameReady }, 10000, "No video frame")
            compare(viewer.sourceVideoWidth, 90)
            compare(viewer.sourceVideoHeight, 160)

            const box = viewport()
            const area = box.parent
            tryVerify(function() { return box.width > 0 && box.height > box.width })
            // Fit (default): fills the window height with the portrait aspect.
            const fit = Math.min(area.width / 90, area.height / 160)
            compare(Math.round(box.width), Math.round(90 * fit))
            compare(Math.round(box.height), Math.round(160 * fit))
            verify(fit > 1, "The window is larger than the tiny fixture")

            // Native 1:1 shows real pixels, so the two modes differ visibly.
            viewer.nativeVideoScale = true
            tryCompare(box, "width", 90)
            compare(box.height, 160)
            const button = findChild(viewer.contentItem, "videoScaleModeButton")
            verify(button !== null)
            button.clicked()
            compare(viewer.nativeVideoScale, false)
        }

        function test_pause_card_without_tmdb_does_not_search() {
            const source = String(Qt.resolvedUrl("media-fixtures/rotated-portrait.mp4"))
            const path = decodeURIComponent(source.replace(/^file:\/\//, ""))
            viewer.files = [{ name: "rotated-portrait.mp4", path: path, url: source }]
            viewer.currentIndex = 0
            viewer.refreshPauseMetadata()
            compare(viewer.pauseMetadata.state, "local")
            compare(viewer.pauseMetadata.parsedTitle, "rotated-portrait.mp4")
        }
    }
}
}
