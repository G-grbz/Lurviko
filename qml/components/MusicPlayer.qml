import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia
import QtCore
import GFile.App

Rectangle {
    id: root
    required property var lang
    property var queue: []
    property var sourceLibrary: []
    property int currentIndex: -1
    property bool opened: false
    property bool queueOpen: false
    property bool panelVisible: true
    property int drawerTab: 0 // 0 queue, 1 playlists
    property bool shuffleMode: false
    property int repeatMode: 0 // 0 off, 1 repeat all, 2 repeat one
    property var playbackOrder: []
    property int playbackOrderPosition: -1
    property real rememberedVolume: 0.82
    property var playlists: []
    // When true the same global player is visually docked into the Music
    // category content area. Playback state remains global; only the chrome
    // changes so the bar feels like part of the page rather than a floating card.
    property bool embeddedMode: false

    property alias backgroundPlayback: playerPrefs.backgroundPlayback
    property alias queueLimit: playerPrefs.queueSize

    signal requestClose()

    radius: embeddedMode ? 14 : 16
    color: AppTheme.surface
    border.width: embeddedMode ? 0 : 1
    border.color: AppTheme.border
    visible: opened && panelVisible
    implicitHeight: queueOpen ? 360 : 126
    clip: true

    Behavior on implicitHeight {
        NumberAnimation { duration: 170; easing.type: Easing.OutCubic }
    }

    readonly property var currentItem: currentIndex >= 0 && currentIndex < queue.length ? queue[currentIndex] : null
    readonly property string currentTitle: currentItem ? String(currentItem.name || "") : ""
    readonly property string currentArtwork: currentItem ? String(currentItem.thumbnailSource || "") : ""
    readonly property string currentUrl: currentItem ? String(currentItem.itemUrl || "") : ""
    readonly property bool playing: player.playbackState === MediaPlayer.PlayingState
    readonly property bool paused: player.playbackState === MediaPlayer.PausedState
    readonly property real position: player.position
    readonly property real duration: player.duration
    readonly property real volume: root.rememberedVolume
    readonly property bool muted: audioOutput.muted
    readonly property string playbackStatus: root.playing ? "Playing" : (root.paused ? "Paused" : "Stopped")

    Settings {
        id: playerPrefs
        category: "MusicPlayer"
        property bool backgroundPlayback: true
        property int queueSize: 50
        property string playlistsJson: "[]"
    }

    function icon(name) { return "qrc:/qt/qml/GFile/App/assets/icons/" + name }

    function formatTime(ms) {
        const total = Math.max(0, Math.floor(Number(ms || 0) / 1000))
        const minutes = Math.floor(total / 60)
        const seconds = total % 60
        return minutes + ":" + (seconds < 10 ? "0" : "") + seconds
    }

    function normalizedItem(item) {
        if (!item)
            return ({})
        return {
            itemUrl: String(item.itemUrl || ""),
            name: String(item.name || ""),
            thumbnailSource: String(item.thumbnailSource || "")
        }
    }

    function normalizedLibrary(items) {
        const result = []
        const src = items || []
        for (let i = 0; i < src.length; ++i) {
            const entry = normalizedItem(src[i])
            if (entry.itemUrl.length)
                result.push(entry)
        }
        return result
    }

    function findUrl(items, url) {
        const target = String(url || "")
        for (let i = 0; i < items.length; ++i) {
            if (String(items[i].itemUrl || "") === target)
                return i
        }
        return -1
    }

    function shuffledCopy(items) {
        const result = items.slice()
        for (let i = result.length - 1; i > 0; --i) {
            const j = Math.floor(Math.random() * (i + 1))
            const temp = result[i]
            result[i] = result[j]
            result[j] = temp
        }
        return result
    }

    function rebuildPlaybackOrder(startIndex) {
        if (!queue.length) {
            playbackOrder = []
            playbackOrderPosition = -1
            return
        }
        const start = Math.max(0, Math.min(Number(startIndex || 0), queue.length - 1))
        const rest = []
        for (let i = 0; i < queue.length; ++i) {
            if (i !== start)
                rest.push(i)
        }
        const orderedRest = shuffleMode ? shuffledCopy(rest) : rest
        playbackOrder = [start].concat(orderedRest)
        playbackOrderPosition = 0
    }

    function selectQueueIndex(index, autoplay) {
        const idx = Number(index)
        if (idx < 0 || idx >= queue.length)
            return
        currentIndex = idx
        rebuildPlaybackOrder(idx)
        loadCurrent(autoplay === undefined ? true : !!autoplay)
    }

    function freshQueue(autoplay) {
        const lib = normalizedLibrary(sourceLibrary)
        if (!lib.length)
            return

        // "New queue" means a genuinely new queue. Do not pin the currently
        // playing item: prefer tracks that were not present in the previous
        // queue at all, then fill from the remaining library only if needed.
        const limit = Math.max(1, Math.min(Number(queueLimit || 50), lib.length))
        const oldUrls = ({})
        for (let i = 0; i < queue.length; ++i)
            oldUrls[String(queue[i].itemUrl || "")] = true

        const freshCandidates = []
        const fallbackCandidates = []
        for (let i = 0; i < lib.length; ++i) {
            const entry = normalizedItem(lib[i])
            if (!entry.itemUrl.length)
                continue
            if (!oldUrls[entry.itemUrl])
                freshCandidates.push(entry)
            else
                fallbackCandidates.push(entry)
        }

        const result = []
        const freshMixed = shuffledCopy(freshCandidates)
        for (let i = 0; i < freshMixed.length && result.length < limit; ++i)
            result.push(freshMixed[i])

        // Small libraries may not contain enough completely new tracks.
        // In that case reuse old tracks only as a last resort, still shuffled.
        if (result.length < limit) {
            const fallbackMixed = shuffledCopy(fallbackCandidates)
            for (let i = 0; i < fallbackMixed.length && result.length < limit; ++i)
                result.push(fallbackMixed[i])
        }
        if (!result.length)
            return

        queue = result
        currentIndex = 0
        opened = true
        panelVisible = true
        rebuildPlaybackOrder(0)
        loadCurrent(autoplay === undefined ? true : !!autoplay)
    }

    function buildQueue(startUrl, autoplay) {
        const lib = sourceLibrary && sourceLibrary.length ? sourceLibrary : queue
        if (!lib || !lib.length)
            return

        let start = findUrl(lib, startUrl)
        if (start < 0)
            start = 0

        const limit = Math.max(1, Math.min(Number(queueLimit || 50), lib.length))
        const first = normalizedItem(lib[start])
        const result = [first]

        for (let offset = 1; offset < lib.length && result.length < limit; ++offset) {
            const idx = (start + offset) % lib.length
            result.push(normalizedItem(lib[idx]))
        }

        queue = result
        currentIndex = 0
        opened = true
        panelVisible = true
        rebuildPlaybackOrder(0)
        loadCurrent(autoplay === undefined ? true : !!autoplay)
    }

    function openUrl(url, title, artwork, items) {
        sourceLibrary = normalizedLibrary(items && items.length ? items : [{ itemUrl: url, name: title, thumbnailSource: artwork }])
        if (findUrl(sourceLibrary, url) < 0)
            sourceLibrary.unshift({ itemUrl: String(url || ""), name: String(title || ""), thumbnailSource: String(artwork || "") })
        buildQueue(url, true)
    }

    function loadCurrent(autoplay) {
        if (!currentItem)
            return
        player.stop()
        player.source = String(currentItem.itemUrl || "")
        if (autoplay)
            player.play()
    }

    function previous() {
        if (!queue.length)
            return
        if (playbackOrder.length !== queue.length || playbackOrderPosition < 0)
            rebuildPlaybackOrder(currentIndex >= 0 ? currentIndex : 0)
        let nextPos = playbackOrderPosition - 1
        if (nextPos < 0)
            nextPos = playbackOrder.length - 1
        playbackOrderPosition = nextPos
        currentIndex = Number(playbackOrder[nextPos])
        loadCurrent(true)
    }

    function next(manual) {
        if (!queue.length)
            return false
        if (playbackOrder.length !== queue.length || playbackOrderPosition < 0)
            rebuildPlaybackOrder(currentIndex >= 0 ? currentIndex : 0)
        let nextPos = playbackOrderPosition + 1
        if (nextPos >= playbackOrder.length) {
            if (manual === false && repeatMode === 0)
                return false
            nextPos = 0
        }
        playbackOrderPosition = nextPos
        currentIndex = Number(playbackOrder[nextPos])
        loadCurrent(true)
        return true
    }

    function toggle() {
        if (!currentItem)
            return
        if (playing)
            player.pause()
        else
            player.play()
    }

    function refreshQueue() {
        freshQueue(true)
    }

    function toggleShuffle() {
        // Shuffle changes only the order of *future* playback. Never reload
        // or seek the current track when the user toggles the mode.
        const liveIndex = currentIndex >= 0 ? currentIndex : 0
        const livePosition = player.position
        const wasPlaying = playing
        shuffleMode = !shuffleMode
        rebuildPlaybackOrder(liveIndex)
        Qt.callLater(function() {
            if (Math.abs(player.position - livePosition) > 750)
                player.position = livePosition
            if (wasPlaying && player.playbackState !== MediaPlayer.PlayingState)
                player.play()
        })
    }

    function cycleRepeatMode() {
        repeatMode = (repeatMode + 1) % 3
    }

    function setRepeatMode(value) {
        repeatMode = Math.max(0, Math.min(2, Number(value || 0)))
    }

    function setShuffleEnabled(value) {
        const enabled = !!value
        if (shuffleMode !== enabled)
            toggleShuffle()
    }

    function seekTo(ms) {
        if (!currentItem || player.duration <= 0)
            return
        player.position = Math.max(0, Math.min(Number(ms || 0), player.duration))
    }

    function seekBy(deltaMs) {
        seekTo(player.position + Number(deltaMs || 0))
    }

    function setVolume(value) {
        rememberedVolume = Math.max(0, Math.min(1, Number(value || 0)))
        audioOutput.muted = false
    }

    function toggleMute() {
        audioOutput.muted = !audioOutput.muted
    }

    function stopPlayback() {
        player.stop()
    }

    function play() {
        if (currentItem)
            player.play()
    }

    function pause() {
        player.pause()
    }

    function minimize() {
        panelVisible = false
        queueOpen = false
    }

    function restorePanel() {
        if (opened)
            panelVisible = true
    }

    function toggleQueue(tab) {
        const requested = tab === undefined ? drawerTab : Number(tab)
        if (queueOpen && drawerTab === requested) {
            queueOpen = false
            return
        }
        drawerTab = requested
        queueOpen = true
    }

    function closePlayer() {
        player.stop()
        opened = false
        panelVisible = false
        queueOpen = false
        playbackOrder = []
        playbackOrderPosition = -1
        requestClose()
    }

    function reloadPlaylists() {
        try {
            const parsed = JSON.parse(String(playerPrefs.playlistsJson || "[]"))
            playlists = Array.isArray(parsed) ? parsed : []
        } catch (e) {
            playlists = []
        }
    }

    function persistPlaylists(nextPlaylists) {
        playlists = nextPlaylists
        playerPrefs.playlistsJson = JSON.stringify(nextPlaylists)
    }

    function saveCurrentQueueAsPlaylist(name) {
        const clean = String(name || "").trim()
        if (!clean.length || !queue.length)
            return
        const list = playlists.slice()
        const items = normalizedLibrary(queue)
        let replaced = false
        for (let i = 0; i < list.length; ++i) {
            if (String(list[i].name || "").toLocaleLowerCase() === clean.toLocaleLowerCase()) {
                list[i] = { name: clean, items: items }
                replaced = true
                break
            }
        }
        if (!replaced)
            list.push({ name: clean, items: items })
        persistPlaylists(list)
    }

    function playPlaylist(index) {
        if (index < 0 || index >= playlists.length)
            return
        const items = normalizedLibrary(playlists[index].items || [])
        if (!items.length)
            return
        sourceLibrary = items
        queue = items.slice(0, Math.min(items.length, Math.max(1, queueLimit)))
        currentIndex = 0
        opened = true
        panelVisible = true
        rebuildPlaybackOrder(0)
        loadCurrent(true)
    }

    function deletePlaylist(index) {
        if (index < 0 || index >= playlists.length)
            return
        const list = playlists.slice()
        list.splice(index, 1)
        persistPlaylists(list)
    }

    Component.onCompleted: reloadPlaylists()

    AudioOutput {
        id: audioOutput
        volume: root.rememberedVolume
    }

    MediaPlayer {
        id: player
        audioOutput: audioOutput
        onMediaStatusChanged: {
            if (mediaStatus === MediaPlayer.EndOfMedia) {
                if (root.repeatMode === 2)
                    root.loadCurrent(true)
                else
                    root.next(false)
            }
        }
    }

    Rectangle {
        visible: root.embeddedMode
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: root.embeddedMode ? 14 : 0
        anchors.rightMargin: root.embeddedMode ? 14 : 0
        anchors.top: parent.top
        height: 1
        color: AppTheme.border
        z: 2
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 100
            spacing: 12

            Rectangle {
                Layout.preferredWidth: 82
                Layout.preferredHeight: 82
                radius: 12
                color: AppTheme.surfaceRaised
                border.color: AppTheme.border
                clip: true

                CrispIcon {
                    anchors.centerIn: parent
                    width: 38
                    height: 38
                    source: AppTheme.icon("music-note.svg")
                    visible: art.status !== Image.Ready
                }
                Image {
                    id: art
                    anchors.fill: parent
                    source: root.currentArtwork
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: true
                    visible: status === Image.Ready
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Text {
                        Layout.fillWidth: true
                        text: root.currentTitle || (root.lang.language === "tr" ? "Müzik oynatıcı" : "Music player")
                        color: AppTheme.text
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        elide: Text.ElideMiddle
                    }
                    Text {
                        text: root.queue.length > 0 && root.currentIndex >= 0
                              ? (root.currentIndex + 1) + " / " + root.queue.length : ""
                        color: AppTheme.textMuted
                        font.pixelSize: 9
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text {
                        text: root.formatTime(player.position)
                        color: AppTheme.textMuted
                        font.pixelSize: 9
                    }
                    GSlider {
                        id: seekSlider
                        Layout.fillWidth: true
                        Layout.preferredHeight: 20
                        from: 0
                        to: Math.max(1, player.duration)
                        value: player.position
                        onMoved: player.position = value
                        background: Rectangle {
                            x: seekSlider.leftPadding
                            y: seekSlider.topPadding + seekSlider.availableHeight / 2 - height / 2
                            width: seekSlider.availableWidth
                            height: 4
                            radius: 2
                            color: AppTheme.surfaceHover
                            Rectangle {
                                width: seekSlider.visualPosition * parent.width
                                height: parent.height
                                radius: parent.radius
                                color: AppTheme.accent
                            }
                        }
                        handle: Rectangle {
                            x: seekSlider.leftPadding + seekSlider.visualPosition * (seekSlider.availableWidth - width)
                            y: seekSlider.topPadding + seekSlider.availableHeight / 2 - height / 2
                            implicitWidth: 12
                            implicitHeight: 12
                            radius: 6
                            color: seekSlider.pressed ? AppTheme.accent : AppTheme.surface
                            border.width: 1
                            border.color: AppTheme.accentBorder
                        }
                    }
                    Text {
                        text: root.formatTime(player.duration)
                        color: AppTheme.textMuted
                        font.pixelSize: 9
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 34
                    spacing: 5

                    GToolButton {
                        background: Rectangle {
                            radius: 9
                            color: parent.checked ? AppTheme.accentSoft : (parent.hovered ? AppTheme.surfaceHover : "transparent")
                            border.width: parent.checked ? 1 : 0
                            border.color: parent.checked ? AppTheme.accentBorder : "transparent"
                            Behavior on color { ColorAnimation { duration: 110 } }
                        }
                        implicitWidth: 34; implicitHeight: 32
                        icon.source: root.icon("viewer-prev.svg")
                        onClicked: root.previous()
                        ToolTip.visible: hovered
                        ToolTip.text: root.lang.language === "tr" ? "Önceki" : "Previous"
                    }
                    GToolButton {
                        background: Rectangle {
                            radius: 9
                            color: parent.checked ? AppTheme.accentSoft : (parent.hovered ? AppTheme.surfaceHover : "transparent")
                            border.width: parent.checked ? 1 : 0
                            border.color: parent.checked ? AppTheme.accentBorder : "transparent"
                            Behavior on color { ColorAnimation { duration: 110 } }
                        }
                        implicitWidth: 40; implicitHeight: 34
                        icon.source: root.icon(root.playing ? "viewer-pause.svg" : "viewer-play.svg")
                        onClicked: root.toggle()
                        ToolTip.visible: hovered
                        ToolTip.text: root.lang.language === "tr" ? "Oynat / duraklat" : "Play / pause"
                    }
                    GToolButton {
                        background: Rectangle {
                            radius: 9
                            color: parent.checked ? AppTheme.accentSoft : (parent.hovered ? AppTheme.surfaceHover : "transparent")
                            border.width: parent.checked ? 1 : 0
                            border.color: parent.checked ? AppTheme.accentBorder : "transparent"
                            Behavior on color { ColorAnimation { duration: 110 } }
                        }
                        implicitWidth: 34; implicitHeight: 32
                        icon.source: root.icon("viewer-next.svg")
                        onClicked: root.next()
                        ToolTip.visible: hovered
                        ToolTip.text: root.lang.language === "tr" ? "Sonraki" : "Next"
                    }

                    GToolButton {
                        implicitWidth: 34; implicitHeight: 32
                        icon.source: root.icon("music-shuffle.svg")
                        checked: root.shuffleMode
                        onClicked: root.toggleShuffle()
                        background: Rectangle {
                            radius: 8
                            color: parent.checked ? AppTheme.accentSoft : (parent.hovered ? AppTheme.surfaceHover : "transparent")
                            border.color: parent.checked ? AppTheme.accentBorder : "transparent"
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: root.shuffleMode
                                      ? (root.lang.language === "tr" ? "Karışık oynatma açık" : "Shuffle on")
                                      : (root.lang.language === "tr" ? "Karışık oynatma kapalı" : "Shuffle off")
                    }
                    GToolButton {
                        implicitWidth: 38; implicitHeight: 32
                        icon.source: root.icon(root.repeatMode === 2 ? "music-repeat-one.svg" : "music-repeat.svg")
                        checked: root.repeatMode !== 0
                        onClicked: root.cycleRepeatMode()
                        background: Rectangle {
                            radius: 8
                            color: parent.checked ? AppTheme.accentSoft : (parent.hovered ? AppTheme.surfaceHover : "transparent")
                            border.color: parent.checked ? AppTheme.accentBorder : "transparent"
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: root.repeatMode === 0
                                      ? (root.lang.language === "tr" ? "Tekrar kapalı" : "Repeat off")
                                      : root.repeatMode === 1
                                        ? (root.lang.language === "tr" ? "Tümünü tekrarla" : "Repeat all")
                                        : (root.lang.language === "tr" ? "Tek parçayı tekrarla" : "Repeat one")
                    }
                    GToolButton {
                        background: Rectangle {
                            radius: 9
                            color: parent.checked ? AppTheme.accentSoft : (parent.hovered ? AppTheme.surfaceHover : "transparent")
                            border.width: parent.checked ? 1 : 0
                            border.color: parent.checked ? AppTheme.accentBorder : "transparent"
                            Behavior on color { ColorAnimation { duration: 110 } }
                        }
                        implicitWidth: 34; implicitHeight: 32
                        icon.source: root.icon("music-refresh.svg")
                        onClicked: root.refreshQueue()
                        ToolTip.visible: hovered
                        ToolTip.text: root.lang.language === "tr" ? "Yeni kuyruk oluştur" : "Generate new queue"
                    }
                    GToolButton {
                        background: Rectangle {
                            radius: 9
                            color: parent.checked ? AppTheme.accentSoft : (parent.hovered ? AppTheme.surfaceHover : "transparent")
                            border.width: parent.checked ? 1 : 0
                            border.color: parent.checked ? AppTheme.accentBorder : "transparent"
                            Behavior on color { ColorAnimation { duration: 110 } }
                        }
                        implicitWidth: 38; implicitHeight: 32
                        icon.source: root.icon("music-queue.svg")
                        onClicked: root.toggleQueue(0)
                        ToolTip.visible: hovered
                        ToolTip.text: root.lang.language === "tr" ? "Kuyruğu göster" : "Show queue"
                    }
                    GToolButton {
                        background: Rectangle {
                            radius: 9
                            color: parent.checked ? AppTheme.accentSoft : (parent.hovered ? AppTheme.surfaceHover : "transparent")
                            border.width: parent.checked ? 1 : 0
                            border.color: parent.checked ? AppTheme.accentBorder : "transparent"
                            Behavior on color { ColorAnimation { duration: 110 } }
                        }
                        implicitWidth: 38; implicitHeight: 32
                        icon.source: root.icon("music-playlist.svg")
                        onClicked: root.toggleQueue(1)
                        ToolTip.visible: hovered
                        ToolTip.text: root.lang.language === "tr" ? "Çalma listeleri" : "Playlists"
                    }

                    Item { Layout.fillWidth: true }

                    GToolButton {
                        background: Rectangle {
                            radius: 9
                            color: parent.checked ? AppTheme.accentSoft : (parent.hovered ? AppTheme.surfaceHover : "transparent")
                            border.width: parent.checked ? 1 : 0
                            border.color: parent.checked ? AppTheme.accentBorder : "transparent"
                            Behavior on color { ColorAnimation { duration: 110 } }
                        }
                        implicitWidth: 30; implicitHeight: 30
                        icon.source: root.icon(audioOutput.muted || root.rememberedVolume <= 0.001
                                               ? "viewer-volume-muted.svg" : "viewer-volume.svg")
                        onClicked: root.toggleMute()
                        ToolTip.visible: hovered
                        ToolTip.text: root.lang.language === "tr" ? "Sesi aç / kapat" : "Mute / unmute"
                    }
                    GSlider {
                        id: volumeSlider
                        Layout.preferredWidth: 96
                        Layout.preferredHeight: 20
                        from: 0
                        to: 1
                        value: root.rememberedVolume
                        onMoved: {
                            root.rememberedVolume = value
                            audioOutput.muted = false
                        }
                        background: Rectangle {
                            x: volumeSlider.leftPadding
                            y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - height / 2
                            width: volumeSlider.availableWidth
                            height: 3
                            radius: 2
                            color: AppTheme.surfaceHover
                            Rectangle {
                                width: volumeSlider.visualPosition * parent.width
                                height: parent.height
                                radius: parent.radius
                                color: AppTheme.accent
                            }
                        }
                        handle: Rectangle {
                            x: volumeSlider.leftPadding + volumeSlider.visualPosition * (volumeSlider.availableWidth - width)
                            y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - height / 2
                            implicitWidth: 10
                            implicitHeight: 10
                            radius: 5
                            color: volumeSlider.pressed ? AppTheme.accent : AppTheme.surface
                            border.width: 1
                            border.color: AppTheme.accentBorder
                        }
                    }
                    GToolButton {
                        background: Rectangle {
                            radius: 9
                            color: parent.checked ? AppTheme.accentSoft : (parent.hovered ? AppTheme.surfaceHover : "transparent")
                            border.width: parent.checked ? 1 : 0
                            border.color: parent.checked ? AppTheme.accentBorder : "transparent"
                            Behavior on color { ColorAnimation { duration: 110 } }
                        }
                        implicitWidth: 34; implicitHeight: 32
                        icon.source: root.icon("music-minimize.svg")
                        onClicked: root.minimize()
                        ToolTip.visible: hovered
                        ToolTip.text: root.lang.language === "tr" ? "Simge durumuna küçült" : "Minimize player"
                    }
                    GToolButton {
                        background: Rectangle {
                            radius: 9
                            color: parent.checked ? AppTheme.accentSoft : (parent.hovered ? AppTheme.surfaceHover : "transparent")
                            border.width: parent.checked ? 1 : 0
                            border.color: parent.checked ? AppTheme.accentBorder : "transparent"
                            Behavior on color { ColorAnimation { duration: 110 } }
                        }
                        implicitWidth: 34; implicitHeight: 32
                        icon.source: root.icon("viewer-close.svg")
                        onClicked: root.closePlayer()
                        ToolTip.visible: hovered
                        ToolTip.text: root.lang.language === "tr" ? "Oynatıcıyı kapat" : "Close player"
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.queueOpen
            radius: 12
            color: AppTheme.surfaceRaised
            border.color: AppTheme.border
            clip: true

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    GButton {
                        id: queueTabButton
                        Layout.preferredWidth: 74
                        Layout.preferredHeight: 34
                        background: Rectangle {
                            radius: 9
                            color: queueTabButton.checked ? AppTheme.accentSoft : (queueTabButton.hovered ? AppTheme.surfaceHover : AppTheme.surface)
                            border.width: 1
                            border.color: queueTabButton.checked ? AppTheme.accentBorder : AppTheme.border
                        }
                        contentItem: Text {
                            text: root.lang.language === "tr" ? "Kuyruk" : "Queue"
                            color: queueTabButton.checked ? AppTheme.accent : AppTheme.text
                            font.pixelSize: 10
                            font.weight: queueTabButton.checked ? Font.DemiBold : Font.Medium
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        checked: root.drawerTab === 0
                        onClicked: root.drawerTab = 0
                    }
                    GButton {
                        id: playlistTabButton
                        Layout.preferredWidth: 104
                        Layout.preferredHeight: 34
                        background: Rectangle {
                            radius: 9
                            color: playlistTabButton.checked ? AppTheme.accentSoft : (playlistTabButton.hovered ? AppTheme.surfaceHover : AppTheme.surface)
                            border.width: 1
                            border.color: playlistTabButton.checked ? AppTheme.accentBorder : AppTheme.border
                        }
                        contentItem: Text {
                            text: root.lang.language === "tr" ? "Çalma listeleri" : "Playlists"
                            color: playlistTabButton.checked ? AppTheme.accent : AppTheme.text
                            font.pixelSize: 10
                            font.weight: playlistTabButton.checked ? Font.DemiBold : Font.Medium
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        checked: root.drawerTab === 1
                        onClicked: root.drawerTab = 1
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        visible: root.drawerTab === 0
                        text: root.queue.length + (root.lang.language === "tr" ? " parça" : " tracks")
                        color: AppTheme.textMuted
                        font.pixelSize: 9
                    }
                    GToolButton {
                        background: Rectangle {
                            radius: 9
                            color: parent.checked ? AppTheme.accentSoft : (parent.hovered ? AppTheme.surfaceHover : "transparent")
                            border.width: parent.checked ? 1 : 0
                            border.color: parent.checked ? AppTheme.accentBorder : "transparent"
                            Behavior on color { ColorAnimation { duration: 110 } }
                        }
                        implicitWidth: 30; implicitHeight: 28
                        icon.source: root.icon("viewer-close.svg")
                        onClicked: root.queueOpen = false
                    }
                }

                ListView {
                    id: queueList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.drawerTab === 0
                    clip: true
                    spacing: 3
                    model: root.queue
                    currentIndex: root.currentIndex
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        width: queueList.width
                        height: 34
                        radius: 7
                        color: index === root.currentIndex ? AppTheme.accentSoft : (trackMouse.containsMouse ? AppTheme.surfaceHover : "transparent")
                        border.color: index === root.currentIndex ? AppTheme.accentBorder : "transparent"
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 9
                            anchors.rightMargin: 9
                            spacing: 8
                            Text {
                                text: String(index + 1)
                                color: AppTheme.textFaint
                                font.pixelSize: 9
                                Layout.preferredWidth: 22
                            }
                            Text {
                                Layout.fillWidth: true
                                text: String(modelData.name || "")
                                color: AppTheme.text
                                font.pixelSize: 10
                                elide: Text.ElideMiddle
                            }
                            CrispIcon {
                                visible: index === root.currentIndex
                                Layout.preferredWidth: 14
                                Layout.preferredHeight: 14
                                source: root.icon(root.playing ? "viewer-play.svg" : "viewer-pause.svg")
                            }
                        }
                        MouseArea {
                            id: trackMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                root.selectQueueIndex(index, true)
                            }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.drawerTab === 1
                    spacing: 7
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: root.lang.language === "tr"
                                  ? "Mevcut kuyruğu bir çalma listesi olarak kaydet"
                                  : "Save the current queue as a playlist"
                            color: AppTheme.textMuted
                            font.pixelSize: 9
                        }
                        GButton {
                            background: Rectangle {
                                radius: 9
                                color: parent.checked ? AppTheme.accentSoft : (parent.hovered ? AppTheme.surfaceHover : AppTheme.surface)
                                border.width: 1
                                border.color: parent.checked ? AppTheme.accentBorder : AppTheme.border
                            }
                            text: root.lang.language === "tr" ? "+ Çalma listesi" : "+ Playlist"
                            enabled: root.queue.length > 0
                            onClicked: playlistNamePopup.open()
                        }
                    }
                    ListView {
                        id: playlistView
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 4
                        model: root.playlists
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            width: playlistView.width
                            height: 38
                            radius: 7
                            color: playlistMouse.containsMouse ? AppTheme.surfaceHover : "transparent"
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 6
                                spacing: 8
                                Text {
                                    Layout.fillWidth: true
                                    text: String(modelData.name || "")
                                    color: AppTheme.text
                                    font.pixelSize: 10
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                                Text {
                                    text: String((modelData.items || []).length) + (root.lang.language === "tr" ? " parça" : " tracks")
                                    color: AppTheme.textMuted
                                    font.pixelSize: 9
                                }
                                GToolButton {
                        background: Rectangle {
                            radius: 9
                            color: parent.checked ? AppTheme.accentSoft : (parent.hovered ? AppTheme.surfaceHover : "transparent")
                            border.width: parent.checked ? 1 : 0
                            border.color: parent.checked ? AppTheme.accentBorder : "transparent"
                            Behavior on color { ColorAnimation { duration: 110 } }
                        }
                                    implicitWidth: 30; implicitHeight: 28
                                    icon.source: root.icon("viewer-play.svg")
                                    onClicked: root.playPlaylist(index)
                                }
                                GToolButton {
                        background: Rectangle {
                            radius: 9
                            color: parent.checked ? AppTheme.accentSoft : (parent.hovered ? AppTheme.surfaceHover : "transparent")
                            border.width: parent.checked ? 1 : 0
                            border.color: parent.checked ? AppTheme.accentBorder : "transparent"
                            Behavior on color { ColorAnimation { duration: 110 } }
                        }
                                    implicitWidth: 30; implicitHeight: 28
                                    icon.source: root.icon("trash.svg")
                                    onClicked: root.deletePlaylist(index)
                                }
                            }
                            MouseArea {
                                id: playlistMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.NoButton
                            }
                        }
                        Text {
                            anchors.centerIn: parent
                            visible: root.playlists.length === 0
                            text: root.lang.language === "tr" ? "Henüz çalma listesi yok" : "No playlists yet"
                            color: AppTheme.textMuted
                            font.pixelSize: 10
                        }
                    }
                }
            }
        }
    }

    GModalPopup {
        id: playlistNamePopup
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 390
        height: 190
        padding: 0
        onOpened: {
            playlistNameInput.text = ""
            playlistNameInput.forceActiveFocus()
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 12
            Text {
                Layout.fillWidth: true
                text: root.lang.language === "tr" ? "Çalma listesi oluştur" : "Create playlist"
                color: AppTheme.text
                font.pixelSize: 17
                font.weight: Font.Bold
            }
            GTextField {
                id: playlistNameInput
                Layout.fillWidth: true
                placeholderText: root.lang.language === "tr" ? "Çalma listesi adı" : "Playlist name"
                onAccepted: {
                    if (text.trim().length) {
                        root.saveCurrentQueueAsPlaylist(text)
                        playlistNamePopup.close()
                    }
                }
            }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                GModalButton {
                    text: root.lang.language === "tr" ? "İptal" : "Cancel"
                    onClicked: playlistNamePopup.close()
                }
                GModalButton {
                    primary: true
                    text: root.lang.language === "tr" ? "Kaydet" : "Save"
                    enabled: playlistNameInput.text.trim().length > 0
                    onClicked: {
                        root.saveCurrentQueueAsPlaylist(playlistNameInput.text)
                        playlistNamePopup.close()
                    }
                }
            }
        }
    }
}
