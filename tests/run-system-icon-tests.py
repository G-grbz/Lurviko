#!/usr/bin/env python3
"""Build and test asynchronous system icon rendering without personal settings."""
import os
from pathlib import Path
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="gfile-icon-tests-") as temporary:
    root = Path(temporary)
    build = root / "build"
    (root / "config").mkdir()
    (root / "config" / "kdeglobals").write_text("[Icons]\nTheme=breeze\n")
    subprocess.run(["cmake", "-S", str(repo / "tests/system-icons"), "-B", str(build)], check=True)
    subprocess.run(["cmake", "--build", str(build), "-j4"], check=True)
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen",
               XDG_CONFIG_HOME=str(root / "config"), XDG_DATA_HOME=str(root / "data"),
               XDG_CACHE_HOME=str(root / "cache"))
    subprocess.run([str(build / "system-icon-tests")], env=env, check=True, timeout=30)
