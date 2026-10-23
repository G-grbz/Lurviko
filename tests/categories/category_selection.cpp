#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QThread>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QSettings>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <cstdio>
#include <functional>
#include "backend_test_types.h"

static void require(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static bool until(std::function<bool()> check) {
    QElapsedTimer clock; clock.start();
    while (!check() && clock.elapsed() < 15000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return check();
}
static QVariant evaluate(QObject *target, const QString &code) {
    QQmlExpression expression(qmlContext(target), target, code);
    auto value = expression.evaluate();
    require(!expression.hasError(), qPrintable(expression.error().toString()));
    return value;
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("Lurviko-QA"); app.setApplicationName("CategorySelection");
    QQuickStyle::setStyle("Basic");
    // REGISTER_BACKEND_TYPES
    const QString base = QString::fromLocal8Bit(qgetenv("LURVIKO_NAVIGATION_TEST_ROOT"));
    const QStringList keys = {"images", "videos", "music", "documents", "archives", "appimage"};
    QSettings settings;
    settings.setValue("Appearance/musicHoverEnabled", false);
    QVariantList musicTracks;
    for (const auto &key : keys) {
        const QString root = base + "/" + key;
        settings.setValue("contentIndex/mediaRoots/" + key, QStringList{root});
        const QString suffix = key == "images" ? ".svg" : key == "videos" ? ".mp4"
                             : key == "music" ? ".mp3" : key == "archives" ? ".zip"
                             : key == "appimage" ? ".AppImage" : ".txt";
        for (int i = 0; i < 4; ++i) {
            const QString parent = root + (i < 2 ? "/first" : "/second");
            QDir().mkpath(parent);
            const QString path = parent + "/item-" + QString::number(i % 2) + suffix;
            QFile file(path); require(file.open(QIODevice::WriteOnly), "fixture write");
            file.write(key == "images" ? "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16'><rect width='16' height='16'/></svg>" : "fixture data");
            if (key == "images") file.write(QByteArray(32768, ' '));
            if (key == "videos") file.resize(2 * 1024 * 1024);
            file.setFileTime(QDateTime::fromMSecsSinceEpoch(1700000000000LL + (i / 2) * 86400000LL), QFileDevice::FileModificationTime);
            file.close();
            if (key == "music") musicTracks.append(QVariantMap{{"itemUrl", QUrl::fromLocalFile(path).toString()},
                {"name", QFileInfo(path).fileName()}, {"title", QFileInfo(path).fileName()}, {"artist", "Artist"}, {"album", "Album"}});
        }
    }
    settings.sync();
    ContentIndexModel index;
    require(until([&] { return !index.indexing() && index.countForCategory("documents") == 4; }), "isolated index completes");
    QQmlApplicationEngine engine;
    engine.addImageProvider("gfilethumb", new ThumbnailProvider);
    engine.addImageProvider("systemicon", new SystemIconProvider);
    engine.addImageProvider("bundledicon", new BundledIconProvider);
    engine.addImageProvider("folderart", new FolderIconProvider);
    engine.rootContext()->setContextProperty("categoryIndex", &index);
    engine.rootContext()->setContextProperty("fixtureTracks", musicTracks);
    engine.loadData(R"qml(
import QtQuick
import QtQuick.Controls
import Lurviko.App
import Lurviko.Backend
ApplicationWindow {
    width: 1200; height: 800; visible: true
    LanguageManager {id: language}
    FavoritesModel {id: favorites}
    QuickAccessModel {id: quickAccess}
    AdminEditManager {id: admin}
    QtObject {id: auth; property string googleAccessToken: ""; property string oneDriveAccessToken: ""}
    QtObject {id: prefs}
    QtObject {id: cloud}
    QtObject {id: library; property var tracks: fixtureTracks; function setItems(items) {} }
    QtObject {id: player; property var libraryManager: library; property bool visible: false; property var playlists: []; property string currentUrl: ""}
    BrowsePage {
        objectName: "page"; anchors.fill: parent; lang: language
        contentIndexModel: categoryIndex; musicPlayer: player; favoritesModel: favorites; quickAccessModel: quickAccess
        adminEditor: admin; cloudAuth: auth; cloudIntegrationPreferences: prefs
        googleDrive: cloud; oneDrive: cloud; initialLocation: "category:/documents"
    }
}
)qml");
    require(!engine.rootObjects().isEmpty(), "category page loads");
    auto page = engine.rootObjects().first()->findChild<QObject *>("page");
    auto pane = page->property("activePane").value<QObject *>();
    require(pane, "active category pane");
    auto directory = pane->property("directory").value<QObject *>();
    require(directory, "real category model");
    for (const auto &key : keys) {
        evaluate(pane, "navigateTo('category:/" + key + "')");
        const bool ready = until([&] { return !directory->property("loading").toBool() && directory->property("count").toInt() == 4
            && evaluate(pane, "categorySelectionUrls().length").toInt() == 4; });
        if (!ready) std::fprintf(stderr, "category=%s index=%d location=%s count=%d scope=%d visible=%d\n", qPrintable(key), index.countForCategory(key), qPrintable(directory->property("location").toString()), directory->property("count").toInt(), evaluate(pane, "categorySelectionUrls().length").toInt(), pane->property("visible").toBool());
        require(ready, qPrintable("category has all real URLs: " + key));
        require(evaluate(pane, "toggleCategoryUrls(categorySelectionUrls()); selectedCount").toInt() == 4, "select entire category");
        const QStringList selected = pane->property("selectedUrls").toStringList();
        require(selected.size() == 4 && selected[0].startsWith("file:/") && selected.join(',').contains("/second/"), "mixed parent selection stays file based");
        evaluate(pane, "toggleCategoryUrls([selectedUrls[0]])");
        require(pane->property("selectedCount").toInt() == 3, "single deselection preserves other files");
        evaluate(pane, "toggleCategoryUrls(categorySelectionUrls())");
        require(pane->property("selectedCount").toInt() == 4, "partial select all fills selection");
        auto toolbar = pane->findChild<QObject *>("categorySelectionToolbar");
        auto actions = pane->findChild<QObject *>("categorySelectionActions");
        require(toolbar && toolbar->property("visible").toBool() && actions && actions->property("enabled").toBool(), "toolbar actions enabled");
        require(QMetaObject::invokeMethod(actions, "clicked"), "open real selection actions");
        auto menu = pane->findChild<QObject *>("itemContextMenu");
        require(until([&] { return menu->property("opened").toBool(); }), "real batch menu opens");
        evaluate(pane, "closeContextMenus()");
        evaluate(pane, "copySelection()");
        require(until([&] { const auto *mime = QApplication::clipboard()->mimeData(); return mime && mime->urls().size() == 4; }), "copy contains real cross-directory URLs");
        evaluate(pane, "categoryContextForUrls([selectedUrls[0]], this, 0, 0)");
        require(pane->property("selectedCount").toInt() == 4, "right click preserves whole selection");
        evaluate(pane, "closeContextMenus(); renameSelection()");
        auto rename = pane->findChild<QObject *>("renameDialog");
        require(rename && until([&] { return rename->property("opened").toBool(); }) && rename->property("batchMode").toBool(), "batch rename dialog supports aggregate selections");
        require(rename->property("targetUrls").toStringList().size() == 4, "batch rename retains every real source URL");
        QMetaObject::invokeMethod(rename, "close");
        evaluate(pane, "toggleCategoryUrls(categorySelectionUrls())");
        require(pane->property("selectedCount").toInt() == 0, "select-all toggles off");
        if (key == "images" || key == "videos" || key == "music") {
            evaluate(pane, "toggleCategoryUrls([categorySelectionUrls()[0]]); renameSelection()");
            require(until([&] { return rename->property("opened").toBool(); }) && !rename->property("batchMode").toBool(), "media single rename opens a visible dialog");
            QMetaObject::invokeMethod(rename, "close");
        }
    }
    // Real cross-directory archive with equal basenames must retain both files.
    FileOperations ops;
    bool finished = false, success = false;
    QObject::connect(&ops, &FileOperations::operationFinished, [&](bool ok, const QString &, bool, bool) { finished = true; success = ok; });
    const QString first = base + "/documents/first/item-0.txt", second = base + "/documents/second/item-0.txt";
    ops.compressItems({QUrl::fromLocalFile(first).toString(), QUrl::fromLocalFile(second).toString()}, "zip");
    require(until([&] { return finished; }) && success, "cross-parent compression completes");
    QProcess listing; listing.start("bsdtar", {"-tf", base + "/documents/first/Archive.zip"});
    require(listing.waitForFinished() && listing.exitCode() == 0, "archive readable");
    const QByteArray members = listing.readAllStandardOutput();
    require(members.contains("first/item-0.txt") && members.contains("second/item-0.txt"), "same basenames have distinct archive paths");
    evaluate(pane, "visible = false");
    std::fprintf(stderr, "PASS: all six category selections, individual toggles, real batch menu, cross-parent clipboard, stable context selection and distinct archive members\n");
}
