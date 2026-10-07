# Tests

Run commands from the repository root. Tests use temporary data/configuration where supported; do not point them at personal files.

## Automated CI checks

```bash
bash .github/scripts/run-headless-tests.sh
```

| Check | Coverage |
| --- | --- |
| `test-gfile-ui-controls.py` | Shared QML control conventions |
| `test-subtitle-translation-formatting.py` | SRT formatting and protected subtitle syntax |
| `test-subtitle-readable-cues.py` | Long-cue splitting, timing, styling and live/cache parity |
| `run-qml-tests.py` | Gallery/grouped layout, resizing and navigation anchoring |
| `run-system-icon-tests.py` | Asynchronous system icon rendering |

Build the application with Ninja into `build/` first. QML checks use Qt Quick Test and the application's real native objects/resources; `GFILE_TEST_BUILD_DIR` can select an external build tree. Icon checks need CMake, Qt Test, KDE IconThemes/Archive and the Breeze icon theme. These checks use isolated settings and the offscreen/software Qt backend and do not need a running desktop or AI models.

## Native integration harnesses

Build the application into `build/` first. These harnesses link the application's native objects and are intended for a development machine with the corresponding runtime services. They are retained separately from headless CI because media, pointer interaction and window behavior depend on the session.

```bash
python3 tests/run-navigation-tests.py
python3 tests/run-search-tests.py
python3 tests/run-sync-tests.py
GFILE_MENU_QPA=wayland python3 tests/run-menu-tests.py
GFILE_SUBTITLE_QPA=wayland python3 tests/run-subtitle-tests.py
```

Use Wayland commands in a Wayland desktop session; inspect each runner's environment options for another backend. Menu/media harnesses may require access to the session audio service. The subtitle harness uses a fake AI worker rather than downloading translation models.

## CodeQL

[`.github/workflows/codeql.yml`](../.github/workflows/codeql.yml) performs C++ analysis after a full manual CMake build, plus Python and GitHub Actions analysis. It runs on pushes, pull requests, manual dispatch and weekly. QML is covered by build/layout checks rather than a CodeQL language analyzer.

CodeQL checks security patterns; regression tests check behavior. A successful scan does not mean every possible defect has been ruled out. Findings appear in GitHub's **Security → Code scanning** tab.
