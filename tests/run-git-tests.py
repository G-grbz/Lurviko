#!/usr/bin/env python3
"""Bounded porcelain parsing and real asynchronous Git repository regressions."""
from pathlib import Path
import subprocess
import tempfile

source = Path(__file__).resolve().parent / "git"
with tempfile.TemporaryDirectory(prefix="lurviko-git-tests-") as temporary:
    subprocess.run(["cmake", "-S", str(source), "-B", temporary, "-G", "Ninja"], check=True)
    subprocess.run(["cmake", "--build", temporary, "--parallel", "2"], check=True)
    subprocess.run([str(Path(temporary) / "git-tests")], check=True, timeout=60)
