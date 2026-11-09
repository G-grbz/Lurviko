#!/usr/bin/env bash
# Download linuxdeploy and its Qt/AppImage plugins into a tool directory and
# put them on PATH (via symlinks in the same directory).
# Usage: packaging/scripts/fetch-appimage-tools.sh <tool-dir>
# Set LINUXDEPLOY_RELEASE to a tag (default: continuous) to pin the tools.
set -euo pipefail
tools="$(mkdir -p "${1:?Usage: fetch-appimage-tools.sh <tool-dir>}" && cd "$1" && pwd)"
release="${LINUXDEPLOY_RELEASE:-continuous}"
fetch() {
  local project="$1" file="$2"
  if [[ ! -s "$tools/$file" ]]; then
    curl -fsSL --retry 3 -o "$tools/$file.part" \
      "https://github.com/linuxdeploy/$project/releases/download/$release/$file"
    mv "$tools/$file.part" "$tools/$file"
  fi
  chmod +x "$tools/$file"
  # Every tool is itself an AppImage; refuse anything else (e.g. an HTML error page).
  head -c 4 "$tools/$file" | grep -q $'\x7fELF' || { echo "Not an executable: $file" >&2; exit 1; }
}
fetch linuxdeploy linuxdeploy-x86_64.AppImage
fetch linuxdeploy-plugin-qt linuxdeploy-plugin-qt-x86_64.AppImage
fetch linuxdeploy-plugin-appimage linuxdeploy-plugin-appimage-x86_64.AppImage
ln -sf linuxdeploy-x86_64.AppImage "$tools/linuxdeploy"
ln -sf linuxdeploy-plugin-qt-x86_64.AppImage "$tools/linuxdeploy-plugin-qt"
ln -sf linuxdeploy-plugin-appimage-x86_64.AppImage "$tools/linuxdeploy-plugin-appimage"
echo "$tools"
