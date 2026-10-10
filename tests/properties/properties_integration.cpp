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
#include <QWheelEvent>
#include <cstdio>
#include <functional>
#include <grp.h>
#include <sys/stat.h>
#include <unistd.h>
#include "backend_test_types.h"

static void pause(int ms) { QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec(); }
static void require(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static bool until(std::function<bool()> check) {
    QElapsedTimer timer; timer.start();
    while (!check() && timer.elapsed() < 10000) pause(10);
    return check();
}
static QQuickItem *findItem(QQuickItem *root, const QString &name) {
    if (root->objectName() == name) return root;
    for (auto child : root->childItems()) if (auto found = findItem(child, name)) return found;
    return nullptr;
}
static int modeOf(const QString &path) {
    struct stat st {};
    require(::stat(QFile::encodeName(path).constData(), &st) == 0, "fixture stat succeeds");
    return st.st_mode & 07777;
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("Lurviko-QA"); app.setApplicationName("Properties");
    QQuickStyle::setStyle("Basic");
    // REGISTER_BACKEND_TYPES
    const QString base = QString::fromLocal8Bit(qgetenv("LURVIKO_PROPERTIES_TEST_ROOT")) + "/fixture";
    require(QDir().mkpath(base), "fixture directory created");
    const QString longName = "LICENSE-FontAwesome-with-a-very-long-file-name.txt";
    const QString path = base + "/" + longName;
    { QFile file(path); require(file.open(QIODevice::WriteOnly), "fixture opens"); file.write("Permission and filename layout test\n"); }
    require(::chmod(QFile::encodeName(path).constData(), 0640) == 0, "initial mode set");
    FilePropertiesManager manager;
    manager.inspect(QUrl::fromLocalFile(path).toString());
    require(manager.permissionMode() == 0640 && manager.permissionsText() == "rw-r-----", "actual owner/group/other permissions read");
    require(manager.permissionsEditable() && !manager.ownerName().isEmpty() && !manager.groupName().isEmpty(), "editable ownership metadata available");
    require(!manager.setPermissionMode(-1) && !manager.setPermissionMode(01000), "invalid modes rejected");
    require(manager.setPermissionMode(0750) && modeOf(path) == 0750, "execute bit and individual permissions saved");
    require(manager.setPermissionMode(0000) && modeOf(path) == 0000, "owner can remove all access");
    require(manager.setPermissionMode(0640) && modeOf(path) == 0640, "owner can restore permissions without read access");
    const QString folderName = "Folder-with-a-very-long-name-that-needs-padding";
    const QString folder = base + "/" + folderName;
    QDir().mkpath(folder);
    require(::chmod(QFile::encodeName(folder).constData(), 01750) == 0, "sticky folder fixture set");
    manager.inspect(folder);
    require(manager.setPermissionMode(0700) && modeOf(folder) == 01700, "directory traversal changes preserve sticky bit");
    const QString hardlink = base + "/hardlink.txt";
    require(::link(QFile::encodeName(path).constData(), QFile::encodeName(hardlink).constData()) == 0, "hardlink fixture created");
    manager.inspect(hardlink);
    require(manager.linkType() == "hardlink" && manager.setPermissionMode(0600) && modeOf(path) == 0600, "hardlinks share inode permissions");
    QFile::remove(hardlink);
    const QString link = base + "/link.txt";
    require(::symlink(QFile::encodeName(path).constData(), QFile::encodeName(link).constData()) == 0, "symlink fixture created");
    manager.inspect(link);
    require(!manager.permissionsEditable() && !manager.setPermissionMode(0777) && modeOf(path) == 0600, "symlink target permissions never changed");
    QFile::remove(link);
    const QString replaced = base + "/replace.txt";
    { QFile file(replaced); require(file.open(QIODevice::WriteOnly), "replacement fixture opens"); }
    manager.inspect(replaced);
    QFile::remove(replaced);
    require(::symlink(QFile::encodeName(path).constData(), QFile::encodeName(replaced).constData()) == 0, "replacement symlink created");
    require(!manager.setPermissionMode(0777) && modeOf(path) == 0600, "symlink replacement after inspect rejected");
    QFile::remove(replaced);
    manager.inspect("smb://example.invalid/share/file");
    require(!manager.permissionsEditable() && !manager.setPermissionMode(0777), "remote permissions do not use local chmod");
    std::fprintf(stderr, "PASS: local permissions, execute, ownership, directory access, special bits, no-read recovery and link boundaries\n");

    const QString nested = folder + "/nested";
    QDir().mkpath(nested);
    const QStringList enclosed = {folder + "/plain.txt", folder + "/program.sh", folder + "/.hidden.txt", nested + "/deep.txt"};
    for (const auto &item : enclosed) {
        QFile file(item); require(file.open(QIODevice::WriteOnly), "recursive fixture opens"); file.write("fixture"); file.close();
        ::chmod(QFile::encodeName(item).constData(), item.endsWith("program.sh") ? 0700 : 0600);
    }
    const QString externalLink = folder + "/outside";
    require(::symlink(QFile::encodeName(path).constData(), QFile::encodeName(externalLink).constData()) == 0, "recursive external symlink created");
    manager.inspect(folder);
    require(manager.availableOwners().contains(manager.ownerName()) && manager.availableGroups().contains(manager.groupName()), "current ownership is in available choices");
    require(manager.availableOwners(true).contains("root") && manager.availableGroups(true).contains("root"), "administrator choices enumerate system users and groups");
    require(manager.applyAccess(0750, manager.ownerName(), manager.groupName(), true), "recursive operation starts asynchronously");
    require(manager.accessBusy(), "recursive operation reports busy before event-loop completion");
    require(until([&] { return !manager.accessBusy(); }), "recursive operation finishes");
    require(manager.accessFailed() == 0 && manager.accessCompleted() == 6, "all regular and hidden descendants processed once");
    require(modeOf(folder) == 01750 && modeOf(nested) == 0750, "recursive directory access and sticky bit preserved");
    require(modeOf(enclosed[0]) == 0640 && modeOf(enclosed[1]) == 0750
            && modeOf(enclosed[2]) == 0640 && modeOf(enclosed[3]) == 0640, "recursive +X keeps documents non-executable");
    require(modeOf(path) == 0600, "recursive changes never leave through a symlink");
    if (::geteuid() != 0) {
        require(!manager.applyAccess(0750, "root", manager.groupName(), false, false), "ownership escalation requires explicit administrator authentication");
    }
    manager.inspect(path);
    const QString originalGroup = manager.groupName();
    for (const auto &name : manager.availableGroups()) {
        if (name == originalGroup) continue;
        const auto group = ::getgrnam(name.toLocal8Bit().constData());
        if (!group) continue;
        const gid_t expected = group->gr_gid;
        require(manager.applyAccess(0600, manager.ownerName(), name), "ordinary-user group change starts");
        require(until([&] { return !manager.accessBusy(); }), "member-group change finishes");
        if (manager.accessFailed()) std::fprintf(stderr, "Group change error: %s\n", qPrintable(manager.accessError()));
        require(manager.accessFailed() == 0, "member-group change succeeds without administrator authentication");
        struct stat result {};
        require(::stat(QFile::encodeName(path).constData(), &result) == 0 && result.st_gid == expected, "selected group is actually saved on disk");
        require(manager.applyAccess(0600, manager.ownerName(), originalGroup)
                && until([&] { return !manager.accessBusy(); }) && manager.accessFailed() == 0, "original group can be restored");
        break;
    }
    if (::geteuid() != 0 && !manager.availableGroups().contains("root")) {
        require(manager.applyAccess(0600, manager.ownerName(), "root"), "unauthorized group change runs with ordinary OS authority");
        require(until([&] { return !manager.accessBusy(); }) && manager.accessFailed() == 1
                && !manager.accessError().isEmpty() && manager.groupName() == originalGroup,
                "OS refuses a non-member group and the failure is reported without changing ownership");
    }
    const QString cancelFolder = QString::fromLocal8Bit(qgetenv("LURVIKO_PROPERTIES_TEST_ROOT")) + "/cancel";
    QDir().mkpath(cancelFolder);
    for (int i = 0; i < 1000; ++i) { QFile file(cancelFolder + "/" + QString::number(i)); require(file.open(QIODevice::WriteOnly), "cancel fixture opens"); }
    manager.inspect(cancelFolder);
    bool cancelled = false;
    QObject::connect(&manager, &FilePropertiesManager::accessFinished, &app, [&](bool, bool stopped, int, int) { cancelled = stopped; });
    require(manager.applyAccess(0700, manager.ownerName(), manager.groupName(), true), "cancellable recursive operation starts");
    manager.cancelAccess();
    require(until([&] { return !manager.accessBusy(); }) && cancelled, "cancellation settles without blocking the UI");
    std::fprintf(stderr, "PASS: recursive +X, hidden descendants, ownership lists, ordinary-user authority, symlink boundaries and cancellation\n");

    const QString accessFolder = QString::fromLocal8Bit(qgetenv("LURVIKO_PROPERTIES_TEST_ROOT")) + "/folder-access";
    require(QDir().mkpath(accessFolder), "folder access fixture created");
    const QString svgPath = accessFolder + "/icon.svg";
    const QByteArray svgBytes = "<svg xmlns=\"http://www.w3.org/2000/svg\"><path d=\"M0 0h1v1z\"/></svg>";
    { QFile svg(svgPath); require(svg.open(QIODevice::WriteOnly) && svg.write(svgBytes) == svgBytes.size(), "SVG access fixture written"); }
    manager.inspect(accessFolder);
    require(manager.applyAccess(0644, manager.ownerName(), manager.groupName())
            && until([&] { return !manager.accessBusy(); }) && manager.accessFailed() == 0,
            "folder permission code 0644 applies without writing file contents");
    DirectoryModel accessDirectory;
    accessDirectory.setLocation(QUrl::fromLocalFile(accessFolder).toString());
    require(until([&] { return !accessDirectory.loading(); }), "inaccessible directory load finishes");
    if (::geteuid() != 0)
        require(!accessDirectory.errorString().isEmpty() && accessDirectory.rowCount() == 0,
                "missing directory search permission reports access error instead of fabricated zero-byte files");
    require(manager.applyAccess(0744, manager.ownerName(), manager.groupName())
            && until([&] { return !manager.accessBusy(); }) && manager.accessFailed() == 0,
            "folder traversal can be restored through the permission editor");
    accessDirectory.refresh();
    require(until([&] { return !accessDirectory.loading() && accessDirectory.rowCount() == 1; }), "restored folder displays its file again");
    require(accessDirectory.errorString().isEmpty()
            && accessDirectory.data(accessDirectory.index(0, 0), DirectoryModel::SizeRole).toLongLong() == svgBytes.size(),
            "restored folder shows actual SVG size");
    { QFile svg(svgPath); require(svg.open(QIODevice::ReadOnly) && svg.readAll() == svgBytes, "SVG bytes unchanged after removing and restoring folder access"); }
    std::fprintf(stderr, "PASS: folder 0644 access error, editor recovery and unchanged SVG contents\n");

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
    width: 1000; height: 820; visible: true
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
    require(!engine.rootObjects().isEmpty(), "properties QML fixture loads");
    auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    window->requestActivate(); pause(250);
    auto browser = findItem(window->contentItem(), "browser");
    auto pane = qobject_cast<QQuickItem *>(browser->property("activePane").value<QObject *>());
    DirectoryModel *directory = nullptr;
    for (auto candidate : browser->findChildren<DirectoryModel *>())
        if (candidate->location() == QUrl::fromLocalFile(base).toString()) directory = candidate;
    require(directory && until([&] { return !directory->loading() && directory->rowCount() == 2; }), "file and folder list ready");
    pane->setProperty("gridMode", true);
    pane->setProperty("folderPreviewsEnabled", false);
    auto context = qmlContext(directory);
    const auto eval = [&](const QString &expression) {
        QQmlExpression code(context, pane, expression); auto value = code.evaluate();
        if (code.hasError()) std::fprintf(stderr, "%s\n", qPrintable(code.error().toString()));
        require(!code.hasError(), "QML expression executes"); return value;
    };
    const auto select = [&](const QString &name) {
        const int index = directory->indexOfUrl(QUrl::fromLocalFile(base + "/" + name).toString());
        require(index >= 0 && QMetaObject::invokeMethod(pane, "selectIndex", Q_ARG(QVariant, QVariant(index)), Q_ARG(QVariant, QVariant(0))), "fixture selected");
        pause(120); window->grabWindow(); pause(40);
        return index;
    };
    for (int size : {104, 168, 360}) {
        pane->setProperty("iconSize", size);
        for (const auto &name : {longName, folderName}) {
            const int index = select(name);
            require(eval(QString(R"js((function() {
                grid.forceLayout(); const item = grid.itemAtIndex(%1);
                if (!item) return false;
                function find(root, name) { if (root.objectName === name) return root;
                    for (const child of root.children) { const found = find(child, name); if (found) return found; }
                    return null; }
                const label = find(item, "gridFileName"), frame = find(item, "gridItemHighlight");
                if (!label || !frame) return false;
                const point = label.mapToItem(frame, 0, 0);
                return frame.visible && point.x >= 9 && point.x + label.width <= frame.width - 9;
            })())js").arg(index)).toBool(), "long file/folder labels remain inside selected border with padding");
        }
    }
    std::fprintf(stderr, "PASS: file and folder label padding at minimum/default/maximum icon sizes\n");
    pane->setProperty("iconSize", 168);
    select(longName);
    require(QMetaObject::invokeMethod(pane, "openPropertiesForSelection"), "properties action opens");
    pause(200);
    auto popup = eval("propertiesDialog").value<QObject *>();
    require(popup && popup->property("visible").toBool(), "properties popup visible");
    auto scroll = findItem(window->contentItem(), "filePropertiesScroll");
    auto execution = findItem(window->contentItem(), "allowFileExecution");
    auto apply = findItem(window->contentItem(), "applyFilePermissions");
    require(scroll && execution && apply, "permission editor and apply button rendered");
    const auto click = [&](QQuickItem *item) {
        const QPointF pos = item->mapToScene(QPointF(9, item->height() / 2));
        const QPointF global = window->mapToGlobal(pos.toPoint());
        QMouseEvent press(QEvent::MouseButtonPress, pos, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, pos, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(window, &press); QApplication::sendEvent(window, &release); pause(100);
    };
    auto flick = scroll->property("contentItem").value<QObject *>();
    require(flick, "properties scrollbar has a flickable");
    flick->setProperty("contentY", 0);
    const QPointF wheelPoint = scroll->mapToScene(QPointF(scroll->width() / 2, scroll->height() / 2));
    QWheelEvent wheel(wheelPoint, window->mapToGlobal(wheelPoint.toPoint()), QPoint(), QPoint(0, -120),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(window, &wheel); pause(220);
    require(flick->property("contentY").toDouble() >= 200, "one mouse-wheel notch moves the properties content by the configured step");
    const QPointF local = execution->mapToItem(scroll, QPointF());
    flick->setProperty("contentY", flick->property("contentY").toDouble() + local.y() - scroll->height() / 2);
    pause(200); window->grabWindow();
    require(!apply->property("enabled").toBool(), "unchanged permissions cannot apply");
    click(execution);
    require(apply->property("enabled").toBool() && modeOf(path) == 0600, "execute checkbox edits draft without changing disk");
    window->grabWindow().save("/tmp/lurviko-properties-light.png");
    click(apply);
    require(modeOf(path) == 0700 && !apply->property("enabled").toBool(), "Apply saves execute bit and refreshes draft");
    auto groupRead = findItem(window->contentItem(), "permissionGroupRead");
    auto groupWrite = findItem(window->contentItem(), "permissionGroupWrite");
    require(groupRead && groupWrite, "individual group controls rendered");
    click(groupRead);
    require(apply->property("enabled").toBool() && popup->property("permissionDraft").toInt() == 0740,
            "group read edits its own mode bit");
    click(apply);
    require(modeOf(path) == 0740, "individual group permission saved to disk");
    click(groupWrite);
    require(popup->property("permissionDraft").toInt() == 0760 && modeOf(path) == 0740,
            "unsaved group write remains a draft");
    QMetaObject::invokeMethod(popup, "close"); pause(100);
    QMetaObject::invokeMethod(pane, "openPropertiesForSelection"); pause(200);
    require(execution->property("checked").toBool() && groupRead->property("checked").toBool()
            && !groupWrite->property("checked").toBool(), "reopening keeps saved permissions and discards unsaved drafts");
    auto modeInput = findItem(window->contentItem(), "permissionModeInput");
    require(modeInput, "editable octal mode input rendered");
    modeInput->forceActiveFocus();
    modeInput->setProperty("text", "0644");
    QMetaObject::invokeMethod(modeInput, "textEdited"); pause(20);
    require(popup->property("permissionDraft").toInt() == 0644 && !execution->property("checked").toBool(), "octal typing immediately updates the permission checkboxes");
    modeInput->setProperty("text", "06"); QMetaObject::invokeMethod(modeInput, "textEdited"); pause(20);
    require(!apply->property("enabled").toBool() && modeOf(path) == 0740, "incomplete octal input cannot be applied");
    modeInput->setProperty("text", "0644"); QMetaObject::invokeMethod(modeInput, "textEdited");
    QMetaObject::invokeMethod(modeInput, "accepted");
    require(until([&] { return modeOf(path) == 0644; }), "Enter saves valid octal mode");
    auto adminToggle = findItem(window->contentItem(), "administratorAccessPermissions");
    auto ownerCombo = findItem(window->contentItem(), "permissionOwnerCombo");
    auto groupCombo = findItem(window->contentItem(), "permissionGroupCombo");
    require(adminToggle && ownerCombo && groupCombo, "owner/group selectors and administrator toggle rendered");
    if (adminToggle->property("enabled").toBool()) {
        const QPointF adminPoint = adminToggle->mapToItem(scroll, QPointF());
        flick->setProperty("contentY", flick->property("contentY").toDouble() + adminPoint.y() - scroll->height() / 2);
        pause(120); click(adminToggle);
        require(popup->property("administratorDraft").toBool()
                && ownerCombo->property("count").toInt() > 1 && groupCombo->property("count").toInt() > 1,
                "administrator toggle exposes system accounts without invoking authentication");
        click(adminToggle);
        require(!popup->property("administratorDraft").toBool()
                && (::geteuid() == 0 || ownerCombo->property("count").toInt() == 1), "ordinary owner choices are restored when administrator mode is disabled");
    }
    eval("if (!AppTheme.dark) AppTheme.toggle()"); pause(150);
    window->grabWindow().save("/tmp/lurviko-properties-dark.png");
    QMetaObject::invokeMethod(popup, "close");
    select(folderName);
    QMetaObject::invokeMethod(pane, "openPropertiesForSelection");
    require(until([&] { return popup->property("visible").toBool()
                && popup->property("permissionDraft").toInt() == (modeOf(folder) & 0777); }),
            "reopened properties finishes loading the folder permissions");
    auto accessWarning = findItem(window->contentItem(), "folderTraversalWarning");
    require(accessWarning && !accessWarning->isVisible(), "accessible folder does not show a traversal warning");
    modeInput->forceActiveFocus(); modeInput->setProperty("text", "0644");
    QMetaObject::invokeMethod(modeInput, "textEdited"); pause(20);
    require(accessWarning->isVisible(), "folder code without search bits explains inaccessible metadata before applying");
    modeInput->setProperty("text", "0755"); QMetaObject::invokeMethod(modeInput, "textEdited"); pause(20);
    require(!accessWarning->isVisible(), "valid folder search bits remove the warning");
    QMetaObject::invokeMethod(popup, "close");
    select(longName); window->grabWindow().save("/tmp/lurviko-filename-padding.png");
    std::fprintf(stderr, "PASS: real properties UI draft, execute toggle, Apply and reopening; light/dark previews saved\n");
    return 0;
}
