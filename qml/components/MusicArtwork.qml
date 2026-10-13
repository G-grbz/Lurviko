import QtQuick
import Lurviko.App

// Shared cover fallback for music cards and every player presentation.
Image {
    property string artworkSource: ""
    property bool coverFailed: false
    readonly property string fallbackSource: AppTheme.icon("music-cover.png")
    source: artworkSource.length && !coverFailed ? artworkSource : fallbackSource
    asynchronous: true
    cache: true
    fillMode: Image.PreserveAspectCrop
    onArtworkSourceChanged: coverFailed = false
    function useFallbackIfNeeded() {
        if (status === Image.Error && !coverFailed) coverFailed = true
    }
    // An image-provider completion must finish before another source is loaded.
    onStatusChanged: if (status === Image.Error) Qt.callLater(useFallbackIfNeeded)
}
