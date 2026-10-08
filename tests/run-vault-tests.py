#!/usr/bin/env python3
"""Exercise vault authentication limits, persistence and the real security modal with isolated vault data."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[1]
build = repo / "build"
with tempfile.TemporaryDirectory(prefix="gfile-vault-tests-") as temporary:
    root = Path(temporary)
    main = (repo / "src/main.cpp").read_text()
    includes = "\n".join(line for line in main.splitlines() if line.startswith('#include "'))
    registration = main[main.index('    qmlRegisterType<StorageModel>'):
                        main.index('    const QStringList launchLocations')]
    (root / "backend_test_types.h").write_text(includes)
    source = (repo / "tests/vault/vault_integration.cpp").read_text()
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
    link[link.index("-o") + 1] = str(root / "vault-tests")
    subprocess.run(link, cwd=build, check=True)
    runtime = root / "runtime"
    runtime.mkdir(mode=0o700)
    environment = dict(os.environ, XDG_RUNTIME_DIR=str(runtime), QT_QPA_PLATFORM=os.environ.get("LURVIKO_VAULT_QPA", "offscreen"),
        XDG_CONFIG_HOME=str(root / "config"), XDG_DATA_HOME=str(root / "data"),
        XDG_CACHE_HOME=str(root / "cache"), LURVIKO_VAULT_TEST_ROOT=str(root),
        PULSE_SERVER="unix:" + str(root / "no-pulse-server"))
    if environment["QT_QPA_PLATFORM"] == "wayland":
        display = os.environ.get("WAYLAND_DISPLAY", "wayland-0")
        environment["WAYLAND_DISPLAY"] = display if display.startswith("/") else str(Path(os.environ["XDG_RUNTIME_DIR"]) / display)
    if environment["QT_QPA_PLATFORM"] == "offscreen":
        environment["QT_QUICK_BACKEND"] = "software"
    command = [str(root / "vault-tests")]
    if environment["QT_QPA_PLATFORM"] == "offscreen":
        # The security backend needs no media devices. PrivateVaultPage embeds
        # real viewers, so exercise that UI only with desktop services available.
        command.append("--backend-only")
    subprocess.run(command, env=environment, check=True, timeout=90)
