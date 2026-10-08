#!/usr/bin/env python3
"""Run migration and update checks with real native objects and temporary user data."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[1]
build = Path(os.environ.get("LURVIKO_TEST_BUILD_DIR", repo / "build"))
with tempfile.TemporaryDirectory(prefix="lurviko-branding-tests-") as temporary:
    root = Path(temporary)
    commands = subprocess.check_output(["ninja", "-C", str(build), "-t", "commands", "lurviko"], text=True).splitlines()
    args = next(shlex.split(line) for line in commands if " -c " in line and line.endswith("/src/main.cpp"))
    args = args[:args.index("-MD")] + ["-I" + str(repo / "src"), "-o", str(root / "main.o"),
                                    "-c", str(repo / "tests/branding/branding_integration.cpp")]
    subprocess.run(args, cwd=build, check=True)
    link = shlex.split(next(line for line in commands if " -o lurviko " in line))
    link = [arg for arg in link if arg not in (":", "&&") and not arg.startswith("-Wl,--dependency-file=")]
    link = [str(root / "main.o") if arg == "CMakeFiles/lurviko.dir/src/main.cpp.o" else arg for arg in link]
    link[link.index("-o") + 1] = str(root / "branding-tests")
    subprocess.run(link, cwd=build, check=True)
    runtime = root / "runtime"
    runtime.mkdir(mode=0o700)
    environment = dict(os.environ, QT_QPA_PLATFORM="offscreen", XDG_CONFIG_HOME=str(root / "config"),
                       XDG_DATA_HOME=str(root / "data"), XDG_CACHE_HOME=str(root / "cache"), XDG_RUNTIME_DIR=str(runtime))
    subprocess.run([str(root / "branding-tests")], env=environment, check=True, timeout=60)
