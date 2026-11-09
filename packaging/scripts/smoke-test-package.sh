#!/usr/bin/env bash
# Install a built Lurviko package on a *clean* matching system and launch it
# under a virtual X server. Run as root in a disposable container:
#   debian:trixie   -> .deb          fedora:43     -> .rpm
#   archlinux:base  -> .pkg.tar.zst  any glibc>=2.39 distro -> .AppImage
# It is launched twice, on X11 (Xvfb) and on Wayland (headless weston with the
# GL renderer), because each platform loads different Qt plugins. Passes when
# the application is still running after the timeout and its log shows none of
# the known fatal startup problems.
set -euo pipefail
package="$(realpath "${1:?Usage: smoke-test-package.sh <package-file>}")"
seconds="${LURVIKO_SMOKE_SECONDS:-25}"

install_runtime() {
  # Base desktop pieces every Linux desktop has (and AppImages expect from the
  # host): X server for the test, D-Bus, GL, fonts, HarfBuzz, xcb-cursor.
  if command -v apt-get >/dev/null; then
    export DEBIAN_FRONTEND=noninteractive
    apt-get update >/dev/null
    apt-get install -y --no-install-recommends xvfb xauth dbus dbus-x11 weston libgl1 libegl1 libegl-mesa0 libgl1-mesa-dri libopengl0 \
      fontconfig fonts-dejavu-core libharfbuzz0b libxkbcommon-x11-0 libxcb-cursor0 >/dev/null
  elif command -v dnf >/dev/null; then
    dnf -y -q install xorg-x11-server-Xvfb xorg-x11-xauth dbus-daemon dbus-tools weston mesa-libEGL mesa-libGL mesa-dri-drivers \
      libglvnd-opengl fontconfig dejavu-sans-fonts harfbuzz libxkbcommon-x11 xcb-util-cursor which >/dev/null
  elif command -v pacman >/dev/null; then
    pacman -Syu --noconfirm --needed xorg-server-xvfb xorg-xauth dbus weston mesa libglvnd fontconfig ttf-dejavu \
      harfbuzz libxkbcommon-x11 xcb-util-cursor >/dev/null
  else
    echo 'Unsupported test system' >&2; exit 2
  fi
}

case "$package" in
  *.deb)
    install_runtime
    apt-get install -y "$package" >/dev/null
    command=(lurviko) ;;
  *.rpm)
    install_runtime
    dnf -y -q install "$package" >/dev/null
    command=(lurviko) ;;
  *.pkg.tar.zst)
    install_runtime
    pacman -U --noconfirm "$package" >/dev/null
    command=(lurviko) ;;
  *.AppImage)
    install_runtime
    install -m 0755 "$package" /opt/Lurviko.AppImage
    command=(env APPIMAGE_EXTRACT_AND_RUN=1 /opt/Lurviko.AppImage) ;;
  *) echo "Unknown package type: $package" >&2; exit 2 ;;
esac

id lurviko-smoke >/dev/null 2>&1 || useradd -m lurviko-smoke
fatal='error while loading shared libraries|Could not load the Qt platform plugin|failed to start|is not installed|QQmlApplicationEngine failed|No QtMultimedia backends found|Failed to load client buffer integration|Failed to create RHI|Failed to initialize graphics backend|Segmentation fault|core dumped'

launch() {
  local platform="$1" log="/tmp/lurviko-smoke-$1.log" runtime="/tmp/lurviko-smoke-runtime-$1" status
  mkdir -p "$runtime"; chown lurviko-smoke "$runtime"; chmod 700 "$runtime"
  # A standalone runner avoids nested shell quoting; it runs as the test user.
  local runner="/tmp/lurviko-smoke-$platform.sh"
  {
    printf '#!/bin/bash\ncd ~\nexport XDG_RUNTIME_DIR=%q LANG=C.UTF-8\n' "$runtime"
    printf 'command=('; printf '%q ' "${command[@]}"; printf ')\n'
    printf 'seconds=%q\n' "$seconds"
    if [[ $platform == x11 ]]; then
      echo 'exec timeout "$seconds" xvfb-run -a dbus-run-session -- env QT_QPA_PLATFORM=xcb "${command[@]}"'
    else
      # Headless compositor with real EGL/GL (Mesa llvmpipe): the same client
      # side path as a Plasma or GNOME Wayland session.
      cat <<'RUNNER'
weston --backend=headless --renderer=gl --socket=lurviko-smoke --idle-time=0 >/tmp/lurviko-weston.log 2>&1 &
compositor=$!
for _ in $(seq 300); do [[ -S "$XDG_RUNTIME_DIR/lurviko-smoke" ]] && break; sleep 0.1; done
if [[ ! -S "$XDG_RUNTIME_DIR/lurviko-smoke" ]]; then
  echo 'weston did not start'; cat /tmp/lurviko-weston.log; kill "$compositor" 2>/dev/null; exit 3
fi
WAYLAND_DISPLAY=lurviko-smoke QT_QPA_PLATFORM=wayland timeout "$seconds" dbus-run-session -- "${command[@]}"
status=$?
kill "$compositor" 2>/dev/null
exit "$status"
RUNNER
    fi
  } > "$runner"
  chmod 0755 "$runner"
  set +e
  su lurviko-smoke -s /bin/bash -c "$runner" >"$log" 2>&1
  status=$?
  set -e
  echo "--- $platform log (unique lines) ---"
  sort "$log" | uniq -c | sort -rn | head -30
  if grep -qE "$fatal" "$log"; then
    echo "FAIL ($platform): fatal startup problem in the log" >&2
    grep -E "$fatal" "$log" | head -10 >&2
    exit 1
  fi
  if [[ $status -ne 124 ]]; then
    echo "FAIL ($platform): Lurviko exited with status $status before the ${seconds}s smoke window ended" >&2
    exit 1
  fi
  echo "ok ($platform)"
}
launch x11
launch wayland
echo "PASS: ${package##*/} installed and kept running for ${seconds}s on X11 and Wayland"
