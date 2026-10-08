# Tests

Run commands from the repository root. Tests use temporary data/configuration where supported; do not point them at personal files.

## Automated CI checks

`python3 tests/run-chapter-tests.py` generates MKV/MP4 fixtures and checks embedded chapter timestamps/titles, Unicode, caching and probe cancellation. With `LURVIKO_CHAPTER_QPA=wayland`, it additionally exercises the actual VideoViewer chapter menu, seeking while paused, fullscreen and Escape; the preview is saved to `/tmp/lurviko-video-chapters.png`.

`python3 tests/run-branding-tests.py` verifies the rename using isolated legacy configuration/data/cache, an actual encrypted vault, collision backups and symlink rejection. It also checks release-version comparison, stable tags and official GitHub URL validation without network access.

```bash
bash .github/scripts/run-headless-tests.sh
```

| Check | Coverage |
| --- | --- |
| `test-gfile-ui-controls.py` | Shared QML control conventions |
| `test-source-uninstall.py` | Custom install prefixes, DESTDIR, preservation of personal data and unrelated aliases, repeated removal and manifest validation |
| `test-subtitle-translation-formatting.py` | SRT formatting and protected subtitle syntax |
| `test-subtitle-readable-cues.py` | Long-cue splitting, timing, styling and live/cache parity |
| `run-qml-tests.py` | Gallery/grouped layout, resizing, navigation anchoring, collection-wide music queues, stable genre filters/counts, independent music zoom, category zoom and slider ranges across directory navigation/page recreation, compact track rows and album/artist navigation |
| `run-system-icon-tests.py` | Asynchronous system icon rendering |

Build the application with Ninja into `build/` first. QML checks use Qt Quick Test and the application's real native objects/resources; `LURVIKO_TEST_BUILD_DIR` can select an external build tree. Icon checks need CMake, Qt Test, KDE IconThemes/Archive and the Breeze icon theme. These checks use isolated settings and the offscreen/software Qt backend and do not need a running desktop or AI models.

## Native integration harnesses

Build the application into `build/` first. These harnesses link the application's native objects and are intended for a development machine with the corresponding runtime services. They are retained separately from headless CI because media, pointer interaction and window behavior depend on the session.

```bash
python3 tests/run-navigation-tests.py
python3 tests/run-search-tests.py
python3 tests/run-sync-tests.py
python3 tests/run-music-tests.py
python3 tests/run-thumbnail-tests.py
python3 tests/run-mpris-tests.py
python3 tests/run-vault-tests.py
LURVIKO_SHORTCUT_QPA=wayland python3 tests/run-shortcut-tests.py
LURVIKO_RENAME_QPA=wayland python3 tests/run-rename-tests.py
LURVIKO_MENU_QPA=wayland python3 tests/run-menu-tests.py
LURVIKO_SUBTITLE_QPA=wayland python3 tests/run-subtitle-tests.py
```

Use Wayland commands in a Wayland desktop session; inspect each runner's environment options for another backend. Menu/media harnesses may require access to the session audio service. The subtitle harness uses a fake AI worker rather than downloading translation models. Music checks use a fake ffprobe, temporary lyrics and video files to verify responsive loading, bounded cache memory, exclusive scan workers and stable previews during writes.

The thumbnail harness swaps equal-size SVGs with matching modification times and edits an SVG while preserving its size/mtime. It verifies directory watcher revisions and the real asynchronous provider's cached pixels using temporary files.

The MPRIS harness requires `dbus-run-session`, `playerctl` and `ffmpeg`. It uses a private D-Bus session and generated media to check real music tags, Turkish UTF-8 text, playlist artwork without a visible thumbnail delegate, native video pause/seek/volume, fullscreen continuity and returning control to music. Files and settings are isolated. Set `LURVIKO_MPRIS_QPA=wayland` to test in a Wayland desktop session.

The vault harness creates a temporary encrypted vault and isolates data, configuration and runtime files. It checks retry delays, attempt limits, actual process restarts, password/KWallet lockout enforcement, successful-login and expiry resets, disabling protection, password changes and preservation of encrypted file bytes. The default run tests authentication without instantiating media viewers. Set `LURVIKO_VAULT_QPA=wayland` to also exercise the real security modal and automatic countdown recovery on the desktop; screenshots are saved as `/tmp/gfile-vault-*.png`.

The shortcut harness verifies conflict detection, explicit reassignment, alternative/disabled bindings across restarts, recording Escape without dismissing the editor, saved symlink/hardlink shortcuts, context-menu labels and native video input remapping. It also captures light/dark editor previews under `/tmp/gfile-keyboard-*.png` for visual review.

The rename harness covers F2 after scrolling a previous editor out of view, copying the selected basename, selection and scrolling after single/batch renames, and Tab advancing without losing editor focus. It also verifies hidden-name confirmation and cancellation for renaming and new files/folders, their shared persistent opt-out, and restoring these confirmations from View, plus the permanent deletion preference using its original saved key. All operations use temporary files and isolated settings.

## CodeQL

[`.github/workflows/codeql.yml`](../.github/workflows/codeql.yml) performs C++ analysis after a full manual CMake build, plus Python and GitHub Actions analysis. It runs on pushes, pull requests, manual dispatch and weekly. QML is covered by build/layout checks rather than a CodeQL language analyzer.

CodeQL checks security patterns; regression tests check behavior. A successful scan does not mean every possible defect has been ruled out. Findings appear in GitHub's **Security → Code scanning** tab.
