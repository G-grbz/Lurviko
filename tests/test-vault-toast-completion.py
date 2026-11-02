#!/usr/bin/env python3
"""Guards for the shared file/vault/cloud completion card.

Runtime interaction tests require the user's Qt 6 development environment.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
main = (root / "qml/Main.qml").read_text()
browser = (root / "qml/components/BrowserPane.qml").read_text()
renderer = (root / "qml/components/OperationToastLayer.qml").read_text()
presenter = (root / "qml/components/OperationPresentation.qml").read_text()
checks = {
    "single global presenter": "id: unifiedOperationPresentation" in main
        and "presentation: unifiedOperationPresentation" in main,
    "file and vault operations joined": 'id: "file:" + index' in main
        and 'id: "vault:" + root.vaultJobSerial' in main,
    "cloud jobs joined": 'id: "cloud:" + entry.key' in main,
    "no separate vault/card renderer": 'id: vaultTransferCard' not in main
        and 'id: globalDriveTransferCard' not in main,
    "shared completion countdown": "duration: 5000" in renderer
        and "completionCountdownAnimation.restart()" in renderer,
    "instant close action": "onTapped: toastRoot.dismissOperation(operation)" in renderer,
    "shared completion colors": "operation.success ? AppTheme.success : AppTheme.danger" in renderer,
    "matching completed geometry": "radius: 14" in renderer
        and "height: operation.finished ? 108" in renderer
        and "Layout.preferredHeight: 7" in renderer,
    "hover pause": "toastRoot.setDismissPaused(operation, hovered)" in renderer,
    "close icon": 'AppTheme.icon("close-ui.svg")' in renderer,
    "shared completion preference": "showOnCompletion: AppTheme.operationShowOnCompletion" in main
        and "showOnCompletion: AppTheme.operationShowOnCompletion" in browser,
    "success metrics retained": "vaultTransferLastFilesDone" in main
        and "vaultTransferLastBytesDone" in main,
    "no QML Item.layer shadowing": "id: toastRoot" in renderer
        and "id: layer" not in renderer
        and "layer." not in renderer,
    "safe early bindings": "readonly property var visibleJobs:" in renderer
        and "visible: toastRoot.cardsVisible" in renderer,
    "preserve operation controls": "toastRoot.toggleOperationPause(operation)" in renderer
        and "toastRoot.cancelOperation(operation)" in renderer,
}
for label, valid in checks.items():
    print(f'{"PASS" if valid else "FAIL"}: {label}')
if not all(checks.values()):
    raise SystemExit(1)
