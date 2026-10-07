import QtQuick

// Holds video previews until the file has stopped changing for a short time.
// Recordings, downloads and copies update their size/mtime repeatedly; sending
// every intermediate state to ffmpegthumbnailer made the delegate alternate
// between a partial black frame and the fallback video icon.
Image {
    id: preview

    property string candidateSource: ""
    property bool paused: false
    property string fileIdentity: ""
    property string fileSuffix: ""
    property var fileModified: null
    property var fileSize: 0
    property int videoSettleDelay: 2500
    property string settledSource: ""
    property string trackedIdentity: ""
    property string trackedMetadata: ""
    property string trackedCandidate: ""
    property bool componentReady: false

    readonly property bool isVideo: {
        const suffix = fileSuffix.toLowerCase()
        return ["mp4", "mkv", "avi", "mov", "webm", "m4v",
                "ts", "mpeg", "mpg"].indexOf(suffix) >= 0
    }

    function modifiedMilliseconds() {
        if (!fileModified)
            return 0
        if (fileModified.getTime)
            return fileModified.getTime()
        const parsed = Date.parse(String(fileModified))
        return isNaN(parsed) ? 0 : parsed
    }

    function metadataToken() {
        return String(modifiedMilliseconds()) + "|" + String(fileSize || 0)
    }

    function queueSourceUpdate() {
        if (!componentReady || paused)
            return
        updateTimer.restart()
    }

    function applySourceUpdate() {
        if (paused)
            return
        settleTimer.stop()
        if (!candidateSource) {
            trackedIdentity = fileIdentity
            trackedMetadata = metadataToken()
            trackedCandidate = ""
            settledSource = ""
            return
        }

        const metadata = metadataToken()
        const newFile = trackedIdentity !== fileIdentity
        const metadataChanged = !newFile && trackedMetadata !== "" && trackedMetadata !== metadata
        const modifiedMs = modifiedMilliseconds()
        const recentlyModified = modifiedMs > 0
                                 && Date.now() - modifiedMs < videoSettleDelay * 2

        trackedIdentity = fileIdentity
        trackedMetadata = metadata
        trackedCandidate = candidateSource

        if (isVideo && (metadataChanged || (newFile && recentlyModified))) {
            // Only changing/new video files need the stability delay. An old,
            // unchanged file can request its cached cover immediately when a
            // directory delegate is recreated.
            settledSource = ""
            settleTimer.restart()
            return
        }
        settledSource = candidateSource
    }

    source: settledSource
    asynchronous: true
    cache: true
    fillMode: Image.PreserveAspectFit
    smooth: true
    mipmap: true

    onCandidateSourceChanged: queueSourceUpdate()
    onFileIdentityChanged: queueSourceUpdate()
    onFileSuffixChanged: queueSourceUpdate()
    onFileModifiedChanged: queueSourceUpdate()
    onFileSizeChanged: queueSourceUpdate()
    onPausedChanged: {
        if (!paused)
            queueSourceUpdate()
    }
    Component.onCompleted: {
        componentReady = true
        queueSourceUpdate()
    }

    Timer {
        id: updateTimer
        interval: 0
        repeat: false
        onTriggered: preview.applySourceUpdate()
    }

    Timer {
        id: settleTimer
        interval: preview.videoSettleDelay
        repeat: false
        onTriggered: {
            if (preview.paused)
                return
            preview.trackedCandidate = preview.candidateSource
            preview.settledSource = preview.candidateSource
        }
    }
}
