#!/usr/bin/env python3
"""Exercise live category updates using the built application's real QML/backend."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[1]
build = repo / "build"
with tempfile.TemporaryDirectory(prefix="gfile-sync-tests-") as temporary:
    root = Path(temporary)
    main = (repo / "src/main.cpp").read_text()
    includes = "\n".join(line for line in main.splitlines() if line.startswith('#include "'))
    registration = main[main.index('    qmlRegisterType<StorageModel>'):
                        main.index('    const QStringList launchLocations')]
    (root / "backend_test_types.h").write_text(includes)
    source = (repo / "tests/sync/sync_integration.cpp").read_text()
    (root / "main.cpp").write_text(source.replace("// REGISTER_BACKEND_TYPES", registration))
    commands = subprocess.check_output(
        ["ninja", "-C", str(build), "-t", "commands", "g-file"], text=True).splitlines()
    args = next(shlex.split(line) for line in commands
                if " -c " in line and line.endswith("/src/main.cpp"))
    args = args[:args.index("-MD")] + ["-I" + str(repo / "src"),
        "-I" + str(root), "-o", str(root / "main.o"), "-c", str(root / "main.cpp")]
    subprocess.run(args, cwd=build, check=True)
    link = shlex.split(next(line for line in commands if " -o g-file " in line))
    link = [arg for arg in link if arg not in (":", "&&")
            and not arg.startswith("-Wl,--dependency-file=")]
    link = [str(root / "main.o") if arg == "CMakeFiles/g-file.dir/src/main.cpp.o" else arg
            for arg in link]
    link[link.index("-o") + 1] = str(root / "sync-tests")
    subprocess.run(link, cwd=build, check=True)
    # Deliberately slow main-thread disk probes: a disk wait should never
    # stop category input, even when measured CPU usage remains low.
    subprocess.run(["cc", "-shared", "-fPIC", "-o", str(root / "slow-storage.so"),
                    str(repo / "tests/sync/slow_storage.c"), "-ldl"], check=True)
    environment = dict(os.environ, QT_QPA_PLATFORM=os.environ.get("GFILE_SYNC_QPA", "offscreen"),
        XDG_CONFIG_HOME=str(root / "config"), XDG_DATA_HOME=str(root / "data"),
        XDG_CACHE_HOME=str(root / "cache"), GFILE_SYNC_TEST_ROOT=str(root),
        LD_PRELOAD=str(root / "slow-storage.so"))
    if environment["QT_QPA_PLATFORM"] == "offscreen":
        environment["QT_QUICK_BACKEND"] = "software"
    subprocess.run([str(root / "sync-tests")], env=environment, check=True, timeout=45)
