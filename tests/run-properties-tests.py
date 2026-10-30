#!/usr/bin/env python3
"""Check local POSIX permissions and the real properties/grid UI with isolated fixtures."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import shutil

repo = Path(__file__).resolve().parents[1]
build = Path(os.environ.get("LURVIKO_TEST_BUILD_DIR", str(repo / "build"))).resolve()
with tempfile.TemporaryDirectory(prefix="lurviko-properties-tests-") as temporary:
    root = Path(temporary)
    (root / "bin").mkdir()
    (root / "libexec").mkdir()
    shutil.copy2(build / "lurviko-access-helper", root / "libexec/lurviko-access-helper")
    # Exercise the real helper and transport as this test user. Authentication
    # itself is checked separately with the desktop polkit agent, never faked
    # in production. This process has no additional permissions.
    authorization = root / "bin/pkexec"
    authorization.write_text('#!/bin/sh\n[ "$1" = "--disable-internal-agent" ] || exit 127\nshift\nexec "$@"\n')
    authorization.chmod(0o700)
    main = (repo / "src/main.cpp").read_text()
    includes = "\n".join(line for line in main.splitlines() if line.startswith('#include "'))
    registration = main[main.index('    qmlRegisterType<StorageModel>'):
                        main.index('    const QStringList launchLocations')]
    (root / "backend_test_types.h").write_text(includes)
    source = (repo / "tests/properties/properties_integration.cpp").read_text()
    (root / "main.cpp").write_text(source.replace("// REGISTER_BACKEND_TYPES", registration))
    commands = subprocess.check_output(
        ["ninja", "-C", str(build), "-t", "commands", "lurviko"], text=True).splitlines()
    args = next(shlex.split(line) for line in commands
                if " -c " in line and line.endswith("/src/main.cpp"))
    args = args[:args.index("-MD")] + ["-I" + str(repo / "src"),
        "-I" + str(root), "-o", str(root / "main.o"), "-c", str(root / "main.cpp")]
    subprocess.run(args, cwd=build, check=True)
    link = shlex.split(next(line for line in commands if " -o lurviko " in line))
    link = [arg for arg in link if arg not in (":", "&&")
            and not arg.startswith("-Wl,--dependency-file=")]
    link = [str(root / "main.o") if arg == "CMakeFiles/lurviko.dir/src/main.cpp.o" else arg
            for arg in link]
    link[link.index("-o") + 1] = str(root / "bin/properties-tests")
    subprocess.run(link, cwd=build, check=True)
    environment = dict(os.environ, QT_QPA_PLATFORM=os.environ.get("LURVIKO_PROPERTIES_QPA", "offscreen"),
        XDG_CONFIG_HOME=str(root / "config"), XDG_DATA_HOME=str(root / "data"),
        XDG_CACHE_HOME=str(root / "cache"), LURVIKO_PROPERTIES_TEST_ROOT=str(root),
        PULSE_SERVER="unix:" + str(root / "no-pulse-server"))
    environment["PATH"] = str(root / "bin") + os.pathsep + os.environ["PATH"]
    if environment["QT_QPA_PLATFORM"] == "offscreen":
        environment["QT_QUICK_BACKEND"] = "software"
    # Includes several CPU-budgeted 80,000-file scans and the real dialog flow.
    subprocess.run([str(root / "bin/properties-tests")], env=environment, check=True, timeout=120)
