#!/usr/bin/env python3
"""Source-level checks for the shared live Discover watermark editor.

A real Qt GUI test is still needed for clipping/file dialog/persistence.
"""
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
read = lambda name: (ROOT / name).read_text(encoding="utf-8")
theme = read("qml/AppTheme.qml")
card = read("qml/components/ExploreNavigationCard.qml")
sidebar = read("qml/components/AppSidebar.qml")
dialog = read("qml/components/ExploreWatermarkDialog.qml")
cmake = read("CMakeLists.txt")

assert 'category: "DiscoverWatermark"' in theme
assert 'property string iconName: "watermark.png"' in theme
assert 'property real angle: -10' in theme
assert 'property real opacityValue: 0.25' in theme
assert 'property real offsetX: -15' in theme
assert 'property int presetVersion: 0' in theme
assert 'function migrateDiscoverWatermarkPreset()' in theme
assert 'setDiscoverWatermarkDraft("watermark.png", 1.5, -10, 0.25, -15, 0)' in theme
assert (ROOT / 'assets/icons/png/watermark.png').is_file()
assert 'assets/icons/png/*.png' in cmake
assert 'color: "transparent"' in card
assert 'border.width: 0' in card
assert 'objectName: "exploreActiveMarker"' in card
assert 'visible: card.active' in card
assert 'objectName: "exploreIconSlot"' in card
assert 'hoverEnabled: false' in card
assert 'No icon tile: retain placement but remove its frame and fill.' in card
assert 'opacity: AppTheme.discoverWatermarkOpacity' in card
assert '{icon: "watermark.png", tr: "Varsayılan", en: "Default"}' in dialog
for key in ('scale', 'angle', 'opacityValue', 'offsetX', 'offsetY'):
    assert 'property real ' + key + ':' in theme
for method in ('setDiscoverWatermarkDraft(', 'saveDiscoverWatermark(',
               'restoreDiscoverWatermark(', 'resetDiscoverWatermarkDraft('):
    assert 'function ' + method in theme
assert 'discoverWatermarkSettings.sync()' in theme
assert 'readonly property url discoverWatermarkSource:' in theme
for setting in ('Scale', 'Angle', 'Opacity', 'OffsetX', 'OffsetY', 'Source'):
    assert 'AppTheme.discoverWatermark' + setting in card
assert 'signal watermarkEditorRequested()' in card
assert 'onWatermarkEditorRequested: watermarkDialog.open()' in sidebar
assert 'ExploreWatermarkDialog {' in sidebar
assert 'function openWatermarkEditor()' in sidebar
assert 'function openWatermarkEditor()' in read('qml/components/SidebarShell.qml')
assert 'signal watermarkEditorRequested()' in read('qml/pages/BrowsePage.qml')
assert 'onWatermarkEditorRequested: appSidebar.openWatermarkEditor()' in read('qml/Main.qml')
assert 'qml/components/ExploreWatermarkDialog.qml' in cmake
assert 'onAboutToShow: AppTheme.restoreDiscoverWatermark()' in dialog
assert 'onClosed: AppTheme.restoreDiscoverWatermark()' in dialog
assert 'AppTheme.saveDiscoverWatermark()' in dialog
assert 'AppTheme.resetDiscoverWatermarkDraft()' in dialog
assert 'ExploreNavigationCard {' in dialog
assert 'id: iconPicker' in dialog
assert 'AppTheme.localIconSource(source)' in dialog
assert 'onMoved: control.edited(value)' in dialog
assert 'AppTheme.discoverWatermarkIcon = modelData.icon' in dialog
print('Watermark icon picker, live preview, save/cancel/reset and persistence wiring verified.')
