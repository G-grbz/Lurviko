#include <QApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QImage>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QTimer>
#include <QUrl>
#include <cstdio>
#include <functional>
#include "backend_test_types.h"

static void require(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static void pause(int ms) {
    QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec();
}
static bool until(const std::function<bool()> &check) {
    QElapsedTimer timer; timer.start();
    while (!check() && timer.elapsed() < 8000) pause(10);
    return check();
}
static QByteArray command(const QString &program, const QStringList &arguments,
                          const QProcessEnvironment &environment = QProcessEnvironment::systemEnvironment()) {
    QProcess process; process.setProcessEnvironment(environment); process.start(program, arguments);
    require(process.waitForStarted(), "test command starts");
    require(until([&] { return process.state() == QProcess::NotRunning; }), "test command completes");
    if (process.exitCode()) std::fprintf(stderr, "%s\n", process.readAllStandardError().constData());
    require(process.exitCode() == 0, "test command succeeds");
    return process.readAllStandardOutput().trimmed();
}
static QByteArray playerctl(const QStringList &arguments) {
    return command("playerctl", QStringList{"-p", "lurviko"} + arguments);
}
static bool invoke(QObject *object, const char *method) { return QMetaObject::invokeMethod(object, method); }

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("Lurviko-QA"); app.setApplicationName("Mpris");
    QQuickStyle::setStyle("Basic");
    // REGISTER_BACKEND_TYPES
    const QString base = QString::fromUtf8(qgetenv("LURVIKO_MPRIS_TEST_ROOT"));
    const QString musicPath = base + "/Yanlış klasör - Şarkı.mp3";
    const QString videoPath = base + "/Güneş ve Yağmur.mp4";
    const QString coverPath = base + "/Kapak ıİşğüöç.png";
    QImage cover(160, 160, QImage::Format_RGB32); cover.fill(Qt::red);
    require(cover.save(coverPath), "Unicode cover fixture writes");
    command("ffmpeg", {"-v", "error", "-f", "lavfi", "-i", "sine=frequency=440:duration=24",
        "-i", coverPath, "-map", "0:a", "-map", "1:v", "-c:a", "libmp3lame", "-c:v", "mjpeg",
        "-disposition:v", "attached_pic", "-id3v2_version", "3",
        "-metadata", "title=Sar Zamanımızı Geriye Op.9", "-metadata", "artist=Şarkıcı Işık",
        "-metadata", "album=Gerçek Albüm", "-metadata", "date=2025", "-metadata", "genre=Türkçe Pop", musicPath});
    command("ffmpeg", {"-v", "error", "-f", "lavfi", "-i", "color=c=blue:s=160x90:r=10:d=24",
        "-c:v", "mpeg4", "-metadata", "title=Güneş ve Yağmur", videoPath});
    QQmlApplicationEngine engine;
    engine.addImageProvider("bundledicon", new BundledIconProvider);
    engine.addImageProvider("gfilethumb", new ThumbnailProvider);
    engine.loadData(R"QML(
import QtQuick
import QtQuick.Controls
import Lurviko.App
import Lurviko.Backend
ApplicationWindow {
    id: root
    width: 1000; height: 700; visible: true
    property var activeVideoViewer: null
    property bool mediaViewerConsumesF11: false
    property alias session: session
    property alias music: music
    property alias video: video
    LanguageManager { id: language }
    MusicPlayer { id: music; lang: language; visible: false }
    VideoViewer { id: video; lang: language; hostWindow: root; parent: Overlay.overlay }
    MediaSession { id: session; musicPlayer: music; videoPlayer: root.activeVideoViewer }
    function beginMusic(url) {
        music.queue = [{itemUrl: url, name: decodeURIComponent(url).split('/').pop(), thumbnailSource: ""}]
        music.currentIndex = 0
        music.opened = true
        music.loadCurrent(false)
    }
    function beginVideo(url) {
        video.files = [{url: url, path: decodeURIComponent(url).replace("file://", ""), name: "Güneş ve Yağmur.mp4"}]
        video.openAt(0)
    }
}
)QML", QUrl("qrc:/mpris-fixture.qml"));
    require(!engine.rootObjects().isEmpty(), "actual player/session QML loads");
    QObject *root = engine.rootObjects().first();
    auto *session = qobject_cast<MprisController *>(root->property("session").value<QObject *>());
    QObject *music = root->property("music").value<QObject *>();
    QObject *video = root->property("video").value<QObject *>();
    require(session && music && video, "players and MPRIS session exist");
    const QString musicUrl = QUrl::fromLocalFile(musicPath).toString();
    require(QMetaObject::invokeMethod(root, "beginMusic", Q_ARG(QVariant, musicUrl)), "playlist starts");
    require(until([&] { return session->metadata().value("xesam:album").toString() == "Gerçek Albüm"
                              && !session->artUrl().isEmpty(); }), "tags and cold playlist artwork load asynchronously");
    const QString format = "{{xesam:title}}|{{xesam:artist}}|{{xesam:album}}|{{xesam:contentCreated}}|{{xesam:genre}}";
    const QByteArray exported = playerctl({"metadata", "--format", format});
    std::fprintf(stderr, "Exported UTF-8 metadata: %s\n", exported.constData());
    require(exported == QString(
        "Sar Zamanımızı Geriye Op.9|Şarkıcı Işık|Gerçek Albüm|2025-01-01T00:00:00Z|Türkçe Pop").toUtf8(),
        "actual D-Bus/playerctl metadata preserves tags, punctuation and Turkish characters");
    const QUrl artwork(QString::fromUtf8(playerctl({"metadata", "mpris:artUrl"})));
    require(artwork.isLocalFile() && !QImage(artwork.toLocalFile()).isNull(), "playlist artwork is a readable file URI");
    require(!session->metadata().value("xesam:url").toString().contains("ı"), "Unicode media URI is percent-encoded");
    // Switch tracks before a worker finishes: its old artwork must never replace the new cover.
    session->setArtworkHint(""); session->setTrackUrl(QUrl::fromLocalFile(videoPath).toString());
    session->setTrackUrl(musicUrl); pause(150);
    require(session->artUrl() == artwork.toString(QUrl::FullyEncoded), "stale artwork completion cannot replace current artwork");
    const QString videoUrl = QUrl::fromLocalFile(videoPath).toString();
    require(QMetaObject::invokeMethod(root, "beginVideo", Q_ARG(QVariant, videoUrl)), "native video opens");
    require(until([&] { return session->trackUrl() == videoUrl && session->duration() > 0
                              && session->playbackStatus() == "Playing"; }), "video claims MPRIS with duration/status");
    playerctl({"pause"});
    require(until([&] { return session->playbackStatus() == "Paused" && video->property("paused").toBool(); }),
            "D-Bus pause reaches native video, not music");
    playerctl({"position", "5"});
    require(until([&] { return session->position() >= 4900; }), "MPRIS seek updates native video");
    playerctl({"volume", "0.35"});
    require(until([&] { return qAbs(video->property("volume").toDouble() - .35) < .001; }), "MPRIS volume reaches video");
    playerctl({"play"});
    require(until([&] { return session->playbackStatus() == "Playing"; }), "video resumes through MPRIS");
    require(invoke(video, "toggleHostFullScreen"), "fullscreen opens"); pause(150);
    require(session->active() && session->trackUrl() == videoUrl, "fullscreen keeps video session");
    require(invoke(video, "toggleHostFullScreen"), "fullscreen closes"); pause(150);
    require(session->active() && session->trackUrl() == videoUrl, "fullscreen exit keeps video session");
    require(invoke(video, "close"), "video closes");
    require(until([&] { return session->trackUrl() == musicUrl
                              && session->metadata().value("xesam:album").toString() == "Gerçek Albüm"; }),
            "closing video restores music metadata and controls");
    playerctl({"play"});
    require(until([&] { return music->property("playbackStatus").toString() == "Playing"; }), "MPRIS routes back to music");
    music->setProperty("opened", false);
    require(until([&] { return !session->active(); }), "closed players clear the session");
    require(!session->metadata().contains("xesam:album") && !session->metadata().contains("mpris:artUrl"),
            "old album/artwork never remains after close");
    std::puts("PASS: real tagged playlist metadata, UTF-8 playerctl, cold artwork, video pause/seek/volume/fullscreen and music handover");
    return 0;
}
