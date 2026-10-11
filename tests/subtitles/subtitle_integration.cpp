#include <QApplication>
#include <QAudioOutput>
#include <QElapsedTimer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QEventLoop>
#include <QMediaPlayer>
#include <QMediaMetaData>
#include <QMouseEvent>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>
#include <QVideoSink>
#include <QWheelEvent>
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
    {
        const QFileInfo input(path);
        const QByteArray material = QStringLiteral("v2\n%1\n%2\n%3\n0\nen\nmedium\n")
            .arg(input.canonicalFilePath()).arg(input.size()).arg(input.lastModified().toMSecsSinceEpoch()).toUtf8();
        const QString key = QString::fromLatin1(QCryptographicHash::hash(material, QCryptographicHash::Sha256).toHex());
        const QString directory = QDir(QString::fromLocal8Bit(qgetenv("XDG_CACHE_HOME")))
            .absoluteFilePath("Lurviko/subtitle-ai/jobs/" + key);
        require(QDir().mkpath(directory), "isolated translation cache fixture created");
        QFile source(directory + "/source.srt");
        require(source.open(QIODevice::WriteOnly), "source cache fixture opens");
        source.write("1\n00:00:01,000 --> 00:00:02,000\nSource\n\n"); source.close();
        const auto writeState = [&](int engineVersion) {
            QJsonObject translation{{"complete", true}, {"completed_units", 12}, {"total_units", 12}, {"progress", 100}};
            if (engineVersion) translation.insert("engine_version", engineVersion);
            QFile state(directory + "/state.json");
            require(state.open(QIODevice::WriteOnly | QIODevice::Truncate), "cache state fixture opens");
            state.write(QJsonDocument(QJsonObject{{"source_ready", true}, {"detected_language", "en"},
                {"translations", QJsonObject{{"tr", translation}}}}).toJson());
        };
        SubtitleAiManager cacheManager;
        writeState(0);
        auto cached = cacheManager.cacheInfo(path, "en", "medium", "tr");
        require(cached.value("sourceReady").toBool() && !cached.value("translationReady").toBool()
                && cached.value("translationProgress").toInt() == 0,
                "legacy translation cache is invalidated while source remains ready");
        writeState(1);
        cached = cacheManager.cacheInfo(path, "en", "medium", "tr");
        require(cached.value("translationReady").toBool() && cached.value("translationProgress").toInt() == 100,
                "current translation engine cache remains ready");
        std::fprintf(stderr, "PASS: translation engine version invalidation retains source cache\n");
    }
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
    auto trackRow = [&](const QString &popupName, const QString &rowName) {
        QQmlExpression expression(qmlContext(player), viewer, popupName + ".open(); " + popupName + ".contentItem");
        auto container = qobject_cast<QQuickItem *>(expression.evaluate().value<QObject *>());
        pause(40);
        require(container && !expression.hasError(), "track selection popup available");
        auto row = visualItem(container, rowName);
        require(row, "track row available");
        return row;
    };
    if (qEnvironmentVariableIsSet("LURVIKO_SUBTITLE_RESTART_ONLY")) {
        QMetaObject::invokeMethod(window, "openVideo");
        require(until([&] { return player->activeAudioTrack() == 2
            && manager->selectedSubtitlePath().endsWith("#2") && !manager->selectedSubtitleLoading(); }),
            "fresh application process restores the chosen audio and embedded subtitle");
        require(trackRow("audioTrackPopup", "videoAudioTrack2")->property("highlighted").toBool(),
                "fresh process audio list highlights the actual restored track");
        require(trackRow("subtitleTrackPopup", "videoSubtitleTrack2")->property("highlighted").toBool(),
                "fresh process CC list highlights the restored subtitle");
        require(!trackRow("subtitleTrackPopup", "videoSubtitleOff")->property("highlighted").toBool(),
                "restored subtitle is not marked off");
        QMetaObject::invokeMethod(viewer, "close"); pause(80);
        std::fprintf(stderr, "PASS: audio/CC selection and list highlights survive a real application restart\n");
        return 0;
    }
    // Reproduce a cold opening with saved CC and deliberately slow external
    // tools. Playback preparation must leave the event loop free to animate.
    PlaybackResumeManager bookmarks;
    bookmarks.saveTrackPreferences(path, 2, "embedded", 0, "");
    const QByteArray originalPath = qgetenv("PATH");
    qputenv("PATH", qgetenv("LURVIKO_SLOW_PROBE_DIR") + ':' + originalPath);
    QElapsedTimer opening, pulseClock; opening.start(); pulseClock.start();
    qint64 longestGap = 0;
    int pulses = 0;
    QTimer heartbeat;
    heartbeat.setInterval(10);
    QObject::connect(&heartbeat, &QTimer::timeout, &app, [&] {
        longestGap = qMax(longestGap, pulseClock.restart()); ++pulses;
    });
    heartbeat.start();
    bool sawSavedCcLoading = false;
    bool savedCcHighlightedImmediately = false;
    auto loadingConnection = QObject::connect(manager, &SubtitleAiManager::selectedSubtitleLoadingChanged, &app, [&] {
        if (!manager->selectedSubtitleLoading() || sawSavedCcLoading) return;
        sawSavedCcLoading = true;
        QQmlExpression selection(qmlContext(player), viewer, "Number(selectedSubtitleSource ? selectedSubtitleSource.trackIndex : -1)");
        savedCcHighlightedImmediately = selection.evaluate().toInt() == 0;
    });
    QMetaObject::invokeMethod(window, "openVideo");
    require(viewer->property("videoLoading").toBool(), "loading indicator starts before the first frame");
    require(until([&] { return player->duration() > 0 && player->isSeekable(); }), "embedded fixture loads");
    require(until([&] { return viewer->property("firstVideoFrameReady").toBool(); }), "first decoded video frame arrives");
    const auto firstFrameMs = opening.elapsed();
    require(!viewer->property("videoLoading").toBool(), "loading indicator stops once video is ready");
    require(until([&] { return sawSavedCcLoading; }) && savedCcHighlightedImmediately,
            "saved CC starts preparing with its selection already highlighted");
    QObject::disconnect(loadingConnection);
    auto notice = visualItem(window->contentItem(), "subtitleLoadingNotice");
    require(notice, "subtitle preparation notice available");
    require(trackRow("subtitleTrackPopup", "videoSubtitleTrack0")->property("highlighted").toBool(),
            "requested subtitle is highlighted before extraction completes");
    require(until([&] { return !manager->selectedSubtitleLoading() && manager->selectedSubtitlePath().startsWith("embedded:"); }),
            "saved embedded CC restored asynchronously");
    QQmlExpression loadedSources(qmlContext(player), viewer, "subtitleAiExistingSources.length");
    require(until([&] { return loadedSources.evaluate().toInt() == 3; }), "subtitle discovery completes asynchronously");
    require(player->activeAudioTrack() == 2
            && trackRow("audioTrackPopup", "videoAudioTrack2")->property("highlighted").toBool()
            && !trackRow("audioTrackPopup", "videoAudioTrack0")->property("highlighted").toBool(),
            "restored audio selection matches the track actually playing");
    QMetaObject::invokeMethod(viewer, "selectEmbeddedSubtitle", Q_ARG(QVariant, QVariant(1)));
    require(manager->selectedSubtitleLoading() && notice->isVisible(), "new subtitle shows preparing notice immediately");
    require(trackRow("subtitleTrackPopup", "videoSubtitleTrack1")->property("highlighted").toBool()
            && !trackRow("subtitleTrackPopup", "videoSubtitleTrack0")->property("highlighted").toBool(),
            "CC highlight switches immediately rather than returning to the old selection");
    QQmlExpression pendingText(qmlContext(player), viewer, "subtitleOverlay.displayText");
    require(pendingText.evaluate().toString().isEmpty(), "old CC is hidden while the new subtitle prepares");
    require(until([&] { return !manager->selectedSubtitleLoading() && manager->selectedSubtitlePath().endsWith("#1"); }),
            "requested subtitle finishes loading");
    require(!notice->isVisible(), "preparation notice disappears when the subtitle is ready");
    QQmlExpression closeTracks(qmlContext(player), viewer, "audioTrackPopup.close(); subtitleTrackPopup.close()");
    closeTracks.evaluate();
    heartbeat.stop(); qputenv("PATH", originalPath);
    std::fprintf(stderr, "Cold video opening: first frame %lld ms, longest UI gap %lld ms, %d heartbeat ticks with delayed probes\n",
                 firstFrameMs, longestGap, pulses);
    require(pulses > 25 && longestGap < 500, "slow probing and saved subtitle extraction do not freeze the UI");
    // An obsolete track request must not restore CC after the user turns it off.
    require(manager->requestEmbeddedSubtitle(path, 2), "new embedded selection starts");
    manager->clearSelectedSubtitle(); pause(300);
    require(!manager->selectedSubtitleLoading() && manager->selectedSubtitlePath().isEmpty(),
            "cancelled embedded selection cannot return stale subtitles");
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
        require(until([&] { return text().startsWith("EN"); }), "CC embedded English selection works before translation");
    };
    auto style = viewer->findChild<SubtitleStyleManager *>();
    require(style, "subtitle style manager available");
    // A high-contrast scene makes blur and alpha measurable rather than relying
    // on a uniform video frame. Exercise the exact components used by the player.
    if (window->rendererInterface()->graphicsApi() != QSGRendererInterface::Software) {
        QQmlComponent renderComponent(&engine);
        renderComponent.setData(R"qml(
import QtQuick
import Lurviko.App
Window {
    width: 480; height: 240; visible: true; color: "#303030"
    Item {
        id: pattern; anchors.fill: parent
        Repeater {
            model: 1200
            delegate: Rectangle {
                required property int index
                width: 8; height: 12; x: (index % 60) * width; y: Math.floor(index / 60) * height
                color: ((index % 60) + Math.floor(index / 60)) % 2 ? "#EEEEEE" : "#222222"
            }
        }
    }
    SubtitleBackdrop {
        objectName: "background"; x: 40; y: 40; width: 400; height: 160
        sourceItem: pattern; backgroundOpacity: 0; backgroundColor: "black"; blurRadius: 48
    }
    SubtitleLabel {
        objectName: "label"; x: 80; y: 92; text: "Shadow test"; font.pixelSize: 30; color: "white"; shadowMode: 0
    }
}
)qml", QUrl());
        auto renderWindow = qobject_cast<QQuickWindow *>(renderComponent.create());
        require(renderWindow, qPrintable(renderComponent.errorString()));
        auto background = renderWindow->findChild<QQuickItem *>("background");
        auto label = renderWindow->findChild<QQuickItem *>("label");
        pause(150);
        const auto transparent = renderWindow->grabWindow();
        require(!transparent.isNull(), "GPU render snapshot available");
        background->setProperty("blurRadius", 0); pause(80);
        const auto noEffect = renderWindow->grabWindow();
        require(noEffect == transparent, "100% transparency leaves no blur rectangle");
        background->setProperty("backgroundOpacity", 0.5); pause(80);
        const auto halfTint = renderWindow->grabWindow();
        background->setProperty("backgroundOpacity", 1); pause(80);
        const auto opaque = renderWindow->grabWindow();
        const QColor clearPixel = transparent.pixelColor(50, 50);
        const QColor tintedPixel = halfTint.pixelColor(50, 50);
        require(qAbs(tintedPixel.red() - clearPixel.red() / 2.0) < 4
                && opaque.pixelColor(50, 50).red() < 3, "rendered tint honors opacity");
        background->setProperty("backgroundOpacity", 0.5);
        background->setProperty("blurRadius", 48); pause(150);
        const auto blurred = renderWindow->grabWindow();
        double sharpContrast = 0, blurContrast = 0;
        for (int y = 60; y < 80; ++y) for (int x = 60; x < 400; ++x) {
            sharpContrast += qAbs(halfTint.pixelColor(x, y).red() - halfTint.pixelColor(x + 1, y).red());
            blurContrast += qAbs(blurred.pixelColor(x, y).red() - blurred.pixelColor(x + 1, y).red());
        }
        std::fprintf(stderr, "GPU blur contrast %.1f -> %.1f\n", sharpContrast, blurContrast);
        require(blurContrast < sharpContrast * 0.8 && blurContrast > sharpContrast * 0.2,
                "blur visibly softens backdrop without an opaque patch");
        background->setProperty("backgroundOpacity", 0); pause(80);
        const auto plain = renderWindow->grabWindow();
        label->setProperty("shadowMode", 3); pause(150);
        require(renderWindow->grabWindow() != plain, "shadow mode changes rendered pixels");
        renderWindow->close(); delete renderWindow;
        std::fprintf(stderr, "PASS: GPU-rendered transparency, blur and shadow\n");
    }
    english();
    // Deliver actual wheel events to overflowing audio/CC popups, including
    // their track delegates, rather than calling the scroll helper directly.
    QQmlExpression wheelStep(qmlContext(player), viewer, "AppTheme.setWheelScrollStep(250)");
    wheelStep.evaluate();
    auto checkWheel = [&](QQuickItem *scroll) {
        require(scroll, "popup scroll viewport available");
        auto flick = qobject_cast<QQuickItem *>(scroll->property("contentItem").value<QObject *>());
        require(flick, "popup flickable available");
        flick->setProperty("contentY", 0);
        const qreal maximum = flick->property("contentHeight").toDouble() - flick->height();
        require(maximum > 30, "popup has enough tracks/settings to scroll");
        const QPointF point = scroll->mapToScene(QPointF(scroll->width() / 2, scroll->height() / 2));
        QWheelEvent wheel(point, window->mapToGlobal(point.toPoint()), QPoint(), QPoint(0, -120),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(window, &wheel); pause(220);
        require(qAbs(flick->property("contentY").toDouble() - qMin<qreal>(250, maximum)) < 2,
                "one popup wheel notch uses the configured step and respects the bottom boundary");
        flick->setProperty("contentY", 0);
    };
    require(player->audioTracks().size() == 3, "multiple audio tracks loaded for scrolling");
    for (const QString &name : {QStringLiteral("audioTrack"), QStringLiteral("subtitleTrack")}) {
        QQmlExpression open(qmlContext(player), viewer,
            name + "Popup.height = 100; " + name + "Popup.open(); " + name + "Scroll");
        auto scroll = qobject_cast<QQuickItem *>(open.evaluate().value<QObject *>());
        pause(150); require(!open.hasError(), "track popup opens");
        checkWheel(scroll);
        QQmlExpression close(qmlContext(player), viewer, name + "Popup.close()"); close.evaluate();
    }
    {
        QQmlExpression openStyle(qmlContext(player), viewer, "subtitleStylePopup.open(); subtitleStylePopup.contentItem");
        auto container = qobject_cast<QQuickItem *>(openStyle.evaluate().value<QObject *>());
        pause(180);
        require(container && !openStyle.hasError(), "appearance popup opens");
        QQmlExpression styleScroll(qmlContext(player), viewer, "subtitleStyleScroll");
        checkWheel(qobject_cast<QQuickItem *>(styleScroll.evaluate().value<QObject *>()));
        std::fprintf(stderr, "PASS: real wheel events scroll audio, CC and subtitle appearance by the configured step\n");
        auto slider = visualItem(container, "subtitleTransparencySlider");
        require(slider, "transparency slider found");
        auto scrollIntoView = [&](QQuickItem *item) {
            for (auto parent = item->parentItem(); parent; parent = parent->parentItem()) {
                if (!parent->property("contentY").isValid()) continue;
                const qreal delta = item->mapToItem(parent, QPointF()).y() + item->height() / 2 - parent->height() / 2;
                parent->setProperty("contentY", qBound<qreal>(0, parent->property("contentY").toDouble() + delta,
                    qMax<qreal>(0, parent->property("contentHeight").toDouble() - parent->height())));
                break;
            }
            pause(80);
        };
        scrollIntoView(slider);
        style->setBackgroundOpacity(1.0);
        const QPointF point = slider->mapToScene(QPointF(slider->width() - 5, slider->height() / 2));
        const QPointF global = window->mapToGlobal(point.toPoint());
        QMouseEvent press(QEvent::MouseButtonPress, point, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, point, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(window, &press); QApplication::sendEvent(window, &release); pause(80);
        std::fprintf(stderr, "Transparency pointer: slider %.3f backend %.3f\n", slider->property("value").toDouble(), style->backgroundOpacity());
        QQmlExpression background(qmlContext(player), viewer, "subtitleBackground.tintOpacity");
        require(style->backgroundOpacity() < 0.05, "pointer controls background transparency");
        style->setBackgroundOpacity(0);
        style->setBackgroundBlur(48);
        QQmlExpression blurEnabled(qmlContext(player), viewer, "subtitleBackground.blurActive");
        require(background.evaluate().toDouble() == 0 && !blurEnabled.evaluate().toBool(),
                "full transparency removes tint and blur patch");
        style->setBackgroundColor(QColor("#806AF8BE")); style->setBackgroundOpacity(0.5);
        require(qAbs(background.evaluate().toDouble() - 0.25) < 0.005, "color alpha and background opacity compose");
        auto blurSlider = visualItem(container, "subtitleBlurSlider");
        require(blurSlider, "blur slider found"); scrollIntoView(blurSlider);
        const QPointF blurPoint = blurSlider->mapToScene(QPointF(blurSlider->width() - 5, blurSlider->height() / 2));
        const QPointF blurGlobal = window->mapToGlobal(blurPoint.toPoint());
        QMouseEvent blurPress(QEvent::MouseButtonPress, blurPoint, blurGlobal, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent blurRelease(QEvent::MouseButtonRelease, blurPoint, blurGlobal, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        style->setBackgroundBlur(0);
        QApplication::sendEvent(window, &blurPress); QApplication::sendEvent(window, &blurRelease); pause(50);
        require(style->backgroundBlur() >= 46, "pointer controls blur amount");
        // Select independent modes through the same actions as the dropdowns.
        style->setSubtitleShadowMode(2); style->setSdhShadowMode(0); style->setForcedShadowMode(4);
        for (const auto name : {"normalSubtitleShadowPicker", "sdhSubtitleShadowPicker", "forcedSubtitleShadowPicker"})
            require(visualItem(container, name), "per-kind shadow selector exists");
        SubtitleStyleManager saved;
        require(saved.subtitleShadowMode() == 2 && saved.sdhShadowMode() == 0 && saved.forcedShadowMode() == 4
                && saved.backgroundBlur() == style->backgroundBlur() && qAbs(saved.backgroundOpacity() - 0.5) < 0.001,
                "separate shadows and background settings persist");
        QQmlExpression closeStyle(qmlContext(player), viewer, "subtitleStylePopup.close()"); closeStyle.evaluate();
    }
    QQmlExpression tags(qmlContext(player), viewer,
        "JSON.stringify([subtitleSourceTags(subtitleSourceForTrack(0)), subtitleSourceTags(subtitleSourceForTrack(1)), subtitleSourceTags(subtitleSourceForTrack(2))])");
    require(tags.evaluate().toString() == "[[],[\"FORCED\"],[\"SDH\"]]" && !tags.hasError(), "embedded disposition labels remain distinct");
    QMetaObject::invokeMethod(viewer, "selectEmbeddedSubtitle", Q_ARG(QVariant, QVariant(1)));
    require(until([&] { return overlay->property("semanticKind").toString() == "forced"; }), "forced track uses forced appearance");
    QQmlExpression shadow(qmlContext(player), viewer, "subtitleText.shadowMode");
    require(shadow.evaluate().toInt() == 4, "forced shadow applied to live text");
    QMetaObject::invokeMethod(viewer, "selectEmbeddedSubtitle", Q_ARG(QVariant, QVariant(2)));
    require(until([&] { return overlay->property("semanticKind").toString() == "sdh"; }), "SDH track uses SDH appearance");
    require(shadow.evaluate().toInt() == 0, "SDH shadow independent from forced");
    english();
    require(shadow.evaluate().toInt() == 2, "normal shadow independent from other tracks");
    std::fprintf(stderr, "PASS: transparency, blur controls, persistent per-kind shadows and track labels\n");
    style->setBackgroundBlur(24);
    require(!manager->downloadsAllowed(), "AI downloads default to disabled in a clean profile");
    english();
    QMetaObject::invokeMethod(viewer, "openSubtitleAiTranslateDialog");
    pause(100);
    auto permission = visualItem(window->contentItem(), "aiDownloadPermission");
    require(permission && permission->isVisible() && !permission->property("checked").toBool(),
            "idle AI dialog shows an unchecked download permission");
    const QPointF permissionTop = permission->mapToItem(window->contentItem(), QPointF());
    require(permissionTop.y() >= 0 && permissionTop.y() + permission->height() <= window->height(),
            "download permission fits in the video dialog");
    const auto clickPermission = [&] {
        const QPointF position = permission->mapToScene(QPointF(9, permission->height() / 2));
        const QPointF global = window->mapToGlobal(position.toPoint());
        QMouseEvent press(QEvent::MouseButtonPress, position, global,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, position, global,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(window, &press);
        QApplication::sendEvent(window, &release);
    };
    clickPermission();
    pause(30);
    require(manager->downloadsAllowed(), "checkbox grants download permission");
    {
        SubtitleAiManager restored;
        require(restored.downloadsAllowed(), "download choice persists across manager instances");
    }
    clickPermission();
    pause(30);
    require(!manager->downloadsAllowed(), "checkbox revokes download permission");
    QQmlExpression closeAi(qmlContext(player), viewer, "subtitleAiSubtitlePopup.close()");
    closeAi.evaluate();
    require(!closeAi.hasError(), "AI permission dialog closes");
    std::fprintf(stderr, "PASS: download permission is visible, opt-in, persistent and revocable\n");
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
        longestGap = 0; pulses = 0; pulseClock.restart(); opening.restart(); heartbeat.start();
        viewer->setProperty("files", QVariantList{QVariantMap{{"path", realPath},
            {"url", QUrl::fromLocalFile(realPath)}, {"name", "Real playback fixture"}}});
        require(until([&] { return player->source() == QUrl::fromLocalFile(realPath) && player->duration() > 20000; }),
                "real video fixture loads");
        player->play();
        require(until([&] { return viewer->property("firstVideoFrameReady").toBool(); }), "real film first frame arrives");
        const auto realFirstFrame = opening.elapsed();
        QMetaObject::invokeMethod(viewer, "selectEmbeddedSubtitle", Q_ARG(QVariant, QVariant(1)));
        require(until([&] { return !manager->selectedSubtitleLoading(); }), "real film embedded CC extraction completes");
        heartbeat.stop();
        std::fprintf(stderr, "Real film opening: first frame %lld ms, longest UI gap %lld ms, %d heartbeat ticks\n",
                     realFirstFrame, longestGap, pulses);
        require(longestGap < 500 && !viewer->property("videoLoading").toBool(),
                "real film playback and embedded extraction leave UI responsive and spinner stops");
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
    QMetaObject::invokeMethod(viewer, "close"); pause(80);
    viewer->setProperty("files", QVariantList{QVariantMap{{"path", path}, {"url", QUrl::fromLocalFile(path)}, {"name", "fixture.mkv"}}});
    QMetaObject::invokeMethod(window, "openVideo");
    require(until([&] { return player->source() == QUrl::fromLocalFile(path) && player->audioTracks().size() == 3; }),
            "restart fixture ready");
    QMetaObject::invokeMethod(viewer, "selectAudioTrack", Q_ARG(QVariant, QVariant(2)));
    QMetaObject::invokeMethod(viewer, "selectEmbeddedSubtitle", Q_ARG(QVariant, QVariant(2)));
    require(until([&] { return !manager->selectedSubtitleLoading() && manager->selectedSubtitlePath().endsWith("#2"); }),
            "restart subtitle selected");
    QMetaObject::invokeMethod(viewer, "close"); pause(80);
    auto saved = bookmarks.trackPreferencesFor(path);
    require(saved.value("audioTrack").toInt() == 2 && saved.value("subtitleKind").toString() == "embedded"
            && saved.value("subtitleTrack").toInt() == 2, "closing saves the last chosen audio and subtitle");
    require(QMetaObject::invokeMethod(&app, "aboutToQuit", Qt::DirectConnection), "application quit notification delivered");
    delete window; engine.collectGarbage();
    saved = bookmarks.trackPreferencesFor(path);
    require(saved.value("audioTrack").toInt() == 2 && saved.value("subtitleKind").toString() == "embedded"
            && saved.value("subtitleTrack").toInt() == 2, "quit and destruction cannot overwrite the saved CC with off");
    std::fprintf(stderr, "PASS: last track preferences survive close, quit notification and component destruction\n");
    return 0;
}
