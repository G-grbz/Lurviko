#include <QApplication>
#include <QAudioOutput>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QMediaPlayer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QVideoSink>
#include <cstdio>
#include <functional>
#include "backend_test_types.h"

static int crossWindowWarnings = 0;
static int transientWindowWarnings = 0;
static void captureMessages(QtMsgType, const QMessageLogContext &, const QString &message) {
    if (message.contains("Cannot use same item on different windows")) ++crossWindowWarnings;
    if (message.contains("transient parent") && message.contains("cannot be same as window")) ++transientWindowWarnings;
    std::fprintf(stderr, "%s\n", qPrintable(message));
}
static void pause(int ms) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}
static bool until(std::function<bool()> check) {
    QElapsedTimer timer; timer.start();
    while (!check() && timer.elapsed() < 10000) pause(10);
    return check();
}
static QQuickItem *visualItem(QQuickItem *parent, const QString &name) {
    if (parent->objectName() == name) return parent;
    for (auto child : parent->childItems())
        if (auto found = visualItem(child, name)) return found;
    return nullptr;
}
class FixtureImages : public QQuickImageProvider {
public:
    FixtureImages() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString &, QSize *size, const QSize &) override {
        QImage image(64, 64, QImage::Format_RGB32); image.fill(Qt::blue);
        if (size) *size = image.size();
        return image;
    }
};
static void require(bool ok, const char *description) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", description); std::exit(1); }
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    qInstallMessageHandler(captureMessages);
    app.setOrganizationName("Lurviko-QA"); app.setApplicationName("Subtitles");
    QQuickStyle::setStyle("Basic");
    // REGISTER_BACKEND_TYPES
    const QString path = QString::fromLocal8Bit(qgetenv("LURVIKO_SUBTITLE_TEST_ROOT")) + "/fixture.mkv";
    QQmlApplicationEngine engine;
    engine.addImageProvider("gfilethumb", new FixtureImages);
    engine.addImageProvider("bundledicon", new BundledIconProvider);
    engine.rootContext()->setContextProperty("fixturePath", path);
    engine.rootContext()->setContextProperty("fixtureUrl", QUrl::fromLocalFile(path));
    engine.loadData(R"qml(
import QtQuick
import QtQuick.Controls
import Lurviko.App
ApplicationWindow {
    id: host
    width: 900; height: 600; visible: true
    property int playerOpenedCount: 0
    property int playerClosedCount: 0
    QtObject { id: language; property string language: "tr"; function t(key) { return key } }
    VideoViewer {
        id: viewer; objectName: "viewer"; lang: language
        files: [{path: fixturePath, url: fixtureUrl, name: "fixture.mkv", size: 1000}]
    }
    function openVideo() { viewer.openAt(0) }
    function selectEnglish() { viewer.selectEmbeddedSubtitle(0) }
    Connections {
        target: viewer
        function onOpened() { host.playerOpenedCount++ }
        function onClosed() { host.playerClosedCount++ }
    }
}
)qml");
    require(!engine.rootObjects().isEmpty(), "viewer QML loads");
    auto window = qobject_cast<QQuickWindow *>(engine.rootObjects()[0]);
    auto viewer = window->findChild<QObject *>("viewer");
    auto manager = viewer->findChild<SubtitleAiManager *>("videoSubtitleAiManager");
    auto player = viewer->findChild<QMediaPlayer *>("videoMediaPlayer");
    require(manager && player, "real subtitle manager and media player available");
    QMetaObject::invokeMethod(window, "openVideo");
    require(until([&] { return player->duration() > 0 && player->isSeekable(); }), "embedded fixture loads");
    player->pause(); player->setPosition(1000); pause(80);
    auto overlay = visualItem(window->contentItem(), "videoSubtitleOverlay");
    auto output = visualItem(window->contentItem(), "videoOutput");
    require(overlay && output, "styled overlay and native video output available");
    auto sink = output->property("videoSink").value<QVideoSink *>();
    require(sink, "native sink available");
    const auto text = [&] { return overlay->property("displayText").toString(); };
    const auto english = [&] {
        QMetaObject::invokeMethod(window, "selectEnglish");
        player->setPosition(1000); pause(30);
        require(text().startsWith("EN"), "CC embedded English selection works before translation");
    };
    auto style = viewer->findChild<SubtitleStyleManager *>();
    require(style, "subtitle style manager available");
    style->setBackgroundBlur(24);
    for (bool whisper : {false, true}) {
        english();
        QMetaObject::invokeMethod(viewer, whisper ? "openSubtitleAiGenerateDialog" : "openSubtitleAiTranslateDialog");
        viewer->setProperty("subtitleAiTargetCode", "tr");
        QMetaObject::invokeMethod(viewer, "startSubtitleAi");
        const bool started = manager->busy();
        require(started && manager->liveTranslationActive(), "translation job starts");
        require(until([&] { return manager->liveRevision() > 0 && manager->liveSubtitleAt(1000).isEmpty()
                                  && manager->busy(); }), "translation starts without original overlay");
        pause(120);
        require(text().isEmpty(), "selected English is suppressed while first translated cue is pending");
        require(until([&] { return text().startsWith("TR"); }), "translated cue displayed");
        QMetaObject::invokeMethod(viewer, "toggleHostFullScreen"); pause(180);
        QMetaObject::invokeMethod(viewer, "toggleHostFullScreen"); pause(180);
        std::fprintf(stderr, "Live after fullscreen: text='%s', translation=%d, opens=%d closes=%d\n",
            qPrintable(text()), manager->liveTranslationActive(), window->property("playerOpenedCount").toInt(),
            window->property("playerClosedCount").toInt());
        require(text().startsWith("TR") && manager->liveTranslationActive(),
                "live translation survives fullscreen while the AI job is running");
        require(manager->liveSubtitleAt(6000).isEmpty(), "backend does not fall back to source in translation gaps");
        player->setPosition(6000); pause(30);
        require(text().isEmpty(), "overlay does not fall back to selected English in translation gaps");
        player->setPosition(1000); pause(30);
        // Native backend text can arrive after a seek or track activation.
        for (int i = 0; i < 4; ++i) {
            player->setActiveSubtitleTrack(0);
            sink->setSubtitleText("EN late native subtitle");
            pause(20);
            std::fprintf(stderr, "Native guard: track=%d sink='%s'\n", player->activeSubtitleTrack(),
                         qPrintable(sink->subtitleText()));
            require(player->activeSubtitleTrack() == -1 && sink->subtitleText().isEmpty(),
                    "late native tracks and cues stay disabled");
        }
        bool originalAtHandoff = false;
        auto connection = QObject::connect(manager, &SubtitleAiManager::busyChanged, manager, [&] {
            if (!manager->busy()) originalAtHandoff = text().startsWith("EN");
        });
        require(until([&] { return !manager->busy() && manager->selectedSubtitlePath().endsWith(".result.srt"); }),
                "completed translation sidecar selected");
        QObject::disconnect(connection);
        require(!originalAtHandoff && text().startsWith("TR"), "completion handoff never restores English");
        player->setPosition(6000); pause(30);
        require(text().isEmpty(), "completed translation gaps stay empty");
        std::fprintf(stderr, "PASS: %s translation, pending/gap/completion/native cue checks\n",
                     whisper ? "Whisper" : "embedded");
    }
    english();
    require(manager->startTranscription(path, "en", "fast", "", 0), "source-only Whisper starts");
    require(until([&] { return manager->liveSubtitleAt(1000).startsWith("EN"); }),
            "source-only Whisper still renders original language");
    manager->cancel();
    require(until([&] { return !manager->busy(); }), "cancel settles");
    require(!manager->liveTranslationActive() && text().startsWith("EN"), "cancel restores selected CC");
    std::fprintf(stderr, "PASS: original CC and untranslated Whisper preserved\n");

    // Seed a stale resume checkpoint and a deferred embedded CC preference.
    // A window move must not load either of them into an active session.
    player->setPosition(8000);
    QMetaObject::invokeMethod(viewer, "saveResumePosition");
    player->setPosition(12000);
    player->play(); pause(80);
    bool playbackInterrupted = false;
    bool playbackRewound = false;
    auto stateConnection = QObject::connect(player, &QMediaPlayer::playbackStateChanged, &app,
        [&](QMediaPlayer::PlaybackState state) { if (state != QMediaPlayer::PlayingState) playbackInterrupted = true; });
    qint64 lastPosition = player->position();
    auto positionConnection = QObject::connect(player, &QMediaPlayer::positionChanged, &app,
        [&](qint64 position) { if (position + 250 < lastPosition) playbackRewound = true; lastPosition = position; });
    for (int i = 0; i < 2; ++i) {
        QMetaObject::invokeMethod(viewer, "toggleHostFullScreen"); pause(350);
    }
    QObject::disconnect(stateConnection); QObject::disconnect(positionConnection);
    require(!playbackInterrupted && !playbackRewound && player->position() >= 12000,
            "fullscreen never pauses playback or restores a stale resume position");
    player->pause();
    QMetaObject::invokeMethod(viewer, "toggleHostFullScreen"); pause(180);
    require(player->playbackState() == QMediaPlayer::PausedState, "fullscreen preserves paused state too");
    QMetaObject::invokeMethod(viewer, "toggleHostFullScreen"); pause(180);
    std::fprintf(stderr, "PASS: fullscreen preserves playing/paused state and position\n");

    const QString maskCode = QStringLiteral(R"js((function() {
        const signature = [];
        for (let i = 0; i < 3; ++i) {
            const mask = pauseScreen.children[i];
            signature.push(String(mask.color));
            if (mask.gradient) for (let j = 0; j < mask.gradient.stops.length; ++j)
                signature.push(String(mask.gradient.stops[j].color));
        }
        signature.push(String(viewer.background.color), String(titleText.color),
                       String(infoCard.tintColor), String(bottomPanel.tintColor),
                       String(seekSlider.background.color), String(volumeSlider.handle.color));
        for (const popup of [audioTrackPopup, subtitleTrackPopup, subtitleStylePopup,
                             subtitleStyleFontPicker, subtitleAiSubtitlePopup])
            signature.push(String(popup.background.tintColor), String(popup.palette.text),
                           String(popup.palette.highlight));
        signature.push(String(subtitleAiProfileCombo.background.color),
                       String(subtitleAiProfileCombo.contentItem.color),
                       String(subtitleAiMinimizeButton.background.color));
        return JSON.stringify(signature);
    })())js");
    QQmlExpression light(qmlContext(player), viewer, "AppTheme.settings.darkMode = false"); light.evaluate();
    QQmlExpression mask(qmlContext(player), viewer, maskCode);
    const QString lightMask = mask.evaluate().toString();
    require(!mask.hasError(), "pause masks readable");
    QQmlExpression dark(qmlContext(player), viewer, "AppTheme.settings.darkMode = true"); dark.evaluate();
    const QString darkMask = mask.evaluate().toString();
    require(lightMask == darkMask && !darkMask.isEmpty(), "player chrome, menus and pause screen stay dark in both themes");
    std::fprintf(stderr, "PASS: fixed dark player, menus and pause screen in light/dark theme\n");

    // Keep subtitles, icons, and pause-screen effects alive while switching
    // between windows. Rendering resources must belong to only one window.
    const QString realPath = qEnvironmentVariable("LURVIKO_VIDEO_REAL_PATH");
    if (!realPath.isEmpty()) {
        player->stop();
        player->setSource(QUrl());
        viewer->setProperty("files", QVariantList{QVariantMap{{"path", realPath},
            {"url", QUrl::fromLocalFile(realPath)}, {"name", "Real playback fixture"}}});
        require(until([&] { return player->source() == QUrl::fromLocalFile(realPath) && player->duration() > 20000; }),
                "real video fixture loads");
        require(manager->selectSubtitleFile(path.left(path.lastIndexOf('/')) + "/source.srt"),
                "controlled subtitle loaded over real video");
    }
    player->audioOutput()->setMuted(true);
    QQmlExpression openCc(qmlContext(player), viewer, "subtitleTrackPopup.open()");
    openCc.evaluate();
    require(!openCc.hasError(), "CC menu opens in the viewer context");
    pause(120);
    player->setPosition(1000);
    player->play();
    pause(120);
    for (int i = 0; i < 8; ++i) {
        openCc.evaluate();
        viewer->setProperty("pauseMetadata", QVariantMap{{"state", "ready"}, {"title", "Fixture"},
            {"year", 2026}, {"genres", QStringList{"Drama", "Science Fiction"}}, {"runtime", 120}});
        viewer->setProperty("pauseScreenVisible", true);
        QMetaObject::invokeMethod(viewer, "toggleHostFullScreen"); pause(350);
        QMetaObject::invokeMethod(viewer, "toggleHostFullScreen"); pause(350);
    }
    require(crossWindowWarnings == 0, "fullscreen transitions do not retain effects in the old window");
    require(transientWindowWarnings == 0, "fullscreen window never becomes its own transient parent");
    std::fprintf(stderr, "PASS: 16 fullscreen transitions with subtitle and pause effects\n");
    QElapsedTimer soak; soak.start();
    const int soakMs = qEnvironmentVariableIntValue("LURVIKO_VIDEO_SOAK_SECONDS") * 1000;
    int frames = 0;
    for (auto quickWindow : app.allWindows())
        if (auto rendered = qobject_cast<QQuickWindow *>(quickWindow))
            QObject::connect(rendered, &QQuickWindow::frameSwapped, &app, [&] { ++frames; });
    while (soak.elapsed() < soakMs) {
        player->setPosition(1000);
        player->play();
        openCc.evaluate();
        QMetaObject::invokeMethod(viewer, "toggleHostFullScreen");
        pause(1000);
    }
    require(crossWindowWarnings == 0 && transientWindowWarnings == 0, "soak remains free of window resource warnings");
    if (soakMs) {
        require(frames > 30, "video rendered throughout soak");
        std::fprintf(stderr, "PASS: %d seconds playback soak, %d rendered frames\n", soakMs / 1000, frames);
    }
    QMetaObject::invokeMethod(viewer, "close");
    return 0;
}
