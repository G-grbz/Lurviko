# Release packages

Pushing a version tag runs `.github/workflows/release.yml`. It builds four
packages in clean containers, installs and launches each one on a clean system
of its target distribution on both X11 (Xvfb) and Wayland (headless weston
with GL) (`packaging/scripts/smoke-test-package.sh`), and only
then publishes them with the source archives and `SHA256SUMS`.

| Format | Built on | Smoke-tested on | Build command (as root in that container) |
|---|---|---|---|
| AppImage (+ `.zsync`) | `debian:trixie` (Qt 6.8, glibc ≤ 2.39 symbols) | `ubuntu:24.04` | `bash packaging/scripts/deps/debian.sh`, `bash packaging/scripts/fetch-appimage-tools.sh /tmp/tools`, then `PATH=/tmp/tools:$PATH APPIMAGE_EXTRACT_AND_RUN=1 bash packaging/scripts/build-appimage.sh dist` |
| DEB | `debian:trixie` | `debian:trixie` | `bash packaging/scripts/deps/debian.sh && bash packaging/scripts/build-native.sh deb dist` |
| RPM | `fedora:43` | `fedora:43` | `bash packaging/scripts/deps/fedora.sh && bash packaging/scripts/build-native.sh rpm dist` |
| Arch (`pkg.tar.zst`) | `archlinux:base` | `archlinux:base` | `bash packaging/scripts/build-arch.sh dist` (recipe: `packaging/arch/PKGBUILD`) |

The AUR recipe in `packaging/aur/` is unchanged and still builds from the
release source archive on the user's machine.

## Why the native packages use bytecode-only QML

`qmlcachegen` normally compiles QML bindings to C++ that links Qt's *private*
QML API. That API is only stable within one Qt minor release, and Fedora even
versions it (`Qt_6.10_PRIVATE_API`), so a prebuilt RPM or Arch package would
stop starting after the distribution's next Qt minor update.
`build-native.sh` and `packaging/arch/PKGBUILD` therefore configure
`-DLURVIKO_QML_BYTECODE_ONLY=ON`; `build-arch.sh` fails if the packaged binary
still references a private Qt API. The AppImage bundles its own Qt and keeps
the faster ahead-of-time compiled QML.

## What the AppImage bundles beyond linuxdeploy-plugin-qt

`build-appimage.sh` adds what the Qt plugin does not know about, then lets
linuxdeploy collect their dependencies:

- Qt 6 FFmpeg media backend (`plugins/multimedia`) and FFmpeg libraries;
- the Wayland EGL client buffer integration
  (`plugins/wayland-graphics-integration-client`) — without it Qt cannot create
  an OpenGL context on Wayland and the AppImage aborts at startup;
- KIO workers `file`, `trash`, `remote`, `http`, `ftp` and the `kioworker`
  executable (next to `lurviko`, where KIO looks for it) — without these the
  AppImage cannot copy, move or trash files on hosts without KDE;
- KWindowSystem X11/Wayland plugins and KIO URI filters;
- `libfribidi`, which linuxdeploy otherwise expects from the host.

It refuses to finish if a required plugin is missing, if the `.zsync` file was
not produced, or if any bundled ELF needs a glibc newer than
`LURVIKO_APPIMAGE_MAX_GLIBC` (default 2.39).

## Qt baseline

Lurviko's `CMakeLists.txt` requires Qt **6.8+** and OpenSSL **3.2+**, plus KDE
Frameworks 6. Debian 13 (stock Qt 6.8.2) meets this and is the oldest baseline
the build and automated tests have been run on. Ubuntu 24.04 (Qt 6.4) is too
old. An installable `.deb` package does not mean it can run on older releases.
Do not claim blanket AppImage compatibility: the build host's glibc baseline,
Qt platform plugins, KIO workers, KDE libraries and graphics drivers matter.

## Requirements

- Native build host with supported **Qt 6.8+**, **KDE Frameworks 6**,
  **OpenSSL 3.2+**, C++17, CMake, Ninja and package-specific tools.
- DEB: `dpkg-deb`, `dpkg-shlibdeps` (from `dpkg-dev`), and the corresponding
  Qt/KF6 development packages. CPack adds ELF dependencies; QML imports have
  explicit package dependencies in `packaging/native/CPack.cmake`.
- RPM: `rpm-build`, `rpm`, supported Qt/KF6 development packages; use
  `ffmpeg-free` for Fedora stock codecs (or the full FFmpeg from RPM Fusion).
  RPM's auto
  dependency resolver covers linked ELF libraries, not QML plugins.
- AppImage: `linuxdeploy` and **`linuxdeploy-plugin-qt`** with executable files
  discoverable on PATH. `QML_SOURCES_PATHS` points to `qml/` so static imports
  in embedded Qt resources can be discovered.

Build separately on the intended target system (or a clean matching container)
so native package metadata refers to libraries the distro actually ships.

## Non-negotiable safety and release gates

1. The global `org.freedesktop.FileManager1.service` filename can be owned by
   Dolphin/other file managers. Native packages and AppImages install a
   *user opt-in template* at `/usr/share/Lurviko/dbus-1/services/` instead.
2. Build binaries with `LURVIKO_PACKAGED_BUILD=ON` to avoid resolving optional
   developer-only Python helper paths from the build machine's source tree.
3. Do not embed the AUR package's sources or overwrite its successful recipe.
4. Test package metadata, dependencies, desktop integration and installing /
   removing packages on clean VMs. Test KIO (including network protocols),
   KDE Connect, Wallet, Open With, icon themes, DLNA/media, subtitle workers,
   Wayland and X11. Test Git absent and installed.
5. Verify `lurviko --help` or `lurviko --version` **only if those options are
   supported**; use actual launch/GUI smoke tests otherwise.
6. For AppImage, verify `appimagetool` output, `APPIMAGE_EXTRACT_AND_RUN=1`
   launch on a clean desktop, bundled Qt multimedia codecs, plugin loading and
   `AppDir/usr/lib/lurviko/lurviko-access-helper`. A candidate is **not** a
   portable release until this works on the minimum-target distributions.
7. Packages are published only by the tagged release workflow, after its
   per-distribution install-and-launch smoke tests pass. The scripts themselves
   never upload anything. The automated smoke test does not replace the manual
   desktop checks in item 4 (wallet, KIO network protocols, Wayland/X11, media).

## Installing the optional FileManager1 service template

If the default file manager should be Lurviko, the user can choose its desktop
entry. For optional `Show in folder` D-Bus activation, copy the packaged
service template into `${XDG_DATA_HOME:-$HOME/.local/share}/dbus-1/services/`
only after checking the `Exec` command. Changing global D-Bus activation
without user consent is deliberately avoided.

## Troubleshooting

- AppImage cannot run on older glibc: rebuild on an older baseline *with*
  supported Qt/KF6, or narrow the stated compatibility range.
- `module QtQuick ... is not installed`: inspect `QT_DEBUG_PLUGINS=1` output
  and include the missing QML plugin/dependency.
- KIO remote schemes fail: system-installed KIO worker modules may be
  required; bundling core KDE libraries alone is insufficient.
- Package installs but MIME actions fail: check `kservice`, `xdg-utils`, the
  desktop entry, and KIO workers; do not forcibly change MIME defaults.
