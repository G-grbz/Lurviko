#include "fileoperations.h"
#include "trashmonitor.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>
#include <cstdio>
#include <functional>
#include <sys/stat.h>
#include <unistd.h>
#include "backend_test_types.h"

static void require(bool ok, const char *message)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static bool until(const std::function<bool()> &check)
{
    QElapsedTimer elapsed; elapsed.start();
    while (!check() && elapsed.elapsed() < 20000) {
        QEventLoop loop; QTimer::singleShot(10, &loop, &QEventLoop::quit); loop.exec();
    }
    return check();
}
static void write(const QString &path, const QByteArray &contents)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path); require(file.open(QIODevice::WriteOnly), "fixture opens");
    require(file.write(contents) == contents.size() && file.commit(), "fixture writes");
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setOrganizationName("Lurviko-QA"); app.setApplicationName("Trash");
    QQuickStyle::setStyle("Basic");
    // REGISTER_BACKEND_TYPES
    const QString root = QString::fromLocal8Bit(qgetenv("LURVIKO_TRASH_TEST_ROOT"));
    require(!root.isEmpty(), "explicit isolated test root");
    const QString data = QString::fromLocal8Bit(qgetenv("XDG_DATA_HOME"));
    const QString config = QString::fromLocal8Bit(qgetenv("XDG_CONFIG_HOME"));
    require(data == root + "/data" && config == root + "/config", "private XDG directories");
    struct stat fixtureStat{}, homeStat{};
    require(::stat(QFile::encodeName(root).constData(), &fixtureStat) == 0
            && ::stat(QFile::encodeName(QDir::homePath()).constData(), &homeStat) == 0
            && fixtureStat.st_dev == homeStat.st_dev, "home-filesystem trash isolation");

    const QString files = data + "/Trash/files";
    const QString trashrc = config + "/trashrc";
    write(trashrc, "[Status]\nEmpty=true\n");
    TrashMonitor monitor;
    monitor.refresh(); require(!monitor.hasItems(), "initial empty state");
    { QEventLoop loop; QTimer::singleShot(25, &loop, &QEventLoop::quit); loop.exec(); }
    write(files + "/.hidden", "payload");
    require(until([&] { return monitor.hasItems(); }), "new infrastructure and hidden payload watched");
    require(monitor.iconName("user-trash2", monitor.hasItems()) == "user-trash-full", "missing/custom empty icon cannot hide full state");
    require(QFile::remove(files + "/.hidden"), "remove isolated payload");
    require(until([&] { return !monitor.hasItems(); }), "payload removal watched");
    require(::symlink("missing", QFile::encodeName(files + "/broken-link").constData()) == 0, "dangling symlink fixture");
    require(until([&] { return monitor.hasItems(); }), "dangling trash payload counts");
    require(QFile::remove(files + "/broken-link"), "remove isolated dangling link");
    write(trashrc, "[Status]\nEmpty=false\n");
    require(until([&] { return monitor.hasItems(); }), "global other-volume state preserved");
    write(trashrc, "[Status]\nEmpty=true\n");
    require(until([&] { return !monitor.hasItems(); }), "atomic config replacement watched");

    // Verify paired custom theme variants without depending on the user's theme.
    const QString theme = root + "/themes/qa";
    write(theme + "/index.theme", "[Icon Theme]\nName=QA\nDirectories=16x16/places\n"
          "[16x16/places]\nSize=16\nContext=Places\nType=Fixed\n");
    for (const QString &name : {QString("user-trash1"), QString("user-trash-full1")})
        write(theme + "/16x16/places/" + name + ".svg",
              "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\"><rect width=\"16\" height=\"16\"/></svg>");
    QIcon::setThemeSearchPaths({root + "/themes"}); QIcon::setThemeName("qa");
    require(monitor.iconName("user-trash-full1", monitor.hasItems()) == "user-trash1", "custom empty counterpart");
    write(files + "/state-test", "fixture"); monitor.refresh();
    require(monitor.iconName("user-trash1", monitor.hasItems()) == "user-trash-full1", "custom full counterpart");
    require(QFile::remove(files + "/state-test"), "remove isolated icon fixture");
    monitor.refresh();

    const QString folder = root + "/source/many-files";
    const QString document = root + "/source/İşler Güçler.txt";
    for (int i = 0; i < 2000; ++i)
        write(folder + QString("/nested/file-%1.txt").arg(i), "fixture contents");
    write(document, "unicode name");
    QQmlApplicationEngine engine;
    engine.addImageProvider("gfilethumb", new ThumbnailProvider);
    engine.addImageProvider("systemicon", new SystemIconProvider);
    engine.addImageProvider("bundledicon", new BundledIconProvider);
    engine.addImageProvider("folderart", new FolderIconProvider);
    engine.rootContext()->setContextProperty("fixturePath", root + "/source");
    engine.loadData(R"qml(
import QtQuick
import QtQuick.Controls
import Lurviko.App
import Lurviko.Backend
ApplicationWindow {
    width: 1200; height: 800; visible: true
    LanguageManager {id: language}
    FavoritesModel {id: favorites}
    AdminEditManager {id: admin}
    QtObject {id: auth; property string googleAccessToken: ""; property string oneDriveAccessToken: ""}
    QtObject {id: prefs}
    QtObject {id: google}
    QtObject {id: one}
    QtObject {id: storage; property var favoriteItems: []}
    AppSidebar {
        id: sidebar; objectName: "sidebar"; width: 220; anchors.left: parent.left
        anchors.top: parent.top; anchors.bottom: parent.bottom
        lang: language; favoritesModel: favorites; storageModel: storage
        property string observedTrashIcon: sidebarItemIcon({key: "trash"})
    }
    BrowserPane {
        objectName: "pane"; anchors.left: sidebar.right; anchors.right: parent.right
        anchors.top: parent.top; anchors.bottom: parent.bottom; lang: language; favoritesModel: favorites
        adminEditor: admin; cloudAuth: auth; cloudIntegrationPreferences: prefs
        googleDrive: google; oneDrive: one; initialLocation: fixturePath
    }
}
)qml");
    require(!engine.rootObjects().isEmpty(), "real progress UI loads");
    auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    auto pane = window->findChild<QQuickItem *>("pane");
    require(pane, "browser pane exists");
    auto backend = pane->findChild<FileOperations *>();
    require(backend, "pane progress backend exists");
    auto &operations = *backend;
    auto sidebar = window->findChild<QQuickItem *>("sidebar");
    require(sidebar, "real sidebar loads");
    auto evaluate = [&](const QString &script) {
        QQmlExpression expression(qmlContext(backend), pane, script);
        const auto value = expression.evaluate();
        require(!expression.hasError(), qPrintable(expression.error().toString()));
        return value;
    };
    evaluate("AppTheme.setSystemIcons(true)");
    require(QMetaObject::invokeMethod(sidebar, "setSavedSidebarIcon",
                                     Q_ARG(QVariant, QVariant("trash")),
                                     Q_ARG(QVariant, QVariant("user-trash2"))), "saved custom trash icon fixture");
    require(until([&] { return sidebar->property("observedTrashIcon").toString().endsWith("/user-trash"); }), "initial sidebar icon empty");
    bool resultReady = false, success = false, presented = false;
    int selectionSignals = 0;
    QObject::connect(&operations, &FileOperations::transferItemsFinished, &app,
                     [&](const QString &, const QStringList &) { ++selectionSignals; });
    QObject::connect(&operations, &FileOperations::operationFinished, &app,
                     [&](bool ok, const QString &, bool, bool external) {
        resultReady = true; success = ok; presented = external;
    });
    for (bool kdeProgress : {false, true}) {
        evaluate(kdeProgress ? "AppTheme.setKioProgressEnabled(true)" : "AppTheme.setKioProgressEnabled(false)");
        resultReady = false;
        QElapsedTimer elapsed; elapsed.start();
        operations.trashMany({QUrl::fromLocalFile(folder).toString(), QUrl::fromLocalFile(document).toString()});
        auto rows = operations.operations();
        require(!rows.isEmpty(), "trash card published before worker starts in both modes");
        const auto initial = rows.constLast().toMap();
        const int id = initial.value("id").toInt();
        require(initial.value("kind") == "trash" && initial.value("totalItems").toInt() == 2
                && initial.value("processedItems").toInt() == 0 && !initial.value("finished").toBool(),
                "immediate pending card has requested item count");
        require(evaluate("operationCards.visible && operationCards.children.length > 1").toBool(),
                "actual card visible immediately in both progress modes");
        require(operations.ioBusy() && evaluate("directory.heavyIoBusy").toBool(),
                "trash pauses competing metadata/preview work during I/O");
        require(evaluate("transferMetrics(fileOps.operations[0])").toString().contains("0 / 2"), "UI pending item count");
        int ticks = 0; QTimer heartbeat;
        QObject::connect(&heartbeat, &QTimer::timeout, &app, [&] { ++ticks; }); heartbeat.start(1);
        require(until([&] { return resultReady; }), "real trash job finishes");
        heartbeat.stop();
        std::fprintf(stderr, "%s trash: %lld ms, event loop ticks %d\n", kdeProgress ? "warm" : "first", elapsed.elapsed(), ticks);
        require(success && presented && ticks > 0, "success presented and UI responsive");
        require(!operations.ioBusy() && !evaluate("directory.heavyIoBusy").toBool(), "background work resumes after trash");
        require(!QFileInfo::exists(folder) && !QFileInfo::exists(document), "fixtures actually moved");
        require(QFileInfo::exists(files + "/many-files/nested/file-1999.txt")
                && QFileInfo::exists(files + "/İşler Güçler.txt"), "private trash preserves payloads");
        rows = operations.operations();
        const auto final = rows.constLast().toMap();
        require(final.value("id").toInt() == id && final.value("finished").toBool()
                && final.value("success").toBool() && final.value("percent").toInt() == 100
                && final.value("processedItems").toInt() == 2 && final.value("totalItems").toInt() == 2,
                "final count and progress accurate");
        require(selectionSignals == 0, "trash does not select a foreign destination");
        require(evaluate("transferMetrics(fileOps.operations[0])").toString().contains("2 / 2"), "UI completed item count");
        require(until([&] { return monitor.hasItems(); }), "real job updates full icon");
        require(until([&] { return sidebar->property("observedTrashIcon").toString().contains("user-trash-full"); }),
                "actual QML system icon binding updates when full");
        require(operations.canUndo(), "KDE undo remains available");
        operations.dismissOperation(id);
        evaluate("AppTheme.setKioProgressEnabled(false)");
        resultReady = false;
        operations.restoreFromTrash({"trash:/0-many-files", "trash:/0-" + QString::fromUtf8(QUrl::toPercentEncoding("İşler Güçler.txt"))});
        require(until([&] { return resultReady; }) && success, "restore fixture through KDE");
        require(QFileInfo::exists(folder + "/nested/file-1999.txt") && QFileInfo::exists(document), "restored bytes and names");
        require(until([&] { return !monitor.hasItems(); }), "real restore resets empty state");
        require(until([&] { return sidebar->property("observedTrashIcon").toString().endsWith("/user-trash"); }),
                "actual QML system icon binding updates when empty");
    }
    resultReady = false;
    operations.trashItem(root + "/does-not-exist");
    require(until([&] { return resultReady; }) && !success, "trash error reported");
    require(!operations.operations().constLast().toMap().value("success").toBool(), "failed card cannot report completion");
    operations.dismissOperation(operations.operations().constLast().toMap().value("id").toInt());
    require(operations.operations().isEmpty(), "failed card dismisses");
    resultReady = false;
    operations.trashItem(document);
    const int cancelId = operations.operations().constLast().toMap().value("id").toInt();
    require(operations.cancelOperation(cancelId), "pending trash can be cancelled");
    require(until([&] { return resultReady; }) && !success, "cancel reported as unsuccessful");
    require(QFileInfo::exists(document), "cancelling pending job preserves source");
    std::fprintf(stderr, "PASS: trash progress, restore, undo availability and icon state\n");
}
