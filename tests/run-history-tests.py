#!/usr/bin/env python3
"""Check persistent recent locations across temporary filesystem failures."""
from pathlib import Path
import subprocess
import tempfile

source = Path(__file__).resolve().parent / "navigation"
with tempfile.TemporaryDirectory(prefix="lurviko-history-tests-") as temporary:
    subprocess.run(["cmake", "-S", str(source), "-B", temporary, "-G", "Ninja"], check=True)
    subprocess.run(["cmake", "--build", temporary, "--parallel", "2"], check=True)
    subprocess.run([str(Path(temporary) / "history-tests")], check=True, timeout=30)
