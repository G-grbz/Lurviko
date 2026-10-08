#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QTimer>
#include <cstdio>
#include <functional>
#include "backend_test_types.h"

static void pause(int ms) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}
static bool until(std::function<bool()> check) {
    QElapsedTimer timer; timer.start();
    while (!check() && timer.elapsed() < 15000) pause(10);
    return check();
}
static QList<QQuickItem *> visualItems(QQuickItem *parent) {
    QList<QQuickItem *> result;
    for (auto child : parent->childItems()) {
        result << child;
        result << visualItems(child);
    }
    return result;
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
static bool createFile(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(QByteArray(32768, 'x')) == 32768;
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("Lurviko-QA"); app.setApplicationName("Sync");
    QQuickStyle::setStyle("Basic");
    // REGISTER_BACKEND_TYPES
    QQmlApplicationEngine engine;
    engine.addImageProvider("gfilethumb", new FixtureImages);
    engine.addImageProvider("bundledicon", new BundledIconProvider);
    engine.loadData(R"qml(
import QtQuick
import QtQuick.Controls
import Lurviko.App
ApplicationWindow {
    width: 1020; height: 700; visible: true
    QtObject { id: language; property string language: "tr"; function t(key) { return key } }
    MediaGalleryView {
        id: gallery; objectName: "gallery"; anchors.fill: parent
        lang: language; files: []; categoryKey: "images"; categoryIcon: "image.svg"
        gallerySize: 230; contentIndexModel: null
    }
    function fillGallery() {
        const result = []
        for (let i = 0; i < 1838; ++i)
            result.push({ path: "/fixture/Camera/" + i + ".jpg", name: i + ".jpg",
                parentPath: "/fixture/Camera", size: 32768, modifiedMs: 1700000000000 - i * 1000,
                url: "file:///fixture/Camera/" + i + ".jpg" })
        gallery.files = result
    }
    function updateGallery() {
        const result = gallery.files.slice()
        result[0] = Object.assign({}, result[0], {size: result[0].size + 1})
        gallery.files = result
    }
}
)qml");
    if (engine.rootObjects().isEmpty()) return 1;
    auto window = qobject_cast<QQuickWindow *>(engine.rootObjects()[0]);
    auto gallery = window->findChild<QQuickItem *>("gallery");
    QMetaObject::invokeMethod(window, "fillGallery"); pause(250);
    QList<QPointer<QQuickItem>> originalCards;
    for (auto item : visualItems(window->contentItem()))
        if (item->objectName() == "galleryMediaCard") originalCards << item;
    if (originalCards.isEmpty()) return 2;
    for (int update = 0; update < 8; ++update) {
        QMetaObject::invokeMethod(window, "updateGallery"); pause(30);
    }
    int retained = 0;
    for (const auto &card : originalCards) if (card) ++retained;
    std::fprintf(stderr, "Gallery retained %d/%d cards after 8 small sync updates\n",
                 retained, int(originalCards.size()));

    const QString base = QString::fromLocal8Bit(qgetenv("LURVIKO_SYNC_TEST_ROOT"));
    for (const auto &folder : {"photos", "music", "empty"}) QDir().mkpath(base + "/" + folder);
    for (int i = 0; i < 30; ++i)
        if (!createFile(base + QString("/photos/%1.jpg").arg(i))) return 3;
    QSettings settings;
    for (const auto &key : {"images", "music", "videos", "documents", "archives", "appimage"}) {
        const QString folder = QString(key) == "images" ? "photos" : QString(key) == "music" ? "music" : "empty";
        settings.setValue("contentIndex/mediaRoots/" + QString(key), QStringList{base + "/" + folder});
    }
    settings.sync();
    ContentIndexModel index;
    if (!until([&] {return index.countForCategory("images") == 30 && !index.indexing();})) return 4;
    DirectoryModel images;
    images.setContentIndexSource(&index); images.setLocation("category:/images");
    if (!until([&] {return !images.loading() && images.rowCount() == 30;})) return 5;
    pause(150);
    int resets = 0;
    int loadingScreens = 0;
    QObject::connect(&images, &QAbstractItemModel::modelReset, [&] {++resets;});
    QObject::connect(&images, &DirectoryModel::loadingChanged, [&] {
        if (images.loading()) ++loadingScreens;
    });
    if (!createFile(base + "/music/new.mp3")) return 6;
    if (!until([&] {return index.countForCategory("music") == 1 && !index.indexing();})) return 7;
    pause(200);
    const int unrelatedResets = resets;
    std::fprintf(stderr, "Image-category resets after music update: %d\n", unrelatedResets);

    qputenv("LURVIKO_TEST_SLOW_STORAGE", "1");
    QElapsedTimer probeClock; probeClock.start();
    index.homeStorageTotalBytes();
    const qint64 blockingProbeMs = probeClock.elapsed();
    std::fprintf(stderr, "Control: synchronous storage probe blocks for %lld ms\n", blockingProbeMs);
    if (blockingProbeMs < 150) return 12;
    qint64 maxGap = 0;
    QElapsedTimer heartbeatClock; heartbeatClock.start();
    QTimer heartbeat;
    QObject::connect(&heartbeat, &QTimer::timeout, [&] {
        maxGap = qMax(maxGap, heartbeatClock.restart());
    });
    heartbeat.start(2);
    if (!createFile(base + "/photos/synced.jpg")) return 8;
    if (!until([&] {return !images.loading() && images.rowCount() == 31;})) return 9;
    pause(100);
    qunsetenv("LURVIKO_TEST_SLOW_STORAGE"); heartbeat.stop();
    std::fprintf(stderr, "Main-thread maximum gap with slow disk probe: %lld ms\n", maxGap);
    if (images.storageTotalBytes() <= 0) return 10;
    // Syncthing writes a temporary file and atomically replaces the final
    // path. Exercise a metadata-only index change with the same file count.
    const QString replacement = base + "/photos/.syncthing.synced.jpg.tmp";
    if (!createFile(replacement)) return 13;
    QFile modified(replacement);
    if (!modified.open(QIODevice::Append) || modified.write("x") != 1) return 13;
    modified.close();
    if (std::rename(qPrintable(replacement), qPrintable(base + "/photos/synced.jpg")) != 0) return 13;
    if (!until([&] {
        for (int row = 0; row < images.rowCount(); ++row) {
            const auto item = images.itemAt(row);
            if (item.value("name").toString() == "synced.jpg"
                    && item.value("size").toLongLong() == 32769) return true;
        }
        return false;
    })) return 14;
    if (!QFile::remove(base + "/photos/synced.jpg")) return 15;
    if (!until([&] {return !images.loading() && images.rowCount() == 30;})) return 16;
    std::fprintf(stderr, "PASS: filesystem watcher publishes image additions, metadata-only changes and deletions\n");
    if (loadingScreens != 0) return 17;
    std::fprintf(stderr, "PASS: background synchronization never shows a blocking loading screen\n");
    if (retained != originalCards.size() || unrelatedResets != 0 || maxGap >= 150) return 11;
    std::fprintf(stderr, "PASS: live gallery cards reused, unrelated categories ignored, slow storage cannot block category input\n");
    return 0;
}
