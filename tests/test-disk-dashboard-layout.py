#!/usr/bin/env python3
"""Source regression: overview totals, disk badges and compact gradient bars.

This static check complements the runtime Qt/QML tests (which require Qt 6).
"""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
home = (root / 'qml/pages/HomePage.qml').read_text()
card = (root / 'qml/components/DiskCard.qml').read_text()

for data in ('totalBytes', 'usedBytes', 'freeBytes'):
    assert f'page.storageModel.formatBytes(diskOverviewPanel.{data})' in home, data
assert 'fileSystem: model.fileSystem' in home
assert 'fileSystem: fileSystem' not in home
assert 'readonly property int responsiveHeight: Math.max(138, effectiveIconPixels + 96)' in card
assert 'clip: true' in card
assert 'id: mountChip\n        // The card' in card
assert 'z: 6' in card
assert 'width: parent.width * Math.max(0, Math.min(1, card.ratio))' in card
assert 'Layout.preferredHeight: 5' in card
assert 'DiskMountBadge {' in card
assert 'signal refreshRequested()' in card
assert 'healthStatus: model.healthStatus' not in home
assert 'SMART' not in card
for item in ('Yeni sekmede aç', 'Favorilere ekle', 'Yenile', 'Özellikler'):
    assert item in card
assert 'Layout.preferredHeight: 7' in home
assert card.count('GradientStop { position:') == 2
assert home.count('GradientStop { position:') >= 2
for name in ('onMountToggleRequested', 'onFavoriteToggleRequested', 'onReorderRequested', 'onEditRequested'):
    assert name in home
print('disk dashboard static checks passed')
