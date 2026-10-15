#!/usr/bin/env bash
set -euo pipefail

export PYTHONDONTWRITEBYTECODE=1
export QT_QPA_PLATFORM=offscreen
export QT_QUICK_BACKEND=software

python3 tests/test-gfile-ui-controls.py
python3 tests/test-source-uninstall.py
python3 tests/run-security-tests.py
python3 tests/run-memory-tests.py
python3 tests/run-history-tests.py
python3 tests/run-properties-tests.py
python3 tests/run-administrator-access-tests.py
python3 tests/run-archive-progress-tests.py
python3 tests/run-vault-tests.py
python3 tests/test-vault-recovery-tool.py
python3 tests/test-ai-download-policy.py
python3 tests/run-branding-tests.py
python3 tests/run-chapter-tests.py
python3 tests/run-tmdb-tests.py
python3 tests/test-subtitle-translation-formatting.py
python3 tests/test-subtitle-readable-cues.py
python3 -m pytest -q -p no:cacheprovider tests/test-translation-quality.py
python3 tests/run-qml-tests.py
# Exercise real effect/mask lifetimes: the software Qt Quick backend skips them.
xvfb-run -a env LIBGL_ALWAYS_SOFTWARE=1 QSG_RHI_BACKEND=opengl \
    LURVIKO_QML_TEST_QPA=xcb LURVIKO_QML_TEST_BACKEND=rhi \
    LURVIKO_QML_TEST_FILE=tst_MusicLibraryView.qml python3 tests/run-qml-tests.py
python3 tests/run-music-presentation-tests.py
python3 tests/run-system-icon-tests.py
python3 tests/run-folder-icon-tests.py
