#!/usr/bin/env bash
# Build dependencies for the .deb and the AppImage (Debian 13 "trixie", Qt 6.8).
# Run as root inside a disposable debian:trixie container.
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends \
  build-essential cmake ninja-build extra-cmake-modules pkg-config git ca-certificates file \
  dpkg-dev patchelf squashfs-tools zsync curl binutils \
  qt6-base-dev qt6-base-private-dev qt6-declarative-dev qt6-declarative-private-dev \
  qt6-multimedia-dev libqt6networkauth6-dev qt6-svg-dev qt6-svg-plugins qt6-wayland \
  qt6-tools-dev qt6-tools-dev-tools libqt6sql6-sqlite \
  libkf6kio-dev libkf6service-dev libkf6wallet-dev libkf6iconthemes-dev \
  libkf6archive-dev libkf6windowsystem-dev kio6 libssl-dev libfribidi0 \
  libxcb-cursor0 libxkbcommon-x11-0 \
  qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-layouts \
  qml6-module-qtquick-window qml6-module-qtquick-templates qml6-module-qtquick-effects \
  qml6-module-qtquick-dialogs qml6-module-qtquick-shapes qml6-module-qtqml-models \
  qml6-module-qtqml-workerscript qml6-module-qtcore qml6-module-qtmultimedia
