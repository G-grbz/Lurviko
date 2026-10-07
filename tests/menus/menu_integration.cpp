#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QMouseEvent>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <cstdio>
#include <array>
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
    app.setOrganizationName("GFile-QA"); app.setApplicationName("Menus");
    QQuickStyle::setStyle("Basic");
    // REGISTER_BACKEND_TYPES
    const QString base = QString::fromLocal8Bit(qgetenv("GFILE_MENU_TEST_ROOT")) + "/fixture";
    QDir().mkpath(base);
    QQmlApplicationEngine engine;
    engine.addImageProvider("gfilethumb", new ThumbnailProvider);
    engine.addImageProvider("systemicon", new SystemIconProvider);
    engine.rootContext()->setContextProperty("fixturePath", base);
    engine.loadData(R"qml(
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GFile.App
import GFile.Backend
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
    MusicPlayer {id: music; lang: language; opened: true; panelVisible: false}
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
    std::fprintf(stderr,"PASS: real pointer clicks switch popups and toggle mini player\n");
}
