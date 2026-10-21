#!/usr/bin/env python3
"""Verify real Ark MIME drops, merge counts, result focus and feedback."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import errno
import time
from contextlib import contextmanager


@contextmanager
def isolated_directory():
    directory = tempfile.TemporaryDirectory(prefix="lurviko-archive-drop-tests-")
    try:
        yield directory.name
    finally:
        # Desktop services on the private bus can finish flushing their
        # settings after the bus exits. Retry only this cleanup race, never
        # swallow test failures or permission errors.
        for attempt in range(5):
            try:
                directory.cleanup()
                break
            except OSError as error:
                if error.errno != errno.ENOTEMPTY or attempt == 4:
                    raise
                time.sleep(0.1 * (2 ** attempt))

repo = Path(__file__).resolve().parents[1]
build = Path(os.environ.get("LURVIKO_TEST_BUILD_DIR", str(repo / "build"))).resolve()
with isolated_directory() as temporary:
    root = Path(temporary)
    main = (repo / "src/main.cpp").read_text()
    includes = "\n".join(line for line in main.splitlines() if line.startswith('#include "'))
    registration = main[main.index('    qmlRegisterType<StorageModel>'):
                        main.index('    const QStringList launchLocations')]
    (root / "backend_test_types.h").write_text(includes)
    source = (repo / "tests/transfers/archive_drop_integration.cpp").read_text()
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
    link[link.index("-o") + 1] = str(root / "archive-drop-tests")
    link.append("-lQt6Test")
    subprocess.run(link, cwd=build, check=True)
    environment = dict(os.environ, QT_QPA_PLATFORM=os.environ.get("LURVIKO_NAVIGATION_QPA", "offscreen"),
        XDG_CONFIG_HOME=str(root / "config"), XDG_DATA_HOME=str(root / "data"),
        GIO_USE_VFS="local",
        QT_NO_XDG_DESKTOP_PORTAL="1",
        XDG_CACHE_HOME=str(root / "cache"), LURVIKO_ARCHIVE_DROP_TEST_ROOT=str(root))
    if environment["QT_QPA_PLATFORM"] == "offscreen":
        environment["QT_QUICK_BACKEND"] = "software"
    subprocess.run(["dbus-run-session", "--", str(root / "archive-drop-tests")], env=environment, check=True, timeout=90)
