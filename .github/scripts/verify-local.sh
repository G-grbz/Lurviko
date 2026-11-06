#!/usr/bin/env bash
# Run the same configure/build/test gates used by CI on a local Linux machine.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.."

for executable in cmake ninja python3 xvfb-run dbus-run-session; do
    if ! command -v "$executable" >/dev/null 2>&1; then
        echo "Missing dependency: $executable" >&2
        exit 2
    fi
done

if [ -d .git ]; then
    git diff --check
fi
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel "${LURVIKO_BUILD_JOBS:-2}"
bash .github/scripts/run-headless-tests.sh

echo "PASS: configure, build and all CI headless regression tests."
