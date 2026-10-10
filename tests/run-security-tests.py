#!/usr/bin/env python3
"""Native KDF, path, memory-storage and real bsdtar operand regressions."""
from pathlib import Path
import subprocess
import tempfile

source = Path(__file__).resolve().parent / "security"
with tempfile.TemporaryDirectory(prefix="lurviko-security-tests-") as temporary:
    subprocess.run(["cmake", "-S", str(source), "-B", temporary, "-G", "Ninja"], check=True)
    subprocess.run(["cmake", "--build", temporary, "--parallel", "2"], check=True)
    subprocess.run([str(Path(temporary) / "security-tests")], check=True, timeout=30)
