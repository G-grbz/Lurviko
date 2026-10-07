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
    app.setOrganizationName("GFile-QA"); app.setApplicationName("Navigation");
    QQuickStyle::setStyle("Basic");
    // REGISTER_BACKEND_TYPES
    const QString base = QString::fromLocal8Bit(qgetenv("GFILE_NAVIGATION_TEST_ROOT")) + "/fixture";
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
    engine.rootContext()->setContextProperty("fixturePath", base);
    engine.loadData(R"qml(
import QtQuick
import QtQuick.Controls
import GFile.App
import GFile.Backend
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
    std::fprintf(stderr, "PASS: Grid/List Back/Forward scroll and folder selection, immediate navigation\n");
}
