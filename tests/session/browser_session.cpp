#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QThread>
#include <cstdio>
#include <functional>
#include "backend_test_types.h"

static void require(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static bool until(const std::function<bool()> &check) {
    QElapsedTimer timer; timer.start();
    while (!check() && timer.elapsed() < 10000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return check();
}
static QVariant evaluate(QObject *target, const QString &code) {
    QQmlExpression expression(qmlContext(target), target, code);
    const auto value = expression.evaluate();
    require(!expression.hasError(), qPrintable(expression.error().toString()));
    return value;
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("Lurviko-QA"); app.setApplicationName("BrowserSession");
    app.setQuitOnLastWindowClosed(false);
    QQuickStyle::setStyle("Basic");
    // REGISTER_BACKEND_TYPES
    const QString mode = app.arguments().value(1);
    const QString base = QString::fromLocal8Bit(qgetenv("LURVIKO_SESSION_TEST_ROOT"));
    QSettings settings;
    for (const auto &key : {"images", "videos", "music", "documents", "archives", "appimage"}) {
        const auto folder = base + "/" + key;
        QDir().mkpath(folder);
        settings.setValue("contentIndex/mediaRoots/" + QString::fromLatin1(key), QStringList{folder});
    }
    QDir().mkpath(base + "/first"); QDir().mkpath(base + "/last"); QDir().mkpath(base + "/explicit");
    if (mode == "save") settings.setValue("TabBehavior/resumeSession", true);
    if (mode == "disabled") settings.setValue("TabBehavior/resumeSession", false);
    if (mode == "explicit" || mode == "activation") settings.setValue("TabBehavior/resumeSession", true);
    if (mode.startsWith("dont-ask") || mode == "escape" || mode == "shortcut") {
        settings.setValue("TabBehavior/warnOnMultipleTabs", true);
        settings.setValue("TabBehavior/resumeSession", false);
    }
    settings.sync();
    QQmlApplicationEngine engine;
    engine.addImageProvider("gfilethumb", new ThumbnailProvider);
    engine.addImageProvider("systemicon", new SystemIconProvider);
    engine.addImageProvider("bundledicon", new BundledIconProvider);
    engine.addImageProvider("folderart", new FolderIconProvider);
    if (mode == "explicit") engine.setInitialProperties({{"launchLocation", base + "/explicit"}});
    if (mode == "activation") engine.setInitialProperties({{"fileManagerActivation", true}});
    engine.loadFromModule("Lurviko.App", "Main");
    require(!engine.rootObjects().isEmpty(), "real Main loads for session test");
    auto *root = engine.rootObjects().first();
    auto *window = qobject_cast<QQuickWindow *>(root);
    auto *stack = root->findChild<QObject *>("pageStack");
    auto *preferences = engine.singletonInstance<QObject *>(qmlTypeId("Lurviko.App", 1, 0, "BrowserSession"));
    require(window && stack && preferences, "real session preferences and navigation available");
    auto *popup = root->findChild<QObject *>("closeTabsPopup");
    auto *remember = root->findChild<QObject *>("rememberTabCloseChoice");
    require(popup && remember, "shared close confirmation exists");
    const auto page = [&] { return stack->property("currentItem").value<QObject *>(); };
    if (mode == "defaults" || mode == "disabled" || mode == "activation") {
        require(!root->findChild<QQuickItem *>("browserChromeHost")->isVisible(),
                "default/disabled and explicit file-manager activation start without restored tabs");
        if (mode == "defaults") require(!preferences->property("resumeSession").toBool()
                && preferences->property("warnOnMultipleTabs").toBool(), "resume defaults off, close warning defaults on");
    } else if (mode == "restore") {
        require(until([&] { return page() && evaluate(page(), "tabs.length === 3 && currentTab === 1").toBool(); }),
                "new process restores all tabs in order and selects the saved one");
        const auto state = QJsonDocument::fromJson(evaluate(page(), "JSON.stringify(sessionSnapshot())").toString().toUtf8()).object();
        require(state.value("locations").toArray() == QJsonArray{base + "/first", "category:/images", base + "/last"},
                "folder and category locations survive the actual process restart");
        auto *pane = page()->property("activePane").value<QObject *>();
        require(until([&] { return pane->property("currentLocation").toString() == "category:/images"; }),
                "restored selected category is really displayed");
        require(!evaluate(page(), "canGoBack() || canGoForward()").toBool(), "expired history/search workers are not revived");
    } else if (mode == "explicit") {
        require(evaluate(page(), "tabs.length === 1").toBool(), "explicit launch does not append or replace saved tabs");
        require(evaluate(page(), "currentTabLocation()").toString() == base + "/explicit", "explicit folder wins over saved session");
        require(QMetaObject::invokeMethod(root, "close"), "single-tab window closes");
        require(!window->isVisible() && !popup->property("visible").toBool(), "one tab never prompts");
    } else {
        require(QMetaObject::invokeMethod(root, "browse", Q_ARG(QVariant, base + "/first")), "first folder opens");
        auto *browser = page();
        evaluate(browser, "addTab('category:/images', true)");
        require(until([&] { return !browser->property("switchingTabLocation").toBool()
                && evaluate(browser, "currentTabLocation() === 'category:/images'").toBool(); }), "second tab opens");
        evaluate(browser, "addTab('" + base + "/last', false)");
        require(evaluate(browser, "tabs.length === 3").toBool(), "three real tabs available");
        if (mode == "shortcut") {
            auto *shortcut = root->findChild<QObject *>("quitApplicationShortcut");
            require(shortcut && QMetaObject::invokeMethod(shortcut, "activated"), "application quit shortcut activated");
        } else {
            require(QMetaObject::invokeMethod(root, "close"), "window close requested");
        }
        require(until([&] { return popup->property("opened").toBool(); }) && window->isVisible(), "closing multiple tabs is deferred by our modal");
        require(!root->property("tabCloseApproved").toBool(), "opening the warning never grants close approval");
        if (mode == "escape") {
            require(QMetaObject::invokeMethod(popup, "close"), "Escape dismisses confirmation");
            require(until([&] { return !popup->property("visible").toBool(); }), "dismissal completes");
            require(window->isVisible() && preferences->property("warnOnMultipleTabs").toBool(), "dismissal keeps window and warning preference");
        } else {
            const bool optOut = mode.startsWith("dont-ask");
            remember->setProperty("checked", optOut);
            const bool accept = mode == "dont-ask-yes";
            require(QMetaObject::invokeMethod(root, "resolveTabClose", Q_ARG(QVariant, accept)), "confirmation decision dispatched");
            require(preferences->property("warnOnMultipleTabs").toBool() == !optOut, "don't ask again disables warning for either answer");
            require(QSettings().value("TabBehavior/warnOnMultipleTabs", true).toBool() == !optOut,
                    "warning opt-out is saved synchronously for either answer");
            require(window->isVisible() == !accept, "only Yes closes the window");
            if (!accept) {
                if (mode == "save") require(QSettings().value("TabBehavior/sessionJson").toString().isEmpty(),
                        "cancelled close does not replace the saved session");
                require(until([&] { return !popup->property("visible").toBool(); }), "No closes only the modal");
                if (!optOut) {
                    require(QMetaObject::invokeMethod(root, "close"), "window close requested again");
                    require(until([&] { return popup->property("opened").toBool(); }), "warning remains available after No");
                    require(QMetaObject::invokeMethod(root, "resolveTabClose", Q_ARG(QVariant, true)), "Yes confirms close");
                } else {
                    require(QMetaObject::invokeMethod(root, "close"), "close after opting out");
                }
                require(!window->isVisible(), "confirmed or opted-out close really closes");
            }
        }
        if (mode == "save") {
            settings.sync();
            const auto state = QJsonDocument::fromJson(settings.value("TabBehavior/sessionJson").toString().toUtf8()).object();
            require(state.value("locations").toArray().size() == 3 && state.value("activeIndex").toInt() == 1,
                    "accepted close flushes the complete session synchronously to disk");
        }
    }
    std::fprintf(stderr, "PASS: browser session lifecycle (%s)\n", qPrintable(mode));
}
