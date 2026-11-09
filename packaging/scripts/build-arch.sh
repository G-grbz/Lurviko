#!/usr/bin/env bash
# Build the prebuilt Arch Linux package (pkg.tar.zst) from this checkout.
# Usage (as root in a disposable archlinux container):
#   bash packaging/scripts/build-arch.sh [output-directory]
set -euo pipefail
repo="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
output="${1:-${repo}/dist}"
mkdir -p "$output"
output="$(cd "$output" && pwd)"
[[ $EUID -eq 0 ]] || { echo 'Run as root inside a disposable Arch container (makepkg runs as a build user).' >&2; exit 2; }
pacman -Syu --noconfirm --needed base-devel git sudo >/dev/null
# Install depends + makedepends declared by the recipe itself.
mapfile -t packages < <(bash -c 'source "$1"; printf "%s\n" "${depends[@]}" "${makedepends[@]}"' _ "$repo/packaging/arch/PKGBUILD" | sed -E 's/[<>=].*//')
pacman -S --noconfirm --needed "${packages[@]}" >/dev/null
id builder >/dev/null 2>&1 || useradd -m builder
work="$(mktemp -d /tmp/lurviko-arch.XXXXXX)"
cp "$repo/packaging/arch/PKGBUILD" "$repo/packaging/arch/lurviko.install" "$work/"
mkdir -p "$work/out"
chown -R builder "$work"
su builder -c "cd '$work' && LURVIKO_SOURCE_DIR='$repo' PKGDEST='$work/out' MAKEFLAGS='-j${LURVIKO_PACKAGE_JOBS:-2}' makepkg --nocheck --noconfirm --cleanbuild"
shopt -s nullglob
built=("$work"/out/lurviko-*.pkg.tar.zst)
(( ${#built[@]} == 1 )) || { echo "Expected exactly one Arch package, got: ${built[*]}" >&2; exit 1; }
# Copy as root so the output directory keeps its owner (CI workspace, bind mounts).
install -m 0644 "${built[0]}" "$output/"
files=("$output/${built[0]##*/}")
pacman -Qip "${files[0]}"
listing="$(pacman -Qlp "${files[0]}")"
grep -q ' /usr/bin/lurviko$' <<< "$listing"
grep -q ' /usr/share/applications/lurviko.desktop$' <<< "$listing"
if grep -q ' /usr/share/dbus-1/services/org.freedesktop.FileManager1.service$' <<< "$listing"; then
  echo 'Unsafe system-wide FileManager1 service in package' >&2; exit 1
fi
binary="$work/pkg/lurviko/usr/bin/lurviko"
[[ -x "$binary" ]] || { echo "Packaged binary not found for inspection: $binary" >&2; exit 1; }
if readelf -V "$binary" | grep -q PRIVATE_API; then
  echo 'Package binary depends on private Qt API; it would break on Qt updates' >&2; exit 1
fi
echo "Arch package: ${files[0]}"
