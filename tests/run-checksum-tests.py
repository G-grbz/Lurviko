#!/usr/bin/env python3
"""Checksum calculation, recognition of pasted values and verification files."""
import os
from pathlib import Path
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="lurviko-checksum-tests-") as temporary:
    root = Path(temporary)
    build = root / "build"
    subprocess.run(["cmake", "-S", str(repo / "tests/checksums"), "-B", str(build), "-G", "Ninja"], check=True)
    subprocess.run(["cmake", "--build", str(build), "--parallel", "2"], check=True)
    env = dict(os.environ, XDG_CONFIG_HOME=str(root / "config"), XDG_CACHE_HOME=str(root / "cache"))
    subprocess.run([str(build / "checksum-tests")], env=env, check=True, timeout=120)
