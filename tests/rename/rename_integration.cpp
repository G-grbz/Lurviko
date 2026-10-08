#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QKeyEvent>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QTimer>
#include <cstdio>
#include <functional>
#include "backend_test_types.h"

static void pause(int ms) { QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec(); }
static void require(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static bool until(std::function<bool()> check) {
    QElapsedTimer timer; timer.start();
    while (!check() && timer.elapsed() < 12000) pause(10);
    return check();
}
static QQuickItem *findItem(QQuickItem *root, const QString &name) {
    if (root->objectName() == name) return root;
    for (auto child : root->childItems()) if (auto found = findItem(child, name)) return found;
    return nullptr;
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("Lurviko-QA"); app.setApplicationName("Rename");
    QQuickStyle::setStyle("Basic");
    // REGISTER_BACKEND_TYPES
    // Existing users may already have disabled the dialog using its checkbox.
    { QSettings settings; settings.setValue("BrowserView/skipPermanentDeleteConfirmation", true); settings.sync(); }
    const QString base = QString::fromLocal8Bit(qgetenv("LURVIKO_RENAME_TEST_ROOT")) + "/fixture";
    QDir().mkpath(base);
    QStringList names = {"abcdefg.xxx", "ordinary.txt", "delete-test.txt"};
    for (int i = 0; i < 320; ++i) names << QString("file-%1.txt").arg(i, 3, 10, QChar('0'));
    for (const auto &name : names) {
        QFile file(base + "/" + name); require(file.open(QIODevice::WriteOnly), "fixture opens"); file.write("fixture");
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
    width: 1280; height: 800; visible: true
    LanguageManager {id: lang; language: "en"}
    FavoritesModel {id: favorites}
    QuickAccessModel {id: quickAccess}
    AdminEditManager {id: admin}
    QtObject {id: auth; property string googleAccessToken: ""; property string oneDriveAccessToken: ""}
    QtObject {id: prefs}
    QtObject {id: cloud}
    BrowsePage {
        objectName: "browser"; anchors.fill: parent
        lang: lang; contentIndexModel: null; musicPlayer: null; favoritesModel: favorites; quickAccessModel: quickAccess
        adminEditor: admin; cloudAuth: auth; cloudIntegrationPreferences: prefs
        googleDrive: cloud; oneDrive: cloud; initialLocation: fixturePath
    }
}
)qml");
    require(!engine.rootObjects().isEmpty(), "rename fixture loads");
    auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    window->requestActivate(); pause(350);
    auto browser = findItem(window->contentItem(), "browser");
    auto pane = qobject_cast<QQuickItem *>(browser->property("activePane").value<QObject *>());
    DirectoryModel *directory = nullptr;
    for (auto candidate : browser->findChildren<DirectoryModel *>())
        if (candidate->location() == QUrl::fromLocalFile(base).toString()) directory = candidate;
    require(directory && until([&] { return !directory->loading() && directory->rowCount() == names.size(); }), "file list ready");
    pane->setProperty("folderPreviewsEnabled", false);
    auto context = qmlContext(directory);
    auto eval = [&](QObject *scope, QQmlContext *ctx, const QString &expression) {
        QQmlExpression code(ctx, scope, expression); auto value = code.evaluate();
        if (code.hasError()) std::fprintf(stderr, "%s\n", qPrintable(code.error().toString()));
        require(!code.hasError(), "QML expression executes"); return value;
    };
    auto key = [&](int code, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        window->grabWindow(); // Polish virtual delegates even if another desktop window covers the fixture.
        QKeyEvent press(QEvent::KeyPress, code, modifiers), release(QEvent::KeyRelease, code, modifiers);
        QApplication::sendEvent(window, &press); QApplication::sendEvent(window, &release); pause(120);
        window->grabWindow(); pause(40);
    };
    auto select = [&](const QString &name) {
        const int index = directory->indexOfUrl(QUrl::fromLocalFile(base + "/" + name).toString());
        require(index >= 0, "selected fixture exists in model");
        require(QMetaObject::invokeMethod(pane, "selectIndex", Q_ARG(QVariant, QVariant(index)), Q_ARG(QVariant, QVariant(0))), "select item");
        pause(140); window->grabWindow(); pause(40); pane->forceActiveFocus();
    };
    for (bool gridMode : {true, false}) {
        pane->setProperty("gridMode", gridMode); pause(160);
        select("file-000.txt"); key(Qt::Key_F2);
        require(pane->property("inlineRenameUrl").toString().endsWith("file-000.txt"), "F2 starts inline rename");
        key(Qt::Key_C, Qt::ControlModifier);
        require(app.clipboard()->text() == "file-000", "Ctrl+C copies the selected basename, not the file");
        auto view = findItem(pane, gridMode ? "fileGridView" : "fileListView");
        require(view, "scroll view exists");
        view->setProperty("contentY", view->property("originY").toDouble() + view->property("contentHeight").toDouble() - view->height());
        pause(160); select("file-319.txt"); key(Qt::Key_F2);
        require(pane->property("inlineRenameUrl").toString().endsWith("file-319.txt"), "F2 replaces a distant recycled inline editor");
        auto distantField = window->activeFocusItem();
        require(distantField && distantField->objectName().endsWith("RenameInput"), "distant editor receives focus");
        distantField->setProperty("text", ".txt"); key(Qt::Key_Return);
        require(pane->property("renameConfirmationOpen").toBool(), "grid and list both confirm hidden names");
        key(Qt::Key_Escape); pause(150);
        key(Qt::Key_Escape);
        require(QFile::exists(base + "/file-000.txt") && QFile::exists(base + "/file-319.txt"), "unfinished names remain unchanged");
        select("file-000.txt"); key(Qt::Key_F2);
        const QString movedName = gridMode ? "zz-grid.txt" : "zz-list.txt";
        const QString movedUrl = QUrl::fromLocalFile(base + "/" + movedName).toString();
        window->activeFocusItem()->setProperty("text", movedName); key(Qt::Key_Return);
        require(until([&] { return QFile::exists(base + "/" + movedName) && !pane->property("pendingResultSelection").toBool()
                           && pane->property("selectedUrl").toString() == movedUrl; }), "renamed item stays selected at its new URL");
        pause(180); window->grabWindow();
        const int movedIndex = directory->indexOfUrl(movedUrl);
        require(movedIndex > 300 && pane->property("primaryIndex").toInt() == movedIndex, "renamed item follows its new sort position");
        require(view->property("contentY").toDouble() - view->property("originY").toDouble() > 500, "rename scrolls from first item to the result near the end");
        require(eval(pane, context, QString("(function(){ const view = gridMode ? grid : listView; view.forceLayout(); const item = view.itemAtIndex(%1); "
                                             "if (!item) return false; const point = item.mapToItem(view, 0, 0); return point.y + item.height > 0 && point.y < view.height; })()").arg(movedIndex)).toBool(),
                "renamed item is inside the visible viewport");
        select(movedName); key(Qt::Key_F2); window->activeFocusItem()->setProperty("text", "file-000.txt"); key(Qt::Key_Return);
        const QString restoredUrl = QUrl::fromLocalFile(base + "/file-000.txt").toString();
        require(until([&] { return QFile::exists(base + "/file-000.txt") && !pane->property("pendingResultSelection").toBool()
                           && pane->property("selectedUrl").toString() == restoredUrl; }), "rename back selects the restored result");
        pause(180); window->grabWindow();
        require(view->property("contentY").toDouble() - view->property("originY").toDouble() < 500, "rename back scrolls to its new position near the top");
    }
    select("file-050.txt"); key(Qt::Key_F2);
    window->activeFocusItem()->setProperty("text", "zz-tab.txt"); key(Qt::Key_Tab);
    require(until([&] { return QFile::exists(base + "/zz-tab.txt")
                       && pane->property("inlineRenameUrl").toString().endsWith("file-051.txt"); }), "Tab advances to the next inline editor");
    pause(250);
    require(pane->property("selectedUrl").toString().endsWith("file-051.txt"), "rename completion does not steal sequential-edit focus");
    key(Qt::Key_Escape);
    select("abcdefg.xxx"); key(Qt::Key_F2);
    auto field = window->activeFocusItem(); require(field && field->objectName() == "listRenameInput", "inline text field focused");
    field->setProperty("text", ".xxx"); key(Qt::Key_Return);
    auto hiddenDialog = eval(pane, context, "hiddenRenameDialog").value<QObject *>();
    require(hiddenDialog && hiddenDialog->property("visible").toBool(), "extension-only hidden name requires confirmation");
    require(QFile::exists(base + "/abcdefg.xxx") && !QFile::exists(base + "/.xxx"), "focus loss does not bypass confirmation");
    pause(120); window->grabWindow().save("/tmp/gfile-hidden-rename.png");
    key(Qt::Key_Escape); pause(150);
    require(!hiddenDialog->property("visible").toBool() && pane->property("inlineRenameUrl").toString().endsWith("abcdefg.xxx"), "cancel preserves the inline session");
    field = window->activeFocusItem(); require(field && field->property("text").toString() == ".xxx", "cancel restores draft and focus");
    key(Qt::Key_Return); pause(120);
    require(QMetaObject::invokeMethod(hiddenDialog, "confirmAction"), "confirm hidden rename");
    require(until([&] { return QFile::exists(base + "/.xxx") && !QFile::exists(base + "/abcdefg.xxx") && !pane->property("pendingResultSelection").toBool(); }), "confirmed rename completes");
    pane->setProperty("showHiddenFiles", true);
    require(until([&] { return !directory->loading() && directory->indexOfUrl(QUrl::fromLocalFile(base + "/.xxx").toString()) >= 0; }), "hidden file can be shown");
    select(".xxx"); key(Qt::Key_F2); field = window->activeFocusItem(); field->setProperty("text", ".other"); key(Qt::Key_Return);
    require(!hiddenDialog->property("visible").toBool(), "already hidden names do not prompt again");
    require(until([&] { return QFile::exists(base + "/.other") && !pane->property("pendingResultSelection").toBool(); }), "existing hidden file renamed");
    select("ordinary.txt"); key(Qt::Key_F2); field = window->activeFocusItem(); field->setProperty("text", "renamed.txt"); key(Qt::Key_Return);
    require(!hiddenDialog->property("visible").toBool(), "ordinary names do not prompt");
    require(until([&] { return QFile::exists(base + "/renamed.txt") && !pane->property("pendingResultSelection").toBool(); }), "ordinary rename completes");
    require(pane->property("selectedUrl").toString() == QUrl::fromLocalFile(base + "/renamed.txt").toString(), "ordinary rename focuses its resulting file");
    auto global = [&](const QString &expression) { return eval(window, qmlContext(window), expression); };
    auto hiddenMenuItem = browser->findChild<QObject *>("skipHiddenNameConfirmationItem");
    auto hiddenCheckbox = hiddenDialog->findChild<QObject *>("dontAskHiddenAgain");
    require(hiddenMenuItem && hiddenCheckbox && !global("AppTheme.skipHiddenNameConfirmation").toBool(), "hidden-name confirmation enabled by default");
    auto fileDialog = eval(pane, context, "newFileDialog").value<QObject *>();
    auto folderDialog = eval(pane, context, "newFolderDialog").value<QObject *>();
    auto startCreation = [&](bool folder, const QString &name) {
        QMetaObject::invokeMethod(pane, folder ? "showNewFolderDialog" : "showNewFileDialog"); pause(240);
        auto input = eval(pane, context, folder ? "newFolderInput" : "newFileInput").value<QObject *>();
        require(input, "creation input exists"); input->setProperty("text", name); key(Qt::Key_Return);
    };
    startCreation(false, ".hidden.txt");
    require(hiddenDialog->property("visible").toBool() && hiddenDialog->property("actionType").toString() == "create_file", "new hidden file asks before creating");
    require(!QFile::exists(base + "/.hidden.txt"), "unconfirmed file not created");
    hiddenCheckbox->setProperty("checked", true); key(Qt::Key_Escape); pause(150);
    require(!global("AppTheme.skipHiddenNameConfirmation").toBool(), "cancel does not save don't-ask checkbox");
    require(fileDialog->property("visible").toBool() && window->activeFocusItem()->property("text").toString() == ".hidden.txt", "cancel restores creation draft and focus");
    key(Qt::Key_Return); pause(140);
    require(!hiddenCheckbox->property("checked").toBool(), "each confirmation resets its checkbox");
    QMetaObject::invokeMethod(hiddenDialog, "confirmAction");
    require(until([&] { return QFile::exists(base + "/.hidden.txt") && !directory->loading()
                       && directory->indexOfUrl(QUrl::fromLocalFile(base + "/.hidden.txt").toString()) >= 0; }), "confirmed hidden file created");
    pause(160);
    startCreation(true, ".hidden-folder");
    require(hiddenDialog->property("visible").toBool() && hiddenDialog->property("actionType").toString() == "create_folder", "new hidden folder asks before creating");
    require(!QFile::exists(base + "/.hidden-folder"), "unconfirmed folder not created");
    hiddenCheckbox->setProperty("checked", true); pause(120); window->grabWindow().save("/tmp/gfile-hidden-create.png");
    QMetaObject::invokeMethod(hiddenDialog, "confirmAction");
    require(until([&] { return QFile::exists(base + "/.hidden-folder") && !pane->property("pendingResultSelection").toBool(); }), "confirmed hidden folder created");
    pause(160);
    require(global("AppTheme.skipHiddenNameConfirmation").toBool() && hiddenMenuItem->property("checked").toBool(), "accepted don't-ask checkbox updates shared View preference");
    startCreation(true, ".silent-folder");
    require(!hiddenDialog->property("visible").toBool(), "common opt-out skips folder confirmation");
    require(until([&] { return QFile::exists(base + "/.silent-folder") && !pane->property("pendingResultSelection").toBool(); }), "opted-out folder created");
    pause(160);
    startCreation(false, ".silent.txt");
    require(!hiddenDialog->property("visible").toBool(), "common opt-out skips file confirmation");
    require(until([&] { return QFile::exists(base + "/.silent.txt") && !directory->loading()
                       && directory->indexOfUrl(QUrl::fromLocalFile(base + "/.silent.txt").toString()) >= 0; }), "opted-out file created");
    pause(160);
    select("renamed.txt"); key(Qt::Key_F2); window->activeFocusItem()->setProperty("text", ".silent-renamed.txt"); key(Qt::Key_Return);
    require(!hiddenDialog->property("visible").toBool(), "common opt-out also skips rename confirmation");
    require(until([&] { return QFile::exists(base + "/.silent-renamed.txt") && !pane->property("pendingResultSelection").toBool(); }), "opted-out rename completes");
    global("AppTheme.browserInteractionSettings.sync()");
    { QSettings saved; saved.sync(); require(saved.value("BrowserView/skipHiddenNameConfirmation").toBool(), "common opt-out persists"); }
    QMetaObject::invokeMethod(hiddenMenuItem, "triggered");
    require(!global("AppTheme.skipHiddenNameConfirmation").toBool(), "View restores common hidden-name confirmation");
    for (bool folder : {false, true}) {
        startCreation(folder, folder ? ".ask-again-folder" : ".ask-again.txt");
        require(hiddenDialog->property("visible").toBool(), "View restores file and folder confirmation");
        key(Qt::Key_Escape); pause(150); QMetaObject::invokeMethod(folder ? folderDialog : fileDialog, "close"); pause(160);
    }
    select("file-010.txt"); key(Qt::Key_F2); window->activeFocusItem()->setProperty("text", ".rename-after-reset.txt"); key(Qt::Key_Return);
    require(hiddenDialog->property("visible").toBool(), "View also restores rename confirmation");
    key(Qt::Key_Escape); pause(150); key(Qt::Key_Escape);
    select("file-010.txt");
    const auto secondUrl = QUrl::fromLocalFile(base + "/file-011.txt").toString();
    QMetaObject::invokeMethod(pane, "selectIndex", Q_ARG(QVariant, QVariant(directory->indexOfUrl(secondUrl))),
                              Q_ARG(QVariant, QVariant(int(Qt::ControlModifier))));
    pause(140); pane->forceActiveFocus(); key(Qt::Key_F2); pause(120);
    auto batchDialog = eval(pane, context, "renameDialog").value<QObject *>();
    require(batchDialog->property("visible").toBool(), "batch rename dialog opens");
    eval(pane, context, "renameInput").value<QObject *>()->setProperty("text", ".hidden###"); key(Qt::Key_Return);
    require(hiddenDialog->property("visible").toBool() && hiddenDialog->property("actionType").toString() == "batch_rename", "hidden batch names share the same confirmation");
    key(Qt::Key_Escape); pause(150); QMetaObject::invokeMethod(batchDialog, "close"); pause(160);
    require(QFile::exists(base + "/file-010.txt") && QFile::exists(base + "/file-011.txt"), "cancel preserves batch sources");
    pane->forceActiveFocus(); key(Qt::Key_F2); pause(120);
    eval(pane, context, "renameInput").value<QObject *>()->setProperty("text", "zz-batch###"); key(Qt::Key_Return);
    require(until([&] { return QFile::exists(base + "/zz-batch001.txt") && QFile::exists(base + "/zz-batch002.txt")
                       && !pane->property("pendingResultSelection").toBool() && pane->property("selectedCount").toInt() == 2; }), "batch rename selects completed results");
    const auto batchSelected = eval(pane, context, "selectedUrls.join('\\n')").toString().split('\n');
    require(batchSelected.contains(QUrl::fromLocalFile(base + "/zz-batch001.txt").toString())
            && batchSelected.contains(QUrl::fromLocalFile(base + "/zz-batch002.txt").toString()), "batch selection contains exact resulting URLs");
    pause(180); window->grabWindow();
    require(findItem(pane, "fileListView")->property("contentY").toDouble() > 500, "batch rename reveals the results");
    global("AppTheme.browserInteractionSettings.sync()");
    { QSettings saved; saved.sync(); require(!saved.value("BrowserView/skipHiddenNameConfirmation", true).toBool(), "restored common confirmation persists"); }
    require(global("AppTheme.skipPermanentDeleteConfirmation").toBool(), "previous don't-ask preference preserved");
    auto menuItem = browser->findChild<QObject *>("skipPermanentDeleteConfirmationItem");
    require(menuItem && menuItem->property("checked").toBool(), "view menu reflects saved preference");
    require(QMetaObject::invokeMethod(menuItem, "triggered"), "toggle deletion preference in View");
    require(!global("AppTheme.skipPermanentDeleteConfirmation").toBool() && !menuItem->property("checked").toBool(), "View re-enables deletion confirmation");
    select("delete-test.txt"); QMetaObject::invokeMethod(pane, "deleteSelection"); pause(220);
    auto deleteDialog = eval(pane, context, "confirmDeleteDialog").value<QObject *>();
    require(deleteDialog && deleteDialog->property("visible").toBool(), "re-enabled confirmation appears");
    QMetaObject::invokeMethod(deleteDialog, "close"); pause(160);
    require(QFile::exists(base + "/delete-test.txt"), "cancel does not delete");
    QMetaObject::invokeMethod(menuItem, "triggered");
    require(global("AppTheme.skipPermanentDeleteConfirmation").toBool(), "View can disable confirmation again");
    QMetaObject::invokeMethod(pane, "deleteSelection");
    require(until([&] { return !QFile::exists(base + "/delete-test.txt"); }), "enabled skip deletes without a modal");
    require(!deleteDialog->property("visible").toBool(), "skip keeps dialog closed");
    QMetaObject::invokeMethod(menuItem, "triggered"); global("AppTheme.browserInteractionSettings.sync()");
    QSettings saved; saved.sync();
    require(!saved.value("BrowserView/skipPermanentDeleteConfirmation", true).toBool(), "re-enabled confirmation persists using the original key");
    std::fprintf(stderr, "PASS: distant F2 editing, selection/scroll after single and batch renames, Tab editing, hidden-name confirmation and persisted preferences\n");
}
