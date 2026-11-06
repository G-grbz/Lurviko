#!/usr/bin/env python3
"""Exercise Miller navigation, shared actions, large folders, tabs and split panes."""
import os
import signal
from pathlib import Path
import shlex
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[1]
build = Path(os.environ.get("LURVIKO_TEST_BUILD_DIR", repo / "build")).resolve()
with tempfile.TemporaryDirectory(prefix="lurviko-columns-tests-") as temporary:
    root = Path(temporary)
    main = (repo / "src/main.cpp").read_text()
    includes = "\n".join(line for line in main.splitlines() if line.startswith('#include "'))
    registration = main[main.index('    qmlRegisterType<StorageModel>'):
                        main.index('    const QStringList launchLocations')]
    (root / "backend_test_types.h").write_text(includes)
    source = (repo / "tests/navigation/columns_integration.cpp").read_text()
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
    link[link.index("-o") + 1] = str(root / "navigation-tests")
    subprocess.run(link, cwd=build, check=True)
    environment = dict(os.environ, QT_QPA_PLATFORM=os.environ.get("LURVIKO_NAVIGATION_QPA", "offscreen"),
        XDG_CONFIG_HOME=str(root / "config"), XDG_DATA_HOME=str(root / "data"),
        XDG_CACHE_HOME=str(root / "cache"), LURVIKO_NAVIGATION_TEST_ROOT=str(root))
    if environment["QT_QPA_PLATFORM"] == "offscreen":
        environment["QT_QUICK_BACKEND"] = "software"
    command = ["dbus-run-session", "--", str(root / "navigation-tests")]
    # Do not capture stdout with PIPE: KDE/DBus helper descendants may inherit
    # the write end after navigation-tests exits. communicate() then waits for
    # EOF until the 90-second timeout even though the test already passed.
    # A regular file lets us inspect warnings without waiting for descendant EOF.
    output_path = root / "navigation-tests.log"
    with output_path.open("w+", encoding="utf-8", errors="replace") as output:
        process = subprocess.Popen(command, env=environment, stdout=output,
                                   stderr=subprocess.STDOUT, start_new_session=True)
        try:
            exit_code = process.wait(timeout=90)
        except subprocess.TimeoutExpired:
            # Kill the entire test process group, not just dbus-run-session.
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait()
            output.flush()
            output.seek(0)
            log = output.read()
            print(log[-16000:], end="", flush=True)
            raise RuntimeError("MillerColumns test timed out after 90 s; "
                               "last 16 KB of captured output shown above")
        output.flush()
        output.seek(0)
        log = output.read()
    print(log, end="", flush=True)
    if exit_code:
        raise subprocess.CalledProcessError(exit_code, command)
    forbidden = (
        'QProcess: Destroyed while process ("ffprobe") is still running',
        'Unable to assign [undefined] to bool',
        "Property 'revealColumn' of object",
        "Property 'genreKey' of object",
    )
    for warning in forbidden:
        if warning in log:
            raise AssertionError(f"Navigation lifecycle regression: {warning}")
