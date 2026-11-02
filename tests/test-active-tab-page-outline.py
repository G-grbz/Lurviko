#!/usr/bin/env python3
"""Source-level regression guards for the active tab/page outline and watermark.

Desktop compositing and clipping still require a real Qt 6 render test.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
read = lambda path: (ROOT / path).read_text(encoding='utf-8')
main = read('qml/Main.qml')
theme = read('qml/AppTheme.qml')
tabs = read('qml/components/FluidTabSurface.qml')
chrome = read('qml/components/PageChrome.qml')
browse = read('qml/pages/BrowsePage.qml')
home = read('qml/pages/HomePage.qml')
private = read('qml/pages/PrivateVaultPage.qml')
card = read('qml/components/ExploreNavigationCard.qml')

assert 'readonly property color activeTabOutline:' in theme
assert 'AppTheme.activeTabOutline' in tabs
assert 'strokeColor: activePageOutline.strokeColor' in main
assert 'activeTab.outlineColor' in main
assert 'id: activePageOutline' in main
assert main.index('id: activePageOutline') > main.index('id: mainShell')
assert 'Shape.CurveRenderer' in main
assert main.count('id: activePageOutline') == 1
assert 'PathQuad {' in main and 'readonly property real tabBaseline:' in main
assert 'readonly property real baseline:' not in main, "Shape.baseline is FINAL in Qt; do not redeclare it"
assert 'gapLeft' not in main and 'gapRight' not in main
assert 'activeTab.mapToItem(activePageOutline, 0, 0)' in main
assert 'startX: activePageOutline.tabLeft' in main
assert 'startY: activePageOutline.tabBaseline' in main
assert 'x: activePageOutline.tabRight' in main
assert 'x: activePageOutline.tabLeft' in main
assert 'y: activePageOutline.tabBaseline' in main
assert 'pageOutlineJoined: page.headerHost !== null' in browse
assert 'pageOutlineJoined: chrome.pageOutlineJoined' in chrome
assert 'strokeColor: surface.pageOutlineJoined && surface.selected' in tabs
assert 'pageOutlineJoined: page.headerHost !== null' in home
assert 'pageOutlineJoined: page.headerHost !== null' in private
assert 'onItemAdded: function(index, item)' in browse
assert 'onItemRemoved: function(index, item)' in browse
assert 'activeSurfaceGeneration++' in browse
assert 'activeTabScrollPosition' in main
assert 'id: chromeTabSurface' in chrome
assert 'readonly property Item activeTabSurface:' in chrome
assert 'readonly property Item activeTabSurface:' in browse
assert 'readonly property Item activeTabSurface:' in home
assert 'readonly property Item activeTabSurface:' in private
assert 'readonly property bool activeTabAdministrator:' in browse
assert 'isAdminLocation(tabs[currentTab].location)' in browse
assert 'readonly property Item surfaceItem: tabSurface' in browse
assert 'rotation: AppTheme.discoverWatermarkAngle' in card
assert 'opacity: AppTheme.discoverWatermarkOpacity' in card
assert 'anchors.margins: 6' in card
assert 'glyphHeightFraction: 0.82' in card
assert '(parent.height - 4) / glyphHeightFraction' in card
assert 'AppTheme.discoverWatermarkOffsetX' in card
assert 'AppTheme.discoverWatermarkOffsetY' in card
assert 'AppTheme.discoverWatermarkSource' in card
assert 'rotation: -23' not in card
# The earlier Qt theme background harmonization must not be regressed.
for page in (browse, home, private):
    assert '0.30' in page
assert 'color: "transparent"' in card and 'border.width: 0' in card
print('Single-path tab/page outline, new-tab lifecycle and watermark checks passed.')
