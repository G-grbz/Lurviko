#!/usr/bin/env python3
"""Drive Lurviko's xdg-desktop-portal FileChooser backend over a private D-Bus
session: OpenFile (filters, choices, multiple, folders), SaveFile (overwrite
confirmation), SaveFiles, cancellation and Request.Close."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[1]
build = Path(os.environ.get("LURVIKO_TEST_BUILD_DIR", repo / "build")).resolve()
with tempfile.TemporaryDirectory(prefix="lurviko-portal-tests-") as temporary:
    root = Path(temporary)
    main = (repo / "src/main.cpp").read_text()
    includes = "\n".join(line for line in main.splitlines() if line.startswith('#include "'))
    registration = main[main.index('    qmlRegisterType<StorageModel>'):
                        main.index('    const QStringList launchLocations')]
    (root / "backend_test_types.h").write_text(includes)
    source = (repo / "tests/portal/portal_integration.cpp").read_text()
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
    link[link.index("-o") + 1] = str(root / "portal-tests")
    subprocess.run(link, cwd=build, check=True)
    runtime = root / "runtime"
    runtime.mkdir(mode=0o700)
    environment = dict(os.environ, QT_QPA_PLATFORM=os.environ.get("LURVIKO_PORTAL_QPA", "offscreen"),
        QT_QUICK_BACKEND="software", XDG_CONFIG_HOME=str(root / "config"),
        XDG_DATA_HOME=str(root / "data"), XDG_CACHE_HOME=str(root / "cache"),
        XDG_RUNTIME_DIR=str(runtime), LURVIKO_PORTAL_TEST_ROOT=str(root))
    subprocess.run(["timeout", "-k", "5", "120", "dbus-run-session", "--", str(root / "portal-tests")],
                   env=environment, check=True)
