import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lurviko.App

GModalPopup {
    id: control
    required property var player
    required property var lang
    property var items: []
    property var selectedUrls: ({})
    property bool selectionMode: false
    property string artistName: ""
    property string targetName: ""
    property string errorText: ""
    function label(key) {
        const language = lang.language
        return lang.t(key)
    }
    readonly property int selectedCount: selectionMode ? Object.keys(selectedUrls).length : items.length
    readonly property var destinations: [{text: control.label("music_new_playlist"), value: ""}].concat(
        player.playlists.map(function(list) { return {text: list.name, value: list.name} }))
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(selectionMode ? 640 : 460, parent ? parent.width - 32 : 640)
    height: Math.min(selectionMode ? 620 : 400, parent ? parent.height - 32 : 620)
    padding: 0
    function openForItems(source, seed, selectable) {
        items = player.uniquePlaylistItems(source)
        selectedUrls = ({})
        selectionMode = !!selectable
        targetName = ""
        artistName = ""
        errorText = ""
        nameInput.text = player.suggestPlaylistName(seed)
        open()
    }
    function openForArtist(name) {
        openForItems(player.tracksForArtist(name), name, true)
        artistName = name
    }
    function toggleItem(url) {
        const next = Object.assign({}, selectedUrls)
        if (next[url]) delete next[url]
        else next[url] = true
        selectedUrls = next
    }
    function toggleAll() {
        const next = ({})
        if (selectedCount !== items.length)
            for (const item of items) next[item.itemUrl] = true
        selectedUrls = next
    }
    function saveSelection() {
        const chosen = selectionMode ? items.filter(function(item) { return !!selectedUrls[item.itemUrl] }) : items
        if (!chosen.length) return
        if (player.savePlaylistItems(chosen, nameInput.text, targetName)) close()
        else errorText = control.label("music_playlist_unavailable")
    }
    onOpened: {
        tracks.positionViewAtBeginning()
        if (!selectionMode) { nameInput.forceActiveFocus(); nameInput.selectAll() }
    }
    onClosed: { items = []; selectedUrls = ({}); artistName = ""; errorText = "" }
    Connections {
        target: player.libraryManager
        function onTracksChanged() {
            if (!control.visible || !control.artistName.length) return
            control.items = player.uniquePlaylistItems(player.tracksForArtist(control.artistName, false))
            const next = ({})
            for (const item of control.items)
                if (control.selectedUrls[item.itemUrl]) next[item.itemUrl] = true
            control.selectedUrls = next
        }
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12
        GText {
            Layout.fillWidth: true
            text: control.artistName || control.label("music_save_playlist")
            baseColor: AppTheme.text
            basePixelSize: 18
            baseFontWeight: Font.DemiBold
            elide: Text.ElideRight
        }
        RowLayout {
            Layout.fillWidth: true
            GText {
                Layout.fillWidth: true
                text: control.selectedCount + " / " + control.items.length + " · " + control.label("music_selected_tracks")
                baseColor: AppTheme.textMuted
                basePixelSize: 11
            }
            GButton {
                objectName: "playlistToggleAll"
                visible: control.selectionMode
                enabled: control.items.length > 0
                text: control.label(control.selectedCount === control.items.length ? "clear_selection" : "select_all")
                onClicked: control.toggleAll()
            }
        }
        GText {
            Layout.fillWidth: true
            visible: control.artistName.length > 0 && player.libraryManager.loading
            text: control.label("music_loading_artist")
            baseColor: AppTheme.textMuted
            basePixelSize: 10
        }
        ListView {
            id: tracks
            objectName: "playlistArtistTracks"
            visible: control.selectionMode
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            reuseItems: true
            spacing: 4
            model: control.items
            ScrollBar.vertical: ScrollBar { id: tracksBar; policy: ScrollBar.AsNeeded }
            GPopupWheelScroll {
                parent: tracks
                anchors.fill: parent
                view: tracks
                scrollbar: tracksBar
                enabled: control.visible && control.selectionMode
            }
            delegate: RowLayout {
                id: trackRow
                required property var modelData
                width: tracks.width - 14
                height: 44
                spacing: 4
                GCheckBox {
                    objectName: "artistTrackSelection"
                    Layout.preferredWidth: 32
                    Layout.fillHeight: true
                    checked: !!control.selectedUrls[trackRow.modelData.itemUrl]
                    Accessible.name: control.label("music_select_track") + " " + String(trackRow.modelData.title || trackRow.modelData.name || "")
                    onClicked: control.toggleItem(trackRow.modelData.itemUrl)
                }
                GButton {
                    id: playTrack
                    objectName: "artistTrackPlay"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    quiet: true
                    text: String(trackRow.modelData.title || trackRow.modelData.name || "")
                    checked: player.currentUrl === trackRow.modelData.itemUrl
                    onClicked: player.playTemporaryQueueItem(trackRow.modelData)
                    contentItem: RowLayout {
                        spacing: 10
                        GText {
                            Layout.fillWidth: true
                            text: playTrack.text
                            textFormat: Text.PlainText
                            baseColor: playTrack.checked ? AppTheme.accent : AppTheme.text
                            font: playTrack.font
                            elide: Text.ElideRight
                        }
                        Image {
                            Layout.preferredWidth: 16
                            Layout.preferredHeight: 16
                            source: AppTheme.icon("viewer-play.svg")
                            sourceSize: Qt.size(16, 16)
                        }
                    }
                }
            }
            GText {
                anchors.centerIn: parent
                width: parent.width - 24
                visible: tracks.count === 0
                text: player.libraryManager.loading ? control.label("music_loading_artist") : control.label("music_no_artist_tracks")
                baseColor: AppTheme.textMuted
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
            }
        }
        GText { text: control.label("music_playlist_destination"); baseColor: AppTheme.textMuted; basePixelSize: 11 }
        GComboBox {
            id: destination
            objectName: "playlistDestination"
            Layout.fillWidth: true
            model: control.destinations
            textRole: "text"
            valueRole: "value"
            currentIndex: Math.max(0, control.destinations.findIndex(function(item) { return item.value === control.targetName }))
            onActivated: control.targetName = String(currentValue || "")
        }
        GTextField {
            id: nameInput
            objectName: "playlistSuggestedName"
            Layout.fillWidth: true
            visible: !control.targetName.length
            placeholderText: control.label("music_playlist_name")
            onAccepted: control.saveSelection()
        }
        GText {
            Layout.fillWidth: true
            visible: control.errorText.length > 0
            text: control.errorText
            baseColor: AppTheme.danger
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            GModalButton { text: control.label("cancel"); onClicked: control.close() }
            GModalButton {
                objectName: "playlistSaveSelection"
                primary: true
                enabled: control.selectedCount > 0
                text: control.label(control.targetName.length ? "music_add_to_playlist" : "save")
                onClicked: control.saveSelection()
            }
        }
    }
}
