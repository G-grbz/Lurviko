#!/usr/bin/env python3
"""Lightweight regression guards for Now Playing overlay hit-testing and tabs.

These are source-level guards, not a replacement for running the QML on Qt.
"""
from pathlib import Path

src = (Path(__file__).resolve().parents[1] / 'qml/components/MusicExpandedView.qml').read_text()

# Qt Quick Rectangle / Item do not necessarily consume mouse input.  The shield
# must be *behind* the content so controls continue to receive their own events.
shield = src.index('id: presentationInputShield')
main_layout = src.index('    ColumnLayout {\n        anchors.fill: parent\n        spacing: 0')
assert shield < main_layout, 'Input shield must be behind controls'
shield_body = src[src.rfind('    MouseArea {', 0, shield):main_layout]
for fragment in ('anchors.fill: parent', 'acceptedButtons: Qt.AllButtons',
                 'hoverEnabled: true', 'onWheel:', 'wheel.accepted = true'):
    assert fragment in shield_body, f'Missing overlay pointer/wheel protection: {fragment}'

for button_name, tab_index in [('lyricsTabButton', 0), ('queueTabButton', 1)]:
    marker = f'id: {button_name}'
    start = src.index(marker)
    end = src.find('\n                            GButton {', start + len(marker))
    button = src[start:end if end >= 0 else len(src)]
    assert f'checked: root.sideTab === {tab_index}' in button
    assert f'color: {button_name}.checked ? AppTheme.accent : AppTheme.text' in button
    assert f'color: {button_name}.checked ? AppTheme.accentSoft' in button
    assert f'border.color: {button_name}.checked ? AppTheme.accentBorder' in button

print('Now Playing presentation regression guards passed.')
