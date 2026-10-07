#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
install_prefix="${GFILE_INSTALL_PREFIX:-$HOME/.local}"

cd "$project_dir"

# Qt embeds these files in the executable. Touching them also handles editors or
# copy tools that preserve an old modification date.
find assets/icons -maxdepth 1 -type f \( -name '*.svg' -o -name '*.png' \) -exec touch {} +

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix "$install_prefix"

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$install_prefix/share/applications"
fi

printf 'g-File ikonları yenilendi ve %s dizinine kuruldu.\n' "$install_prefix"
printf 'Açık g-File pencerelerini kapatıp uygulamayı yeniden başlatın.\n'
