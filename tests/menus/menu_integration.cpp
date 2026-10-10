#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QWheelEvent>
#include <cstdio>
#include <array>
#include <functional>
#include <sys/stat.h>
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
    app.setOrganizationName("Lurviko-QA"); app.setApplicationName("Menus");
    QQuickStyle::setStyle("Basic");
    // REGISTER_BACKEND_TYPES
    const QString base = QString::fromLocal8Bit(qgetenv("LURVIKO_MENU_TEST_ROOT")) + "/fixture";
    QDir().mkpath(base + "/folder");
    QFile source(base + "/source.txt");
    require(source.open(QIODevice::WriteOnly), "link source fixture opens");
    source.write("Copy actions fixture\n");
    source.close();
    QQmlApplicationEngine engine;
    engine.addImageProvider("gfilethumb", new ThumbnailProvider);
    engine.addImageProvider("systemicon", new SystemIconProvider);
    engine.addImageProvider("bundledicon", new BundledIconProvider);
    engine.rootContext()->setContextProperty("fixturePath", base);
    engine.loadData(R"qml(
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lurviko.App
import Lurviko.Backend
ApplicationWindow {
    width: 1600; height: 900; visible: true
    LanguageManager {id: language}
    FavoritesModel {id: favorites}
    QuickAccessModel {id: quickAccess}
    AdminEditManager {id: admin}
    QtObject {id: auth; property string googleAccessToken: ""; property string oneDriveAccessToken: ""}
    QtObject {id: prefs}
    QtObject {id: google}
    QtObject {id: one}
    QtObject {id: storage; property var favoriteItems: []}
    MusicPlayer {id: music; objectName: "music"; lang: language; opened: true; panelVisible: false}
    RowLayout {
        anchors.fill: parent; anchors.margins: 11; spacing: 7
        AppSidebar {
            objectName: "sidebar"; Layout.preferredWidth: 248; Layout.fillHeight: true
            lang: language; favoritesModel: favorites; storageModel: storage; musicPlayer: music
        }
        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true; clip: true
            StackView {
                anchors.fill: parent; anchors.margins: 1; clip: true
                initialItem: Component {
                    BrowsePage {
                        objectName: "page"; lang: language
                        contentIndexModel: null; musicPlayer: music; favoritesModel: favorites; quickAccessModel: quickAccess
                        adminEditor: admin; cloudAuth: auth; cloudIntegrationPreferences: prefs
                        googleDrive: google; oneDrive: one; initialLocation: fixturePath
                    }
                }
            }
        }
    }
}
)qml");
    require(!engine.rootObjects().isEmpty(), "BrowsePage loads");
    auto window = qobject_cast<QQuickWindow *>(engine.rootObjects()[0]);
    pause(400);
    auto page = findView(window->contentItem(), "page");
    require(page, "StackView creates BrowsePage");
    auto pane = qobject_cast<QQuickItem *>(page->property("activePane").value<QObject *>());
    require(pane, "BrowsePage has active pane");
    DirectoryModel *directory = nullptr;
    for (auto candidate : page->findChildren<DirectoryModel *>())
        if (candidate->location() == QUrl::fromLocalFile(base).toString()) directory = candidate;
    require(directory, "directory model exists");
    auto paneContext = qmlContext(directory);
    auto sidebar = findView(window->contentItem(), "sidebar");
    require(sidebar, "sidebar exists");
    auto item = [&](QObject *scope, QQmlContext *context, const char *name) {
        QQmlExpression expression(context, scope, QString::fromLatin1(name));
        auto result = expression.evaluate().value<QObject *>();
        require(result && !expression.hasError(), name);
        return result;
    };
    auto click = [&](QQuickItem *button, int settleMs = 240, QPointF local = QPointF(-1,-1)) {
        const QPointF position = button->mapToScene(local.x() < 0 ? QPointF(button->width()/2, button->height()/2) : local);
        const QPointF global = window->mapToGlobal(position.toPoint());
        QMouseEvent move(QEvent::MouseMove, position, global, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(window, &move);
        pause(20);
        QMouseEvent press(QEvent::MouseButtonPress, position, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, position, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(window, &press);
        pause(settleMs >= 240 ? 90 : 5);
        QApplication::sendEvent(window, &release);
        pause(settleMs);
    };
    auto context = qmlContext(pane);
    auto hiddenButton = qobject_cast<QQuickItem *>(item(page,context,"hiddenFilesButton"));
    auto viewButton = qobject_cast<QQuickItem *>(item(page,context,"viewButton"));
    auto wheelButton = qobject_cast<QQuickItem *>(item(page,context,"wheelSpeedButton"));
    auto directoryButton = qobject_cast<QQuickItem *>(item(page,context,"directoryMenuButton"));
    auto hidden = item(page,context,"hiddenFilesMenu");
    auto view = item(page,context,"toolbarViewMenu");
    auto wheel = item(page,context,"wheelSpeedPopup");
    click(hiddenButton); require(hidden->property("visible").toBool(), "first hidden click opens menu");
    click(viewButton);
    std::fprintf(stderr,"Switch hidden -> view: old=%d new=%d\n",hidden->property("visible").toBool(),view->property("visible").toBool());
    require(!hidden->property("visible").toBool() && view->property("visible").toBool(), "single click switches from hidden menu to view menu");
    click(wheelButton); require(!view->property("visible").toBool() && wheel->property("visible").toBool(), "single click switches menu to popup");
    click(hiddenButton); require(!wheel->property("visible").toBool() && hidden->property("visible").toBool(), "single click switches popup to menu");
    click(hiddenButton); require(!hidden->property("visible").toBool(), "same button toggles menu closed");
    for (int i = 0; i < 3; ++i) {
        click(hiddenButton, 20); require(hidden->property("visible").toBool(), "rapid hidden open");
        click(viewButton, 20); require(view->property("visible").toBool(), "rapid hidden to view");
        click(hiddenButton, 20); require(hidden->property("visible").toBool(), "rapid view to hidden");
        pause(240); require(hidden->property("visible").toBool(), "rapid target stays open after previous exit");
        click(hiddenButton);
    }
    click(viewButton);
    require(view->property("height").toReal() > 180, "toolbar menu retains full height with button parent");
    click(directoryButton);
    require(!view->property("visible").toBool() && pane->property("directoryMenuVisible").toBool(), "view to directory menu switches in one click");
    auto directoryPopupItem = qobject_cast<QQuickItem *>(item(pane,paneContext,"backgroundContextMenu.contentItem.parent"));
    const qreal directoryTop = directoryPopupItem->mapToScene(QPointF()).y();
    const qreal buttonBottom = directoryButton->parentItem()->mapToScene(QPointF(directoryButton->x(), directoryButton->y()+directoryButton->height())).y();
    require(qAbs(directoryTop-buttonBottom-6) < 2, "directory menu sits immediately below toolbar button");
    click(directoryButton);
    require(!pane->property("directoryMenuVisible").toBool(), "same directory button closes menu");
    click(directoryButton);
    click(hiddenButton);
    require(!pane->property("directoryMenuVisible").toBool() && hidden->property("visible").toBool(), "directory to hidden menu switches in one click");
    click(directoryButton, 20);
    click(viewButton, 20);
    click(directoryButton, 20);
    pause(240);
    require(!view->property("visible").toBool() && pane->property("directoryMenuVisible").toBool(), "rapid directory menu switch stays open");
    click(pane);
    require(!pane->property("directoryMenuVisible").toBool(), "clicking file area dismisses directory menu");
    auto backgroundMenu = item(pane,paneContext,"backgroundContextMenu");
    const auto blankPosition = pane->mapToScene(QPointF(pane->width()/2, pane->height()/2));
    const QPointF blankGlobal = window->mapToGlobal(blankPosition.toPoint());
    QMouseEvent rightPress(QEvent::MouseButtonPress, blankPosition, blankGlobal, Qt::RightButton, Qt::RightButton, Qt::NoModifier);
    QMouseEvent rightRelease(QEvent::MouseButtonRelease, blankPosition, blankGlobal, Qt::RightButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(window, &rightPress);
    QApplication::sendEvent(window, &rightRelease);
    pause(240);
    require(backgroundMenu->property("visible").toBool(), "background right click still opens context menu");
    require(backgroundMenu->property("parent").value<QObject *>() == pane, "right click restores file area anchor after toolbar use");
    click(hiddenButton);
    require(!backgroundMenu->property("visible").toBool() && hidden->property("visible").toBool(), "context menu to toolbar switches in one click");
    auto recentButton = qobject_cast<QQuickItem *>(item(pane,paneContext,"recentLocationsButton"));
    auto recentMenu = item(pane,paneContext,"recentLocationsMenu");
    click(recentButton);
    require(!hidden->property("visible").toBool() && recentMenu->property("visible").toBool(), "toolbar to recent locations switches in one click");
    click(recentButton);
    require(!recentMenu->property("visible").toBool(), "same recent locations button closes menu");
    const std::array<std::pair<QQuickItem *, QObject *>, 5> menus = {{
        {hiddenButton,hidden}, {viewButton,view}, {wheelButton,wheel},
        {directoryButton,backgroundMenu}, {recentButton,recentMenu}
    }};
    for (size_t source = 0; source < menus.size(); ++source) {
        click(menus[source].first);
        for (size_t target = 0; target < menus.size(); ++target) {
            if (source == target) continue;
            click(menus[target].first);
            require(!menus[source].second->property("visible").toBool() && menus[target].second->property("visible").toBool(), "all dropdown pairs switch in one normal-duration click");
            click(menus[source].first);
            require(!menus[target].second->property("visible").toBool() && menus[source].second->property("visible").toBool(), "reverse dropdown switch works in one click");
        }
        click(menus[source].first);
    }
    click(directoryButton);
    auto sortButton = qobject_cast<QQuickItem *>(item(pane,paneContext,
        "(function(){ for(let i=0; i<backgroundContextMenu.count; ++i) { const entry=backgroundContextMenu.itemAt(i); if(entry && entry.subMenu===sortMenu) return entry; } return null; })()"));
    auto sortPopup = item(pane,paneContext,"sortMenu");
    click(sortButton);
    require(sortPopup->property("visible").toBool(), "directory sort submenu opens");
    auto sortPopupItem = qobject_cast<QQuickItem *>(item(pane,paneContext,"sortMenu.contentItem.parent"));
    click(sortPopupItem,240,QPointF(3,3));
    require(sortPopup->property("visible").toBool() && backgroundMenu->property("visible").toBool(), "clicking submenu padding preserves parent and submenu");
    click(hiddenButton);
    require(!backgroundMenu->property("visible").toBool() && !sortPopup->property("visible").toBool() && hidden->property("visible").toBool(), "submenu to toolbar switches in one click");
    click(hiddenButton);
    // Follow the actual selection-menu path, including modal Enter acceptance.
    auto evaluatePane = [&](const QString &script) {
        QQmlExpression expression(paneContext, pane, script);
        auto result = expression.evaluate();
        require(!expression.hasError(), qPrintable(expression.error().toString()));
        return result;
    };
    auto openCopyActions = [&](const QString &name = QStringLiteral("source.txt")) {
        require(until([&] { return !directory->loading() && directory->indexOfUrl(base + "/" + name) >= 0; }), "source listed");
        require(QMetaObject::invokeMethod(pane, "selectIndex", Q_ARG(QVariant, QVariant(directory->indexOfUrl(base + "/" + name))), Q_ARG(QVariant, QVariant(0))), "copy source selected");
        evaluatePane("itemContextMenu.x=100; itemContextMenu.y=100; itemContextMenu.open()");
        require(until([&] { return pane->property("submenuActivationReady").toBool(); }), "context submenu activation is ready after opening animation");
        auto button = qobject_cast<QQuickItem *>(item(pane, paneContext,
            "(function(){ for(let i=0; i<itemContextMenu.count; ++i) { const entry=itemContextMenu.itemAt(i); if(entry && entry.subMenu===copyActionsMenu) { itemContextMenu.currentIndex=i; return entry; } } return null; })()"));
        pause(50);
        require(evaluatePane("(function(){ for(let i=1; i<itemContextMenu.count; ++i) { if(itemContextMenu.itemAt(i).subMenu===copyActionsMenu) return itemContextMenu.itemAt(i-1).shortcutText.indexOf(KeyboardShortcuts.displaySequence('Ctrl+C')) >= 0; } return false; })()").toBool(), "copy actions sits directly below Copy");
        click(button);
        require(item(pane, paneContext, "copyActionsMenu")->property("visible").toBool(), "copy actions submenu opens");
    };
    require(QMetaObject::invokeMethod(pane, "selectIndex", Q_ARG(QVariant, QVariant(directory->indexOfUrl(base + "/source.txt"))), Q_ARG(QVariant, QVariant(0))), "context menu fixture selected");
    evaluatePane("itemContextMenu.maximumPopupHeight=220; itemContextMenu.open()"); pause(250);
    auto menuList = qobject_cast<QQuickItem *>(item(pane, paneContext, "itemContextMenu.contentItem.children[0]"));
    const QPointF wheelPoint = menuList->mapToScene(QPointF(menuList->width() / 2, menuList->height() / 2));
    QWheelEvent wheelEvent(wheelPoint, window->mapToGlobal(wheelPoint.toPoint()), QPoint(), QPoint(0, -120),
                           Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(window, &wheelEvent); pause(220);
    require(evaluatePane("itemContextMenu.contentItem.children[0].contentY-itemContextMenu.contentItem.children[0].originY >= 200").toBool(),
            "context menu wheel uses the configured pixel step instead of tiny default movement");
    evaluatePane("itemContextMenu.contentItem.children[0].positionViewAtEnd()"); pause(50);
    require(evaluatePane("itemContextMenu.contentItem.children[0].contentY > itemContextMenu.contentItem.children[0].originY").toBool(), "context menu is actually scrolled");
    evaluatePane("closeContextMenus()"); pause(100);
    evaluatePane("itemContextMenu.open()"); pause(250);
    require(evaluatePane("Math.abs(itemContextMenu.contentItem.children[0].contentY-itemContextMenu.contentItem.children[0].originY)<1").toBool(), "new context menu starts at the top");
    evaluatePane("closeContextMenus(); itemContextMenu.maximumPopupHeight=620"); pause(100);
    auto acceptLinkName = [&](const char *inputId, const QString &name) {
        auto input = item(pane, paneContext, inputId);
        input->setProperty("text", name);
        require(QMetaObject::invokeMethod(input, "forceActiveFocus"), "link name receives focus");
        QKeyEvent press(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QKeyEvent release(QEvent::KeyRelease, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(window, &press);
        QApplication::sendEvent(window, &release);
        pause(240);
    };
    evaluatePane("KeyboardShortcuts.assign('symlink', ['Ctrl+Shift+Alt+L'])");
    openCopyActions();
    auto symlinkAction = qobject_cast<QQuickItem *>(item(pane, paneContext, "copyActionsMenu.itemAt(0)"));
    auto shortcutLabel = findView(symlinkAction, "menuShortcutText");
    require(shortcutLabel && shortcutLabel->property("text").toString() == "Ctrl+Alt+Shift+L"
            && !shortcutLabel->property("truncated").toBool(), "long saved shortcut is shown without ellipsis");
    const auto modifier = [&](QEvent::Type type, int key, Qt::KeyboardModifiers flags) {
        QKeyEvent event(type, key, flags);
        QApplication::sendEvent(window, &event);
        pause(30);
    };
    const qreal popupWidth = item(pane, paneContext, "copyActionsMenu")->property("width").toReal();
    modifier(QEvent::KeyPress, Qt::Key_Control, Qt::ControlModifier);
    require(symlinkAction->property("shortcutHighlighted").toBool(), "Ctrl highlights compatible saved shortcuts");
    modifier(QEvent::KeyPress, Qt::Key_Shift, Qt::ControlModifier | Qt::ShiftModifier);
    require(symlinkAction->property("shortcutHighlighted").toBool()
            && !item(pane, paneContext, "copyActionsMenu.itemAt(3)")->property("shortcutHighlighted").toBool(),
            "Ctrl+Shift highlights matching actions only");
    require(item(pane, paneContext, "copyActionsMenu")->property("width").toReal() == popupWidth,
            "modifier hints do not resize the open popup");
    window->grabWindow().save("/tmp/lurviko-menu-shortcuts.png");
    modifier(QEvent::KeyRelease, Qt::Key_Shift, Qt::ControlModifier);
    modifier(QEvent::KeyRelease, Qt::Key_Control, Qt::NoModifier);
    require(!symlinkAction->property("shortcutHighlighted").toBool(), "releasing modifiers clears shortcut hints");
    std::fprintf(stderr, "PASS: complete shortcut column and live Ctrl/Shift hints\n");
    click(symlinkAction);
    require(item(pane, paneContext, "newSymlinkDialog")->property("visible").toBool(), "symlink action opens dialog");
    require(!item(pane, paneContext, "itemContextMenu")->property("visible").toBool(), "link action dismisses parent menu");
    require(item(pane, paneContext, "newSymlinkTargetInput")->property("text").toString() == base + "/source.txt", "symlink source is prefilled");
    acceptLinkName("newSymlinkNameInput", "source-link.txt");
    require(QFileInfo(base + "/source-link.txt").isSymLink()
            && QFileInfo(base + "/source-link.txt").symLinkTarget() == base + "/source.txt", "Enter creates symlink to selected source");
    require(until([&] { return !directory->loading() && !pane->property("pendingResultSelection").toBool()
        && pane->property("selectedUrl").toString() == QUrl::fromLocalFile(base + "/source-link.txt").toString(); }), "created symlink receives selection after folder refresh");
    openCopyActions();
    click(qobject_cast<QQuickItem *>(item(pane, paneContext, "copyActionsMenu.itemAt(1)")));
    require(item(pane, paneContext, "newHardlinkDialog")->property("visible").toBool(), "hardlink action opens dialog");
    require(item(pane, paneContext, "newHardlinkTargetInput")->property("text").toString() == base + "/source.txt", "hardlink source is prefilled");
    acceptLinkName("newHardlinkNameInput", "source-hardlink.txt");
    struct stat sourceStat{}, linkStat{};
    require(::stat(QFile::encodeName(base + "/source.txt").constData(), &sourceStat) == 0
            && ::stat(QFile::encodeName(base + "/source-hardlink.txt").constData(), &linkStat) == 0
            && sourceStat.st_dev == linkStat.st_dev && sourceStat.st_ino == linkStat.st_ino, "Enter creates hardlink with matching inode");
    require(until([&] { return !directory->loading() && !pane->property("pendingResultSelection").toBool()
        && pane->property("selectedUrl").toString() == QUrl::fromLocalFile(base + "/source-hardlink.txt").toString(); }), "created hardlink receives selection after folder refresh");
    openCopyActions("source-link.txt");
    require(!item(pane, paneContext, "copyActionsMenu.itemAt(1)")->property("enabled").toBool(), "hardlink disabled for symlink");
    evaluatePane("closeContextMenus()"); pause(100);
    openCopyActions("folder");
    require(pane->property("selectedIsDir").toBool(), "folder fixture is selected");
    require(item(pane, paneContext, "copyActionsMenu.itemAt(0)")->property("enabled").toBool()
            && !item(pane, paneContext, "copyActionsMenu.itemAt(1)")->property("enabled").toBool(), "folder allows symlink and rejects hardlink");
    require(evaluatePane("copyActionsMenu.itemAt(3).shortcutText.indexOf(KeyboardShortcuts.displaySequence('Ctrl+D')) >= 0").toBool(), "duplicate shortcut retained in submenu");
    evaluatePane("closeContextMenus()"); pause(100);
    std::fprintf(stderr, "PASS: selection copy actions create symlink and hardlink through dialogs\n");
    // Internal sidebar ids are visible in one of its child item contexts.
    auto sidebarContext = qmlContext(sidebar->childItems().first());
    auto miniButton = qobject_cast<QQuickItem *>(item(sidebar,sidebarContext,"miniMusicButton"));
    auto miniPopup = item(sidebar,sidebarContext,"miniMusicPopup");
    click(miniButton); require(miniPopup->property("visible").toBool(), "mini player opens");
    auto miniPopupItem = qobject_cast<QQuickItem *>(item(sidebar,sidebarContext,"miniMusicPopup.contentItem.parent"));
    const QPointF expectedMini = sidebar->mapToScene(QPointF((sidebar->width()-miniPopup->property("width").toReal())/2,
        sidebar->height()-miniPopup->property("height").toReal()-48));
    const QPointF actualMini = miniPopupItem->mapToScene(QPointF());
    std::fprintf(stderr,"Mini player position: actual=%.1f,%.1f expected=%.1f,%.1f; directory top=%.1f button bottom=%.1f\n",
        actualMini.x(),actualMini.y(),expectedMini.x(),expectedMini.y(),directoryTop,buttonBottom);
    require(qAbs(actualMini.x()-expectedMini.x()) < 2 && qAbs(actualMini.y()-expectedMini.y()) < 2, "mini player stays centered above sidebar footer");
    click(miniPopupItem);
    require(miniPopup->property("visible").toBool(), "clicking inside mini player does not dismiss it");
    click(miniButton); require(!miniPopup->property("visible").toBool(), "mini player second click closes without reopening");
    click(miniButton); require(miniPopup->property("visible").toBool(), "mini player opens again");
    click(viewButton); require(!miniPopup->property("visible").toBool() && view->property("visible").toBool(), "mini player to toolbar switches in one click");
    click(miniButton); require(!view->property("visible").toBool() && miniPopup->property("visible").toBool(), "toolbar to mini player switches in one click");
    click(pane); require(!miniPopup->property("visible").toBool(), "clicking file area dismisses mini player");
    auto music = findView(window->contentItem(), "music");
    require(music, "global music player exists");
    music->setProperty("width", 1000);
    music->setProperty("height", 126);
    music->setProperty("x", 320);
    music->setProperty("y", 754);
    music->setProperty("z", 100);
    music->setProperty("lyricsText", "Test lyrics");
    music->setProperty("panelVisible", true);
    pause(200);
    auto metadata = music->findChild<MusicMetadataManager *>();
    require(metadata, "music metadata reader exists");
    auto musicContext = qmlContext(metadata);
    auto lyricsButton = qobject_cast<QQuickItem *>(item(music, musicContext, "lyricsButton"));
    auto lyricsPopup = item(music, musicContext, "lyricsPopup");
    click(lyricsButton); require(lyricsPopup->property("visible").toBool(), "lyrics button opens popup");
    click(lyricsButton); require(!lyricsPopup->property("visible").toBool(), "lyrics second click closes without reopening");
    for (int i = 0; i < 3; ++i) {
        click(lyricsButton, 20); require(lyricsPopup->property("visible").toBool(), "rapid lyrics open");
        click(lyricsButton, 20); require(!lyricsPopup->property("visible").toBool(), "rapid lyrics close");
    }
    click(lyricsButton); click(pane);
    require(!lyricsPopup->property("visible").toBool(), "outside click dismisses lyrics popup");
    std::fprintf(stderr,"PASS: real pointer clicks switch popups and toggle mini player\n");
}
