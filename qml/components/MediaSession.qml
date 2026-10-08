import QtQml
import Lurviko.Backend

MprisController {
    id: session
    property var musicPlayer: null
    property var videoPlayer: null
    readonly property bool usingVideo: !!videoPlayer && videoPlayer.playbackSessionOpen
    readonly property var activePlayer: usingVideo ? videoPlayer : musicPlayer

    active: !!activePlayer && (usingVideo || activePlayer.opened) && activePlayer.currentUrl.length > 0
    playbackStatus: active ? activePlayer.playbackStatus : "Stopped"
    title: active ? activePlayer.currentTitle : ""
    trackMetadata: active ? (usingVideo ? activePlayer.sessionMetadata : activePlayer.trackMeta) : ({})
    trackUrl: active ? activePlayer.currentUrl : ""
    artworkHint: active ? activePlayer.currentArtwork : ""
    duration: active ? activePlayer.duration : 0
    position: active ? activePlayer.position : 0
    volume: activePlayer ? activePlayer.volume : 0.82
    shuffle: active && !usingVideo ? activePlayer.shuffleMode : false
    repeatMode: active && !usingVideo ? activePlayer.repeatMode : 0

    onPlayRequested: if (active) activePlayer.play()
    onPauseRequested: if (active) activePlayer.pause()
    onPlayPauseRequested: if (active) { if (usingVideo) activePlayer.togglePlayback(); else activePlayer.toggle() }
    onStopRequested: if (active) activePlayer.stopPlayback()
    onNextRequested: if (active) { if (usingVideo) activePlayer.nextVideo(); else activePlayer.next() }
    onPreviousRequested: if (active) { if (usingVideo) activePlayer.previousVideo(); else activePlayer.previous() }
    onSeekRequested: (offsetMs) => { if (active) activePlayer.seekBy(offsetMs) }
    onSetPositionRequested: (positionMs) => { if (active) activePlayer.seekTo(positionMs) }
    onVolumeRequested: (value) => { if (active) activePlayer.setVolume(value) }
    onShuffleRequested: (enabled) => { if (active && !usingVideo) activePlayer.setShuffleEnabled(enabled) }
    onRepeatModeRequested: (mode) => { if (active && !usingVideo) activePlayer.setRepeatMode(mode) }
}
