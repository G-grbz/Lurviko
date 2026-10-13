#!/usr/bin/env python3
"""Source-level regression checks for the expanded music presentation.

These checks are not a substitute for loading the QML in a Qt/KF6 runtime.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
view = (root / 'qml/components/MusicExpandedView.qml').read_text()
player = (root / 'qml/components/MusicPlayer.qml').read_text()

# Reuse global player playlists, don't fork persistent playlist state.
assert 'property int sideTab: 0 // 0 lyrics, 1 queue, 2 playlists' in view
assert 'id: playlistsTabButton' in view
assert 'checked: root.sideTab === 2' in view
assert 'onClicked: root.sideTab = 2' in view
assert 'model: root.sideTab === 2 ? player.playlists : []' in view
assert 'MusicPlaylistDialog {' in view
assert 'playlistNamePopup.openForItems(player.queue' in view
assert 'onClicked: player.playPlaylist(index)' in view
assert 'player.deletePlaylist(root.pendingPlaylistRemoval)' in view
assert 'id: confirmPlaylistRemoval' in view
for api in ('saveCurrentQueueAsPlaylist', 'playPlaylist', 'deletePlaylist'):
    assert f'function {api}(' in player

# Tab contrast remains correct for light and dark themes.
for name in ('lyricsTabButton', 'queueTabButton', 'playlistsTabButton'):
    assert f'color: {name}.checked ? AppTheme.accent : AppTheme.text' in view

# Compact footer: 94 px, capped volume width; no second audio output.
assert 'Layout.preferredHeight: 94' in view
assert 'Layout.maximumWidth: 104' in view
assert 'Layout.maximumWidth: 146' in view
assert 'onMoved: player.setVolume(value)' in view
assert 'id: presentationInputShield' in view
assert 'AudioOutput {' not in view and 'MediaPlayer {' not in view

# Escape should close the modal (instead of the entire expanded presentation).
assert 'enabled: root.visible && !playlistNamePopup.visible && !confirmPlaylistRemoval.visible' in view
print('Expanded playlist and compact footer checks passed.')
