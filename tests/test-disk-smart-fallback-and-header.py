#!/usr/bin/env python3
"""Guard disk header badge alignment and non-blocking, cautious SMART fallback."""
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

# SMART must not be faked from disk usage and must never delay enumeration.
assert 'QString smartStatusViaSmartctl(' in cpp
assert 'QStringLiteral("--health")' in cpp
assert 'QStringLiteral("--nocheck=standby")' in cpp
assert 'QStringLiteral("--json")' in cpp
assert 'QStringLiteral("sat")' in cpp
assert 'result.value(QStringLiteral("smart_status"))' in cpp
assert 'passed.isBool()' in cpp
assert 'refreshSmartStatuses();' in cpp
assert 'm_smartWatcher.setFuture(QtConcurrent::run([pending]' in cpp
assert 'm_smartResults.value(entry.stableId' in cpp
assert 'selected.healthStatus = smartStatusForBlock(' in cpp
assert 'smartStatusViaSmartctl(blockDevice' not in cpp
assert 'QFutureWatcher<QHash<QString, QString>> m_smartWatcher' in header
print('Disk header and asynchronous SMART fallback source checks passed')
