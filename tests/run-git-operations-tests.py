#!/usr/bin/env python3
"""Git management against isolated working repositories and local bare remotes."""
import os
from pathlib import Path
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="lurviko-git-operations-") as temporary:
    root = Path(temporary)
    build = root / "build"
    subprocess.run(["cmake", "-S", str(repo / "tests/git-operations"), "-B", str(build), "-G", "Ninja"], check=True)
    subprocess.run(["cmake", "--build", str(build), "--parallel", "2"], check=True)
    env = dict(os.environ, XDG_CONFIG_HOME=str(root / "config"), XDG_CACHE_HOME=str(root / "cache"),
               GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=os.devnull, GIT_TERMINAL_PROMPT="0")
    subprocess.run([str(build / "git-operations-tests")], env=env, check=True, timeout=240)
