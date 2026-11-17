import QtQuick
import Lurviko.App

// Shared cover fallback for music cards and every player presentation.
Image {
    property string artworkSource: ""
    property bool coverFailed: false
    // While true (fast scrolling), a new cover is not requested and nothing
    // is drawn: the card shows its plain artwork background instead of
    // starting an extraction for every track flying past. The current cover
    // is requested once it is false.
    property bool deferred: false
    // Initial value as a binding (no fallback flash on creation); later
    // updates are assigned by commitSource().
    property string committedSource: deferred ? "" : artworkSource
    readonly property string fallbackSource: AppTheme.icon("music-cover.png")
    source: committedSource.length && !coverFailed ? committedSource
                                                  : (deferred ? "" : fallbackSource)
    asynchronous: true
    cache: true
    fillMode: Image.PreserveAspectCrop
    function commitSource() {
        if (!deferred)
            committedSource = artworkSource
        else if (committedSource !== artworkSource)
            committedSource = ""
    }
    onArtworkSourceChanged: { coverFailed = false; commitSource() }
    onDeferredChanged: commitSource()
    function useFallbackIfNeeded() {
        if (status === Image.Error && !coverFailed) coverFailed = true
    }
    // An image-provider completion must finish before another source is loaded.
    onStatusChanged: if (status === Image.Error) Qt.callLater(useFallbackIfNeeded)
}
