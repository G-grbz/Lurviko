#!/usr/bin/env python3
"""Render production Main and detached music windows with isolated user data.

Set LURVIKO_APPEARANCE_QPA=xcb and LURVIKO_APPEARANCE_BACKEND=rhi under
Xvfb to exercise GPU premultiplied-alpha rendering as well as software.
"""
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
    directory = tempfile.TemporaryDirectory(prefix="lurviko-desktop-appearance-")
    try:
        yield directory.name
    finally:
        # Desktop services activated on the private bus can flush settings
        # just after that bus exits. Retry only this cleanup race; assertion,
        # process, permission and other filesystem failures still propagate.
        for attempt in range(5):
            try:
                directory.cleanup()
                break
            except OSError as error:
                if error.errno != errno.ENOTEMPTY or attempt == 4:
                    raise
                time.sleep(0.1 * (2 ** attempt))

repo = Path(__file__).resolve().parents[1]
build = Path(os.environ.get("LURVIKO_TEST_BUILD_DIR", repo / "build")).resolve()
with isolated_directory() as temporary:
    root = Path(temporary)
    home = root / "home"
    for name in ("Desktop", "Documents", "Downloads", "Music", "Pictures", "Videos"):
        (home / name).mkdir(parents=True)
    main = (repo / "src/main.cpp").read_text()
    includes = "\n".join(line for line in main.splitlines() if line.startswith('#include "'))
    registration = main[main.index("    qmlRegisterType<StorageModel>"):
                        main.index("    const QStringList launchLocations")]
    (root / "backend_test_types.h").write_text(includes)
    source = (repo / "tests/appearance/desktop_integration.cpp").read_text()
    (root / "main.cpp").write_text(source.replace("// REGISTER_BACKEND_TYPES", registration))
    commands = subprocess.check_output(["ninja", "-C", str(build), "-t", "commands", "lurviko"], text=True).splitlines()
    args = next(shlex.split(line) for line in commands if " -c " in line and line.endswith("/src/main.cpp"))
    args = args[:args.index("-MD")] + ["-I" + str(repo / "src"), "-I" + str(root),
        "-o", str(root / "main.o"), "-c", str(root / "main.cpp")]
    subprocess.run(args, cwd=build, check=True)
    link = shlex.split(next(line for line in commands if " -o lurviko " in line))
    link = [arg for arg in link if arg not in (":", "&&") and not arg.startswith("-Wl,--dependency-file=")]
    link = [str(root / "main.o") if arg == "CMakeFiles/lurviko.dir/src/main.cpp.o" else arg for arg in link]
    link[link.index("-o") + 1] = str(root / "appearance-tests")
    subprocess.run(link, cwd=build, check=True)
    environment = dict(os.environ, HOME=str(home),
        XDG_CONFIG_HOME=str(root / "config"), XDG_DATA_HOME=str(root / "data"), XDG_CACHE_HOME=str(root / "cache"),
        QT_QPA_PLATFORM=os.environ.get("LURVIKO_APPEARANCE_QPA", "offscreen"),
        QT_QUICK_BACKEND=os.environ.get("LURVIKO_APPEARANCE_BACKEND", "software"),
        GIO_USE_VFS="local",
        QT_NO_XDG_DESKTOP_PORTAL="1", PULSE_SERVER="unix:" + str(root / "no-pulse"))
    for scale in os.environ.get("LURVIKO_APPEARANCE_SCALES", environment.get("QT_SCALE_FACTOR", "1")).split(","):
        environment["QT_SCALE_FACTOR"] = scale
        for arguments in (["--start-custom"], []):
            subprocess.run(["dbus-run-session", "--", str(root / "appearance-tests"), *arguments],
                           env=environment, check=True, timeout=40)
