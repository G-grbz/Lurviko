#!/usr/bin/env bash
# Qt 6.8 (Debian 13) compatibility gate. Run as an unprivileged user after
# building into $LURVIKO_TEST_BUILD_DIR. Qt 6.9+-only QML or C++ API fails here.
# Not run here (environment-only differences, still covered on Arch/latest Qt):
#   run-desktop-appearance-tests.py  offscreen windows have no native alpha
#   run-menu-tests.py                needs an installed default text editor
#   run-file-operations-tests.py     permission-denied fixture differs in containers
set -euo pipefail
export PYTHONDONTWRITEBYTECODE=1 QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/lurviko-qt68-runtime}"
mkdir -p "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"
for test in \
    tests/test-gfile-ui-controls.py tests/run-security-tests.py tests/run-memory-tests.py \
    tests/run-history-tests.py tests/run-index-tests.py tests/run-directory-size-tests.py \
    tests/run-category-selection-tests.py tests/run-browser-session-tests.py tests/run-columns-tests.py \
    tests/run-search-tests.py tests/run-git-tests.py tests/run-git-operations-tests.py \
    tests/run-properties-tests.py tests/run-samba-share-tests.py tests/run-administrator-access-tests.py \
    tests/run-archive-progress-tests.py tests/run-archive-drop-tests.py tests/run-vault-tests.py \
    tests/test-vault-recovery-tool.py tests/run-branding-tests.py tests/run-chapter-tests.py \
    tests/run-tmdb-tests.py tests/run-qml-tests.py tests/run-music-presentation-tests.py \
    tests/run-system-icon-tests.py tests/run-folder-icon-tests.py tests/run-thumbnail-tests.py; do
  echo "::group::$test"
  python3 "$test"
  echo "::endgroup::"
done
