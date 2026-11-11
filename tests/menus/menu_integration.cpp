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
    QFile svg(base + "/graphic.svg");
    require(svg.open(QIODevice::WriteOnly), "svg context fixture opens");
    svg.write("<svg xmlns=\"http://www.w3.org/2000/svg\"/>");
    svg.close();
    QQmlApplicationEngine engine;
    engine.addImageProvider("gfilethumb", new ThumbnailProvider);
    engine.addImageProvider("systemicon", new SystemIconProvider);
    engine.addImageProvider("bundledicon", new BundledIconProvider);
    engine.addImageProvider("folderart", new FolderIconProvider);
    engine.addImageProvider("fileart", new ArtworkIconProvider);
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
        // Construct each event right before delivery (Qt 6.8 shares event-point
        // state between QMouseEvent instances of the same device).
        QMouseEvent press(QEvent::MouseButtonPress, position, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(window, &press);
        pause(settleMs >= 240 ? 90 : 5);
        QMouseEvent release(QEvent::MouseButtonRelease, position, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
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
    std::fprintf(stderr,"Switch hidden -> view: old=%d new=%d recoverySeen=%d applied=%d\n",
        hidden->property("visible").toBool(), view->property("visible").toBool(),
        page->property("toolbarRecoverySeen").toInt(), page->property("toolbarRecoveryApplied").toInt());
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
    if (hidden->property("visible").toBool() || !recentMenu->property("visible").toBool())
        std::fprintf(stderr, "Toolbar -> recent: old=%d new=%d recoverySeen=%d applied=%d\n",
            hidden->property("visible").toBool(), recentMenu->property("visible").toBool(),
            page->property("toolbarRecoverySeen").toInt(),
            page->property("toolbarRecoveryApplied").toInt());
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
            if (menus[source].second->property("visible").toBool() || !menus[target].second->property("visible").toBool())
                std::fprintf(stderr, "Dropdown switch: source=%zu target=%zu old=%d new=%d recoverySeen=%d applied=%d\n",
                    source, target, menus[source].second->property("visible").toBool(),
                    menus[target].second->property("visible").toBool(),
                    page->property("toolbarRecoverySeen").toInt(), page->property("toolbarRecoveryApplied").toInt());
            require(!menus[source].second->property("visible").toBool() && menus[target].second->property("visible").toBool(), "all dropdown pairs switch in one normal-duration click");
            click(menus[source].first);
            if (menus[target].second->property("visible").toBool() || !menus[source].second->property("visible").toBool())
                std::fprintf(stderr, "Reverse dropdown: source=%zu target=%zu old=%d new=%d recoverySeen=%d applied=%d\n",
                    source, target, menus[target].second->property("visible").toBool(),
                    menus[source].second->property("visible").toBool(),
                    page->property("toolbarRecoverySeen").toInt(), page->property("toolbarRecoveryApplied").toInt());
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
    // Inspect the same outside press *before* its release. A menu that is
    // merely closing with an animation may swallow the button's click.
    const QPointF toolbarPoint = hiddenButton->mapToScene(
        QPointF(hiddenButton->width() / 2, hiddenButton->height() / 2));
    const QPointF toolbarGlobal = window->mapToGlobal(toolbarPoint.toPoint());
    QMouseEvent submenuMove(QEvent::MouseMove, toolbarPoint, toolbarGlobal,
                            Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(window, &submenuMove);
    pause(20);
    QMouseEvent submenuPress(QEvent::MouseButtonPress, toolbarPoint, toolbarGlobal,
                             Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(window, &submenuPress);
    const bool parentClosedOnPress = !backgroundMenu->property("visible").toBool();
    const bool childClosedOnPress = !sortPopup->property("visible").toBool();
    const bool toolbarSawPress = page->property("toolbarPressedMenu").value<QObject *>() == hidden;
    pause(90);
    QMouseEvent submenuRelease(QEvent::MouseButtonRelease, toolbarPoint, toolbarGlobal,
                               Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(window, &submenuRelease);
    pause(240);
    if (backgroundMenu->property("visible").toBool() || sortPopup->property("visible").toBool()
            || !hidden->property("visible").toBool()) {
        std::fprintf(stderr,
            "Submenu -> toolbar: parent=%d submenu=%d hidden=%d "
            "parentClosedOnPress=%d childClosedOnPress=%d toolbarSawPress=%d "
            "recoverySeen=%d applied=%d\n",
            backgroundMenu->property("visible").toBool(), sortPopup->property("visible").toBool(),
            hidden->property("visible").toBool(), parentClosedOnPress,
            childClosedOnPress, toolbarSawPress,
            page->property("toolbarRecoverySeen").toInt(),
            page->property("toolbarRecoveryApplied").toInt());
    }
    require(!backgroundMenu->property("visible").toBool() && !sortPopup->property("visible").toBool() && hidden->property("visible").toBool(), "submenu to toolbar switches in one click");
    click(hiddenButton);
    // One outside press must dismiss the complete directory/submenu stack.
    // Previously the first click closed only Sort, leaving the parent open.
    click(directoryButton);
    click(sortButton);
    require(sortPopup->property("visible").toBool(), "sort submenu reopens for outside-click regression");
    click(pane, 240, QPointF(18, pane->height() - 18));
    require(!sortPopup->property("visible").toBool() && !backgroundMenu->property("visible").toBool(),
            "one outside click dismisses directory menu and Sort submenu");
    // Follow the actual selection-menu path, including modal Enter acceptance.
    auto evaluatePane = [&](const QString &script) {
        QQmlExpression expression(paneContext, pane, script);
        auto result = expression.evaluate();
        require(!expression.hasError(), qPrintable(expression.error().toString()));
        return result;
    };
    require(until([&] { return !directory->loading() && directory->indexOfUrl(base + "/source.txt") >= 0; }), "icon fixture listed");
    require(QMetaObject::invokeMethod(pane, "selectIndex", Q_ARG(QVariant, QVariant(directory->indexOfUrl(base + "/source.txt"))),
            Q_ARG(QVariant, QVariant(0))), "context icon fixture selected");
    evaluatePane("itemContextMenu.x=100; itemContextMenu.y=100; itemContextMenu.open()");
    require(until([&] { return pane->property("submenuActivationReady").toBool(); }), "icon context menu ready");
    // The menu may retint bundled actions using its typography palette.
    // Compare the glyph identity, not the URL's encoded color parameters.
    // Native MIME/application icons must instead retain their exact URL.
    const auto checkIcon = [&](QObject *entry, const QString &source) {
        // The menu can still be laying out its entries on a slow CI runner;
        // wait for the glyph instead of sampling a single frame.
        QQuickItem *glyph = nullptr;
        require(until([&] {
                    glyph = findView(qobject_cast<QQuickItem *>(entry), "menuActionIcon");
                    return glyph && glyph->isVisible();
                }), "context icon is visible beside its action");
        const QUrl actual = glyph->property("source").toUrl();
        if (source.startsWith("image://bundledicon/")) {
            const QString leaf = source.section('/', -1).section('|', 0, 0)
                                      .section('%', 0, 0).section('?', 0, 0);
            require(actual.toString().startsWith("image://bundledicon/" + leaf),
                    "context action uses its own symbolic glyph, never a folder fallback");
            require(!actual.toString().contains("folder.svg"),
                    "menu action never substitutes the directory icon");
        } else {
            require(actual == QUrl(source),
                    "native MIME/application artwork retains its original colors and source");
        }
        require(until([&] { return glyph->property("status").toInt() == 1; }),
                "context icon actually renders");
    };
    // Qt QUrl may encode the provider separator once or twice. Regression:
    // neither variant may leak into AppTheme.icon() as an invalid filename.
    for (const auto &encoded : {"%7C%23ffffff", "%257C%2523ffffff", "|%23ffffff"}) {
        const QString expression = QString("AppTheme.menuActionIcon('image://bundledicon/edit-cut.svg%1', '', false)")
                                   .arg(QString::fromLatin1(encoded));
        const QString result = evaluatePane(expression).toString();
        require(result.startsWith("image://bundledicon/edit-cut.svg|"),
                "encoded action icon URL resolves to the original glyph");
        require(!result.contains("folder.svg"), "encoded glyph URL cannot become a directory icon");
    }
    for (const auto &name : {"plus.svg", "refresh.svg", "favorite.svg"}) {
        const QString expression = QString("AppTheme.menuActionIcon(AppTheme.icon('%1'), '', false)")
                                   .arg(QString::fromLatin1(name));
        const QString result = evaluatePane(expression).toString();
        require(result.startsWith("image://bundledicon/" + QString::fromLatin1(name) + "|"),
                "new, refresh and favorite menu actions follow the current menu theme");
    }
    const QString appIcon = evaluatePane("AppTheme.systemIcon('accessories-text-editor')").toString();
    const QString originalApp = evaluatePane("AppTheme.menuActionIcon(AppTheme.systemIcon('accessories-text-editor'), '', true)").toString();
    require(appIcon == originalApp, "MIME/application icon URLs are never recolored");
    for (const auto &action : {"cut", "copy", "rename", "trash", "properties"}) {
        const QString lookup = QString("(function(){for(let i=0;i<itemContextMenu.count;++i){const e=itemContextMenu.itemAt(i);if(e.shortcutAction==='%1') return e;}return null;})()").arg(action);
        auto entry = item(pane, paneContext, qPrintable(lookup));
        const QString source = evaluatePane(lookup + ".icon.source.toString()").toString();
        require(source.contains("image://bundledicon/"), "built-in file actions retain bundled symbolic artwork");
        checkIcon(entry, source);
    }
    // Changing the right-click target while an existing context popup exits
    // must never display the previous file's default app or artwork.
    require(evaluatePane("openWithModel.defaultApplicationName === 'Lurviko Menu Fixture'").toBool(),
            "text file shows its text editor as default application");
    const int svgIndex = directory->indexOfUrl(base + "/graphic.svg");
    require(svgIndex >= 0, "second MIME fixture is present");
    require(QMetaObject::invokeMethod(pane, "selectIndex", Q_ARG(QVariant, QVariant(svgIndex)),
                                      Q_ARG(QVariant, QVariant(0))), "svg fixture selected");
    evaluatePane("showContextMenu(itemContextMenu, root, 110, 115)");
    pause(260);
    require(evaluatePane("openWithModel.defaultApplicationName === 'Lurviko SVG Fixture'").toBool(),
            "svg context uses its own default app, not previous editor");
    require(evaluatePane("itemContextMenu.itemAt(0).text.indexOf('Lurviko SVG Fixture') >= 0").toBool(),
            "svg default-app label is current before menu is shown");
    require(evaluatePane("itemContextMenu.itemAt(0).icon.source.toString() === AppTheme.systemIcon('inkscape')").toBool(),
            "svg app logo follows the new MIME, not the text file");
    const int textIndex = directory->indexOfUrl(base + "/source.txt");
    require(QMetaObject::invokeMethod(pane, "selectIndex", Q_ARG(QVariant, QVariant(textIndex)),
                                      Q_ARG(QVariant, QVariant(0))), "text fixture reselected");
    evaluatePane("showContextMenu(itemContextMenu, root, 110, 115)");
    pause(260);
    require(evaluatePane("openWithModel.defaultApplicationName === 'Lurviko Menu Fixture'").toBool(),
            "text default app restored after switching back from svg");
    require(evaluatePane("itemContextMenu.itemAt(0).text.indexOf('Lurviko Menu Fixture') >= 0").toBool(),
            "text default-app label is refreshed on new right-click");
    // Right-clicking another file while a menu is open reloads the service
    // actions. Their count must only toggle the submenu's entry, never open
    // the Application Actions popup by itself.
    require(evaluatePane("serviceMenuModel.clear(); applicationServiceMenu.visible").toBool() == false,
            "application actions closed while its model is empty");
    require(evaluatePane("updateServiceMenuContext(); serviceMenuModel.count > 0 && !applicationServiceMenu.visible").toBool(),
            "loading the next file's actions does not pop up Application Actions");
    pause(200);
    require(!item(pane, paneContext, "applicationServiceMenu")->property("visible").toBool(),
            "Application Actions stays closed after the reload settles");
    // Several files: the label names their default application(s). Each file
    // still opens with its own type's default (openDefaults), unchanged.
    require(QMetaObject::invokeMethod(pane, "selectIndex", Q_ARG(QVariant, QVariant(textIndex)),
                                      Q_ARG(QVariant, QVariant(0))), "text fixture selected for multi-selection");
    require(QMetaObject::invokeMethod(pane, "selectIndex", Q_ARG(QVariant, QVariant(svgIndex)),
                                      Q_ARG(QVariant, QVariant(int(Qt::ControlModifier)))), "svg added to the selection");
    evaluatePane("showContextMenu(itemContextMenu, root, 110, 115)");
    pause(260);
    const QString mixedLabel = evaluatePane("itemContextMenu.itemAt(0).text").toString();
    require(evaluatePane("selectedCount").toInt() == 2 && mixedLabel.contains("Lurviko Menu Fixture")
            && mixedLabel.contains("Lurviko SVG Fixture"),
            qPrintable("mixed selection lists both default applications: " + mixedLabel));
    require(QMetaObject::invokeMethod(pane, "selectIndex", Q_ARG(QVariant, QVariant(textIndex)),
                                      Q_ARG(QVariant, QVariant(0))), "text fixture reselected after multi-selection");
    // Leave the text file's menu open as the following checks expect.
    evaluatePane("showContextMenu(itemContextMenu, root, 110, 115)");
    pause(260);
    require(evaluatePane("openWithModel.quickApps.some(app => app.name === 'Lurviko Menu Fixture')").toBool(), "Open With includes the installed desktop-file fixture");
    // Opening the submenu refreshes Open With (onAboutToShow), which can
    // recreate the Instantiator's entries. Look the entry up only once the
    // submenu is open; an entry found before that may already be detached.
    evaluatePane("openWithMenu.open()"); pause(220);
    const char *appEntryLookup =
        "(function(){for(let i=0;i<openWithMenu.count;++i){const e=openWithMenu.itemAt(i);if(e && e.text !== undefined && e.text.indexOf('Lurviko Menu Fixture')===0 && e.visible) return e;}return null;})()";
    // Under load the first open() can be dropped while the parent menu is
    // still settling; reopen until the submenu is actually shown.
    require(until([&] {
                if (!evaluatePane("openWithMenu.visible").toBool())
                    evaluatePane("openWithMenu.open()");
                return evaluatePane(appEntryLookup).value<QObject *>() != nullptr;
            }), "Open With lists the installed desktop-file fixture once the submenu is open");
    auto appEntry = item(pane, paneContext, appEntryLookup);
    checkIcon(appEntry, evaluatePane("AppTheme.systemIcon('accessories-text-editor')").toString());
    evaluatePane("openWithMenu.close(); applicationServiceMenu.open()"); pause(220);
    auto servicesModel = qobject_cast<ServiceMenuModel *>(item(pane, paneContext, "serviceMenuModel"));
    require(servicesModel, "service actions model is available");
    QString favoriteKey;
    for (const auto &name : {"Lurviko Icon Action", "Lurviko Inherited Icon"}) {
        const QString lookup = QString("(function(){for(let i=0;i<applicationServiceMenu.count;++i){const e=applicationServiceMenu.itemAt(i);if(e.text.indexOf('%1')>=0) return e;}return null;})()").arg(name);
        auto entry = item(pane, paneContext, qPrintable(lookup));
        // KDE uses the service's first action icon for this submenu. Actions
        // without their own icon should inherit it rather than a generic file.
        checkIcon(entry, evaluatePane("AppTheme.systemIcon('accessories-text-editor')").toString());
        if (!QString::fromLatin1(name).contains("Inherited"))
            favoriteKey = entry->property("actionKey").toString();
    }
    require(!favoriteKey.isEmpty(), "service action keeps its trigger/favorite identity");
    servicesModel->toggleFavoriteKey(favoriteKey);
    require(servicesModel->favoriteCount() == 1, "service action is pinned in the isolated settings");
    evaluatePane("applicationServiceMenu.close(); itemContextMenu.open()"); pause(220);
    auto pinned = item(pane, paneContext,
        "(function(){for(let i=0;i<itemContextMenu.count;++i){const e=itemContextMenu.itemAt(i);if(e && e.text!==undefined && e.text.indexOf('Lurviko Icon Action')>=0) return e;}return null;})()");
    checkIcon(pinned, evaluatePane("AppTheme.systemIcon('accessories-text-editor')").toString());
    servicesModel->toggleFavoriteKey(favoriteKey);
    // A click outside the selected-file context menu may not activate a
    // hovered application-action submenu or leave one popup layer behind.
    evaluatePane("applicationServiceMenu.open()"); pause(220);
    require(item(pane, paneContext, "applicationServiceMenu")->property("visible").toBool(),
            "application actions submenu opens for outside-click regression");
    click(pane, 240, QPointF(pane->width() - 18, pane->height() - 18));
    require(!item(pane, paneContext, "applicationServiceMenu")->property("visible").toBool()
            && !item(pane, paneContext, "itemContextMenu")->property("visible").toBool(),
            "one outside click dismisses application actions and its parent");
    evaluatePane("itemContextMenu.open()"); pause(240);
    require(item(pane, paneContext, "itemContextMenu")->property("visible").toBool(),
            "selected-file menu reopens");
    click(pane, 240, QPointF(pane->width() - 18, pane->height() - 18));
    require(!item(pane, paneContext, "itemContextMenu")->property("visible").toBool()
            && !item(pane, paneContext, "applicationServiceMenu")->property("visible").toBool(),
            "one outside click closes selected-file menu without opening application actions");
    evaluatePane("itemContextMenu.open()"); pause(240);
    evaluatePane("openWithMenu.open()"); pause(220);
    require(item(pane, paneContext, "openWithMenu")->property("visible").toBool(),
            "Open With submenu opens for outside-click regression");
    click(pane, 240, QPointF(pane->width() - 18, pane->height() - 18));
    require(!item(pane, paneContext, "openWithMenu")->property("visible").toBool()
            && !item(pane, paneContext, "itemContextMenu")->property("visible").toBool(),
            "one outside click dismisses Open With and its parent");
    evaluatePane("closeContextMenus()"); pause(150);
    // A second right-click outside an existing file popup must reach the
    // new context target. Dismissing on right-button *release* used to close
    // the just-opened menu as well, intermittently requiring a second click.
    evaluatePane("itemContextMenu.x=100; itemContextMenu.y=100; itemContextMenu.open()");
    pause(240);
    const QPointF swapPoint = pane->mapToScene(QPointF(pane->width() / 2, pane->height() / 2));
    const QPointF swapGlobal = window->mapToGlobal(swapPoint.toPoint());
    QMouseEvent swapRightPress(QEvent::MouseButtonPress, swapPoint, swapGlobal,
                               Qt::RightButton, Qt::RightButton, Qt::NoModifier);
    QMouseEvent swapRightRelease(QEvent::MouseButtonRelease, swapPoint, swapGlobal,
                                 Qt::RightButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(window, &swapRightPress);
    pause(90);
    QApplication::sendEvent(window, &swapRightRelease);
    pause(240);
    require(backgroundMenu->property("visible").toBool(),
            "right click outside file menu opens the new directory menu on first try");
    evaluatePane("closeContextMenus()"); pause(150);
    std::fprintf(stderr, "PASS: built-in, Open With, service, inherited and pinned context-menu icons render through the real models\n");
    std::fprintf(stderr, "PASS: outside click dismisses nested directory, application actions and Open With menus in one press\n");
    std::fprintf(stderr, "PASS: switching text/svg context updates MIME-default app name and icon before new menu opens\n");
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
    QMouseEvent wheelMove(QEvent::MouseMove, wheelPoint, window->mapToGlobal(wheelPoint.toPoint()),
                          Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(window, &wheelMove); pause(30);
    const qreal expectedScroll = evaluatePane("Math.min(AppTheme.wheelScrollStep, itemContextMenu.contentItem.children[0].contentHeight-itemContextMenu.contentItem.children[0].height)").toReal();
    QWheelEvent wheelEvent(wheelPoint, window->mapToGlobal(wheelPoint.toPoint()), QPoint(), QPoint(0, -120),
                           Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(window, &wheelEvent); pause(220);
    const qreal actualScroll = evaluatePane("itemContextMenu.contentItem.children[0].contentY-itemContextMenu.contentItem.children[0].originY").toReal();
    if (qAbs(actualScroll - expectedScroll) > 2)
        std::fprintf(stderr, "Menu scroll: actual=%.1f expected=%.1f\n", actualScroll, expectedScroll);
    require(expectedScroll > 0 && qAbs(actualScroll - expectedScroll) <= 2,
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
    const int miniNativeBefore = sidebar->property("miniMusicNativeClicks").toInt();
    const int miniRecoveredBefore = sidebar->property("miniMusicRecoveredClicks").toInt();
    click(miniButton);
    if (view->property("visible").toBool() || !miniPopup->property("visible").toBool())
        std::fprintf(stderr, "Toolbar -> mini player: view=%d mini=%d nativeDelta=%d recoveredDelta=%d\n",
            view->property("visible").toBool(), miniPopup->property("visible").toBool(),
            sidebar->property("miniMusicNativeClicks").toInt() - miniNativeBefore,
            sidebar->property("miniMusicRecoveredClicks").toInt() - miniRecoveredBefore);
    require(!view->property("visible").toBool() && miniPopup->property("visible").toBool(), "toolbar to mini player switches in one click");
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
    require(music->property("lyricsNativeClicks").toInt() + music->property("lyricsRecoveredClicks").toInt() == 1,
            "lyrics opener toggles exactly once");
    click(lyricsButton); require(!lyricsPopup->property("visible").toBool(), "lyrics second click closes without reopening");
    for (int i = 0; i < 3; ++i) {
        click(lyricsButton, 20); require(lyricsPopup->property("visible").toBool(), "rapid lyrics open");
        click(lyricsButton, 20); require(!lyricsPopup->property("visible").toBool(), "rapid lyrics close");
    }
    click(lyricsButton); click(pane);
    require(!lyricsPopup->property("visible").toBool(), "outside click dismisses lyrics popup");
    std::fprintf(stderr,"PASS: real pointer clicks switch popups and toggle mini player\n");
}
