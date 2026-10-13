#!/usr/bin/env python3
"""Verify the actual build's baked folder resources and bounded pixel cache."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[1]
build = Path(os.environ.get("LURVIKO_TEST_BUILD_DIR", str(repo / "build"))).resolve()
objects = [build / "CMakeFiles/lurviko.dir/src/bundlediconprovider.cpp.o"]
for name in ("folder_rasters", "lurviko_raw_res_0"):
    candidates = list((build / "CMakeFiles/lurviko.dir").rglob(f"qrc_{name}.cpp.o"))
    if len(candidates) != 1:
        raise SystemExit("Build Lurviko first (one folder resource object is required).")
    objects.append(candidates[0])
if not all(path.is_file() for path in objects):
    raise SystemExit("Build Lurviko first with Ninja (folder raster resources are required).")
flags = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "--libs", "Qt6Quick"], text=True))
with tempfile.TemporaryDirectory(prefix="lurviko-folder-icon-tests-") as temporary:
    executable = Path(temporary) / "folder-icon-tests"
    subprocess.run(["c++", "-std=c++17", "-O2", "-fPIC", "-pthread", "-I" + str(repo / "src"),
                    str(repo / "tests/folder-icons/folder_icons.cpp"), *map(str, objects),
                    *flags, "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True, timeout=30)
