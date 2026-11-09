#!/usr/bin/env bash
# Build dependencies for the .rpm (Fedora). Run as root in a fedora container.
set -euo pipefail
dnf -y install \
  gcc-c++ cmake ninja-build extra-cmake-modules git rpm-build file findutils \
  qt6-qtbase-devel qt6-qtbase-private-devel qt6-qtdeclarative-devel qt6-qtmultimedia-devel \
  qt6-qtnetworkauth-devel qt6-qtsvg-devel qt6-qtwayland \
  kf6-kio-devel kf6-kservice-devel kf6-kwallet-devel kf6-kiconthemes-devel \
  kf6-karchive-devel kf6-kwindowsystem-devel openssl-devel
