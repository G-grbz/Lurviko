#!/usr/bin/env python3
"""Verify that unused allocator pages return to the OS without losing live data."""
from pathlib import Path
import subprocess
import tempfile

source = Path(__file__).resolve().parent / "memory"
with tempfile.TemporaryDirectory(prefix="lurviko-memory-tests-") as temporary:
    subprocess.run(["cmake", "-S", str(source), "-B", temporary, "-G", "Ninja"], check=True)
    subprocess.run(["cmake", "--build", temporary, "--parallel", "2"], check=True)
    subprocess.run([str(Path(temporary) / "memory-tests")], check=True, timeout=30)
