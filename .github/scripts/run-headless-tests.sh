#!/usr/bin/env bash
set -euo pipefail

export PYTHONDONTWRITEBYTECODE=1
export QT_QPA_PLATFORM=offscreen
export QT_QUICK_BACKEND=software

python3 tests/test-gfile-ui-controls.py
python3 tests/test-source-uninstall.py
python3 tests/run-security-tests.py
python3 tests/test-ai-download-policy.py
python3 tests/run-branding-tests.py
python3 tests/run-chapter-tests.py
python3 tests/test-subtitle-translation-formatting.py
python3 tests/test-subtitle-readable-cues.py
python3 -m pytest -q -p no:cacheprovider tests/test-translation-quality.py
python3 tests/run-qml-tests.py
python3 tests/run-system-icon-tests.py
