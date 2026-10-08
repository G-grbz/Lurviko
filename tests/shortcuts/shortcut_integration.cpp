#include <QApplication>
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
#include <QTimer>
#include <cstdio>
#include <functional>
#include "backend_test_types.h"

static void require(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static void pause(int ms) {
    QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec();
}
static bool until(std::function<bool()> check) {
    QElapsedTimer timer; timer.start();
    while (!check() && timer.elapsed() < 6000) pause(10);
    return check();
}
static QQuickItem *findItem(QQuickItem *root, const QString &name) {
    if (root->objectName() == name) return root;
    for (auto child : root->childItems()) if (auto found = findItem(child, name)) return found;
    return nullptr;
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("Lurviko-QA"); app.setApplicationName("Shortcuts");
    QQuickStyle::setStyle("Basic");
    // REGISTER_BACKEND_TYPES
    const QString base = QString::fromLocal8Bit(qgetenv("LURVIKO_SHORTCUT_TEST_ROOT")) + "/fixture";
    QDir().mkpath(base);
    QFile source(base + "/source.txt"); require(source.open(QIODevice::WriteOnly), "fixture opens"); source.write("fixture"); source.close();
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
    width: 1160; height: 740; visible: true
    property bool mediaViewerConsumesF11: false
    property var activeVideoViewer: null
    property var manager: KeyboardShortcuts
    property var editor: shortcutEditor
    property var language: lang
    LanguageManager { id: lang }
    FavoritesModel { id: favorites }
    QuickAccessModel { id: quickAccess }
    AdminEditManager { id: admin }
    QtObject { id: auth; property string googleAccessToken: ""; property string oneDriveAccessToken: "" }
    QtObject { id: prefs }
    QtObject { id: cloud }
    BrowsePage {
        id: browser; objectName: "browser"; anchors.fill: parent
        lang: lang; contentIndexModel: null; musicPlayer: null; favoritesModel: favorites; quickAccessModel: quickAccess
        adminEditor: admin; cloudAuth: auth; cloudIntegrationPreferences: prefs
        googleDrive: cloud; oneDrive: cloud; initialLocation: fixturePath
    }
    KeyboardShortcutsDialog { id: shortcutEditor; lang: lang }
}
)qml");
    require(!engine.rootObjects().isEmpty(), "shortcut UI loads");
    auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    window->requestActivate(); pause(350);
    auto manager = qobject_cast<KeyboardShortcutManager *>(window->property("manager").value<QObject *>());
    auto editor = window->property("editor").value<QObject *>();
    require(manager && editor && manager->catalog().size() >= 65, "rich action catalog loads");
    for (const auto &entry : manager->catalog()) {
        const auto id = entry.toMap().value("id").toString();
        require(manager->validateBindings(id, manager->bindings().value(id).toStringList()).value("ok").toBool(), "default bindings have no conflicts");
    }
    require(manager->bindings().value("symlink").toStringList().isEmpty()
            && manager->bindings().value("hardlink").toStringList().isEmpty(), "link actions start unassigned");
    const auto initialCopy = manager->bindings().value("copy");
    require(!manager->assign("symlink", {"Ctrl+C"}).value("ok").toBool(), "conflicting assignment is rejected");
    require(manager->bindings().value("copy") == initialCopy, "rejection preserves previous bindings");
    require(!manager->assign("symlink", {"F11"}).value("ok").toBool(), "global shortcuts conflict across contexts");
    require(!manager->assign("symlink", {"Ctrl+Bogus"}).value("ok").toBool(), "invalid key is rejected");
    require(manager->assign("symlink", {"Ctrl+C"}, true).value("ok").toBool(), "explicit reassignment succeeds");
    require(!manager->bindings().value("copy").toStringList().contains("Ctrl+C"), "reassignment removes conflicting alias");
    require(manager->assign("hardlink", {"F8", "Ctrl+Alt+H"}).value("ok").toBool(), "multiple alternative bindings accepted");
    require(manager->assign("trash", {}).value("ok").toBool(), "shortcut can be disabled");
    {
        KeyboardShortcutManager restored;
        require(restored.bindings() == manager->bindings(), "custom aliases, reassignment and empty bindings survive restart");
    }
    manager->resetAll();
    auto evaluate = [&](QObject *scope, QQmlContext *context, const QString &expression) {
        QQmlExpression code(context, scope, expression); auto value = code.evaluate();
        if (code.hasError()) std::fprintf(stderr, "%s\n", qPrintable(code.error().toString()));
        require(!code.hasError(), "QML expression executes"); return value;
    };
    auto key = [&](int code, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        QKeyEvent press(QEvent::KeyPress, code, modifiers);
        QKeyEvent release(QEvent::KeyRelease, code, modifiers);
        QApplication::sendEvent(window, &press); QApplication::sendEvent(window, &release); pause(120);
    };
    require(QMetaObject::invokeMethod(editor, "open"), "editor opens"); pause(260);
    require(manager->editorOpen(), "editor suspends action shortcuts");
    QVariantMap symlink;
    for (const auto &entry : manager->catalog()) if (entry.toMap().value("id") == "symlink") symlink = entry.toMap();
    require(QMetaObject::invokeMethod(editor, "selectAction", Q_ARG(QVariant, QVariant(symlink))), "select link action");
    auto record = findItem(window->contentItem(), "recordShortcutButton");
    require(record && QMetaObject::invokeMethod(record, "clicked"), "record button starts capture");
    key(Qt::Key_F7);
    require(!manager->recording() && evaluate(editor, qmlContext(editor), "draft.join() ").toString() == "F7", "real key press records combination");
    require(QMetaObject::invokeMethod(editor, "save", Q_ARG(QVariant, QVariant(false))), "save action executes");
    require(manager->bindings().value("symlink").toStringList() == QStringList{"F7"}, "UI save applies new binding");
    // Recording reserved keys must not close the modal or run other actions.
    manager->startRecording(); key(Qt::Key_Escape);
    require(editor->property("visible").toBool() && !manager->recording(), "Escape can be recorded without closing editor");
    require(evaluate(editor, qmlContext(editor), "validation.error").toString() == "conflict", "recorded conflicting key is explained");
    QMetaObject::invokeMethod(editor, "selectAction", Q_ARG(QVariant, QVariant(symlink)));
    window->property("language").value<QObject *>()->setProperty("language", "en");
    evaluate(window, qmlContext(window), "(function(){ if(AppTheme.dark) AppTheme.toggle(); })()");
    pause(100); window->grabWindow().save("/tmp/gfile-keyboard-light.png");
    window->property("language").value<QObject *>()->setProperty("language", "tr");
    evaluate(window, qmlContext(window), "(function(){ if(!AppTheme.dark) AppTheme.toggle(); })()");
    pause(100); window->grabWindow().save("/tmp/gfile-keyboard-dark.png");
    require(QMetaObject::invokeMethod(editor, "close"), "editor closes"); pause(160);
    require(!manager->editorOpen(), "closing resumes shortcuts");
    auto browser = findItem(window->contentItem(), "browser");
    auto pane = qobject_cast<QQuickItem *>(browser->property("activePane").value<QObject *>());
    DirectoryModel *directory = nullptr;
    for (auto candidate : browser->findChildren<DirectoryModel *>()) if (candidate->location() == QUrl::fromLocalFile(base).toString()) directory = candidate;
    require(directory && until([&] { return !directory->loading() && directory->rowCount() == 1; }), "file list ready");
    auto context = qmlContext(directory);
    QMetaObject::invokeMethod(pane, "selectIndex", Q_ARG(QVariant, QVariant(0)), Q_ARG(QVariant, QVariant(0)));
    auto linkDialog = evaluate(pane, context, "newSymlinkDialog").value<QObject *>();
    pane->forceActiveFocus(); key(Qt::Key_F, Qt::ControlModifier);
    require(pane->property("searchVisible").toBool(), "default Find shortcut still works");
    key(Qt::Key_F7);
    require(!linkDialog->property("visible").toBool(), "custom file shortcut does not interrupt search text entry");
    key(Qt::Key_Escape);
    require(!pane->property("searchVisible").toBool(), "default Escape still closes search");
    QMetaObject::invokeMethod(pane, "selectIndex", Q_ARG(QVariant, QVariant(0)), Q_ARG(QVariant, QVariant(0)));
    pane->forceActiveFocus(); key(Qt::Key_F7);
    require(linkDialog && linkDialog->property("visible").toBool(), "assigned shortcut opens selected source's symlink dialog");
    require(evaluate(pane, context, "newSymlinkTargetInput.text").toString() == base + "/source.txt", "shortcut uses correct source");
    require(evaluate(pane, context, "copyActionsMenu.itemAt(0).text").toString().contains("F7"), "context menu displays custom shortcut");
    QMetaObject::invokeMethod(linkDialog, "close"); pause(150);
    manager->assign("hardlink", {"F8"}); pane->forceActiveFocus(); key(Qt::Key_F8);
    auto hardDialog = evaluate(pane, context, "newHardlinkDialog").value<QObject *>();
    require(hardDialog->property("visible").toBool(), "assigned hardlink shortcut runs");
    QMetaObject::invokeMethod(hardDialog, "close"); pause(150);
    pane->forceActiveFocus(); key(Qt::Key_T, Qt::ControlModifier);
    require(evaluate(browser, qmlContext(browser), "tabs.length").toInt() == 2, "default New Tab shortcut still works");
    key(Qt::Key_W, Qt::ControlModifier);
    require(evaluate(browser, qmlContext(browser), "tabs.length").toInt() == 1, "default Close Tab shortcut still works");
    // Native video handling reads the same bindings, including fullscreen windows.
    VideoPlayerInputManager input;
    QObject viewer; viewer.setProperty("visible", true);
    input.setShortcutManager(manager); input.registerViewer(&viewer);
    int toggled = 0; QObject::connect(&input, &VideoPlayerInputManager::togglePlaybackPressed, [&] { ++toggled; });
    int escaped = 0; QObject::connect(&input, &VideoPlayerInputManager::escapePressed, [&] { ++escaped; });
    key(Qt::Key_Escape); require(escaped == 1, "default native video Escape remains active");
    require(manager->assign("video_play", {"Alt+P"}).value("ok").toBool(), "video control can be remapped");
    key(Qt::Key_Space); require(toggled == 0, "old video binding is removed");
    key(Qt::Key_P, Qt::AltModifier); require(toggled == 1, "native video filter uses new combination");
    manager->setEditorOpen(true); key(Qt::Key_P, Qt::AltModifier);
    require(toggled == 1, "shortcut editor suppresses video controls"); manager->setEditorOpen(false);
    manager->resetAll();
    require(manager->bindings().value("symlink").toStringList().isEmpty(), "reset restores unassigned action");
    std::fprintf(stderr, "PASS: persisted shortcuts, conflicts, recording, UI assignment, menu hints and native playback remapping\n");
}
