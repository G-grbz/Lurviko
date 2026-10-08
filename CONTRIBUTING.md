# Contributing to Lurviko

Thank you for helping improve Lurviko. Please keep changes focused and describe the problem they solve.

## Development

Start with the dependencies and build instructions in [README.md](README.md). UI conventions and shared controls are documented in [docs/LURVIKO_UI.md](docs/LURVIKO_UI.md).

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 4
./build/lurviko
```

Run the automated headless checks before submitting a change:

```bash
bash .github/scripts/run-headless-tests.sh
```

For file operations, navigation, live indexing, menus or subtitles, also run the relevant integration harness described in [tests/README.md](tests/README.md). Test file operations with disposable files. Some harnesses require a graphical session and the application build in `build/`.

## Bug reports

Include your distribution and release, Qt/KDE versions, Wayland or X11 session, relevant paths/file types, reproduction steps, and terminal output. A screenshot or short recording helps with layout problems. Remove access tokens and personal file content from reports.

## Pull requests

- Explain what changed and how you verified it.
- Preserve keyboard navigation, large-directory responsiveness and both themes.
- Reuse the existing QML controls instead of introducing a parallel visual style.
- Add regression coverage when it meaningfully protects a behavioral fix.
- Keep generated builds, credentials, downloaded models and local settings out of the repository.
- Preserve notices and identify the origin/license of any added third-party asset.
