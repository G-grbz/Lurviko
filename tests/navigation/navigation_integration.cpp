#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <cstdio>
#include <functional>
#include <unistd.h>
#include "backend_test_types.h"

static void pause(int ms) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}
static void require(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static bool until(std::function<bool()> check) {
    QElapsedTimer timer; timer.start();
    while (!check() && timer.elapsed() < 12000) pause(10);
    return check();
}
static void invoke(QObject *object, const char *method, QVariant arg) {
    require(QMetaObject::invokeMethod(object, method, Q_ARG(QVariant, arg)), method);
}
static QQuickItem *findView(QQuickItem *root, const QString &name) {
    if (root->objectName() == name) return root;
    for (auto child : root->childItems()) if (auto found = findView(child, name)) return found;
    return nullptr;
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("Lurviko-QA"); app.setApplicationName("Navigation");
    QQuickStyle::setStyle("Basic");
    // REGISTER_BACKEND_TYPES
    const QString base = QString::fromLocal8Bit(qgetenv("LURVIKO_NAVIGATION_TEST_ROOT")) + "/fixture";
    for (int i = 0; i < 300; ++i) {
        const QString child = base + QString("/folder-%1").arg(i, 3, 10, QChar('0'));
        QDir().mkpath(child);
        if (i == 90) for (int j = 0; j < 180; ++j) {
            QFile file(child + QString("/file-%1.txt").arg(j));
            require(file.open(QIODevice::WriteOnly), "fixture writes"); file.write("fixture");
        }
    }
    QQmlApplicationEngine engine;
    engine.addImageProvider("gfilethumb", new ThumbnailProvider);
    engine.addImageProvider("systemicon", new SystemIconProvider);
    engine.addImageProvider("bundledicon", new BundledIconProvider);
    engine.rootContext()->setContextProperty("fixturePath", base);
    engine.loadData(R"qml(
import QtQuick
import QtQuick.Controls
import Lurviko.App
import Lurviko.Backend
ApplicationWindow {
    width: 1400; height: 900; visible: true
    LanguageManager {id: language}
    FavoritesModel {id: favorites}
    QuickAccessModel {id: quickAccess}
    AdminEditManager {id: admin}
    QtObject {id: auth; property string googleAccessToken: ""; property string oneDriveAccessToken: ""}
    QtObject {id: prefs}
    QtObject {id: google}
    QtObject {id: one}
    BrowsePage {
        objectName: "page"; anchors.fill: parent; lang: language
        contentIndexModel: null; musicPlayer: null; favoritesModel: favorites; quickAccessModel: quickAccess
        adminEditor: admin; cloudAuth: auth; cloudIntegrationPreferences: prefs
        googleDrive: google; oneDrive: one; initialLocation: fixturePath
    }
}
)qml");
    require(!engine.rootObjects().isEmpty(), "BrowsePage loads");
    auto window = qobject_cast<QQuickWindow *>(engine.rootObjects()[0]);
    auto page = window->findChild<QQuickItem *>("page");
    auto pane = qobject_cast<QQuickItem *>(page->property("activePane").value<QObject *>());
    DirectoryModel *directory = nullptr;
    for (auto candidate : window->findChildren<DirectoryModel *>())
        if (candidate->location() == QUrl::fromLocalFile(base).toString()) directory = candidate;
    require(directory && until([&] { return !directory->loading() && directory->rowCount() == 300; }), "parent loads");
    pane->setProperty("folderPreviewsEnabled", false);
    for (bool gridMode : {true, false}) {
        pane->setProperty("gridMode", gridMode); pause(150);
        auto view = findView(pane, gridMode ? "fileGridView" : "fileListView");
        require(view, "viewport exists");
        QMetaObject::invokeMethod(pane, "selectIndex", Q_ARG(QVariant, QVariant(90)), Q_ARG(QVariant, QVariant(0)));
        pause(30); // Deliberately leave before the 90 ms snapshot debounce.
        const double saved = view->property("contentY").toDouble() - view->property("originY").toDouble();
        const auto child = directory->itemAt(90);
        const QString childUrl = child.value("itemUrl").toString();
        require(saved > 200, "parent is scrolled");
        invoke(pane, "navigateTo", childUrl);
        require(until([&] { return !directory->loading() && directory->rowCount() == 180; }), "child loads");
        pause(100);
        view->setProperty("contentY", view->property("originY").toDouble() + 650);
        const double childSaved = view->property("contentY").toDouble() - view->property("originY").toDouble();
        invoke(page, "goBack", QVariant::fromValue(static_cast<QObject *>(pane)));
        const bool backReady = until([&] { return !directory->loading() && directory->rowCount() == 300 && pane->property("pendingTabScrollOffset").toDouble() < 0; });
        if (!backReady) {
            QQmlExpression debug(qmlContext(pane), pane, "JSON.stringify({location:currentLocation,pending:pendingTabScrollLocation,offset:pendingTabScrollOffset,attempts:pendingTabScrollAttempts})");
            std::fprintf(stderr, "Back state: %s count=%d loading=%d\n", qPrintable(debug.evaluate().toString()), directory->rowCount(), directory->loading());
        }
        require(backReady, "Back finishes");
        pause(100);
        const double restored = view->property("contentY").toDouble() - view->property("originY").toDouble();
        std::fprintf(stderr, "%s Back scroll %.1f -> %.1f\n", gridMode ? "Grid" : "List", saved, restored);
        require(qAbs(restored - saved) < 3, "Back restores exact viewport");
        require(pane->property("selectedUrl").toString() == childUrl, "Back preserves selected child folder");
        QMetaObject::invokeMethod(page, "goForward");
        require(until([&] { return !directory->loading() && directory->rowCount() == 180 && pane->property("pendingTabScrollOffset").toDouble() < 0; }), "Forward finishes");
        pause(100);
        require(qAbs(view->property("contentY").toDouble() - view->property("originY").toDouble() - childSaved) < 3, "Forward restores child viewport");
        invoke(page, "goBack", QVariant::fromValue(static_cast<QObject *>(pane)));
        require(until([&] { return !directory->loading() && directory->rowCount() == 300 && pane->property("pendingTabScrollOffset").toDouble() < 0; }), "return to parent");
    }
    const QString links = base + "/folder-090/links";
    QDir().mkpath(links + "/one"); QDir().mkpath(links + "/two"); QDir().mkpath(links + "/three");
    QFile target(links + "/three/target.txt");
    require(target.open(QIODevice::WriteOnly), "link target opens"); target.write("target"); target.close();
    require(::symlink("../two/second", QFile::encodeName(links + "/one/first").constData()) == 0, "first relative symlink created");
    require(::symlink("../three/third", QFile::encodeName(links + "/two/second").constData()) == 0, "second relative symlink created");
    require(::symlink("target.txt", QFile::encodeName(links + "/three/third").constData()) == 0, "third relative symlink created");
    invoke(pane, "navigateTo", links + "/one");
    require(until([&] { return !directory->loading() && directory->indexOfUrl(links + "/one/first") >= 0; }), "symlink folder opens");
    QMetaObject::invokeMethod(pane, "selectIndex", Q_ARG(QVariant, QVariant(directory->indexOfUrl(links + "/one/first"))), Q_ARG(QVariant, QVariant(0)));
    for (const QString &next : {links + "/two/second", links + "/three/third", links + "/three/target.txt"}) {
        const QString source = pane->property("selectedUrl").toString();
        std::fprintf(stderr, "Show Target: %s -> %s (type=%s target=%s)\n", qPrintable(source), qPrintable(next),
                     qPrintable(pane->property("selectedLinkType").toString()), qPrintable(pane->property("selectedLinkTarget").toString()));
        invoke(pane, "showSelectedLinkTarget", false);
        require(until([&] { return !directory->loading() && pane->property("selectedUrl").toString() == QUrl::fromLocalFile(next).toString(); }), "Show Target follows each symlink hop");
        pause(100);
        require(pane->property("selectedUrl").toString() == QUrl::fromLocalFile(next).toString(), "revealed symlink selection stays stable");
    }
    const QString icons = links + "/icons";
    QDir().mkpath(icons);
    QFile iconFile(icons + "/go-previous.svg");
    require(iconFile.open(QIODevice::WriteOnly), "icon fixture opens");
    iconFile.write("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\"><path d=\"M10 2 4 8l6 6\"/></svg>");
    iconFile.close();
    for (int i = 0; i < 160; ++i) {
        QFile filler(icons + QStringLiteral("/aaa-%1.txt").arg(i, 3, 10, QLatin1Char('0')));
        require(filler.open(QIODevice::WriteOnly), "scroll fixture opens"); filler.write("fixture");
    }
    require(::symlink("go-previous.svg", QFile::encodeName(icons + "/arrow-left.svg").constData()) == 0, "same-folder symlink created");
    invoke(pane, "navigateTo", icons);
    require(until([&] { return !directory->loading() && directory->rowCount() == 162; }), "icon folder opens");
    QMetaObject::invokeMethod(pane, "startSearch", Q_ARG(QVariant, QVariant("left")), Q_ARG(QVariant, QVariant(false)));
    require(until([&] { return directory->searchActive() && !directory->loading() && directory->rowCount() == 1; }), "Ctrl+F left returns arrow-left only");
    QMetaObject::invokeMethod(pane, "selectIndex", Q_ARG(QVariant, QVariant(0)), Q_ARG(QVariant, QVariant(0)));
    invoke(pane, "showSelectedLinkTarget", false);
    const bool revealed = until([&] {
        return !directory->loading() && !directory->searchActive()
            && pane->property("searchVisible").toBool()
            && pane->property("selectedUrl").toString() == QUrl::fromLocalFile(icons + "/go-previous.svg").toString();
    });
    if (!revealed)
        std::fprintf(stderr, "Filtered reveal: search=%d rows=%d selected=%s\n", directory->searchActive(), directory->rowCount(), qPrintable(pane->property("selectedUrl").toString()));
    require(revealed, "Show Target keeps Ctrl+F open and reveals an excluded target in the same folder");
    pause(100);
    auto targetView = findView(pane, pane->property("gridMode").toBool() ? "fileGridView" : "fileListView");
    require(targetView && targetView->property("contentY").toDouble() > 500, "revealed target scrolls into view");
    const auto searchState = [&] {
        QQmlExpression state(qmlContext(directory), pane, "searchState().searchQuery");
        return state.evaluate().toString();
    };
    require(searchState() == "left", "revealing the target keeps the original query text");
    require(page->property("currentTab").toInt() == 0, "target is revealed in the same tab");
    // The visible query is a draft after revealing. Enter must still be able
    // to run it again without reopening Ctrl+F or typing the query again.
    require(QMetaObject::invokeMethod(pane, "runBuiltInSearch"), "run retained query");
    require(until([&] { return directory->searchActive() && !directory->loading() && directory->rowCount() == 1; }), "retained query can run again");
    QMetaObject::invokeMethod(pane, "selectIndex", Q_ARG(QVariant, QVariant(0)), Q_ARG(QVariant, QVariant(0)));
    invoke(pane, "showSelectedLinkTarget", false);
    require(until([&] { return !directory->loading() && !directory->searchActive()
        && pane->property("selectedUrl").toString() == QUrl::fromLocalFile(icons + "/go-previous.svg").toString(); }), "same target can be revealed after searching again");
    // Different-parent reveals must retain the field too, while ordinary
    // navigation continues to use its existing search-dismiss behavior.
    const QString firstUrl = QUrl::fromLocalFile(links + "/one/first").toString();
    require(QMetaObject::invokeMethod(pane, "revealExternalItem",
        Q_ARG(QVariant, QVariant(links + "/one")), Q_ARG(QVariant, QVariant(firstUrl)),
        Q_ARG(QVariant, QVariant(true))), "cross-folder reveal requested");
    require(until([&] { return !directory->loading() && pane->property("selectedUrl").toString() == firstUrl; }), "cross-folder reveal selects source symlink");
    invoke(pane, "showSelectedLinkTarget", false);
    require(until([&] { return !directory->loading() && pane->property("selectedUrl").toString() == QUrl::fromLocalFile(links + "/two/second").toString(); }), "cross-folder link target reveals in same tab");
    require(pane->property("searchVisible").toBool() && searchState() == "left", "cross-folder target preserves Ctrl+F and query");
    std::fprintf(stderr, "PASS: Grid/List navigation, chained symlinks and revealing targets outside the active search\n");
}
