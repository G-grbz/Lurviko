#!/usr/bin/env python3
"""Exercise actual KDE trash/restore jobs in a private session and trash directory."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[1]
build = repo / "build"
# Use the home filesystem: KDE otherwise chooses the mount's shared .Trash
# instead of XDG_DATA_HOME. All test data stays in this ignored checkout folder.
with tempfile.TemporaryDirectory(prefix="trash-qa-", dir=repo / ".tools") as temporary:
    root = Path(temporary)
    runtime = root / "runtime"
    runtime.mkdir(mode=0o700)
    main = (repo / "src/main.cpp").read_text()
    includes = "\n".join(line for line in main.splitlines() if line.startswith('#include "'))
    registration = main[main.index('    qmlRegisterType<StorageModel>'):
                        main.index('    const QStringList launchLocations')]
    (root / "backend_test_types.h").write_text(includes)
    source = (repo / "tests/trash/trash_integration.cpp").read_text()
    (root / "main.cpp").write_text(source.replace("// REGISTER_BACKEND_TYPES", registration))
    commands = subprocess.check_output(
        ["ninja", "-C", str(build), "-t", "commands", "lurviko"], text=True).splitlines()
    compile_args = next(shlex.split(line) for line in commands
                        if " -c " in line and line.endswith("/src/main.cpp"))
    compile_args = compile_args[:compile_args.index("-MD")] + ["-I" + str(repo / "src"),
        "-I" + str(root), "-o", str(root / "main.o"), "-c", str(root / "main.cpp")]
    subprocess.run(compile_args, cwd=build, check=True)
    link = shlex.split(next(line for line in commands if " -o lurviko " in line))
    link = [arg for arg in link if arg not in (":", "&&")
            and not arg.startswith("-Wl,--dependency-file=")]
    link = [str(root / "main.o") if arg == "CMakeFiles/lurviko.dir/src/main.cpp.o" else arg
            for arg in link]
    link[link.index("-o") + 1] = str(root / "trash-tests")
    subprocess.run(link, cwd=build, check=True)
    environment = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software",
        XDG_CONFIG_HOME=str(root / "config"), XDG_DATA_HOME=str(root / "data"),
        XDG_CACHE_HOME=str(root / "cache"), XDG_RUNTIME_DIR=str(runtime),
        LURVIKO_TRASH_TEST_ROOT=str(root))
    subprocess.run(["dbus-run-session", "--", str(root / "trash-tests")],
                   env=environment, check=True, timeout=90)
