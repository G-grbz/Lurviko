#include <QApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCall>
#include <QDBusPendingReply>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QPointer>
#include <QProcess>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>
#include <cstdio>
#include <functional>
#include "backend_test_types.h"

static void require(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static void pause(int ms) {
    QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec();
}
static bool until(const std::function<bool()> &check, int timeout = 10000) {
    QElapsedTimer timer; timer.start();
    while (!check() && timer.elapsed() < timeout) pause(10);
    return check();
}
static void touch(const QString &path, const QByteArray &bytes = "x") {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path); require(file.open(QIODevice::WriteOnly), "fixture opens");
    file.write(bytes);
}
static QByteArray pathBytes(const QString &path) { return QFile::encodeName(path) + '\0'; }

// A separate connection, like xdg-desktop-portal's: calls on the portal's
// own connection would be delivered locally, bypassing the bus.
static QDBusConnection client() {
    static QDBusConnection connection = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "portal-client");
    return connection;
}
static int serial = 0;
struct Call {
    QDBusPendingCall pending;
    QString handle;
};
static Call call(const QString &method, const QVariantMap &options, const QString &title = QString()) {
    const QString handle = QStringLiteral("/org/freedesktop/portal/desktop/request/1_1/t%1").arg(++serial);
    QDBusMessage message = QDBusMessage::createMethodCall(
        FileChooserPortal::serviceName, FileChooserPortal::objectPath,
        QStringLiteral("org.freedesktop.impl.portal.FileChooser"), method);
    message << QVariant::fromValue(QDBusObjectPath(handle)) << QStringLiteral("org.example.Test")
            << QString() << title << options;
    return {client().asyncCall(message, 60000), handle};
}
static QDBusMessage reply(Call &call) {
    require(until([&] { return call.pending.isFinished(); }), "portal replies");
    const QDBusMessage message = call.pending.reply();
    if (message.type() == QDBusMessage::ErrorMessage)
        std::fprintf(stderr, "D-Bus error: %s\n", qPrintable(message.errorMessage()));
    require(message.type() == QDBusMessage::ReplyMessage && message.arguments().size() == 2,
            "reply carries (u response, a{sv} results)");
    require(message.signature() == QLatin1String("ua{sv}"), "reply signature is ua{sv}");
    return message;
}
static uint responseOf(const QDBusMessage &message) { return message.arguments().at(0).toUInt(); }
static QVariantMap resultsOf(const QDBusMessage &message) {
    return qdbus_cast<QVariantMap>(message.arguments().at(1));
}
static QStringList urisOf(const QDBusMessage &message) {
    return qdbus_cast<QStringList>(resultsOf(message).value("uris"));
}

static QSet<QQuickWindow *> seen;
static QQuickWindow *nextWindow() {
    QQuickWindow *found = nullptr;
    until([&] {
        for (QWindow *window : QGuiApplication::topLevelWindows()) {
            auto *quick = qobject_cast<QQuickWindow *>(window);
            if (quick && quick->objectName() == "fileChooserWindow" && quick->isVisible() && !seen.contains(quick)) {
                found = quick;
                return true;
            }
        }
        return false;
    });
    require(found, "picker window opens");
    seen.insert(found);
    return found;
}
static QObject *child(QObject *root, const char *name) {
    QObject *object = root->findChild<QObject *>(name);
    require(object, name);
    return object;
}
static DirectoryModel *directoryOf(QQuickWindow *window) {
    auto *model = qobject_cast<DirectoryModel *>(child(window, "fileChooserPane")->property("directory").value<QObject *>());
    require(model, "picker pane exposes its directory model");
    return model;
}
static QStringList names(DirectoryModel *model) {
    QStringList result;
    for (int row = 0; row < model->rowCount(); ++row)
        result << model->data(model->index(row, 0), DirectoryModel::NameRole).toString();
    result.sort();
    return result;
}
static bool listed(DirectoryModel *model, QStringList expected) {
    expected.sort();
    return !model->loading() && names(model) == expected;
}
static void invoke(QObject *object, const char *method, const QVariantList &args = {}) {
    bool ok = false;
    switch (args.size()) {
    case 0: ok = QMetaObject::invokeMethod(object, method); break;
    case 1: ok = QMetaObject::invokeMethod(object, method, Q_ARG(QVariant, args[0])); break;
    case 2: ok = QMetaObject::invokeMethod(object, method, Q_ARG(QVariant, args[0]), Q_ARG(QVariant, args[1])); break;
    case 4: ok = QMetaObject::invokeMethod(object, method, Q_ARG(QVariant, args[0]), Q_ARG(QVariant, args[1]),
                                           Q_ARG(QVariant, args[2]), Q_ARG(QVariant, args[3])); break;
    }
    require(ok, method);
}
static void select(QQuickWindow *window, const QString &path, bool add = false) {
    QObject *pane = child(window, "fileChooserPane");
    DirectoryModel *model = directoryOf(window);
    const int row = model->indexOfUrl(QUrl::fromLocalFile(path).toString());
    require(row >= 0, "item to select is listed");
    invoke(pane, "selectIndex", {row, int(add ? Qt::ControlModifier : Qt::NoModifier)});
}
static QString url(const QString &path) { return QUrl::fromLocalFile(path).toString(); }

int main(int argc, char **argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QApplication app(argc, argv);
    app.setOrganizationName("Lurviko-QA"); app.setApplicationName("Portal");
    qDBusRegisterMetaType<QList<QByteArray>>();  // SaveFiles "files" (aay)
    QQuickStyle::setStyle("Basic");
    // REGISTER_BACKEND_TYPES
    const QString root = QString::fromLocal8Bit(qgetenv("LURVIKO_PORTAL_TEST_ROOT"));
    const QString files = root + "/files";
    touch(files + "/a.png"); touch(files + "/c.PNG"); touch(files + "/b.txt", "plain text\n");
    touch(files + "/existing.txt"); touch(files + "/sub/inner.txt");

    QQmlEngine engine;
    installImageProviders(engine);
    FileChooserPortal portal(&engine);
    require(portal.registerOnSessionBus(), "portal claims org.freedesktop.impl.portal.desktop.lurviko");

    // OpenFile: filters (glob and MIME), current filter, choices, multiple.
    const QList<PortalFilter> filters = {
        {"Images", {{0, "*.png"}}},
        {"Text", {{1, "text/plain"}}},
    };
    const QList<PortalChoice> choices = {
        {"encoding", "Encoding", {{"utf8", "UTF-8"}, {"latin1", "Latin-1"}}, "latin1"},
        {"readonly", "Read only", {}, "true"},
    };
    Call open = call("OpenFile", {
        {"multiple", true},
        {"filters", QVariant::fromValue(filters)},
        {"current_filter", QVariant::fromValue(filters.at(1))},
        {"choices", QVariant::fromValue(choices)},
        {"current_folder", pathBytes(files)},
        {"accept_label", "_Attach"},
    }, "Pick files");
    QQuickWindow *window = nextWindow();
    require(window->title() == "Pick files", "caller's title is used");
    require(child(window, "fileChooserAccept")->property("text").toString() == "Attach",
            "accept label drops the mnemonic underscore");
    DirectoryModel *model = directoryOf(window);
    require(until([&] { return listed(model, {"b.txt", "existing.txt", "sub"}); }),
            "the current MIME filter lists text files and folders only");
    if (qEnvironmentVariableIsSet("LURVIKO_PORTAL_SHOT"))
        window->grabWindow().save(qEnvironmentVariable("LURVIKO_PORTAL_SHOT"));
    invoke(window, "applyFilter", {0});
    require(until([&] { return listed(model, {"a.png", "c.PNG", "sub"}); }),
            "glob filters match case-insensitively");
    select(window, files + "/a.png");
    select(window, files + "/c.PNG", true);
    invoke(window, "acceptCurrent");
    QDBusMessage result = reply(open);
    require(responseOf(result) == 0, "accepted OpenFile succeeds");
    require(urisOf(result) == QStringList({url(files + "/a.png"), url(files + "/c.PNG")}),
            "every selected file is returned");
    const auto picked = qdbus_cast<QList<PortalChoicePair>>(resultsOf(result).value("choices"));
    require(picked.size() == 2 && picked[0].id == "encoding" && picked[0].value == "latin1"
            && picked[1].id == "readonly" && picked[1].value == "true", "choices are returned with their values");
    require(qdbus_cast<PortalFilter>(resultsOf(result).value("current_filter")).label == "Images",
            "the active filter is returned");
    std::puts("PASS: OpenFile filters, choices, multiple selection and reply types");

    // Double-clicking (activating) a file in open mode chooses it.
    open = call("OpenFile", {{"current_folder", pathBytes(files)}});
    window = nextWindow();
    model = directoryOf(window);
    require(until([&] { return listed(model, {"a.png", "b.txt", "c.PNG", "existing.txt", "sub"}); }),
            "no filter lists everything");
    invoke(child(window, "fileChooserPane"), "openItem", {url(files + "/sub"), true, "sub", "inode/directory"});
    require(until([&] { return listed(model, {"inner.txt"}); }), "activating a folder enters it");
    invoke(child(window, "fileChooserPane"), "openItem", {url(files + "/sub/inner.txt"), false, "inner.txt", "text/plain"});
    result = reply(open);
    require(responseOf(result) == 0 && urisOf(result) == QStringList({url(files + "/sub/inner.txt")}),
            "activating a file chooses it instead of opening it");
    std::puts("PASS: folder activation navigates, file activation chooses");

    // The command bar's eye button shows hidden files (shared preference).
    touch(files + "/.secret");
    open = call("OpenFile", {{"current_folder", pathBytes(files)}});
    window = nextWindow();
    model = directoryOf(window);
    QObject *hidden = child(window, "fileChooserHiddenFiles");
    require(until([&] { return listed(model, {"a.png", "b.txt", "c.PNG", "existing.txt", "sub"}); }),
            "hidden files start hidden");
    QMetaObject::invokeMethod(hidden, "toggle");
    QMetaObject::invokeMethod(hidden, "toggled");
    require(until([&] { return listed(model, {".secret", "a.png", "b.txt", "c.PNG", "existing.txt", "sub"}); }),
            "the eye button shows hidden files");
    QMetaObject::invokeMethod(hidden, "toggle");
    QMetaObject::invokeMethod(hidden, "toggled");
    require(until([&] { return listed(model, {"a.png", "b.txt", "c.PNG", "existing.txt", "sub"}); }),
            "and hides them again");
    QObject *pickerPane = child(window, "fileChooserPane");
    QMetaObject::invokeMethod(child(window, "fileChooserView_list"), "clicked");
    require(until([&] { return pickerPane->property("viewMode").toString() == "list"; }), "list view button");
    QMetaObject::invokeMethod(child(window, "fileChooserView_grid"), "clicked");
    require(until([&] { return pickerPane->property("viewMode").toString() == "grid"; }), "grid view button");
    window->close();
    reply(open);
    QFile::remove(files + "/.secret");
    std::puts("PASS: picker command bar: hidden files and grid/list view");

    // Folder selection.
    open = call("OpenFile", {{"directory", true}, {"current_folder", pathBytes(files)}});
    window = nextWindow();
    model = directoryOf(window);
    require(until([&] { return listed(model, {"sub"}); }), "folder pickers list folders only");
    invoke(window, "acceptCurrent");
    result = reply(open);
    require(responseOf(result) == 0 && urisOf(result) == QStringList({url(files)}),
            "with nothing selected the current folder is chosen");
    std::puts("PASS: folder selection");

    // SaveFile: an existing name asks before replacing it.
    Call save = call("SaveFile", {{"current_name", "existing.txt"}, {"current_folder", pathBytes(files)}});
    window = nextWindow();
    QObject *name = child(window, "fileChooserName");
    require(name->property("text").toString() == "existing.txt", "suggested name is prefilled");
    require(until([&] { return !directoryOf(window)->loading(); }), "save folder lists");
    invoke(window, "acceptCurrent");
    QObject *overwrite = child(window, "fileChooserOverwrite");
    require(until([&] { return overwrite->property("opened").toBool(); }), "replacing a file asks first");
    require(!save.pending.isFinished(), "nothing is returned before the confirmation");
    QMetaObject::invokeMethod(child(window, "fileChooserOverwriteYes"), "clicked");
    result = reply(save);
    require(responseOf(result) == 0 && urisOf(result) == QStringList({url(files + "/existing.txt")}),
            "confirmed replacement returns the existing file");

    save = call("SaveFile", {{"current_name", "draft.txt"}, {"current_folder", pathBytes(files)}});
    window = nextWindow();
    child(window, "fileChooserName")->setProperty("text", "sub");
    invoke(window, "acceptCurrent");
    require(until([&] { return listed(directoryOf(window), {"inner.txt"}); }), "typing a folder name enters it");
    require(child(window, "fileChooserName")->property("text").toString() == "draft.txt",
            "the suggested name returns after entering a folder");
    child(window, "fileChooserName")->setProperty("text", "new.txt");
    invoke(window, "acceptCurrent");
    result = reply(save);
    require(responseOf(result) == 0 && urisOf(result) == QStringList({url(files + "/sub/new.txt")}),
            "a new name is returned in the current folder without a question");
    std::puts("PASS: SaveFile name, folder entry and overwrite confirmation");

    // Changing the type of a file being saved changes its extension.
    const QList<PortalFilter> imageFilters = {{"PNG", {{0, "*.png"}}}, {"JPEG", {{0, "*.jpg"}, {0, "*.jpeg"}}}};
    save = call("SaveFile", {{"current_name", "photo.png"}, {"current_folder", pathBytes(files)},
                             {"filters", QVariant::fromValue(imageFilters)}});
    window = nextWindow();
    invoke(window, "selectFilter", {1});
    require(child(window, "fileChooserName")->property("text").toString() == "photo.jpg",
            "choosing JPEG renames photo.png to photo.jpg");
    child(window, "fileChooserName")->setProperty("text", "photo.jpeg");
    invoke(window, "selectFilter", {1});
    require(child(window, "fileChooserName")->property("text").toString() == "photo.jpeg",
            "a name already matching the type is kept");
    invoke(window, "acceptCurrent");
    result = reply(save);
    require(urisOf(result) == QStringList({url(files + "/photo.jpeg")})
            && qdbus_cast<PortalFilter>(resultsOf(result).value("current_filter")).label == "JPEG",
            "the chosen type is returned with the name");
    std::puts("PASS: SaveFile name follows the chosen file type");

    // SaveFiles: one folder for several names.
    QList<QByteArray> saveNames = {pathBytes("report.pdf"), pathBytes("b.txt")};
    save = call("SaveFiles", {{"files", QVariant::fromValue(saveNames)}, {"current_folder", pathBytes(files)}});
    window = nextWindow();
    require(until([&] { return !directoryOf(window)->loading(); }), "save-files folder lists");
    invoke(window, "acceptCurrent");
    overwrite = child(window, "fileChooserOverwrite");
    require(until([&] { return overwrite->property("opened").toBool(); }), "an existing name asks first");
    QMetaObject::invokeMethod(child(window, "fileChooserOverwriteYes"), "clicked");
    result = reply(save);
    require(responseOf(result) == 0
            && urisOf(result) == QStringList({url(files + "/report.pdf"), url(files + "/b.txt")}),
            "SaveFiles returns each name inside the chosen folder");
    std::puts("PASS: SaveFiles");

    // Cancellation by the user and Request.Close by the caller.
    open = call("OpenFile", {{"current_folder", pathBytes(files)}});
    window = nextWindow();
    QPointer<QQuickWindow> closing = window;
    window->close();
    result = reply(open);
    require(responseOf(result) == 1, "closing the picker cancels (response 1)");
    require(until([&] { return closing.isNull(); }), "the cancelled window is destroyed");

    open = call("OpenFile", {{"current_folder", pathBytes(files)}});
    window = nextWindow();
    closing = window;
    const QDBusMessage close = QDBusMessage::createMethodCall(
        FileChooserPortal::serviceName, open.handle, "org.freedesktop.impl.portal.Request", "Close");
    client().asyncCall(close);
    result = reply(open);
    require(responseOf(result) == 2, "Request.Close ends the request (response 2)");
    require(until([&] { return closing.isNull(); }), "the closed window is destroyed");
    std::puts("PASS: user cancellation and Request.Close");

    // The picker reopens where the user last chose a file when the caller
    // names no folder, or only the home folder.
    open = call("OpenFile", {{"current_folder", pathBytes(files + "/sub")}});
    window = nextWindow();
    require(until([&] { return listed(directoryOf(window), {"inner.txt"}); }), "the caller's folder is used");
    select(window, files + "/sub/inner.txt");
    invoke(window, "acceptCurrent");
    require(responseOf(reply(open)) == 0, "file chosen in sub");
    for (const QVariantMap &options : {QVariantMap{}, QVariantMap{{"current_folder", pathBytes(QDir::homePath())}}}) {
        open = call("OpenFile", options);
        window = nextWindow();
        require(until([&] { return listed(directoryOf(window), {"inner.txt"}); }),
                "without a specific folder the picker starts in the last used folder");
        window->close();
        require(responseOf(reply(open)) == 1, "cancelled");
    }
    save = call("SaveFile", {{"current_name", "later.txt"}});
    window = nextWindow();
    require(until([&] { return listed(directoryOf(window), {"inner.txt"}); }), "save dialogs start there too");
    window->close();
    reply(save);
    open = call("OpenFile", {{"current_folder", pathBytes(files)}});
    window = nextWindow();
    require(until([&] { return !directoryOf(window)->loading() && names(directoryOf(window)).contains("a.png"); }),
            "a specific folder from the caller still wins");
    window->close();
    reply(open);
    const QString gone = root + "/gone";
    QDir().mkpath(gone);
    FileChooserRequest::rememberFolder("org.example.Gone", url(gone));
    require(FileChooserRequest::rememberedFolder("org.example.Gone") == url(gone), "folder remembered per app");
    require(FileChooserRequest::rememberedFolder("org.example.Other") == url(gone), "other apps fall back to the last folder");
    QDir(gone).removeRecursively();
    require(FileChooserRequest::rememberedFolder("org.example.Gone").isEmpty(), "a removed folder is forgotten");
    std::puts("PASS: last used folder is remembered");

    // Settings: portals.conf editing.
    const QString system = "[preferred]\ndefault=kde\norg.freedesktop.impl.portal.Settings=kde;gtk;\n";
    bool removeFile = false;
    const QString created = FileDialogIntegration::createdHeader() + "\n" + system;
    const QString enabled = FileDialogIntegration::updatedConfig(created, true, &removeFile);
    require(!removeFile && enabled.contains("org.freedesktop.impl.portal.FileChooser=lurviko;kde\n")
            && enabled.contains("org.freedesktop.impl.portal.Settings=kde;gtk;\n")
            && enabled.startsWith(FileDialogIntegration::createdHeader()),
            "enabling copies the system preferences and falls back to the previous default");
    require(FileDialogIntegration::updatedConfig(enabled, true) == enabled, "enabling twice changes nothing");
    FileDialogIntegration::updatedConfig(enabled, false, &removeFile);
    require(removeFile, "disabling deletes a file Lurviko created");
    const QString owned = "[preferred]\ndefault=kde\norg.freedesktop.impl.portal.FileChooser=gtk\n\n[other]\nx=1\n";
    const QString ownedOn = FileDialogIntegration::updatedConfig(owned, true, &removeFile);
    require(!removeFile && ownedOn.contains("org.freedesktop.impl.portal.FileChooser=lurviko;gtk\n")
            && ownedOn.contains("[other]\nx=1"), "a user's own FileChooser choice becomes the fallback");
    const QString ownedOff = FileDialogIntegration::updatedConfig(ownedOn, false, &removeFile);
    require(!removeFile && ownedOff == owned, "disabling restores a user's own file exactly");
    const QString bare = FileDialogIntegration::updatedConfig("[other]\nx=1\n", true);
    require(bare == "[other]\nx=1\n\n[preferred]\norg.freedesktop.impl.portal.FileChooser=lurviko\n",
            "a missing [preferred] section is added");
    std::puts("PASS: portals.conf enable/disable editing");

    // The Plasma env script for KDE/Qt and GTK apps.
    const QString script = root + "/env.sh";
    QFile scriptFile(script);
    require(scriptFile.open(QIODevice::WriteOnly) && scriptFile.write(FileDialogIntegration::environmentScript().toUtf8()) > 0,
            "env script fixture");
    scriptFile.close();
    const auto sourced = [&](const QString &gdkDebug) {
        QProcess shell;
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.remove("GDK_DEBUG");
        if (!gdkDebug.isNull()) environment.insert("GDK_DEBUG", gdkDebug);
        shell.setProcessEnvironment(environment);
        shell.start("sh", {"-ec", ". \"$1\"; printf '%s|%s|%s' \"$PLASMA_INTEGRATION_USE_PORTAL\" \"$GTK_USE_PORTAL\" \"$GDK_DEBUG\"", "sh", script});
        shell.waitForFinished(5000);
        return QString::fromUtf8(shell.readAllStandardOutput());
    };
    require(sourced(QString()) == "1|1|portals", "env script routes Qt and GTK apps to the portal");
    require(sourced("interactive") == "1|1|interactive,portals", "an existing GDK_DEBUG is kept");
    require(sourced("portals,interactive") == "1|1|portals,interactive", "portals is not added twice");
    std::puts("PASS: Plasma env script for KDE/Qt and GTK apps");
    return 0;
}
