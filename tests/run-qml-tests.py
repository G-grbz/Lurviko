#!/usr/bin/env python3
"""Run gallery tests against the application's real QML and native types.

Requires a Ninja build in build/. GFILE_TEST_BUILD_DIR selects another tree.
"""
import os
import json
from pathlib import Path
import shlex
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[1]
build = Path(os.environ.get("GFILE_TEST_BUILD_DIR", str(repo / "build"))).resolve()
if not (build / "build.ninja").is_file():
    raise SystemExit("Build the application with Ninja first (see README.md).")
with tempfile.TemporaryDirectory(prefix="gfile-qml-tests-") as temporary:
    root = Path(temporary)
    main = (repo / "src/main.cpp").read_text()
    includes = "\n".join(line for line in main.splitlines() if line.startswith('#include "'))
    registration = main[main.index("    qmlRegisterType<StorageModel>"):
                        main.index("    const QStringList launchLocations")]
    # Quick Test creates an engine per test file; each needs its own singleton.
    registration = registration[:registration.index("    PlaybackResumeManager playbackResumeManager;")]
    for name in ("PlaybackResumeManager", "VideoPlayerInputManager"):
        registration += (f'    qmlRegisterSingletonType<{name}>("GFile.Backend", 1, 0, "{name}", '
            f'[](QQmlEngine *, QJSEngine *) -> QObject * {{ return new {name}; }});\n')
    source = '''#include <QApplication>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QtQuickTest/quicktest.h>
''' + includes + '''
class Setup : public QObject {
    Q_OBJECT
public slots:
    void applicationAvailable() {
        qApp->setOrganizationName("GFile-QA");
        qApp->setApplicationName("Gallery");
        QQuickStyle::setStyle("Basic");
''' + registration + '''
    }
};
QUICK_TEST_MAIN_WITH_SETUP(gfile_gallery, Setup)
#include "main.moc"
'''
    (root / "main.cpp").write_text(source)
    autogen = json.loads((build / "CMakeFiles/g-file_autogen.dir/AutogenInfo.json").read_text())
    subprocess.run([autogen["QT_MOC_EXECUTABLE"], str(root / "main.cpp"),
                    "-o", str(root / "main.moc")], check=True)
    commands = subprocess.check_output(
        ["ninja", "-C", str(build), "-t", "commands", "g-file"], text=True).splitlines()
    args = next(shlex.split(line) for line in commands
                if " -c " in line and line.endswith("/src/main.cpp"))
    args = args[:args.index("-MD")] + ["-I" + str(repo / "src"), "-I" + str(root),
        "-o", str(root / "main.o"), "-c", str(root / "main.cpp")]
    subprocess.run(args, cwd=build, check=True)
    link = shlex.split(next(line for line in commands if " -o g-file " in line))
    link = [arg for arg in link if arg not in (":", "&&")
            and not arg.startswith("-Wl,--dependency-file=")]
    link = [str(root / "main.o") if arg == "CMakeFiles/g-file.dir/src/main.cpp.o" else arg
            for arg in link]
    link[link.index("-o") + 1] = str(root / "qml-tests")
    link.extend(["-lQt6QuickTest", "-lQt6Test"])
    subprocess.run(link, cwd=build, check=True)
    report = root / "results.txt"
    environment = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software",
        XDG_CONFIG_HOME=str(root / "config"), XDG_DATA_HOME=str(root / "data"),
        XDG_CACHE_HOME=str(root / "cache"),
        PULSE_SERVER="unix:" + str(root / "no-pulse-server"))
    result = subprocess.run([str(root / "qml-tests"), "-input", str(repo / "tests/qml"),
                            "-o", f"{report},txt"], env=environment, timeout=90)
    output = report.read_text() if report.exists() else "No test report was produced."
    unexpected_warnings = []
    for line in output.splitlines():
        if line.startswith("QWARN") and not any(expected in line for expected in (
                "Invalid image provider", "Cannot open: qrc:/", "No thumbnail available",
                # Existing Popup Window attachment warning on current Qt;
                # these tests exercise image/gallery layout, not video windows.
                "QML VideoViewer: Window.window only supports types derived from Item")):
            unexpected_warnings.append(line)
        if line.startswith(("PASS", "FAIL", "Totals")) or (result.returncode and line.startswith("   ")):
            print(line)
    for line in unexpected_warnings[:10]:
        print(line)
    raise SystemExit(result.returncode or (1 if unexpected_warnings else 0))
