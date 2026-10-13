#!/usr/bin/env python3
"""Check real Main presentation lifetime and report memory with warmed previews.

LURVIKO_PRESENTATION_BASELINE=1 measures an older build without lifecycle gates.
LURVIKO_PRESENTATION_SAVE_BINARY saves the harness for before/after comparisons.
"""
import os
import errno
from pathlib import Path
import shlex
import shutil
import signal
import subprocess
import tempfile
import time


class SessionDirectory(tempfile.TemporaryDirectory):
    def cleanup(self):
        # D-Bus activated KDE helpers can finish their isolated config writes
        # just after the private bus exits. Retry that specific removal race.
        for attempt in range(20):
            try:
                super().cleanup()
                return
            except OSError as error:
                if error.errno != errno.ENOTEMPTY or attempt == 19:
                    raise
                time.sleep(0.1)

repo = Path(__file__).resolve().parents[1]
build = Path(os.environ.get("LURVIKO_TEST_BUILD_DIR", str(repo / "build"))).resolve()
with SessionDirectory(prefix="lurviko-music-presentation-") as temporary:
    root = Path(temporary)
    main = (repo / "src/main.cpp").read_text()
    includes = "\n".join(line for line in main.splitlines() if line.startswith('#include "'))
    registration = main[main.index('    qmlRegisterType<StorageModel>'):
                        main.index('    const QStringList launchLocations')]
    (root / "backend_test_types.h").write_text(includes)
    source = (repo / "tests/music/presentation_memory.cpp").read_text()
    (root / "main.cpp").write_text(source.replace("// REGISTER_BACKEND_TYPES", registration))
    commands = subprocess.check_output(["ninja", "-C", str(build), "-t", "commands", "lurviko"], text=True).splitlines()
    args = next(shlex.split(line) for line in commands if " -c " in line and line.endswith("/src/main.cpp"))
    args = args[:args.index("-MD")] + ["-I" + str(repo / "src"), "-I" + str(root),
        "-o", str(root / "main.o"), "-c", str(root / "main.cpp")]
    subprocess.run(args, cwd=build, check=True)
    link = shlex.split(next(line for line in commands if " -o lurviko " in line))
    link = [arg for arg in link if arg not in (":", "&&") and not arg.startswith("-Wl,--dependency-file=")]
    link = [str(root / "main.o") if arg == "CMakeFiles/lurviko.dir/src/main.cpp.o" else arg for arg in link]
    binary = root / "presentation-tests"
    link[link.index("-o") + 1] = str(binary)
    subprocess.run(link, cwd=build, check=True)
    if os.environ.get("LURVIKO_PRESENTATION_SAVE_BINARY"):
        shutil.copy2(binary, os.environ["LURVIKO_PRESENTATION_SAVE_BINARY"])
    environment = dict(os.environ, QT_QPA_PLATFORM=os.environ.get("LURVIKO_PRESENTATION_QPA", "offscreen"),
        XDG_CONFIG_HOME=str(root / "config"), XDG_DATA_HOME=str(root / "data"), XDG_CACHE_HOME=str(root / "cache"),
        LURVIKO_PRESENTATION_ROOT=str(root), PULSE_SERVER="unix:" + str(root / "no-pulse"))
    if environment["QT_QPA_PLATFORM"] == "offscreen":
        environment["QT_QUICK_BACKEND"] = "software"
    command = ["dbus-run-session", "--", str(binary)]
    with subprocess.Popen(command, env=environment, start_new_session=True) as process:
        try:
            result = process.wait(timeout=90)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()
            raise
        if result:
            raise subprocess.CalledProcessError(result, command)
