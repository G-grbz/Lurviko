#!/usr/bin/env python3
"""Exercise large searches in the real BrowserPane with isolated settings.

Requires an existing Ninja application build (cmake --build build -j4).
Reuse its libraries and generated QML resources while replacing only main().
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[1]
build = repo / "build"
with tempfile.TemporaryDirectory(prefix="gfile-search-tests-") as temporary:
    root = Path(temporary)
    main = (repo / "src/main.cpp").read_text()
    includes = "\n".join(line for line in main.splitlines() if line.startswith('#include "'))
    registration = main[main.index('    qmlRegisterType<StorageModel>'):
                        main.index('    const QStringList launchLocations')]
    source = (repo / "tests/search/search_integration.cpp").read_text()
    start = source.index('  qmlRegisterType<StorageModel>')
    end = source.index('  QQmlApplicationEngine engine;', start)
    (root / "main.cpp").write_text(includes + "\n" + source[:start] + registration + source[end:])
    commands = subprocess.check_output(
        ["ninja", "-C", str(build), "-t", "commands", "g-file"], text=True).splitlines()
    compile_args = next(shlex.split(line) for line in commands
                        if " -c " in line and line.endswith("/src/main.cpp"))
    compile_args = compile_args[:compile_args.index("-MD")] + [
        "-I" + str(repo / "src"), "-o", str(root / "main.o"), "-c",
        str(root / "main.cpp")]
    subprocess.run(compile_args, cwd=build, check=True)
    link = shlex.split(next(line for line in commands if " -o g-file " in line))
    link = [arg for arg in link if arg not in (":", "&&")
            and not arg.startswith("-Wl,--dependency-file=")]
    link = [str(root / "main.o") if arg == "CMakeFiles/g-file.dir/src/main.cpp.o" else arg
            for arg in link]
    link[link.index("-o") + 1] = str(root / "search-tests")
    subprocess.run(link, cwd=build, check=True)
    environment = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software",
                       XDG_CONFIG_HOME=str(root / "config"), XDG_DATA_HOME=str(root / "data"),
                       XDG_CACHE_HOME=str(root / "cache"), GFILE_SEARCH_TEST_ROOT=str(root))
    subprocess.run([str(root / "search-tests")], env=environment, check=True, timeout=45)
