#!/usr/bin/env bash
set -euo pipefail
# Requires linuxdeploy and linuxdeploy-plugin-qt on PATH (or use the env vars).
# Build on a baseline whose glibc matches the MINIMUM supported target; a
# Fedora-newest-built AppImage is not automatically compatible with old Linux.
repo="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
output="${1:-${repo}/dist}"
mkdir -p "$output"
output="$(cd "$output" && pwd)"
work="${LURVIKO_PACKAGING_WORK:-${repo}/build-package-appimage}"
appdir="${LURVIKO_APPDIR:-${repo}/build-package-AppDir}"
linuxdeploy="${LINUXDEPLOY:-linuxdeploy}"
qt_plugin="${LINUXDEPLOY_QT_PLUGIN:-linuxdeploy-plugin-qt}"
command -v cmake >/dev/null
command -v "$linuxdeploy" >/dev/null || { echo "linuxdeploy not found: $linuxdeploy" >&2; exit 2; }
command -v "$qt_plugin" >/dev/null || { echo "Qt linuxdeploy plugin not found: $qt_plugin" >&2; exit 2; }
# linuxdeploy-plugin-qt uses qmake to locate Qt plugins. An unqualified
# "qmake" may be Qt 5 even when Lurviko itself was compiled against Qt 6.
if [[ -z "${QMAKE:-}" ]]; then
  if command -v qmake6 >/dev/null 2>&1; then
    QMAKE="$(command -v qmake6)"
  elif [[ -x /usr/lib/qt6/bin/qmake ]]; then
    QMAKE=/usr/lib/qt6/bin/qmake
  elif [[ -x /usr/lib64/qt6/bin/qmake ]]; then
    QMAKE=/usr/lib64/qt6/bin/qmake
  elif command -v qmake >/dev/null 2>&1; then
    QMAKE="$(command -v qmake)"
  fi
fi
if [[ -z "${QMAKE:-}" ]] || ! command -v "$QMAKE" >/dev/null 2>&1; then
  echo 'Qt 6 qmake not found. Set QMAKE=/path/to/qt6/qmake.' >&2
  exit 2
fi
qt_version="$("$QMAKE" -query QT_VERSION)"
if [[ "$qt_version" != 6.* ]]; then
  echo "Expected Qt 6 qmake, but $QMAKE reports $qt_version" >&2
  echo 'Set QMAKE=/usr/lib/qt6/bin/qmake (or your distro Qt 6 equivalent).' >&2
  exit 2
fi
export QMAKE
qt_plugins="$("$QMAKE" -query QT_INSTALL_PLUGINS)"
if [[ ! -d "$qt_plugins/platforms" ]]; then
  echo "Qt 6 platform plugin directory not found: $qt_plugins/platforms" >&2
  exit 2
fi
# Qt 6.10+ uses one Wayland plugin; older Qt 6 used two plugins.
if [[ -z "${EXTRA_PLATFORM_PLUGINS:-}" ]]; then
  if [[ -f "$qt_plugins/platforms/libqwayland.so" ]]; then
    export EXTRA_PLATFORM_PLUGINS=libqwayland.so
  elif [[ -f "$qt_plugins/platforms/libqwayland-egl.so" && -f "$qt_plugins/platforms/libqwayland-generic.so" ]]; then
    export EXTRA_PLATFORM_PLUGINS='libqwayland-egl.so;libqwayland-generic.so'
  else
    echo "Qt 6 Wayland platform plugin missing in $qt_plugins/platforms" >&2
    echo 'Install the Qt 6 Wayland package before building the AppImage.' >&2
    exit 2
  fi
fi
# The older strip bundled by linuxdeploy may not understand modern .relr.dyn.
export NO_STRIP="${NO_STRIP:-1}"
echo "AppImage Qt: $qt_version via $QMAKE; Qt plugins: $qt_plugins" >&2
echo "AppImage platforms: xcb + $EXTRA_PLATFORM_PLUGINS; NO_STRIP=$NO_STRIP" >&2
# A partial AppDir from an old build risks shipping stale binaries and plugins.
if [[ -e "$appdir" ]]; then
  echo "Refusing to overwrite existing AppDir: $appdir" >&2
  echo 'Choose a new LURVIKO_APPDIR or remove the previous test directory.' >&2
  exit 2
fi
cmake -S "$repo" -B "$work" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
  -DCMAKE_INSTALL_LIBEXECDIR=lib/lurviko \
  -DLURVIKO_PACKAGED_BUILD=ON \
  -DLURVIKO_INSTALL_SYSTEM_FILEMANAGER1=OFF
cmake --build "$work" --parallel "${LURVIKO_PACKAGE_JOBS:-2}"
DESTDIR="$appdir" cmake --install "$work"
test -x "$appdir/usr/bin/lurviko"
test -f "$appdir/usr/share/applications/lurviko.desktop"
test -f "$appdir/usr/share/Lurviko/subtitle-ai/worker.py"
test -x "$appdir/usr/lib/lurviko/lurviko-access-helper"
# linuxdeploy-plugin-qt only knows Qt's own plugin families. Lurviko also needs
# KDE Frameworks plugins (KIO workers for copy/move/trash, KWindowSystem for
# X11/Wayland integration, URI filters) and Qt 6's FFmpeg media backend, or the
# AppImage starts but cannot copy files or play media on hosts without them.
copy_plugins() {
  local family="$1"; shift
  local source="$qt_plugins/$family"
  [[ -d "$source" ]] || { echo "Missing plugin directory: $source" >&2; exit 2; }
  mkdir -p "$appdir/usr/plugins/$family"
  local copied=0 file
  for file in "$source"/*.so; do
    [[ -e "$file" ]] || continue
    local name="${file##*/}"
    if (( $# )); then
      local wanted=0 pattern
      for pattern in "$@"; do [[ "$name" == $pattern ]] && wanted=1; done
      (( wanted )) || continue
    fi
    cp -a "$file" "$appdir/usr/plugins/$family/"
    copied=$((copied + 1))
  done
  (( copied > 0 )) || { echo "No plugins copied from $source" >&2; exit 2; }
}
copy_plugins multimedia 'libffmpegmediaplugin.so'
# Wayland's OpenGL client buffer integration. Without it Qt cannot create a GL
# context on Wayland ("Failed to load client buffer integration: wayland-egl")
# and Qt Quick aborts at startup. The *-server plugins are compositor-only.
copy_plugins wayland-graphics-integration-client 'libqt-plugin-wayland-egl.so' 'libdmabuf-client*.so' 'libvulkan-client*.so'
copy_plugins kf6/kio 'kio_file.so' 'kio_trash.so' 'kio_remote.so' 'kio_http.so' 'kio_ftp.so'
copy_plugins kf6/kwindowsystem
copy_plugins kf6/urifilters
# Workers such as trash run in a separate kioworker process. KIO looks for it
# next to the application binary, so it shares the bundle's qt.conf/plugins.
kioworker=""
for candidate in "$(dirname "$("$QMAKE" -query QT_INSTALL_LIBS)")/libexec/kf6/kioworker" \
                 "$("$QMAKE" -query QT_INSTALL_LIBS)/libexec/kf6/kioworker" \
                 /usr/lib/x86_64-linux-gnu/libexec/kf6/kioworker /usr/lib/kf6/kioworker /usr/libexec/kf6/kioworker; do
  if [[ -x "$candidate" ]]; then kioworker="$candidate"; break; fi
done
[[ -n "$kioworker" ]] || { echo 'KIO kioworker executable not found (install the KIO runtime package)' >&2; exit 2; }
install -m 0755 "$kioworker" "$appdir/usr/bin/kioworker"
# linuxdeploy's host-library exclude list skips libfribidi, but FFmpeg's
# subtitle/text filters need it; minimal desktops may not ship it.
extra_libraries=()
for library in libfribidi.so.0; do
  path="$(ldconfig -p | awk -v lib="$library" '$1 == lib && /x86-64/ {print $NF; exit}')"
  [[ -n "$path" ]] && extra_libraries+=(--library "$path")
done
version="${LURVIKO_VERSION:-$(sed -nE 's/^project\(Lurviko VERSION ([0-9.]+).*/\1/p' "$repo/CMakeLists.txt")}"
[[ -n "$version" ]] || { echo 'Cannot determine the Lurviko version' >&2; exit 2; }
# Output name and zsync delta-update information (AppImageUpdate, Gear Lever,
# AppImageLauncher): the embedded string tells updaters where the newest
# release's .zsync file lives on GitHub.
export LDAI_OUTPUT="Lurviko-${version}-x86_64.AppImage"
export LDAI_UPDATE_INFORMATION="${LURVIKO_APPIMAGE_UPDATE_INFORMATION:-gh-releases-zsync|G-grbz|Lurviko|latest|Lurviko-*-x86_64.AppImage.zsync}"
# QML modules are embedded in the executable; the plugin must scan source.
export QML_SOURCES_PATHS="$repo/qml"
export EXTRA_QT_MODULES="${EXTRA_QT_MODULES:-svg;multimedia;networkauth}"
# Running AppImage tools in headless CI often needs extract-and-run mode.
export APPIMAGE_EXTRACT_AND_RUN="${APPIMAGE_EXTRACT_AND_RUN:-1}"
# linuxdeploy-plugin-qt deploys every Qt SQL driver when it detects QtSql.
# Lurviko uses SQLite; unrelated Firebird/ODBC drivers may require client
# libraries not installed on the packaging host. Never touch system Qt paths:
# give linuxdeploy a private copy of the Qt plugin tree containing just SQLite.
qt_plugin_filter_root="$(mktemp -d "${TMPDIR:-/tmp}/lurviko-qt-plugins.XXXXXXXX")"
trap 'rm -rf -- "$qt_plugin_filter_root"' EXIT
mkdir -p "$qt_plugin_filter_root/plugins"
cp -a "$qt_plugins/." "$qt_plugin_filter_root/plugins/"
if [[ ! -f "$qt_plugin_filter_root/plugins/sqldrivers/libqsqlite.so" ]]; then
  echo "Required Qt 6 SQLite plugin is missing: $qt_plugins/sqldrivers/libqsqlite.so" >&2
  exit 2
fi
for driver in "$qt_plugin_filter_root"/plugins/sqldrivers/*; do
  [[ -e "$driver" || -L "$driver" ]] || continue
  if [[ "${driver##*/}" != libqsqlite.so ]]; then
    rm -f -- "$driver"
  fi
done
# The deployer only learns Qt plugin paths from qmake -query. Redirect those
# queries to the private tree while passing through all other qmake operations.
cat > "$qt_plugin_filter_root/qmake" <<'QMAKE_SHIM'
#!/usr/bin/env bash
set -euo pipefail
if [[ "${1:-}" == -query && "$#" -eq 2 && "$2" == QT_INSTALL_PLUGINS ]]; then
  printf '%s\n' "$LURVIKO_QT_FILTER_PLUGINS"
elif [[ "${1:-}" == -query && "$#" -eq 1 ]]; then
  "$LURVIKO_REAL_QMAKE" "$@" |
    sed "s|^QT_INSTALL_PLUGINS:.*|QT_INSTALL_PLUGINS:${LURVIKO_QT_FILTER_PLUGINS}|"
else
  exec "$LURVIKO_REAL_QMAKE" "$@"
fi
QMAKE_SHIM
chmod +x "$qt_plugin_filter_root/qmake"
export LURVIKO_REAL_QMAKE="$QMAKE"
export LURVIKO_QT_FILTER_PLUGINS="$qt_plugin_filter_root/plugins"
export QMAKE="$qt_plugin_filter_root/qmake"
echo 'AppImage SQL drivers: libqsqlite.so only (isolated Qt plugin view)' >&2
(cd "$output" && "$linuxdeploy" --appdir "$appdir" \
  --executable "$appdir/usr/lib/lurviko/lurviko-access-helper" \
  --executable "$appdir/usr/bin/kioworker" \
  "${extra_libraries[@]}" \
  --deploy-deps-only "$appdir/usr/plugins/multimedia" \
  --deploy-deps-only "$appdir/usr/plugins/wayland-graphics-integration-client" \
  --deploy-deps-only "$appdir/usr/plugins/kf6" \
  --plugin qt --output appimage)
test -f "$output/$LDAI_OUTPUT" || { echo "AppImage was not created: $output/$LDAI_OUTPUT" >&2; exit 1; }
if [[ -n "$LDAI_UPDATE_INFORMATION" && ! -f "$output/$LDAI_OUTPUT.zsync" ]]; then
  echo "zsync file missing for delta updates: $output/$LDAI_OUTPUT.zsync" >&2
  exit 1
fi
# Never report success if neither QPA platform plugin was actually bundled.
if [[ ! -f "$appdir/usr/plugins/platforms/libqxcb.so" ]]; then
  echo 'AppImage is incomplete: missing Qt xcb platform plugin in usr/plugins/platforms' >&2
  exit 1
fi
IFS=';' read -r -a extra_platforms <<< "$EXTRA_PLATFORM_PLUGINS"
for platform in "${extra_platforms[@]}"; do
  [[ -z "$platform" ]] && continue
  if [[ ! -f "$appdir/usr/plugins/platforms/$platform" ]]; then
    echo "AppImage is incomplete: missing platform plugin $platform" >&2
    exit 1
  fi
done
test -x "$appdir/usr/bin/kioworker"
for required in multimedia/libffmpegmediaplugin.so kf6/kio/kio_file.so kf6/kio/kio_trash.so \
    wayland-graphics-integration-client/libqt-plugin-wayland-egl.so wayland-shell-integration/libxdg-shell.so; do
  [[ -f "$appdir/usr/plugins/$required" ]] || { echo "AppImage is incomplete: missing $required" >&2; exit 1; }
done
# Keep the advertised baseline honest: every bundled ELF must run on glibc
# LURVIKO_APPIMAGE_MAX_GLIBC (2.39 = Ubuntu 24.04 / Linux Mint 22).
max_glibc="${LURVIKO_APPIMAGE_MAX_GLIBC:-2.39}"
if command -v objdump >/dev/null; then
  required="$(find "$appdir" -type f \( -name '*.so*' -o -perm -u+x \) -exec objdump -T {} + 2>/dev/null |
    grep -oE 'GLIBC_2\.[0-9]+' | sed 's/GLIBC_//' | sort -t. -k2 -n -u | tail -1)"
  if [[ -n "$required" && "$(printf '%s\n%s\n' "$required" "$max_glibc" | sort -t. -k2 -n | tail -1)" != "$max_glibc" ]]; then
    echo "AppImage needs glibc $required, newer than the supported baseline $max_glibc" >&2
    exit 1
  fi
  echo "AppImage glibc requirement: ${required:-unknown} (baseline $max_glibc)" >&2
fi
echo "AppImage: $output/$LDAI_OUTPUT (update information: ${LDAI_UPDATE_INFORMATION:-none})" >&2
