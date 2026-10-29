#!/usr/bin/env python3
"""Exercise real Main shutdown, opt-in restoration and explicit startup requests."""
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
    directory = tempfile.TemporaryDirectory(prefix="lurviko-browser-session-")
    try:
        yield directory.name
    finally:
        # Services activated on a private bus can flush settings just after
        # that bus exits. Retry only this directory-cleanup race.
        for attempt in range(5):
            try:
                directory.cleanup()
                break
            except OSError as error:
                if error.errno != errno.ENOTEMPTY or attempt == 4:
                    raise
                time.sleep(0.1 * (2 ** attempt))

repo = Path(__file__).resolve().parents[1]
build = repo / "build"
with isolated_directory() as temporary:
    root = Path(temporary)
    main = (repo / "src/main.cpp").read_text()
    includes = "\n".join(line for line in main.splitlines() if line.startswith('#include "'))
    registration = main[main.index("    qmlRegisterType<StorageModel>"):
                        main.index("    const QStringList launchLocations")]
    (root / "backend_test_types.h").write_text(includes)
    source = (repo / "tests/session/browser_session.cpp").read_text()
    (root / "main.cpp").write_text(source.replace("// REGISTER_BACKEND_TYPES", registration))
    commands = subprocess.check_output(["ninja", "-C", str(build), "-t", "commands", "lurviko"], text=True).splitlines()
    arguments = next(shlex.split(line) for line in commands if " -c " in line and line.endswith("/src/main.cpp"))
    arguments = arguments[:arguments.index("-MD")] + ["-I" + str(repo / "src"), "-I" + str(root),
        "-o", str(root / "main.o"), "-c", str(root / "main.cpp")]
    subprocess.run(arguments, cwd=build, check=True)
    link = shlex.split(next(line for line in commands if " -o lurviko " in line))
    link = [arg for arg in link if arg not in (":", "&&") and not arg.startswith("-Wl,--dependency-file=")]
    link = [str(root / "main.o") if arg == "CMakeFiles/lurviko.dir/src/main.cpp.o" else arg for arg in link]
    link[link.index("-o") + 1] = str(root / "browser-session-tests")
    subprocess.run(link, cwd=build, check=True)
    environment = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software",
        XDG_CONFIG_HOME=str(root / "config"), XDG_DATA_HOME=str(root / "data"), XDG_CACHE_HOME=str(root / "cache"),
        GIO_USE_VFS="local", QT_NO_XDG_DESKTOP_PORTAL="1", PULSE_SERVER="unix:" + str(root / "no-pulse"),
        LURVIKO_SESSION_TEST_ROOT=str(root))
    for mode in ("defaults", "save", "restore", "disabled", "explicit", "activation", "dont-ask-yes", "dont-ask-no", "escape", "shortcut"):
        subprocess.run(["dbus-run-session", "--", str(root / "browser-session-tests"), mode],
                       env=environment, check=True, timeout=40)
