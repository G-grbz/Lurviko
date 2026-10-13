#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQuickTextureFactory>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QSettings>
#include <QElapsedTimer>
#include <QDateTime>
#include <QEventLoop>
#include <QTimer>
#include <cstdio>
#include <functional>
#include <memory>
#include "backend_test_types.h"

static void require(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static void pause(int ms) { QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec(); }
static bool until(const std::function<bool()> &check) {
    QElapsedTimer clock; clock.start();
    while (!check() && clock.elapsed() < 10000) pause(10);
    return check();
}
static qint64 rssKiB() {
    QFile file("/proc/self/status"); require(file.open(QIODevice::ReadOnly), "memory statistics available");
    for (const auto &line : file.readAll().split('\n'))
        if (line.startsWith("VmRSS:")) return line.simplified().split(' ').value(1).toLongLong();
    return 0;
}
static QList<QQuickItem *> presentations(QObject *root) {
    QList<QQuickItem *> result;
    for (auto item : root->findChildren<QQuickItem *>())
        if (QString::fromLatin1(item->metaObject()->className()).startsWith("MusicExpandedView_")) result.append(item);
    return result;
}
static void invoke(QObject *root, const char *name) { require(QMetaObject::invokeMethod(root, name), name); }

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("Lurviko-QA"); app.setApplicationName("MusicPresentation");
    app.setApplicationVersion(LURVIKO_VERSION);
    app.setQuitOnLastWindowClosed(false);
    QQuickStyle::setStyle("Basic");
    // REGISTER_BACKEND_TYPES
    const bool baseline = qEnvironmentVariableIsSet("LURVIKO_PRESENTATION_BASELINE");
    const QString base = QString::fromLocal8Bit(qgetenv("LURVIKO_PRESENTATION_ROOT"));
    const QString folder = base + "/folder";
    QDir().mkpath(folder);
    QSettings().setValue("updates/lastCheck", QDateTime::currentSecsSinceEpoch());
    for (const auto &key : {"documents", "appimage", "archives", "images", "music", "videos"})
        QSettings().setValue(QString("contentIndex/mediaRoots/") + key, QStringList{folder});
    QQmlApplicationEngine engine;
    auto *thumbnails = new ThumbnailProvider;
    engine.addImageProvider("gfilethumb", thumbnails);
    engine.addImageProvider("systemicon", new SystemIconProvider);
    engine.addImageProvider("bundledicon", new BundledIconProvider);
    engine.addImageProvider("folderart", new FolderIconProvider);
    engine.setInitialProperties({{"launchLocation", QUrl::fromLocalFile(folder).toString()}});
    engine.loadFromModule("Lurviko.App", "Main");
    require(!engine.rootObjects().isEmpty(), "actual Main loads");
    QObject *root = engine.rootObjects().first();
    QQuickItem *player = nullptr;
    for (auto candidate : root->findChildren<QQuickItem *>())
        if (candidate->metaObject()->indexOfProperty("sourceLibrary") >= 0) player = candidate;
    require(player, "shared playback engine exists");
    pause(400);
    std::fprintf(stderr, "MEMORY idle: %lld KiB, %lld expanded views\n", (long long)rssKiB(), (long long)presentations(root).size());
    if (!baseline) require(presentations(root).isEmpty(), "unused expanded views are not allocated");

    // Warm the real provider with more full-sized previews than its budget.
    // The second pass checks recent navigation still reuses the cached images.
    QStringList images;
    for (int i = 0; i < 80; ++i) {
        QImage image(512, 512, QImage::Format_ARGB32);
        image.fill(QColor::fromHsv(i * 4, 160, 220));
        const QString path = base + QString("/cover-%1.png").arg(i);
        require(image.save(path), "preview fixture saves"); images.append(path);
        auto *response = thumbnails->requestImageResponse(path, QSize(512, 512));
        bool done = false;
        QObject::connect(response, &QQuickImageResponse::finished, &app, [&] { done = true; });
        require(until([&] { return done; }), "preview completes");
        std::unique_ptr<QQuickTextureFactory> texture(response->textureFactory());
        require(texture && texture->textureSize() == QSize(512, 512), "preview resolution is preserved");
        delete response;
    }
    QElapsedTimer warm; warm.start();
    for (const auto &path : images.mid(70)) {
        auto *response = thumbnails->requestImageResponse(path, QSize(512, 512));
        bool done = false;
        QObject::connect(response, &QQuickImageResponse::finished, &app, [&] { done = true; });
        require(until([&] { return done; }), "recent preview revisit completes"); delete response;
    }
    std::fprintf(stderr, "MEMORY warm previews: %lld KiB; 10 revisits: %lld ms\n", (long long)rssKiB(), (long long)warm.elapsed());

    const QVariantList queue{QVariantMap{{"itemUrl", "file:///fixture/song.mp3"}, {"name", "Fixture song"},
        {"thumbnailSource", QUrl::fromLocalFile(images.last()).toString()}}};
    player->setProperty("queue", queue); player->setProperty("currentIndex", 0);
    player->setProperty("opened", true); player->setProperty("panelVisible", true);
    player->setProperty("lyricsText", QStringLiteral("A realistic lyric line with words and a newline.\n").repeated(1000));
    const QString current = player->property("currentUrl").toString();
    std::fprintf(stderr, "STAGE open expanded\n");
    invoke(root, "openExpandedMusic");
    require(until([&] { for (auto view : presentations(root)) if (view->isVisible()) return true; return false; }), "expanded view opens");
    for (auto view : presentations(root)) if (view->isVisible()) view->setProperty("sideTab", 2);
    std::fprintf(stderr, "STAGE minimize expanded\n");
    invoke(root, "minimizeExpandedMusic"); pause(150);
    if (!baseline) require(presentations(root).isEmpty(), "minimizing releases expanded UI resources");
    std::fprintf(stderr, "STAGE reopen expanded\n");
    invoke(root, "openExpandedMusic"); pause(150);
    if (!baseline) for (auto view : presentations(root)) if (view->isVisible())
        require(view->property("sideTab").toInt() == 2, "expanded tab survives recreation");
    std::fprintf(stderr, "STAGE detach\n");
    invoke(root, "openDetachedMusic"); pause(150);
    QQuickWindow *detached = nullptr;
    for (auto window : root->findChildren<QQuickWindow *>()) if (window->isVisible()) detached = window;
    require(detached && !detached->transientParent(), "detached window remains an independent task");
    invoke(root, "minimizeExpandedMusic"); pause(150);
    if (!baseline) require(presentations(root).isEmpty(), "detached UI is released on minimize");
    const QString location = root->property("sidebarLocation").toString();
    invoke(player, "openNormal"); pause(100);
    require(root->property("sidebarLocation").toString() == location && player->property("panelVisible").toBool()
            && !root->property("musicExpanded").toBool() && !root->property("musicDetached").toBool(), "restoring compact player does not navigate");
    require(player->property("opened").toBool() && player->property("currentUrl").toString() == current,
            "presentation changes retain the shared track");
    std::fprintf(stderr, "MEMORY returned to compact: %lld KiB\nPASS: real Main presentation lifecycle, tab retention, independent detached window and unchanged playback selection\n", (long long)rssKiB());
}
