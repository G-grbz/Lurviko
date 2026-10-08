#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QQuickItem>
#include <QImage>
#include <cmath>
#include <cstdio>
#include <functional>
#include "backend_test_types.h"

static void require(bool ok, const char *message) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static void pause(int ms) { QEventLoop e; QTimer::singleShot(ms, &e, &QEventLoop::quit); e.exec(); }
static bool until(const std::function<bool()> &check) {
    QElapsedTimer timer; timer.start();
    while (!check() && timer.elapsed() < 8000) pause(10);
    return check();
}
static QQuickItem *findItem(QQuickItem *root, const QString &name) {
    if (root->objectName() == name) return root;
    for (auto *child : root->childItems()) if (auto *found = findItem(child, name)) return found;
    return nullptr;
}
static void checkTimelineMarkers(QQuickWindow *window, const QString &name, double duration) {
    auto *track = findItem(window->contentItem(), name);
    require(track && track->width() > 0, "chapter marker track is laid out");
    auto *first = findItem(track, name + "Boundary0");
    require(first && !first->isVisible(), "chapter at time zero does not draw a redundant boundary");
    for (int index = 1; index < 3; ++index) {
        auto *marker = findItem(track, name + "Boundary" + QString::number(index));
        require(marker, "embedded chapter has a timeline marker");
        const double expected = track->width() * (index * 2500.0) / duration;
        require(std::abs(marker->x() + marker->width() / 2 - expected) < 0.1,
                "chapter boundary aligns with its time after timeline resizing");
    }
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("Lurviko-QA"); app.setApplicationName("Chapters");
    QQuickStyle::setStyle("Basic");
    // REGISTER_BACKEND_TYPES
    const QString root = QString::fromUtf8(qgetenv("LURVIKO_CHAPTER_TEST_ROOT"));
    const auto mkv = QUrl::fromLocalFile(root + "/Bölümlü film ıİ.mkv");
    const auto mp4 = QUrl::fromLocalFile(root + "/chapters.mp4");
    const auto parsed = VideoChapterReader::parseChapters(R"json({"chapters":[
        {"start":50,"end":60,"time_base":"1/10","tags":{"TITLE":"Geç Bölüm"}},
        {"start_time":"0.25","end_time":"2.5","tags":{"title":"Giriş"}},
        {"start_time":"-1","end_time":"3"},
        {"start_time":"NaN"}, {"start_time":"inf"}, {"tags":{"title":"invalid"}}
    ]})json");
    require(parsed.size() == 2 && parsed[0].toMap().value("startMs").toLongLong() == 250, "timestamps sorted, malformed chapters skipped");
    require(parsed[1].toMap().value("startMs").toLongLong() == 5000, "timebase fallback converted to milliseconds");
    require(parsed[1].toMap().value("title").toString() == QStringLiteral("Geç Bölüm"), "Unicode and uppercase TITLE retained");
    require(VideoChapterReader::parseChapters("bad JSON").isEmpty(), "invalid JSON ignored");

    VideoChapterReader reader;
    int ticks = 0; QTimer pulse; pulse.setInterval(1);
    QObject::connect(&pulse, &QTimer::timeout, [&] { ++ticks; }); pulse.start();
    for (const auto &url : {mkv, mp4}) {
        reader.setSource(url);
        require(reader.loading(), "probe starts asynchronously");
        require(until([&] { return !reader.loading(); }), "actual probe completes");
        require(reader.error().isEmpty() && reader.chapters().size() == 3, "MKV/MP4 chapters read");
        require(reader.chapters()[0].toMap().value("title").toString() == QStringLiteral("Giriş · Şığ ıÖ"), "actual embedded Unicode title read");
        require(reader.chapterAt(2499) == 0 && reader.chapterAt(2500) == 1 && reader.chapterAt(8000) == -1, "active chapter follows boundaries");
    }
    require(ticks > 0, "UI event loop remains responsive during probe");
    reader.setSource({}); reader.setSource(mkv);
    require(!reader.loading() && reader.chapters().size() == 3, "reopening uses bounded cache");
    reader.setSource(QUrl::fromLocalFile(root + "/no-chapters.mkv"));
    require(until([&] { return !reader.loading(); }) && reader.chapters().isEmpty() && reader.error().isEmpty(), "chapterless media is valid");
    reader.setSource(QUrl::fromLocalFile(root + "/missing.mkv"));
    require(!reader.loading() && reader.chapters().isEmpty() && !reader.error().isEmpty(), "missing file clears previous data");
    reader.setSource({});
    reader.setSource(QUrl("https://127.0.0.1:1/unavailable.mkv"));
    reader.setSource({}); pause(100);
    require(!reader.loading() && reader.chapters().isEmpty() && reader.error().isEmpty(), "cancelled probe never publishes stale results");
    std::puts("PASS: chapter parsing, actual MKV/MP4 probing, Unicode, boundaries, cache and cancellation");
    if (app.arguments().contains("--backend-only")) return 0;

    QQmlApplicationEngine engine;
    engine.addImageProvider("bundledicon", new BundledIconProvider);
    engine.addImageProvider("gfilethumb", new ThumbnailProvider);
    engine.loadData(R"qml(import QtQuick
import QtQuick.Controls
import Lurviko.App
import Lurviko.Backend
ApplicationWindow {
    id: root; width: 1100; height: 750; visible: true
    property var activeVideoViewer: null
    property bool mediaViewerConsumesF11: false
    LanguageManager { id: language }
    VideoViewer { id: video; objectName: "video"; lang: language; hostWindow: root; parent: Overlay.overlay }
    function begin(url) {
        video.files = [{url:url, path:decodeURIComponent(url).replace("file://",""), name:"Bölümlü film ıİ.mkv"}]
        video.openAt(0)
    }
})qml");
    require(!engine.rootObjects().isEmpty(), "actual VideoViewer loads");
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QObject *video = window->findChild<QObject *>("video");
    require(QMetaObject::invokeMethod(window, "begin", Q_ARG(QVariant, mkv.toString())), "video starts");
    auto *chapters = video->findChild<VideoChapterReader *>("videoChapterReader");
    require(chapters && until([&] { return chapters->chapters().size() == 3 && video->property("duration").toDouble() > 0 && video->property("playing").toBool(); }), "video and chapter metadata ready");
    require(QMetaObject::invokeMethod(video, "pause"), "pause before choosing chapter");
    require(until([&] { return video->property("paused").toBool(); }), "video remains paused");
    pause(150);
    checkTimelineMarkers(window, "videoSeekChapterMarkers", video->property("duration").toDouble());
    checkTimelineMarkers(window, "videoPauseChapterMarkers", video->property("duration").toDouble());
    auto *slider = findItem(window->contentItem(), "videoSeekSlider");
    auto *track = findItem(window->contentItem(), "videoSeekChapterMarkers");
    auto *handle = qvariant_cast<QQuickItem *>(slider->property("handle"));
    require(handle && std::abs(track->width() - (slider->property("availableWidth").toDouble() - handle->width())) < 0.1,
            "chapter timeline matches the slider handle's seekable range");
    auto *button = video->findChild<QQuickItem *>("videoChaptersButton");
    auto *audio = video->findChild<QQuickItem *>("videoAudioButton");
    require(button && audio && button->mapToScene(QPointF()).x() < audio->mapToScene(QPointF()).x(), "chapters button is left of Audio");
    require(QMetaObject::invokeMethod(button, "clicked"), "chapter menu opens");
    auto *popup = video->findChild<QObject *>("videoChapterPopup");
    require(until([&] { return popup && popup->property("visible").toBool(); }), "chapter popup visible");
    pause(150);
    require(window->grabWindow().save("/tmp/lurviko-video-chapters.png"), "chapter selector screenshot saved");
    auto *row = findItem(window->contentItem(), "videoChapterRow1");
    require(row && QMetaObject::invokeMethod(row, "clicked"), "second chapter selected through real row");
    require(until([&] { return video->property("position").toDouble() >= 2450 && video->property("position").toDouble() <= 2550; }), "selected chapter seeks to correct time");
    require(!popup->property("visible").toBool() && video->property("paused").toBool(), "menu closes and pause state preserved");
    require(video->property("activeChapterIndex").toInt() == 1, "current chapter highlighted");
    require(QMetaObject::invokeMethod(video, "toggleHostFullScreen"), "enter fullscreen");
    require(until([&] { return video->property("hostFullScreen").toBool() && !video->property("movingToWindow").toBool(); }), "fullscreen entered");
    pause(150);
    checkTimelineMarkers(button->window(), "videoSeekChapterMarkers", video->property("duration").toDouble());
    require(QMetaObject::invokeMethod(button, "clicked"), "fullscreen chapter menu opens");
    require(until([&] { return popup->property("visible").toBool(); }), "chapter selection works in fullscreen");
    require(QMetaObject::invokeMethod(video, "handleGlobalAction", Q_ARG(QVariant, "escape")), "dismiss chapter popup with Escape");
    require(!popup->property("visible").toBool() && video->property("hostFullScreen").toBool(), "first Escape closes chapters and keeps fullscreen");
    require(QMetaObject::invokeMethod(video, "handleGlobalAction", Q_ARG(QVariant, "escape")), "leave fullscreen with next Escape");
    require(until([&] { return !video->property("hostFullScreen").toBool(); }), "fullscreen exited");
    require(QMetaObject::invokeMethod(video, "close"), "video closes");
    require(until([&] { return chapters->source().isEmpty(); }), "closing player clears reader");
    require(until([&] { return !findItem(window->contentItem(), "videoSeekChapterMarkersBoundary1"); }), "closing player clears stale timeline markers");
    std::puts("PASS: chapter menu, timeline boundaries/resizing, selection/seek, paused playback, fullscreen and Escape");
}
