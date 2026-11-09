#!/usr/bin/env bash
set -euo pipefail
# Usage: packaging/scripts/build-native.sh deb|rpm [output-directory]
# Run this on the *target distribution* with Qt >= 6.8, KF6 and OpenSSL >= 3.2.
kind="${1:-}"
case "$kind" in deb) generator=DEB; command -v dpkg-deb >/dev/null || { echo 'dpkg-deb missing' >&2; exit 2; };;
               rpm) generator=RPM; command -v rpmbuild >/dev/null || { echo 'rpmbuild missing' >&2; exit 2; };;
               *) echo 'Usage: build-native.sh deb|rpm [output-directory]' >&2; exit 2;; esac
repo="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
output="${2:-${repo}/dist}"
mkdir -p "$output"
output="$(cd "$output" && pwd)"
work="${LURVIKO_PACKAGING_WORK:-${repo}/build-package-${kind}}"
# Avoid modifying or reusing the developer build directory.
cmake -S "$repo" -B "$work" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
  -DLURVIKO_PACKAGED_BUILD=ON \
  -DLURVIKO_INSTALL_SYSTEM_FILEMANAGER1=OFF \
  -DLURVIKO_ENABLE_NATIVE_PACKAGES=ON \
  -DLURVIKO_QML_BYTECODE_ONLY="${LURVIKO_QML_BYTECODE_ONLY:-ON}" \
  -DCMAKE_INSTALL_LIBEXECDIR=lib/lurviko
cmake --build "$work" --parallel "${LURVIKO_PACKAGE_JOBS:-2}"
# CPack's staging tree stays in the work directory; only packages reach $output.
rm -rf "$work/cpack"
(cd "$work" && cpack -G "$generator" -B "$work/cpack" --verbose)
shopt -s nullglob
staged=("$work"/cpack/lurviko*."$kind")
(( ${#staged[@]} > 0 )) || { echo "CPack created no .$kind package" >&2; exit 1; }
cp -f "${staged[@]}" "$output/"
shopt -u nullglob
# Check files and metadata; this does not replace installation/smoke tests.
shopt -s nullglob
if [[ $kind == deb ]]; then
  files=("$output"/lurviko*.deb)
  (( ${#files[@]} > 0 )) || { echo 'No DEB was created' >&2; exit 1; }
  for f in "${files[@]}"; do
    dpkg-deb -I "$f"
    listing="$(dpkg-deb -c "$f")"
    grep -qE 'usr/bin/lurviko$' <<< "$listing"
    grep -qE 'usr/share/applications/lurviko.desktop$' <<< "$listing"
    if grep -qE 'usr/share/dbus-1/services/org.freedesktop.FileManager1.service$' <<< "$listing"; then
      echo 'Unsafe system-wide FileManager1 service in package' >&2; exit 1
    fi
  done
else
  files=("$output"/lurviko*.rpm)
  (( ${#files[@]} > 0 )) || { echo 'No RPM was created' >&2; exit 1; }
  for f in "${files[@]}"; do
    listing="$(rpm -qpl "$f")"
    grep -q '/usr/bin/lurviko$' <<< "$listing"
    rpm -qp --requires "$f"
    if grep -q '^/usr/share/dbus-1/services/org.freedesktop.FileManager1.service$' <<< "$listing"; then
      echo 'Unsafe system-wide FileManager1 service in package' >&2; exit 1
    fi
  done
fi
printf '\nPackage candidate created in %s — test installation on a clean %s machine.\n' "$output" "$kind"
