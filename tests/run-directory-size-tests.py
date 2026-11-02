#!/usr/bin/env python3
"""Persistent shared size index: deltas, restart recovery and real filesystem events."""
from pathlib import Path
import os
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="lurviko-size-index-tests-") as temporary:
    build = Path(temporary) / "build"
    subprocess.run(["cmake", "-S", str(repo / "tests/directory-size"), "-B", str(build),
                    "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release"], check=True)
    subprocess.run(["cmake", "--build", str(build), "--parallel", "2"], check=True)
    environment = dict(os.environ, XDG_CONFIG_HOME=temporary + "/config",
                       XDG_DATA_HOME=temporary + "/data", XDG_CACHE_HOME=temporary + "/cache")
    subprocess.run([str(build / "directory-size-tests")], env=environment, check=True, timeout=180)
