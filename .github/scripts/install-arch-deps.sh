#!/usr/bin/env bash
set -euo pipefail

# Called only inside the disposable Arch Linux CI container.
pacman -S --noconfirm --needed \
    base-devel cmake ninja extra-cmake-modules \
    qt6-base qt6-declarative qt6-multimedia qt6-networkauth qt6-svg \
    kio kservice kwallet kiconthemes karchive kwindowsystem \
    openssl python breeze-icons desktop-file-utils curl unzip zstd ffmpeg
