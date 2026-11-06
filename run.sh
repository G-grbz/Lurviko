#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT_DIR/build"
BINARY="$BUILD_DIR/lurviko"

build() {
    cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build "$BUILD_DIR"
}

case "${1:-}" in
    --build|-b)
        build
        shift
        ;;
    --rebuild)
        rm -rf "$BUILD_DIR"
        build
        shift
        ;;
    --help|-h)
        cat <<'HELP'
Usage: ./run.sh [--build|-b|--rebuild] [lurviko arguments...]

Without options, run the existing development binary immediately.
If build/lurviko does not exist, it is built once automatically.

  --build, -b   Incrementally configure/build, then run
  --rebuild     Remove build/ and build from scratch, then run
HELP
        exit 0
        ;;
esac

if [[ ! -x "$BINARY" ]]; then
    build
fi

exec "$BINARY" "$@"
