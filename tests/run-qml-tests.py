#!/usr/bin/env python3
"""Run gallery tests against the application's real QML and native types.

Requires a Ninja build in build/. LURVIKO_TEST_BUILD_DIR selects another tree.
"""
import os
import json
from pathlib import Path
import shlex
import subprocess
import tempfile
import math
import struct
import wave

repo = Path(__file__).resolve().parents[1]
build = Path(os.environ.get("LURVIKO_TEST_BUILD_DIR", str(repo / "build"))).resolve()
if not (build / "build.ninja").is_file():
    raise SystemExit("Build the application with Ninja first (see README.md).")
with tempfile.TemporaryDirectory(prefix="gfile-qml-tests-") as temporary:
    root = Path(temporary)
    audio_fixture = root / "music-preview.wav"
    with wave.open(str(audio_fixture), "wb") as audio:
        audio.setnchannels(1)
        audio.setsampwidth(2)
        audio.setframerate(8000)
        second = b"".join(struct.pack("<h", int(1000 * math.sin(i * 2 * math.pi * 440 / 8000)))
                          for i in range(8000))
        audio.writeframes(second * 100)
    # Exercise the format that usually exposes an attached artwork video stream,
    # not just WAV playback with no audio device.
    covered_fixture = root / "music-preview-covered.mp3"
    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
                    "-i", str(audio_fixture), "-i", str(repo / "assets/icons/png/music-cover.png"),
                    "-map", "0:a", "-map", "1:v", "-c:a", "libmp3lame", "-b:a", "48k",
                    "-c:v", "copy", "-disposition:v", "attached_pic", str(covered_fixture)], check=True)
    covered_streams = json.loads(subprocess.check_output([
        "ffprobe", "-v", "error", "-show_streams", "-of", "json", str(covered_fixture)], text=True))
    if not any(stream.get("disposition", {}).get("attached_pic") == 1
               for stream in covered_streams.get("streams", [])):
        raise SystemExit("MP3 regression fixture must contain attached artwork")
    rename_folder = root / "rename-fixture"
    rename_folder.mkdir()
    rename_fixture = rename_folder / "rename-after-preview.txt"
    rename_fixture.write_text("Rename regression fixture\n")
    artwork_folder = root / "folder-artwork-fixture"
    artwork_folder.mkdir()
    for name in [".hidden-folder", "Mor", "Mavi Fotoğraflar", "MyMusic", "visible-folder"]:
        (artwork_folder / name).mkdir()
    for index in range(45):
        (artwork_folder / f"Folder-{index:03d}").mkdir()
    for name in [".hidden-file.txt", "visible-file.txt", "lurviko.pkg.tar.zst", "archive.tar.lz4"]:
        (artwork_folder / name).write_text("Artwork test fixture\n")
    main = (repo / "src/main.cpp").read_text()
    includes = "\n".join(line for line in main.splitlines() if line.startswith('#include "'))
    registration = main[main.index("    qmlRegisterType<StorageModel>"):
                        main.index("    const QStringList launchLocations")]
    # Quick Test creates an engine per test file; each needs its own singleton.
    registration = registration[:registration.index("    PlaybackResumeManager playbackResumeManager;")]
    for name in ("PlaybackResumeManager", "VideoPlayerInputManager"):
        registration += (f'    qmlRegisterSingletonType<{name}>("Lurviko.Backend", 1, 0, "{name}", '
            f'[](QQmlEngine *, QJSEngine *) -> QObject * {{ return new {name}; }});\n')
    source = '''#include <QApplication>
#include <QQmlEngine>
#include <QSettings>
#include <QQmlIncubator>
#include <QElapsedTimer>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickItem>
#include <QQuickWindow>
#include <QIcon>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QPainter>
#include <QStyleOption>
#include <QFontDatabase>
#include <KIconTheme>
#include <QtQuickTest/quicktest.h>
''' + includes + '''
class ThemeProbeStyle : public QProxyStyle {
public:
    explicit ThemeProbeStyle(bool translucent) : QProxyStyle(QStyleFactory::create("Fusion")),
        translucent(translucent) { setObjectName("theme-probe"); }
    void drawPrimitive(PrimitiveElement element, const QStyleOption *option,
                       QPainter *painter, const QWidget *widget = nullptr) const override {
        if (element == PE_PanelMenu) {
            QColor color("#cbdceb");
            if (translucent) color.setAlpha(204);
            painter->fillRect(option->rect, color);
            return;
        }
        if (element != PE_Widget) {
            QProxyStyle::drawPrimitive(element, option, painter, widget);
            return;
        }
        ++paints;
        if (translucent) {
            QColor color = option->palette.color(QPalette::Window);
            color.setAlpha(153);
            painter->fillRect(option->rect, color);
        }
    }
    bool translucent;
    mutable int paints = 0;
};
class Setup : public QObject {
    Q_OBJECT
    const QPalette savedPalette = QApplication::palette();
    const QString savedStyle = QApplication::style()->objectName();
public slots:
    void setSystemThemeTestAppearance(bool dark, bool translucent) {
        QApplication::setStyle(new ThemeProbeStyle(translucent));
        QPalette palette;
        palette.setColor(QPalette::Window, QColor(dark ? "#20272d" : "#e3e7ed"));
        palette.setColor(QPalette::WindowText, QColor(dark ? "#f2f4f7" : "#17212b"));
        palette.setColor(QPalette::Base, QColor(dark ? "#10171d" : "#ffffff"));
        palette.setColor(QPalette::AlternateBase, QColor(dark ? "#282f35" : "#eef0f2"));
        palette.setColor(QPalette::Text, palette.color(QPalette::WindowText));
        palette.setColor(QPalette::Button, palette.color(QPalette::Window));
        palette.setColor(QPalette::ButtonText, palette.color(QPalette::WindowText));
        palette.setColor(QPalette::Highlight, QColor("#e0c35c"));
        palette.setColor(QPalette::HighlightedText, QColor("#151515"));
        palette.setColor(QPalette::Mid, QColor("#777777"));
        palette.setColor(QPalette::Dark, QColor("#555555"));
        palette.setColor(QPalette::PlaceholderText, QColor("#888888"));
        QApplication::setPalette(palette);
    }
    int systemThemeProbePaints() const {
        const auto style = dynamic_cast<ThemeProbeStyle *>(QApplication::style());
        return style ? style->paints : -1;
    }
    void setTransparentSystemThemeTestAppearance() {
        setSystemThemeTestAppearance(false, true);
        QPalette palette = QApplication::palette();
        for (const auto role : {QPalette::Base, QPalette::AlternateBase}) {
            QColor color = palette.color(role);
            color.setAlpha(0);
            palette.setColor(role, color);
        }
        QApplication::setPalette(palette);
    }
    void setFlatSystemThemeTestAppearance(bool dark) {
        setSystemThemeTestAppearance(dark, true);
        QPalette palette = QApplication::palette();
        palette.setColor(QPalette::Base, palette.color(QPalette::Window));
        QApplication::setPalette(palette);
    }
    void restoreSystemThemeTestAppearance() {
        QApplication::setStyle(QStyleFactory::create(savedStyle));
        QApplication::setPalette(savedPalette);
    }
    void cancelMouseGrab(QQuickItem *item) { item->ungrabMouse(); }
    QObject *objectChild(QObject *root, const QString &name) {
        return root ? root->findChild<QObject *>(name) : nullptr;
    }
    QVariant savedAppearanceSetting(const QString &key) {
        QSettings settings;
        settings.sync();
        return settings.value(QStringLiteral("Appearance/") + key);
    }
    bool settleQmlCreations(QQuickItem *item) {
        auto *engine = qmlEngine(item);
        QList<QQmlIncubationController *> controllers;
        if (engine && engine->incubationController()) controllers << engine->incubationController();
        if (item && item->window() && item->window()->incubationController()
                && !controllers.contains(item->window()->incubationController()))
            controllers << item->window()->incubationController();
        QElapsedTimer deadline;
        deadline.start();
        int settledTurns = 0;
        qint64 quietSince = 0;
        while (deadline.elapsed() < 1000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
            bool pending = false;
            for (auto *controller : controllers) {
                controller->incubateFor(10);
                pending |= controller->incubatingObjectCount() > 0;
            }
            settledTurns = pending ? 0 : settledTurns + 1;
            if (pending) quietSince = deadline.elapsed();
            // A new layout/polish turn can enqueue a delegate after the
            // controller first reports idle. Require a full quiet frame span,
            // rather than two immediate processEvents calls at teardown.
            if (settledTurns >= 2 && deadline.elapsed() - quietSince >= 40) return true;
        }
        return false;
    }
    void applicationAvailable() {
        qApp->setOrganizationName("Lurviko-QA");
        qApp->setApplicationName("Gallery");
        qApp->setDesktopFileName("org.lurviko.Tests");
        QQuickWindow::setDefaultAlphaBuffer(true);
        QQuickStyle::setStyle("Basic");
        // A bare CI container has no desktop fonts. Qt's last-resort glyph
        // metrics can double label widths and remove visible artist links.
        if (!QFontDatabase::hasFamily(QStringLiteral("DejaVu Sans")))
            qFatal("QML tests require DejaVu Sans; install ttf-dejavu.");
        QFont font = qApp->font();
        font.setFamily(QStringLiteral("DejaVu Sans"));
        qApp->setFont(font);
''' + registration + '''
    }
    void qmlEngineAvailable(QQmlEngine *engine) {
        engine->rootContext()->setContextProperty("inputTestHelper", this);
        engine->rootContext()->setContextProperty("fileArtworkFixtureUrl",
            QUrl::fromLocalFile(qEnvironmentVariable("LURVIKO_QML_ARTWORK_FIXTURE")));
        engine->rootContext()->setContextProperty("bundledIconNames",
            qEnvironmentVariable("LURVIKO_QML_BUNDLED_ICONS").split(';', Qt::SkipEmptyParts));
        engine->rootContext()->setContextProperty("musicHoverFixtureUrl",
            QUrl::fromLocalFile(qEnvironmentVariable("LURVIKO_QML_AUDIO_FIXTURE")));
        engine->rootContext()->setContextProperty("musicHoverCoveredFixtureUrl",
            QUrl::fromLocalFile(qEnvironmentVariable("LURVIKO_QML_COVERED_FIXTURE")));
        engine->rootContext()->setContextProperty("musicHoverRenameFixtureUrl",
            QUrl::fromLocalFile(qEnvironmentVariable("LURVIKO_QML_RENAME_FIXTURE")));
        engine->rootContext()->setContextProperty("musicHoverUseAudioDevice",
            qEnvironmentVariableIntValue("LURVIKO_QML_AUDIO_DEVICE") != 0);
        engine->rootContext()->setContextProperty("musicHoverExternalFixtureUrl",
            QUrl::fromLocalFile(qEnvironmentVariable("LURVIKO_QML_EXTERNAL_AUDIO_FIXTURE")));
        engine->addImageProvider("systemicon", new SystemIconProvider);
        engine->addImageProvider("gfilethumb", new ThumbnailProvider);
        engine->addImageProvider("bundledicon", new BundledIconProvider);
        engine->addImageProvider("folderart", new FolderIconProvider);
    }
};
int main(int argc, char **argv) {
    // File selection exercises KDE service actions backed by QWidget/QMenu.
    // Match the real application instead of Quick Test's QGuiApplication.
    // Keep icon tests independent of the user's desktop theme.
    KIconTheme::forceThemeForTests(QStringLiteral("breeze"));
    QApplication app(argc, argv);
    QIcon::setThemeName(QStringLiteral("breeze"));
    Setup setup;
    return quick_test_main_with_setup(argc, argv, "gfile_gallery", nullptr, &setup);
}
#include "main.moc"
'''
    (root / "main.cpp").write_text(source)
    autogen = json.loads((build / "CMakeFiles/lurviko_autogen.dir/AutogenInfo.json").read_text())
    subprocess.run([autogen["QT_MOC_EXECUTABLE"], str(root / "main.cpp"),
                    "-o", str(root / "main.moc")], check=True)
    commands = subprocess.check_output(
        ["ninja", "-C", str(build), "-t", "commands", "lurviko"], text=True).splitlines()
    args = next(shlex.split(line) for line in commands
                if " -c " in line and line.endswith("/src/main.cpp"))
    args = args[:args.index("-MD")] + ["-I" + str(repo / "src"), "-I" + str(root),
        "-o", str(root / "main.o"), "-c", str(root / "main.cpp")]
    subprocess.run(args, cwd=build, check=True)
    link = shlex.split(next(line for line in commands if " -o lurviko " in line))
    link = [arg for arg in link if arg not in (":", "&&")
            and not arg.startswith("-Wl,--dependency-file=")]
    link = [str(root / "main.o") if arg == "CMakeFiles/lurviko.dir/src/main.cpp.o" else arg
            for arg in link]
    link[link.index("-o") + 1] = str(root / "qml-tests")
    link.extend(["-lQt6QuickTest", "-lQt6Test"])
    subprocess.run(link, cwd=build, check=True)
    report = root / "results.txt"
    environment = dict(os.environ,
        QT_QPA_PLATFORM=os.environ.get("LURVIKO_QML_TEST_QPA", "offscreen"),
        QT_QUICK_BACKEND=os.environ.get("LURVIKO_QML_TEST_BACKEND", "software"),
        # This test executable has no installed desktop identity and never
        # needs a host portal; avoid registration through desktop services.
        QT_NO_XDG_DESKTOP_PORTAL="1",
        LURVIKO_QML_AUDIO_FIXTURE=str(audio_fixture),
        LURVIKO_QML_COVERED_FIXTURE=str(covered_fixture),
        LURVIKO_QML_RENAME_FIXTURE=str(rename_fixture),
        LURVIKO_QML_ARTWORK_FIXTURE=str(artwork_folder),
        XDG_CONFIG_HOME=str(root / "config"), XDG_DATA_HOME=str(root / "data"),
        XDG_CACHE_HOME=str(root / "cache"),
        PULSE_SERVER="unix:" + str(root / "no-pulse-server"))
    environment["LURVIKO_QML_BUNDLED_ICONS"] = ";".join(sorted(
        "favorite-color.svg" if path == repo / "assets/icons/color/favorite.svg" else path.name
        for pattern in ("png/*.png", "symbolic/*.svg", "color/**/*.svg")
        for path in (repo / "assets/icons").glob(pattern)))
    if os.environ.get("LURVIKO_QML_AUDIO_DEVICE") == "1":
        if "PULSE_SERVER" in os.environ:
            environment["PULSE_SERVER"] = os.environ["PULSE_SERVER"]
        else:
            environment.pop("PULSE_SERVER", None)
    test_input = repo / "tests/qml" / os.environ.get("LURVIKO_QML_TEST_FILE", "")
    # KIO menu/rename jobs require a session bus even without a desktop. Keep
    # it private to the isolated test settings rather than using the host bus.
    result = subprocess.run(["dbus-run-session", "--", str(root / "qml-tests"), "-input", str(test_input),
                            "-o", f"{report},txt"], env=environment, timeout=240)
    output = report.read_text() if report.exists() else "No test report was produced."
    if os.environ.get("LURVIKO_QML_TEST_REPORT"):
        Path(os.environ["LURVIKO_QML_TEST_REPORT"]).write_text(output)
    unexpected_warnings = []
    # Missing desktop audio services are expected in decoder-only headless
    # runs. Real-device tests still reject these warnings; decoder/UI warnings
    # and failed assertions are never exempted.
    expected_audio_warnings = () if environment.get("LURVIKO_QML_AUDIO_DEVICE") == "1" else (
        'qt.multimedia.pipewire.registry: Failed to connect to pipewire instance "Host is down"',
        "PulseAudioService: pa_context_connect() failed",
    )
    for line in output.splitlines():
        if line.startswith("QWARN") and not any(expected in line for expected in (
                "Invalid image provider", "Cannot open: qrc:/", "No thumbnail available",
                # The private session has no Plasma notification/progress UI.
                # Actual KIO operations are still asserted against real files.
                'kf.kio.widgets: Failed to check which JobView API is supported "The name org.kde.kuiserver was not provided by any .service files"',
                # Existing Popup Window attachment warning on current Qt;
                # these tests exercise image/gallery layout, not video windows.
                "QML VideoViewer: Window.window only supports types derived from Item",
                *expected_audio_warnings)):
            unexpected_warnings.append(line)
        if line.startswith(("PASS", "FAIL", "Totals")) or (result.returncode and line.startswith("   ")):
            print(line)
    for line in unexpected_warnings[:10]:
        print(line)
    raise SystemExit(result.returncode or (1 if unexpected_warnings else 0))
