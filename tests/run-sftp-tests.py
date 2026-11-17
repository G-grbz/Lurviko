#!/usr/bin/env python3
"""SFTP start page: SSH config/known_hosts parsing, mDNS discovery, address validation."""
import os
from pathlib import Path
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="lurviko-sftp-tests-") as temporary:
    root = Path(temporary)
    build = root / "build"
    subprocess.run(["cmake", "-S", str(repo / "tests/sftp"), "-B", str(build), "-G", "Ninja"], check=True)
    subprocess.run(["cmake", "--build", str(build), "--parallel", "2"], check=True)
    env = dict(os.environ, XDG_CONFIG_HOME=str(root / "config"), XDG_CACHE_HOME=str(root / "cache"))
    subprocess.run([str(build / "sftp-tests")], env=env, check=True, timeout=120)
