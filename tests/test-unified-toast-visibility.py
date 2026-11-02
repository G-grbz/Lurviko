#!/usr/bin/env python3
"""Static guards for global toast wiring and deleted custom shortcut icons.

The real QML rendering checks still require the user's Qt 6 installation.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
main = (root / "qml/Main.qml").read_text()
presenter = (root / "qml/components/OperationPresentation.qml").read_text()
quick = (root / "src/quickaccessmodel.cpp").read_text()
renderer = (root / "qml/components/OperationToastLayer.qml").read_text()

assert 'id: operationToastLayer' in main
assert 'parent: Overlay.overlay' in main, "Global cards should render above StackView"
assert 'backdropSource: stack' in main
assert 'presentation: unifiedOperationPresentation' in main
assert 'newActiveJob' in presenter and '|| newActiveJob' in presenter
assert 'id: toastRoot' in renderer and 'id: layer' not in renderer
assert 'visible: toastRoot.cardsVisible' in renderer
assert 'toastRoot.visibleJobs' in renderer and 'toastRoot.presentation.minimize()' in renderer
assert 'layer.' not in renderer, 'Native Item.layer must not shadow the toast root ID'
assert 'file.isFile() && file.isReadable()' in quick
assert 'if (icon == QStringLiteral("folder.svg")' in quick
print("PASS: shared toast overlay and missing icon fallback")
