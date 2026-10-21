import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import QtQuick.Window
import Lurviko.App

// Two presentations of the SAME global MusicPlayer: embedded and detachable.
// No MediaPlayer or AudioOutput is created here; hiding this view never stops audio.
Rectangle {
    id: root
    required property var player
    required property var lang
    property bool detached: false
    property int sideTab: 0 // 0 lyrics, 1 queue, 2 playlists
    property bool showRemainingTime: false
    property int pendingPlaylistRemoval: -1
    function requestPlaylistRemoval(index) {
        if (index < 0 || index >= player.playlists.length)
            return
        pendingPlaylistRemoval = index
        confirmPlaylistRemoval.open()
    }
    readonly property bool hasTrack: !!player && player.opened && !!player.currentItem
    readonly property bool trMode: !!lang && lang.language === "tr"
    readonly property string artistName: hasTrack ? String(player.trackMeta.artist || "").trim() : ""
    readonly property string trackTitle: hasTrack
        ? String(player.trackMeta.title || player.currentTitle || "").trim() : ""
    readonly property string albumName: hasTrack ? String(player.trackMeta.album || "").trim() : ""
    readonly property string artUrl: hasTrack ? player.currentArtwork : ""
    readonly property real progress: hasTrack && player.duration > 0
        ? Math.max(0, Math.min(1, player.position / player.duration)) : 0

    signal closeRequested()
    signal detachRequested()
    signal attachRequested()
    signal minimizeRequested()

    function localize(trText, enText) { return trMode ? trText : enText }
    function jumpTo(position, size) {
        if (hasTrack && player.duration > 0 && size > 0)
            player.seekTo(Math.max(0, Math.min(1, position / size)) * player.duration)
    }

    color: AppTheme.workspace
    radius: detached ? 0 : 14
    clip: true

    // Low-resolution album wash, not a full-size blur recalculated per frame.
    Item {
        // The fixed light/dark wash would obscure the native Qt/KWin backdrop.
        visible: !AppTheme.useQtTheme
        anchors.fill: parent
        clip: true
        MusicArtwork {
            id: albumWash
            anchors.fill: parent
            artworkSource: root.artUrl
            sourceSize.width: 128
            sourceSize.height: 128
            asynchronous: true
            fillMode: Image.PreserveAspectCrop
            visible: false
        }
        MultiEffect {
            anchors.fill: parent
            source: albumWash
            blurEnabled: true
            blur: 1.0
            blurMax: 32
            opacity: AppTheme.dark ? 0.19 : 0.11
            visible: albumWash.status === Image.Ready
        }
        Rectangle {
            anchors.fill: parent
            color: AppTheme.dark ? "#0C1424" : "#F5F7FC"
            opacity: AppTheme.dark ? 0.75 : 0.85
        }
    }

    // A Rectangle is visual only; transparent gaps in the expanded layout
    // otherwise let clicks/hover/wheel reach the underlying Music library.
    // Keep this behind every real control: it absorbs ONLY unused whitespace.
    // Both embedded and detached presentations share this safeguard.
    MouseArea {
        id: presentationInputShield
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        hoverEnabled: true
        preventStealing: true
        onWheel: function(wheel) { wheel.accepted = true }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Titlebar / display mode switch
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            Layout.leftMargin: 24
            Layout.rightMargin: 20
            spacing: 12

            CrispIcon {
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
                source: AppTheme.icon("music-note.svg")
            }
            ColumnLayout {
                spacing: 1
                Layout.fillWidth: true
                Text {
                    text: root.localize("Şimdi Çalıyor", "Now Playing")
                    color: AppTheme.text
                    font.pixelSize: 17
                    font.weight: Font.Bold
                }
                Text {
                    text: root.localize("Lurviko Müzik", "Lurviko Music")
                    color: AppTheme.textMuted
                    font.pixelSize: 10
                }
            }
            GToolButton {
                objectName: "minimizeExpandedMusic"
                implicitWidth: 38; implicitHeight: 38
                icon.source: AppTheme.icon("music-minimize.svg")
                icon.width: 18; icon.height: 18
                background: Rectangle {
                    radius: 10
                    color: parent.hovered ? AppTheme.accentSoft : AppTheme.surfaceRaised
                    border.color: AppTheme.border
                }
                onClicked: root.minimizeRequested()
                Accessible.name: root.localize("Mini player'a küçült", "Minimize to mini player")
                GToolTip { text: parent.Accessible.name }
            }
            GToolButton {
                implicitWidth: 38; implicitHeight: 38
                icon.source: AppTheme.icon(root.detached ? "viewer-fullscreen.svg" : "clone.svg")
                icon.width: 18; icon.height: 18
                background: Rectangle {
                    radius: 10
                    color: parent.hovered ? AppTheme.accentSoft : AppTheme.surfaceRaised
                    border.color: AppTheme.border
                }
                onClicked: root.detached ? root.attachRequested() : root.detachRequested()
                Accessible.name: root.detached
                    ? root.localize("Müzik sayfasına taşı", "Attach to Music page")
                    : root.localize("Ayrı pencerede aç", "Open in separate window")
                GToolTip { text: parent.Accessible.name }
            }
            GToolButton {
                implicitWidth: 38; implicitHeight: 38
                icon.source: AppTheme.icon("close-ui.svg")
                icon.width: 17; icon.height: 17
                background: Rectangle {
                    radius: 10
                    color: parent.hovered ? AppTheme.surfaceHover : AppTheme.surfaceRaised
                    border.color: AppTheme.border
                }
                onClicked: root.closeRequested()
                Accessible.name: root.localize("Genişletilmiş görünümü kapat", "Close expanded view")
                GToolTip { text: parent.Accessible.name }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: AppTheme.border }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 185

            RowLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 16

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumWidth: 240
                    spacing: 16

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.max(128, Math.min(192, root.height * 0.26))
                        spacing: 20
                        Rectangle {
                            Layout.preferredWidth: Math.max(100, Math.min(180, parent.height - 10))
                            Layout.preferredHeight: Layout.preferredWidth
                            radius: width / 2
                            color: AppTheme.surfaceRaised
                            border.color: AppTheme.accentBorder
                            border.width: 2
                            clip: true
                            MusicArtwork {
                                id: cover
                                anchors.fill: parent
                                artworkSource: root.artUrl
                                sourceSize: Qt.size(AppTheme.previewResolution(width, Screen.devicePixelRatio), AppTheme.previewResolution(height, Screen.devicePixelRatio))
                                asynchronous: true
                                fillMode: Image.PreserveAspectCrop
                                visible: status === Image.Ready
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Text {
                                text: root.localize("SANATÇI", "ARTIST")
                                color: AppTheme.accent
                                font.pixelSize: 10
                                font.weight: Font.Bold
                                font.letterSpacing: 2
                            }
                            MusicArtistLinks {
                                objectName: "expandedMusicArtistLinks"
                                Layout.fillWidth: true
                                artistText: root.artistName
                                fallbackText: root.localize("Bilinmeyen sanatçı", "Unknown artist")
                                onArtistRequested: function(artist) { playlistNamePopup.openForArtist(artist) }
                                color: AppTheme.text
                                font.pixelSize: root.width > 950 ? 30 : 23
                                font.weight: Font.Bold
                                elide: Text.ElideRight
                                maximumLineCount: 2
                                wrapMode: Text.Wrap
                            }
                            Text {
                                Layout.fillWidth: true
                                text: root.albumName || root.trackTitle
                                color: AppTheme.textMuted
                                font.pixelSize: 12
                                elide: Text.ElideRight
                            }
                            RowLayout {
                                spacing: 9
                                GButton {
                                    enabled: root.hasTrack
                                    implicitHeight: 38
                                    highlighted: true
                                    text: player.playing ? root.localize("Duraklat", "Pause") : root.localize("Oynat", "Play")
                                    onClicked: player.toggle()
                                }
                                GToolButton {
                                    implicitWidth: 38; implicitHeight: 38
                                    checked: player.shuffleMode
                                    icon.source: AppTheme.icon("music-shuffle.svg")
                                    icon.color: checked ? AppTheme.accent : AppTheme.text
                                    onClicked: player.toggleShuffle()
                                    Accessible.name: root.localize("Karıştır", "Shuffle")
                                    GToolTip { text: parent.Accessible.name }
                                }
                                GToolButton {
                                    implicitWidth: 38; implicitHeight: 38
                                    checked: player.currentFavorite
                                    icon.source: AppTheme.icon(checked ? "music-heart-filled.svg" : "music-heart.svg")
                                    icon.color: checked ? AppTheme.accent : AppTheme.text
                                    onClicked: player.toggleCurrentFavorite()
                                    Accessible.name: root.localize("Favori", "Favorite")
                                    GToolTip { text: parent.Accessible.name }
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: root.localize("Çalma Kuyruğu", "Up Next")
                            font.pixelSize: 20
                            font.weight: Font.Bold
                            color: AppTheme.text
                        }
                        Text {
                            text: player.queue.length + " " + root.localize("parça", "tracks")
                            font.pixelSize: 11
                            color: AppTheme.textMuted
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 90
                        radius: 12
                        color: AppTheme.surface
                        border.color: AppTheme.border
                        clip: true

                        ListView {
                            id: trackList
                            objectName: "musicExpandedTrackList"
                            GPopupWheelScroll {
                                parent: trackList
                                anchors.fill: parent
                                view: trackList
                                scrollbar: trackList.ScrollBar.vertical
                                enabled: root.visible
                            }
                            anchors.fill: parent
                            anchors.margins: 4
                            clip: true
                            spacing: 1
                            reuseItems: true
                            model: player.queue || []
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                            delegate: Rectangle {
                                required property var modelData
                                required property int index
                                readonly property bool selected: index === player.currentIndex
                                width: trackList.width
                                height: 55
                                radius: 8
                                color: selected ? AppTheme.accentSoft : (entryMouse.containsMouse ? AppTheme.surfaceHover : "transparent")
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 8
                                    spacing: 11
                                    Text {
                                        Layout.preferredWidth: 22
                                        horizontalAlignment: Text.AlignHCenter
                                        text: selected && player.playing ? "♫" : String(index + 1)
                                        color: selected ? AppTheme.accent : AppTheme.textMuted
                                        font.pixelSize: 12
                                    }
                                    Rectangle {
                                        Layout.preferredWidth: 42
                                        Layout.preferredHeight: 42
                                        radius: 6
                                        color: AppTheme.surfaceRaised
                                        clip: true
                                        MusicArtwork {
                                            anchors.fill: parent
                                            artworkSource: String(modelData.thumbnailSource || "")
                                            sourceSize: Qt.size(AppTheme.previewResolution(width, Screen.devicePixelRatio), AppTheme.previewResolution(height, Screen.devicePixelRatio))
                                            fillMode: Image.PreserveAspectCrop
                                            asynchronous: true
                                        }
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: String(modelData.name || "")
                                        color: selected ? AppTheme.accent : AppTheme.text
                                        font.pixelSize: 12
                                        font.weight: selected ? Font.DemiBold : Font.Normal
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        text: selected ? player.formatTime(player.duration) : ""
                                        color: AppTheme.textMuted
                                        font.pixelSize: 10
                                        Layout.preferredWidth: 45
                                        horizontalAlignment: Text.AlignRight
                                    }
                                }
                                MouseArea {
                                    id: entryMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: player.selectQueueIndex(index, true)
                                }
                            }
                            Text {
                                anchors.centerIn: parent
                                visible: trackList.count === 0
                                text: root.localize("Kuyruk boş", "Queue is empty")
                                color: AppTheme.textMuted
                                font.pixelSize: 12
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.preferredWidth: Math.max(238, Math.min(385, root.width * 0.31))
                    Layout.fillHeight: true
                    radius: 12
                    color: AppTheme.surface
                    border.color: AppTheme.border
                    clip: true

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 0
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 52
                            Layout.leftMargin: 12
                            Layout.rightMargin: 12
                            spacing: 6
                            // Fill the panel with three adaptive tabs. A playlist
                            // tab must not push a button outside narrow windows.
                            GButton {
                                id: lyricsTabButton
                                Layout.fillWidth: true
                                Layout.preferredWidth: 105
                                Layout.minimumWidth: 45
                                Layout.preferredHeight: 36
                                text: root.localize("Şarkı Sözleri", "Lyrics")
                                checked: root.sideTab === 0
                                onClicked: root.sideTab = 0
                                background: Rectangle {
                                    radius: 9
                                    color: lyricsTabButton.checked ? AppTheme.accentSoft
                                            : (lyricsTabButton.hovered ? AppTheme.surfaceHover : "transparent")
                                    border.width: 1
                                    border.color: lyricsTabButton.checked ? AppTheme.accentBorder : AppTheme.border
                                }
                                contentItem: Text {
                                    text: lyricsTabButton.text
                                    color: lyricsTabButton.checked ? AppTheme.accent : AppTheme.text
                                    font.pixelSize: 11
                                    font.weight: lyricsTabButton.checked ? Font.DemiBold : Font.Medium
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideRight
                                }
                                Accessible.name: lyricsTabButton.text + (player.lyricsSource ? " · " + String(player.lyricsSource).toUpperCase() : "")
                                GToolTip { text: parent.Accessible.name }
                            }
                            GButton {
                                id: queueTabButton
                                Layout.fillWidth: true
                                Layout.preferredWidth: 70
                                Layout.minimumWidth: 45
                                Layout.preferredHeight: 36
                                text: root.localize("Kuyruk", "Queue")
                                checked: root.sideTab === 1
                                onClicked: root.sideTab = 1
                                background: Rectangle {
                                    radius: 9
                                    color: queueTabButton.checked ? AppTheme.accentSoft
                                            : (queueTabButton.hovered ? AppTheme.surfaceHover : "transparent")
                                    border.width: 1
                                    border.color: queueTabButton.checked ? AppTheme.accentBorder : AppTheme.border
                                }
                                contentItem: Text {
                                    text: queueTabButton.text
                                    color: queueTabButton.checked ? AppTheme.accent : AppTheme.text
                                    font.pixelSize: 11
                                    font.weight: queueTabButton.checked ? Font.DemiBold : Font.Medium
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideRight
                                }
                                Accessible.name: queueTabButton.text
                                GToolTip { text: parent.Accessible.name }
                            }
                            GButton {
                                id: playlistsTabButton
                                Layout.fillWidth: true
                                Layout.preferredWidth: 122
                                Layout.minimumWidth: 58
                                Layout.preferredHeight: 36
                                text: root.localize("Çalma Listeleri", "Playlists")
                                checked: root.sideTab === 2
                                onClicked: root.sideTab = 2
                                background: Rectangle {
                                    radius: 9
                                    color: playlistsTabButton.checked ? AppTheme.accentSoft
                                            : (playlistsTabButton.hovered ? AppTheme.surfaceHover : "transparent")
                                    border.width: 1
                                    border.color: playlistsTabButton.checked ? AppTheme.accentBorder : AppTheme.border
                                }
                                contentItem: Text {
                                    text: playlistsTabButton.text
                                    color: playlistsTabButton.checked ? AppTheme.accent : AppTheme.text
                                    font.pixelSize: 11
                                    font.weight: playlistsTabButton.checked ? Font.DemiBold : Font.Medium
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideRight
                                }
                                Accessible.name: playlistsTabButton.text
                                GToolTip { text: parent.Accessible.name }
                            }
                        }
                        Rectangle { Layout.fillWidth: true; height: 1; color: AppTheme.border }

                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            ListView {
                                id: syncedLyrics
                                objectName: "musicExpandedSyncedLyrics"
                                GPopupWheelScroll {
                                    parent: syncedLyrics
                                    anchors.fill: parent
                                    view: syncedLyrics
                                    scrollbar: syncedLyrics.ScrollBar.vertical
                                    enabled: root.visible && root.sideTab === 0 && player.lyricsSynced
                                }
                                anchors.fill: parent
                                anchors.margins: 16
                                visible: root.sideTab === 0 && player.lyricsSynced
                                clip: true
                                spacing: 18
                                reuseItems: true
                                model: root.sideTab === 0 && player.lyricsSynced ? player.lyricsLines : []
                                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                                delegate: Item {
                                    required property var modelData
                                    required property int index
                                    width: syncedLyrics.width - 4
                                    height: Math.max(42, lyricLine.implicitHeight + 14)
                                    Text {
                                        id: lyricLine
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: String(modelData.text || "")
                                        textFormat: Text.PlainText
                                        color: index === player.activeLyricIndex ? AppTheme.text : AppTheme.textMuted
                                        opacity: index === player.activeLyricIndex ? 1 : 0.50
                                        font.pixelSize: index === player.activeLyricIndex ? 21 : 17
                                        font.weight: index === player.activeLyricIndex ? Font.Bold : Font.Medium
                                        wrapMode: Text.Wrap
                                        Behavior on opacity { NumberAnimation { duration: 160 } }
                                        Behavior on color { ColorAnimation { duration: 160 } }
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: player.seekTo(Number(modelData.time || 0))
                                    }
                                }
                            }
                            ScrollView {
                                id: plainLyricsScroll
                                objectName: "musicExpandedPlainLyricsScroll"
                                GPopupWheelScroll {
                                    parent: plainLyricsScroll
                                    anchors.fill: parent
                                    view: plainLyricsScroll.contentItem
                                    scrollbar: plainLyricsScroll.ScrollBar.vertical
                                    enabled: root.visible && root.sideTab === 0 && !player.lyricsSynced
                                }
                                anchors.fill: parent
                                anchors.margins: 15
                                visible: root.sideTab === 0 && !player.lyricsSynced
                                clip: true
                                TextArea {
                                    width: plainLyricsScroll.availableWidth
                                    readOnly: true
                                    selectByMouse: true
                                    wrapMode: TextEdit.Wrap
                                    textFormat: TextEdit.PlainText
                                    text: root.sideTab !== 0 || player.lyricsSynced ? ""
                                        : (player.lyricsText.length ? player.lyricsText
                                           : root.localize("Bu parçada şarkı sözü bulunamadı.\n.lrc, .lyrics veya gömülü sözler kullanılabilir.",
                                                           "No lyrics available for this track.\nAdd an .lrc, .lyrics or embedded lyrics tag."))
                                    color: player.lyricsText.length ? AppTheme.text : AppTheme.textMuted
                                    font.pixelSize: 16
                                    background: Item {}
                                }
                            }
                            ListView {
                                id: secondaryQueue
                                objectName: "musicExpandedSecondaryQueue"
                                GPopupWheelScroll {
                                    parent: secondaryQueue
                                    anchors.fill: parent
                                    view: secondaryQueue
                                    scrollbar: secondaryQueue.ScrollBar.vertical
                                    enabled: root.visible && root.sideTab === 1
                                }
                                anchors.fill: parent
                                anchors.margins: 14
                                visible: root.sideTab === 1
                                clip: true
                                spacing: 4
                                model: root.sideTab === 1 ? player.queue : []
                                delegate: Rectangle {
                                    required property var modelData
                                    required property int index
                                    width: secondaryQueue.width
                                    height: 50
                                    radius: 8
                                    color: index === player.currentIndex ? AppTheme.accentSoft : (sideMouse.containsMouse ? AppTheme.surfaceHover : "transparent")
                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.margins: 10
                                        Text {
                                            text: String(index + 1)
                                            color: AppTheme.textMuted
                                            font.pixelSize: 10
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            text: String(modelData.name || "")
                                            color: index === player.currentIndex ? AppTheme.accent : AppTheme.text
                                            font.pixelSize: 12
                                            elide: Text.ElideRight
                                        }
                                    }
                                    MouseArea {
                                        id: sideMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: player.selectQueueIndex(index, true)
                                    }
                                }
                            }
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                spacing: 10
                                visible: root.sideTab === 2
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Text {
                                        Layout.fillWidth: true
                                        text: (player.playlists || []).length + " " + root.localize("çalma listesi", "playlists")
                                        color: AppTheme.textMuted
                                        font.pixelSize: 11
                                    }
                                    GButton {
                                        id: savePlaylistButton
                                        objectName: "expandedAddPlaylist"
                                        text: player.playlistActionText
                                        Layout.preferredHeight: 33
                                        enabled: !!player && player.queue.length > 0
                                        onClicked: playlistNamePopup.openForItems(player.queue, player.trackMeta.album || "", false)
                                        Accessible.name: root.localize("Mevcut kuyruğu çalma listesi olarak kaydet", "Save current queue as a playlist")
                                        GToolTip { text: parent.Accessible.name }
                                    }
                                }
                                ListView {
                                    id: savedPlaylists
                                    objectName: "musicExpandedSavedPlaylists"
                                    GPopupWheelScroll {
                                        parent: savedPlaylists
                                        anchors.fill: parent
                                        view: savedPlaylists
                                        scrollbar: savedPlaylists.ScrollBar.vertical
                                        enabled: root.visible && root.sideTab === 2
                                    }
                                    visible: count > 0
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true
                                    spacing: 6
                                    reuseItems: true
                                    model: root.sideTab === 2 ? player.playlists : []
                                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                                    delegate: Rectangle {
                                        required property var modelData
                                        required property int index
                                        width: savedPlaylists.width
                                        height: 61
                                        radius: 10
                                        color: playlistHover.hovered ? AppTheme.surfaceHover : AppTheme.surfaceRaised
                                        border.color: AppTheme.border
                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.leftMargin: 10
                                            anchors.rightMargin: 6
                                            spacing: 8
                                            CrispIcon {
                                                Layout.preferredWidth: 25
                                                Layout.preferredHeight: 25
                                                source: AppTheme.icon("music-playlist.svg")
                                            }
                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: 3
                                                Text {
                                                    objectName: "expandedPlaylistName"
                                                    Layout.fillWidth: true
                                                    text: String(modelData.name || "")
                                                    textFormat: Text.PlainText
                                                    color: AppTheme.text
                                                    font.pixelSize: 12
                                                    font.weight: Font.DemiBold
                                                    elide: Text.ElideRight
                                                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                                                    TapHandler {
                                                        acceptedButtons: Qt.LeftButton
                                                        enabled: !!modelData.items && modelData.items.length > 0
                                                        onTapped: player.playPlaylist(index)
                                                    }
                                                }
                                                Text {
                                                    text: String((modelData.items || []).length) + " " + root.localize("parça", "tracks")
                                                    color: AppTheme.textMuted
                                                    font.pixelSize: 10
                                                }
                                            }
                                            GToolButton {
                                                implicitWidth: 30; implicitHeight: 30
                                                icon.source: AppTheme.icon("viewer-play.svg")
                                                enabled: !!modelData.items && modelData.items.length > 0
                                                onClicked: player.playPlaylist(index)
                                                Accessible.name: root.localize("Listeyi oynat", "Play playlist")
                                                GToolTip { text: parent.Accessible.name }
                                            }
                                            GToolButton {
                                                implicitWidth: 30; implicitHeight: 30
                                                icon.source: AppTheme.icon("trash.svg")
                                                onClicked: root.requestPlaylistRemoval(index)
                                                Accessible.name: root.localize("Listeyi sil", "Delete playlist")
                                                GToolTip { text: parent.Accessible.name }
                                            }
                                        }
                                        HoverHandler { id: playlistHover }
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    visible: savedPlaylists.count === 0
                                    text: root.localize("Henüz kayıtlı çalma listesi yok.\nMevcut kuyruğu kaydetmek için + Çalma listesi'ne bas.",
                                                        "No saved playlists yet.\nUse + Playlist to save the current queue.")
                                    color: AppTheme.textMuted
                                    font.pixelSize: 12
                                    wrapMode: Text.Wrap
                                    verticalAlignment: Text.AlignVCenter
                                    horizontalAlignment: Text.AlignHCenter
                                }
                            }
                        }
                    }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: AppTheme.border }

        // Compact, balanced controls: metadata | transport + seek | capped volume.
        // Free space is layout-only (the input shield absorbs unused clicks).
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 94
            color: AppTheme.surface
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 20
                spacing: 10
                Rectangle {
                    Layout.preferredWidth: 54
                    Layout.preferredHeight: 54
                    radius: 8
                    color: AppTheme.surfaceRaised
                    clip: true
                    MusicArtwork {
                        anchors.fill: parent
                        artworkSource: root.artUrl
                        sourceSize: Qt.size(AppTheme.previewResolution(width, Screen.devicePixelRatio), AppTheme.previewResolution(height, Screen.devicePixelRatio))
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                    }
                }
                ColumnLayout {
                    Layout.preferredWidth: Math.max(112, Math.min(235, root.width * 0.19))
                    Layout.maximumWidth: 235
                    spacing: 4
                    Text {
                        Layout.fillWidth: true
                        text: root.trackTitle || root.localize("Parça seçilmedi", "No track selected")
                        color: AppTheme.text
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    MusicArtistLinks {
                        objectName: "expandedFooterArtistLinks"
                        Layout.fillWidth: true
                        artistText: root.artistName
                        fallbackText: root.albumName
                        onArtistRequested: function(artist) { playlistNamePopup.openForArtist(artist) }
                        color: AppTheme.textMuted
                        font.pixelSize: 10
                        elide: Text.ElideRight
                    }
                }
                Item { Layout.fillWidth: true; Layout.minimumWidth: 4 }
                ColumnLayout {
                    Layout.preferredWidth: Math.max(220, Math.min(480, root.width * 0.37))
                    Layout.minimumWidth: 220
                    Layout.maximumWidth: 480
                    Layout.alignment: Qt.AlignVCenter
                    spacing: 1
                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 8
                        GToolButton {
                            implicitWidth: 28; implicitHeight: 28
                            checked: player.shuffleMode
                            icon.source: AppTheme.icon("music-shuffle.svg")
                            icon.color: checked ? AppTheme.accent : AppTheme.text
                            onClicked: player.toggleShuffle()
                            Accessible.name: root.localize("Karıştır", "Shuffle")
                            GToolTip { text: parent.Accessible.name }
                        }
                        GToolButton {
                            implicitWidth: 30; implicitHeight: 30
                            icon.source: AppTheme.icon("viewer-prev.svg")
                            onClicked: player.previous()
                            Accessible.name: root.localize("Önceki parça", "Previous track")
                            GToolTip { text: parent.Accessible.name }
                        }
                        GToolButton {
                            implicitWidth: 36; implicitHeight: 34
                            icon.source: AppTheme.icon(player.playing ? "viewer-pause.svg" : "viewer-play.svg")
                            onClicked: player.toggle()
                            Accessible.name: player.playing ? root.localize("Duraklat", "Pause") : root.localize("Oynat", "Play")
                            GToolTip { text: parent.Accessible.name }
                        }
                        GToolButton {
                            implicitWidth: 30; implicitHeight: 30
                            icon.source: AppTheme.icon("viewer-next.svg")
                            onClicked: player.next()
                            Accessible.name: root.localize("Sonraki parça", "Next track")
                            GToolTip { text: parent.Accessible.name }
                        }
                        GToolButton {
                            implicitWidth: 28; implicitHeight: 28
                            checked: player.repeatMode !== 0
                            icon.source: AppTheme.icon(player.repeatMode === 2 ? "music-repeat-one.svg" : "music-repeat.svg")
                            icon.color: checked ? AppTheme.accent : AppTheme.text
                            onClicked: player.cycleRepeatMode()
                            Accessible.name: root.localize("Tekrar", "Repeat")
                            GToolTip { text: parent.Accessible.name }
                        }
                        GToolButton {
                            objectName: "expandedRefreshQueue"
                            implicitWidth: 28; implicitHeight: 28
                            icon.source: AppTheme.icon("music-refresh.svg")
                            enabled: root.hasTrack
                            onClicked: player.refreshQueue()
                            Accessible.name: root.localize("Yeni kuyruk oluştur", "Generate new queue")
                            GToolTip { text: parent.Accessible.name }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 7
                        Text {
                            Layout.preferredWidth: 34
                            text: player.formatTime(player.position)
                            color: AppTheme.textMuted
                            font.pixelSize: 10
                            horizontalAlignment: Text.AlignRight
                        }
                        Item {
                            id: seekTrack
                            Layout.fillWidth: true
                            Layout.preferredHeight: 22
                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width
                                height: 4
                                radius: 2
                                color: AppTheme.surfaceHover
                                Rectangle {
                                    width: parent.width * root.progress
                                    height: parent.height
                                    radius: 2
                                    color: AppTheme.accent
                                }
                            }
                            Rectangle {
                                x: Math.max(0, Math.min(parent.width - width, parent.width * root.progress - width / 2))
                                anchors.verticalCenter: parent.verticalCenter
                                width: 11; height: 11
                                radius: 6
                                color: AppTheme.text
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onPressed: function(mouse) { root.jumpTo(mouse.x, width) }
                                onPositionChanged: function(mouse) { if (pressed) root.jumpTo(mouse.x, width) }
                            }
                        }
                        Text {
                            Layout.preferredWidth: 34
                            text: root.showRemainingTime
                                ? "-" + player.formatTime(Math.max(0, player.duration - player.position))
                                : player.formatTime(player.duration)
                            color: AppTheme.textMuted
                            font.pixelSize: 10
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.showRemainingTime = !root.showRemainingTime
                            }
                        }
                    }

                }
                Item { Layout.fillWidth: true; Layout.minimumWidth: 4 }
                Rectangle {
                    Layout.preferredHeight: 28
                    Layout.preferredWidth: 1
                    visible: root.width >= 850
                    color: AppTheme.border
                }
                RowLayout {
                    Layout.preferredWidth: root.width < 850 ? 96 : 146
                    Layout.maximumWidth: 146
                    spacing: 4
                    GToolButton {
                        implicitWidth: 30; implicitHeight: 30
                        icon.source: AppTheme.icon(player.muted ? "viewer-volume-muted.svg" : "viewer-volume.svg")
                        onClicked: player.toggleMute()
                        Accessible.name: root.localize("Sesi aç/kapat", "Mute/unmute")
                        GToolTip { text: parent.Accessible.name }
                    }
                    GSlider {
                        Layout.fillWidth: true
                        Layout.maximumWidth: 104
                        from: 0; to: 1
                        value: player.volume
                        onMoved: player.setVolume(value)
                        Accessible.name: Math.round(value * 100) + "%"
                        GToolTip { text: parent.Accessible.name }
                    }
                }
            }
        }
    }

    // Own modal instances are hosted in whichever Window displays this view;
    // embedded/detached actions both update the SAME global MusicPlayer playlists.
    MusicPlaylistDialog {
        id: playlistNamePopup
        objectName: "expandedMusicPlaylistDialog"
        player: root.player
        lang: root.lang
    }
    GModalPopup {
        id: confirmPlaylistRemoval
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(390, root.width - 30)
        height: 165
        padding: 0
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 14
            Text {
                Layout.fillWidth: true
                text: root.localize("Çalma listesini silmek istiyor musun?", "Delete this playlist?")
                color: AppTheme.text
                font.pixelSize: 15
                font.weight: Font.Bold
                wrapMode: Text.Wrap
            }
            Text {
                Layout.fillWidth: true
                text: root.localize("Yalnızca liste silinir; müzik dosyaların korunur.", "Only the playlist is removed. Your music files stay untouched.")
                color: AppTheme.textMuted
                font.pixelSize: 11
                wrapMode: Text.Wrap
            }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                GModalButton {
                    text: root.localize("Vazgeç", "Cancel")
                    onClicked: confirmPlaylistRemoval.close()
                }
                GModalButton {
                    danger: true
                    text: root.localize("Sil", "Delete")
                    onClicked: {
                        if (root.pendingPlaylistRemoval >= 0)
                            player.deletePlaylist(root.pendingPlaylistRemoval)
                        root.pendingPlaylistRemoval = -1
                        confirmPlaylistRemoval.close()
                    }
                }
            }
        }
    }

    Shortcut {
        sequence: "Escape"
        context: Qt.WindowShortcut
        enabled: root.visible && !playlistNamePopup.visible && !confirmPlaylistRemoval.visible
        onActivated: root.closeRequested()
    }

    Connections {
        target: player
        function onActiveLyricIndexChanged() {
            if (root.sideTab === 0 && player.lyricsSynced && player.activeLyricIndex >= 0 && root.visible)
                syncedLyrics.positionViewAtIndex(player.activeLyricIndex, ListView.Center)
        }
        function onLyricsLinesChanged() {
            if (root.sideTab === 0 && player.activeLyricIndex >= 0 && root.visible)
                Qt.callLater(function() {
                    if (root.visible && syncedLyrics.count > player.activeLyricIndex)
                        syncedLyrics.positionViewAtIndex(player.activeLyricIndex, ListView.Center)
                })
        }
    }
    onVisibleChanged: {
        if (visible && sideTab === 0 && player.lyricsSynced && player.activeLyricIndex >= 0)
            Qt.callLater(function() {
                if (root.visible && syncedLyrics.count > player.activeLyricIndex)
                    syncedLyrics.positionViewAtIndex(player.activeLyricIndex, ListView.Center)
            })
    }
}
