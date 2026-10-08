#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QThreadPool>
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
static bool until(const std::function<bool()> &check, int timeout = 12000) {
    QElapsedTimer timer; timer.start();
    while (!check() && timer.elapsed() < timeout) pause(10);
    return check();
}
static void write(const QString &path, const QByteArray &bytes, bool append = false) {
    QFile f(path); require(f.open(QIODevice::WriteOnly | (append ? QIODevice::Append : QIODevice::Truncate)), "fixture opens");
    require(f.write(bytes) == bytes.size(), "fixture writes");
}
static QByteArray read(const QString &path) {
    QFile f(path); require(f.open(QIODevice::ReadOnly), "fixture reads"); return f.readAll();
}
static qint64 rssKiB() {
    for (const auto &line : read("/proc/self/status").split('\n'))
        if (line.startsWith("VmRSS:")) return line.simplified().split(' ').value(1).toLongLong();
    return 0;
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("Lurviko-QA"); app.setApplicationName("Music");
    const QString base = QString::fromLocal8Bit(qgetenv("LURVIKO_MUSIC_TEST_ROOT"));
    const QString bin = base + "/bin";
    QDir().mkpath(bin); QDir().mkpath(base + "/files");
    qputenv("PATH", bin.toUtf8() + ':' + qgetenv("PATH"));
    qputenv("PROBE_LOG", (base + "/probes.log").toUtf8());
    qputenv("FAKE_PROBE_DELAY", "0.15");
    write(bin + "/ffprobe", R"py(#!/usr/bin/python3
import fcntl,json,os,time
with open(os.environ['PROBE_LOG'],'a') as f:
    fcntl.flock(f,fcntl.LOCK_EX)
    f.write('start '+os.path.basename(__import__('sys').argv[-1])+'\n');f.flush()
time.sleep(float(os.environ.get('FAKE_PROBE_DELAY','0')))
print(json.dumps({'format':{'tags':{'title':'Probed title','artist':'Test artist'}}}))
with open(os.environ['PROBE_LOG'],'a') as f:
    fcntl.flock(f,fcntl.LOCK_EX)
    f.write('end\n');f.flush()
)py");
    QFile::setPermissions(bin + "/ffprobe", QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);

    const auto url = [&](const QString &name) { return QUrl::fromLocalFile(base + "/files/" + name + ".mp3").toString(); };
    for (const QString &name : {"a", "b", "c"}) {
        write(base + "/files/" + name + ".mp3", "audio");
        write(base + "/files/" + name + ".lrc", "[00:01.00]First line\n[00:02.00]Second line\n");
    }
    MusicMetadataManager metadata;
    QString readyUrl; QVariantMap result; int readyCount = 0;
    QObject::connect(&metadata, &MusicMetadataManager::metadataReady, &app, [&](const QString &u, const QVariantMap &r) {
        readyUrl = u; result = r; ++readyCount;
    });
    int ticks = 0; QTimer heartbeat; heartbeat.setInterval(10);
    QObject::connect(&heartbeat, &QTimer::timeout, &app, [&] { ++ticks; }); heartbeat.start();
    QElapsedTimer requestTime; requestTime.start();
    metadata.request(url("a")); metadata.request(url("b")); metadata.request(url("c"));
    require(requestTime.elapsed() < 50, "metadata request does not block UI");
    require(until([&] { return readyUrl == url("c"); }), "latest lyrics request completes");
    require(readyCount == 1 && ticks >= 15, "stale lyrics ignored and event loop responsive");
    require(result.value("lyricsLines").toList().size() == 2, "timed lyrics parsed");
    require(result.value("title").toString() == "Probed title", "audio metadata retained");
    require(!read(base + "/probes.log").contains("start b.mp3"), "superseded pending request not probed");
    const int cachedCount = readyCount; metadata.request(url("c"));
    require(readyCount == cachedCount + 1, "unchanged lyrics reuse cache");
    write(base + "/files/c.lrc", "[00:01.00]Updated lyrics with a new size\n");
    metadata.request(url("c"));
    require(until([&] { return result.value("lyricsText").toString().startsWith("Updated"); }), "changed sidecar invalidates cache");
    metadata.request(url("a")); metadata.clear();
    const int clearedCount = readyCount; pause(350);
    require(readyCount == clearedCount, "closing discards in-flight result");
    write(base + "/files/oversize.lrc", QByteArray(1024 * 1024 + 1, 'x'));
    metadata.request(url("oversize"));
    require(until([&] { return readyUrl == url("oversize"); }), "oversized lyrics complete safely");
    require(result.value("lyricsText").toString().isEmpty(), "oversized sidecar is bounded");

    qputenv("FAKE_PROBE_DELAY", "0");
    QByteArray lyric;
    for (int i = 0; i < 2500; ++i) lyric += "[00:01.00]" + QByteArray(88, 'x') + '\n';
    qint64 warmed = 0;
    for (int i = 0; i < 80; ++i) {
        const QString name = QStringLiteral("stress-%1").arg(i);
        write(base + "/files/" + name + ".lrc", lyric);
        metadata.request(url(name));
        require(until([&] { return readyUrl == url(name); }), "repeated lyrics request completes");
        pause(1);
        if (i == 29) warmed = rssKiB();
    }
    const qint64 steady = rssKiB();
    std::fprintf(stderr, "Lyrics RSS after 30/80 tracks: %lld / %lld KiB\n", (long long)warmed, (long long)steady);
    require(steady - warmed < 24 * 1024, "lyrics cache does not retain every track");
    metadata.clear();

    write(base + "/probes.log", ""); qputenv("FAKE_PROBE_DELAY", "0.10");
    MusicLibraryManager library;
    for (int generation = 0; generation < 30; ++generation) {
        QVariantList items;
        for (int i = 0; i < 3; ++i) {
            const QString name = QStringLiteral("generation-%1-%2").arg(generation).arg(i);
            write(base + "/files/" + name + ".mp3", "audio");
            items.push_back(QVariantMap{{"itemUrl", url(name)}, {"name", name}, {"size", 5}, {"modifiedMs", generation}});
        }
        library.setItems(items); pause(3);
    }
    require(until([&] { return !library.loading(); }), "latest music scan completes");
    int active = 0, peak = 0;
    for (const auto &line : read(base + "/probes.log").split('\n')) {
        if (line.startsWith("start")) peak = qMax(peak, ++active);
        else if (line == "end") --active;
    }
    require(peak == 1 && active == 0, "library generations never overlap workers");
    require(library.tracks().size() == 3 && library.tracks().first().toMap().value("name").toString().startsWith("generation-29-"), "latest library generation retained");

    write(base + "/files/transcode.mkv", "initial");
    DirectoryModel directory; directory.setLocation(base + "/files");
    require(until([&] { return !directory.loading() && directory.rowCount() > 0; }), "watched directory opens");
    const auto rowFor = [&](const QString &name) {
        for (int row = 0; row < directory.rowCount(); ++row)
            if (directory.data(directory.index(row, 0), DirectoryModel::NameRole).toString() == name) return row;
        return -1;
    };
    auto revision = [&](const QString &name) { return directory.data(directory.index(rowFor(name), 0), DirectoryModel::PreviewRevisionRole).toString(); };
    const QString original = revision("transcode.mkv");
    pause(400);
    for (int i = 0; i < 5; ++i) {
        write(base + "/files/transcode.mkv", QByteArray(100, 'v'), true);
        require(until([&] { return directory.data(directory.index(rowFor("transcode.mkv"), 0), DirectoryModel::SizeRole).toLongLong() == 7 + (i + 1) * 100; }), "transcode size updates live");
        require(revision("transcode.mkv") == original, "writing video keeps its thumbnail revision");
        pause(150);
    }
    require(until([&] { return revision("transcode.mkv") != original; }, 6000), "video thumbnail updates once after writing settles");
    const QString oldAudio = revision("a.mp3");
    write(base + "/files/a.mp3", "updated-audio");
    require(until([&] { return revision("a.mp3") != oldAudio; }), "non-video preview still invalidates normally");

    BundledIconProvider icons;
    QSize encodedSize;
    require(!icons.requestImage("info.svg%7C%231a2030", &encodedSize, QSize(24, 24)).isNull(), "Qt Controls encoded icon URL renders");
    for (const QString &name : {"history.svg", "info.svg", "moon.svg", "lock.svg", "unlock.svg"}) {
        for (const QColor &color : {QColor("#1a2030"), QColor("#f4f7fc")}) {
            QSize size; const QImage icon = icons.requestImage(name + '|' + color.name(), &size, QSize(24, 24));
            require(!icon.isNull() && size == QSize(24, 24), "theme icon renders at bounded size");
            bool opaque = false;
            for (int y = 0; y < icon.height(); ++y) for (int x = 0; x < icon.width(); ++x) {
                if (icon.pixelColor(x, y).alpha() == 255) {
                    opaque = true; require(icon.pixelColor(x, y) == color, "monochrome icon follows foreground");
                }
            }
            require(opaque, "theme icon contains a visible glyph");
        }
    }
    QThreadPool::globalInstance()->waitForDone();
    std::fprintf(stderr, "PASS: responsive bounded lyrics, exclusive library scans, stable transcode thumbnails and theme icons\n");
}
