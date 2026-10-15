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
| `run-properties-tests.py` | Single and multi-selection Properties, aggregate totals, mixed permission masks, group edits, recursive safety and popup controls |
| `run-archive-progress-tests.py` | Actual archive creation, selected-language KIO descriptions, live entry/byte/speed counters, pause/resume, cancellation and safe option-like filenames |
| `run-security-tests.py` | KDF limits, canonical path boundaries, RAM-only storage and real bsdtar option-like filenames |
| `run-memory-tests.py` | Returning unused allocator pages to the OS while preserving live data (glibc 2.33+) |
| `run-history-tests.py` | Recent-location persistence through temporary missing paths, permission errors, restart and equivalent URL spellings |
| `test-ai-download-policy.py` | Default-off installs/downloads, dependency hashes, wheel flags, pinned revisions, cache/offline reuse and error language |
| `test-subtitle-translation-formatting.py` | SRT formatting and protected subtitle syntax |
| `test-subtitle-readable-cues.py` | Long-cue splitting, timing, styling and live/cache parity |
| `test-translation-quality.py` | EOS, fidelity ranking, sentence context, protected styles, live/resume parity, bounded caches and GPU memory recovery (pytest; no downloads) |
| `run-qml-tests.py` | Gallery/grouped layout, resizing, navigation anchoring, collection-wide music queues, stable genre filters/counts, independent music zoom, category zoom and slider ranges across directory navigation/page recreation, compact track rows and album/artist navigation |
| `run-music-presentation-tests.py` | Real Main expanded/detached UI lifetime, remembered tabs, independent detached task, compact restore without navigation, and memory measurements with warmed previews |
| `run-system-icon-tests.py` | Asynchronous system icon rendering |
| `run-folder-icon-tests.py` | All 49 baked folder variants, original SVG colors/alpha, bounded size requests, shared concurrent cache entries and cold/warm timings |

Build the application with Ninja into `build/` first. QML checks use Qt Quick Test and the application's real native objects/resources; `LURVIKO_TEST_BUILD_DIR` can select an external build tree. They require DejaVu Sans (`ttf-dejavu` on Arch) for reproducible text geometry and `dbus-run-session` for an isolated KIO session bus. Icon checks need CMake, Qt Test, KDE IconThemes/Archive and the Breeze icon theme. These checks use isolated settings and the offscreen/software Qt backend and do not need a running desktop or AI models.

`python3 tests/run-index-tests.py` checks category-specific hidden-file settings, pruning of excluded/unrelated subtrees, extension/name/path filters, file-size limits, root boundaries, automatic add/delete updates and idle CPU use. It needs a C++17 compiler, Qt Core/Concurrent development files and Qt's moc. `--baseline /path/contentindexmodel.cpp` compares the same fixtures against a saved source version; `--home-settings /path/Lurviko.conf` additionally performs a read-only home scan without updating personal settings or caches.

## Native integration harnesses

Build the application into `build/` first. These harnesses link the application's native objects and are intended for a development machine with the corresponding runtime services. Most are retained separately from headless CI because media, pointer interaction and window behavior depend on the session; the Properties harness also runs in headless CI.

```bash
python3 tests/run-navigation-tests.py
python3 tests/run-search-tests.py
python3 tests/run-sync-tests.py
python3 tests/run-music-tests.py
python3 tests/run-music-presentation-tests.py
python3 tests/run-thumbnail-tests.py
python3 tests/run-properties-tests.py
python3 tests/run-archive-progress-tests.py
python3 tests/run-mpris-tests.py
python3 tests/run-vault-tests.py
LURVIKO_SHORTCUT_QPA=wayland python3 tests/run-shortcut-tests.py
LURVIKO_RENAME_QPA=wayland python3 tests/run-rename-tests.py
LURVIKO_MENU_QPA=wayland python3 tests/run-menu-tests.py
LURVIKO_SUBTITLE_QPA=wayland python3 tests/run-subtitle-tests.py
```

Use Wayland commands in a Wayland desktop session; inspect each runner's environment options for another backend. Menu/media harnesses may require access to the session audio service. The subtitle harness uses a fake AI worker rather than downloading translation models. Music checks use a fake ffprobe, temporary lyrics and video files to verify responsive loading, bounded cache memory, exclusive scan workers and stable previews during writes.

The thumbnail harness swaps equal-size SVGs with matching modification times and edits an SVG while preserving its size/mtime. It verifies directory watcher revisions and the real asynchronous provider's cached pixels using temporary files.

The properties harness checks actual owner/group/other mode bits, execution,
directory traversal, ownership labels, special-bit preservation, restoring a
file with no read access, and symlink replacement boundaries. Recursive checks
include hidden entries, executable/document distinctions, external symlinks,
account/group enumeration, ordinary-user authority and cancellation. It also
exercises octal input, the real Apply/execute controls, popup wheel scrolling and
selected filename padding at three icon sizes. Bulk regressions cover aggregate totals, overlapping selections, mixed permission checkboxes, ownership-only edits, real member-group changes and preflight of every administrator tree before dispatch. Administrator authentication is
not invoked by this harness. A `0644` folder regression verifies an explicit
access error, recovery through the permission editor, unchanged SVG bytes and
the folder-access explanation in Properties. Light/dark screenshots are saved to
`/tmp/lurviko-properties-*.png`.

The MPRIS harness requires `dbus-run-session`, `playerctl` and `ffmpeg`. It uses a private D-Bus session and generated media to check real music tags, Turkish UTF-8 text, playlist artwork without a visible thumbnail delegate, native video pause/seek/volume, fullscreen continuity and returning control to music. Files and settings are isolated. Set `LURVIKO_MPRIS_QPA=wayland` to test in a Wayland desktop session.

The vault harness creates a temporary encrypted vault and isolates data, configuration and runtime files. It checks retry delays, attempt limits, actual process restarts, password/KWallet lockout enforcement, successful-login and expiry resets, disabling protection, password changes and preservation of encrypted file bytes. Security regressions cover malformed/oversized headers, GCM tag tampering, the per-file GCM size limit, RAM storage, instance isolation and cleanup path boundaries. The default run tests authentication without instantiating media viewers. Set `LURVIKO_VAULT_QPA=wayland` to also exercise the real security modal and automatic countdown recovery on the desktop; screenshots are saved as `/tmp/gfile-vault-*.png`.

The shortcut harness verifies conflict detection, explicit reassignment, alternative/disabled bindings across restarts, recording Escape without dismissing the editor, saved symlink/hardlink shortcuts, context-menu labels and native video input remapping. It also captures light/dark editor previews under `/tmp/gfile-keyboard-*.png` for visual review.

The rename harness covers F2 after scrolling a previous editor out of view, copying the selected basename, selection and scrolling after single/batch renames, and Tab advancing without losing editor focus. It also verifies hidden-name confirmation and cancellation for renaming and new files/folders, their shared persistent opt-out, and restoring these confirmations from View, plus the permanent deletion preference using its original saved key. All operations use temporary files and isolated settings.

## CodeQL

`python3 tests/run-tmdb-tests.py` exercises the real TMDB search state machine
with asynchronous fixture HTTP replies and AddressSanitizer. It covers exhausted
searches, movie/TV fallback, errors, retries, confident matches and cached results
without network access or real API credentials.

Music-library cover regressions switch tabs and densities with loaded artwork.
The headless suite also runs these checks under Xvfb with Mesa/OpenGL, because
Qt Quick's software backend does not exercise shader mask lifetimes. To run
the same regression on a Wayland desktop:

```sh
LURVIKO_QML_TEST_QPA=wayland LURVIKO_QML_TEST_BACKEND=rhi \
LURVIKO_QML_TEST_FILE=tst_MusicLibraryView.qml python3 tests/run-qml-tests.py
```

[`.github/workflows/codeql.yml`](../.github/workflows/codeql.yml) performs C++ analysis after a full manual CMake build, plus Python and GitHub Actions analysis. It runs on pushes, pull requests, manual dispatch and weekly. QML is covered by build/layout checks rather than a CodeQL language analyzer.

CodeQL checks security patterns; regression tests check behavior. A successful scan does not mean every possible defect has been ruled out. Findings appear in GitHub's **Security → Code scanning** tab.
