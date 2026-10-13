import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Effects
import QtMultimedia
import Lurviko.App

Rectangle {
    id: root
    objectName: "musicHoverPreview"
    required property var track
    required property var lang
    required property var anchorItem
    property var viewport: parent
    property bool audioAllowed: true
    property real previewVolume: 0.35
    property bool sampleFinished: false
    // Tests can use the real decoder with no audio sink on a headless machine.
    property bool silent: false
    readonly property bool previewPlaying: !!audioLoader.item && audioLoader.item.playing
    readonly property bool decoderActive: audioLoader.active
    readonly property bool pointerInside: false
    readonly property int sampleDurationMs: 30000
    readonly property bool shaderEffectsAvailable: GraphicsInfo.api !== GraphicsInfo.Software
    readonly property real previewProgress: audioLoader.item
        ? Math.max(0, Math.min(1, audioLoader.item.elapsed / sampleDurationMs)) : sampleFinished ? 1 : 0

    width: Math.min(340, Math.max(1, viewport ? viewport.width - 24 : 340))
    height: Math.min(196, Math.max(1, viewport ? viewport.height - 24 : 196))
    radius: 16
    antialiasing: true
    color: "#111525"
    border.width: 1
    border.color: AppTheme.accentBorder
    clip: true
    // Match the image/video previews: no focus, clicks or global mouse grabs.
    enabled: false
    z: 15000
    x: {
        if (!anchorItem || !viewport) return 12
        const point = anchorItem.mapToItem(viewport, 0, 0)
        const right = point.x + anchorItem.width + 10
        const preferred = right + width <= viewport.width - 12 ? right : point.x - width - 10
        return Math.max(12, Math.min(viewport.width - width - 12, preferred))
    }
    y: {
        if (!anchorItem || !viewport) return 12
        const point = anchorItem.mapToItem(viewport, 0, 0)
        return Math.max(12, Math.min(viewport.height - height - 12, point.y))
    }
    function label(key) { const language = lang.language; return lang.t(key) }
    function sampleStart(durationMs) {
        const duration = Math.max(0, Number(durationMs || 0))
        return duration > sampleDurationMs ? Math.min(Math.floor(duration * 0.45), duration - sampleDurationMs) : 0
    }
    function finishSample() { sampleFinished = true }
    NumberAnimation on opacity {
        from: 0; to: 1; duration: AppTheme.musicHoverAnimationsEnabled ? 140 : 0
    }
    // One small texture, only while hovering; no full-size cover decode or blur.
    Item {
        id: background
        anchors.fill: parent
        anchors.margins: 1
        // Rectangle.clip only clips its rectangular bounds, not its radius.
        // Mask this single, bounded cover/gradient layer inside the frame.
        layer.enabled: root.visible && root.shaderEffectsAvailable
        layer.smooth: true
        layer.effect: Component {
            MultiEffect {
                maskEnabled: true
                maskSource: coverMask
                maskThresholdMin: 0.01
                maskThresholdMax: 1.0
                maskSpreadAtMin: 0.0
                maskSpreadAtMax: 0.0
                autoPaddingEnabled: false
            }
        }
        MusicArtwork {
            id: artwork
            objectName: "musicHoverCover"
            anchors.fill: parent
            // Software rendering cannot use a shader mask. Keep the cover
            // inset instead of painting square corners over the round frame.
            anchors.margins: root.shaderEffectsAvailable ? 0 : root.radius
            artworkSource: String(root.track.thumbnailSource || "")
            sourceSize: Qt.size(AppTheme.previewResolution(width, Screen.devicePixelRatio),
                                AppTheme.previewResolution(height, Screen.devicePixelRatio))
        }
        Rectangle {
            anchors.fill: parent
            radius: root.shaderEffectsAvailable ? 0 : root.radius - 1
            gradient: Gradient {
                GradientStop { position: 0; color: "#66080b14" }
                GradientStop { position: 0.45; color: "#9e080b14" }
                GradientStop { position: 1; color: "#ef080b14" }
            }
        }
    }
    Rectangle {
        id: coverMask
        width: background.width
        height: background.height
        radius: Math.max(0, root.radius - 1)
        color: "white"
        antialiasing: true
        visible: false
        layer.enabled: background.layer.enabled
        layer.smooth: true
    }
    // Keep the frame above the artwork, and text readable in either app theme.
    Rectangle {
        anchors.fill: parent
        radius: root.radius
        color: "transparent"
        border.width: 1
        border.color: AppTheme.accentBorder
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 6
        Item { Layout.fillHeight: true }
        Text {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: String(root.track.title || root.track.name || "")
            textFormat: Text.PlainText
            font.pixelSize: 15
            font.weight: Font.DemiBold
            color: "white"
            maximumLineCount: 2
            wrapMode: Text.Wrap
            elide: Text.ElideRight
        }
        Text {
            Layout.fillWidth: true
            text: String(root.track.artist || "")
            textFormat: Text.PlainText
            font.pixelSize: 11
            color: "#e3e7f0"
            elide: Text.ElideRight
        }
        Text {
            Layout.fillWidth: true
            text: [root.track.album, root.track.year, root.track.genre].filter(Boolean).join(" · ")
            textFormat: Text.PlainText
            font.pixelSize: 10
            color: "#bec6d8"
            elide: Text.ElideRight
        }
        Text {
            Layout.fillWidth: true
            text: root.label(!root.audioAllowed ? "music_preview_paused"
                            : root.sampleFinished ? "music_preview_finished" : "music_preview_sample")
            font.pixelSize: 10
            color: "#e3e7f0"
            wrapMode: Text.Wrap
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 3
            radius: 1.5
            color: "#40ffffff"
            Rectangle {
                width: parent.width * root.previewProgress
                height: parent.height
                radius: parent.radius
                color: AppTheme.accent
            }
        }
    }
    // No media objects, polling timers or decoding survive the hover card.
    Loader {
        id: audioLoader
        objectName: "musicHoverAudioLoader"
        active: root.audioAllowed && !root.sampleFinished
                && String(root.track.itemUrl || "").startsWith("file://")
        sourceComponent: Item {
            readonly property bool playing: sample.playbackState === MediaPlayer.PlayingState
            readonly property int elapsed: sample.started ? sample.position - sample.startPosition : 0
            AudioOutput { id: output; volume: Math.max(0, Math.min(0.5, root.previewVolume)) }
            MediaPlayer {
                id: sample
                objectName: "musicHoverMediaPlayer"
                source: String(root.track.itemUrl || "")
                audioOutput: root.silent ? null : output
                activeVideoTrack: -1
                activeSubtitleTrack: -1
                property bool started: false
                property int startPosition: 0
                function startSample() {
                    if (started || duration <= 0
                            || (mediaStatus !== MediaPlayer.LoadedMedia && mediaStatus !== MediaPlayer.BufferedMedia)) return
                    started = true
                    // Album artwork can be exposed as an attached video track.
                    // The hover decoder needs only audio, not video or subtitles.
                    activeVideoTrack = -1
                    activeSubtitleTrack = -1
                    startPosition = root.sampleStart(duration)
                    position = startPosition
                    play()
                }
                // Media backend notifications can be emitted while a track
                // transition holds internal locks. Do not seek/start or select
                // tracks reentrantly from those notifications.
                onDurationChanged: Qt.callLater(sample.startSample)
                onMediaStatusChanged: {
                    Qt.callLater(sample.startSample)
                    if (mediaStatus === MediaPlayer.EndOfMedia) Qt.callLater(root.finishSample)
                }
                onPlaybackStateChanged: if (playbackState === MediaPlayer.PlayingState) expiry.restart()
                onPositionChanged: function(position) {
                    if (started && position >= startPosition + root.sampleDurationMs) Qt.callLater(root.finishSample)
                }
                onErrorOccurred: Qt.callLater(root.finishSample)
            }
            Timer { id: expiry; objectName: "musicHoverExpiry"; interval: root.sampleDurationMs; onTriggered: root.finishSample() }
            // QMediaPlayer tears down its backend itself. Do not call stop()
            // while the Loader is already destroying its media objects.
        }
    }
}
