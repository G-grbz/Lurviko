#!/usr/bin/env python3
"""Guard disk header alignment and the removal of background disk health probes."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
card = (root / 'qml/components/DiskCard.qml').read_text()
cpp = (root / 'src/storagemodel.cpp').read_text()
header = (root / 'src/storagemodel.h').read_text()

# Filesystem tag and favorite star have the same RowLayout/vertical alignment.
marker = '            // Both metadata and favorite are centered within the SAME row;'
assert marker in card
start = card.index(marker)
end = card.index('        // Push storage usage', start)
area = card[start:end]
assert 'DiskInfoChip {' in area and 'DiskIconActionButton {' in area
assert area.count('Layout.alignment: Qt.AlignVCenter') >= 3
assert area.index('DiskInfoChip {') < area.index('DiskIconActionButton {')

# SMART was deliberately removed: no background probes, storage roles, or UI badges.
for source in (card, cpp, header, (root / 'qml/pages/HomePage.qml').read_text()):
    assert 'smartctl' not in source.lower()
    assert 'healthStatus' not in source
    assert 'SMART' not in source
assert 'DiskMountBadge {' in card
assert 'card.percentText' in card
assert 'refreshNetworkCapacities();' in cpp
print('Disk header and SMART removal source checks passed')
