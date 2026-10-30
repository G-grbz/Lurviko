#!/usr/bin/env python3
"""Exercise context icons, popup switching and mini-player toggling using real QML/backend."""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

repo = Path(__file__).resolve().parents[1]
build = Path(os.environ.get("LURVIKO_TEST_BUILD_DIR", repo / "build")).resolve()
with tempfile.TemporaryDirectory(prefix="gfile-menu-tests-") as temporary:
    root = Path(temporary)
    main = (repo / "src/main.cpp").read_text()
    includes = "\n".join(line for line in main.splitlines() if line.startswith('#include "'))
    registration = main[main.index('    qmlRegisterType<StorageModel>'):
                        main.index('    const QStringList launchLocations')]
    (root / "backend_test_types.h").write_text(includes)
    source = (repo / "tests/menus/menu_integration.cpp").read_text()
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
    link[link.index("-o") + 1] = str(root / "menu-tests")
    subprocess.run(link, cwd=build, check=True)
    environment = dict(os.environ, QT_QPA_PLATFORM=os.environ.get("LURVIKO_MENU_QPA", "offscreen"),
        XDG_CONFIG_HOME=str(root / "config"), XDG_DATA_HOME=str(root / "data"),
        XDG_CACHE_HOME=str(root / "cache"), LURVIKO_MENU_TEST_ROOT=str(root))
    if environment["QT_QPA_PLATFORM"] == "offscreen":
        environment["QT_QUICK_BACKEND"] = "software"
    applications = root / "data/applications"
    applications.mkdir(parents=True)
    (applications / "lurviko-menu-fixture.desktop").write_text(
        "[Desktop Entry]\nType=Application\nName=Lurviko Menu Fixture\n"
        "Icon=accessories-text-editor\nExec=/usr/bin/true %U\nMimeType=text/plain;\n")
    configuration = root / "config"
    configuration.mkdir(parents=True, exist_ok=True)
    (configuration / "mimeapps.list").write_text(
        "[Default Applications]\ntext/plain=lurviko-menu-fixture.desktop;\n")
    services = root / "data/kio/servicemenus"
    services.mkdir(parents=True)
    service = services / "lurviko-menu-fixture.desktop"
    service.write_text(
        "[Desktop Entry]\nType=Service\nMimeType=text/plain;\nActions=fixture;inherited;\n"
        "X-KDE-Submenu=Lurviko Fixture\nX-KDE-Icon=folder\n"
        "[Desktop Action fixture]\nName=Lurviko Icon Action\n"
        "Icon=accessories-text-editor\nExec=/usr/bin/true %U\n"
        "[Desktop Action inherited]\nName=Lurviko Inherited Icon\nExec=/usr/bin/true %U\n")
    service.chmod(0o755)
    subprocess.run(["kbuildsycoca6", "--noincremental"], env=environment, check=True,
                   stdout=subprocess.DEVNULL, timeout=60)
    command = [str(root / "menu-tests")]
    if "--debug" in sys.argv:
        command = ["gdb", "--batch", "-ex", "run", "-ex", "bt", "--args"] + command
    subprocess.run(["dbus-run-session", "--"] + command, env=environment, check=True, timeout=120)
