#!/usr/bin/env python3
"""Exercise real TMDB callback lifetimes with fake HTTP and AddressSanitizer."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[1]
build = Path(os.environ.get("LURVIKO_TEST_BUILD_DIR", repo / "build"))
with tempfile.TemporaryDirectory(prefix="lurviko-tmdb-tests-") as temporary:
    root = Path(temporary)
    commands = subprocess.check_output(
        ["ninja", "-C", str(build), "-t", "commands", "lurviko"], text=True).splitlines()
    original = next(shlex.split(line) for line in commands if " -c " in line and line.endswith("/src/main.cpp"))
    flags = original[:original.index("-MD")] + ["-I" + str(repo / "src"),
        "-fsanitize=address", "-fno-omit-frame-pointer", "-g", "-O1"]
    for filename, source in [("main", repo / "tests/tmdb/lookup_lifetime.cpp"),
                             ("tmdb", repo / "src/tmdbmetadatamanager.cpp")]:
        subprocess.run(flags + ["-o", str(root / (filename + ".o")), "-c", str(source)], cwd=build, check=True)
    link = shlex.split(next(line for line in commands if " -o lurviko " in line))
    link = [arg for arg in link if arg not in (":", "&&") and not arg.startswith("-Wl,--dependency-file=")]
    substitutions = {"CMakeFiles/lurviko.dir/src/main.cpp.o": str(root / "main.o"),
                     "CMakeFiles/lurviko.dir/src/tmdbmetadatamanager.cpp.o": str(root / "tmdb.o")}
    link = [substitutions.get(arg, arg) for arg in link]
    link[link.index("-o") + 1] = str(root / "tmdb-tests")
    subprocess.run(link + ["-fsanitize=address"], cwd=build, check=True)
    env = dict(os.environ, XDG_CONFIG_HOME=str(root / "config"), XDG_DATA_HOME=str(root / "data"),
               XDG_CACHE_HOME=str(root / "cache"), TMDB_API_TOKEN="fixture-token",
               ASAN_OPTIONS="detect_leaks=0:halt_on_error=1")
    subprocess.run([str(root / "tmdb-tests")], env=env, check=True, timeout=60)
