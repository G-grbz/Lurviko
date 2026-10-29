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
#include <sys/resource.h>
#include <unistd.h>
#include "backend_test_types.h"

static void pause(int ms) { QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec(); }
static void require(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static bool until(std::function<bool()> check, int timeout = 10000) {
    QElapsedTimer timer; timer.start();
    while (!check() && timer.elapsed() < timeout) pause(10);
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
static double cpuSeconds() {
    rusage usage{}; getrusage(RUSAGE_SELF, &usage);
    return usage.ru_utime.tv_sec + usage.ru_utime.tv_usec / 1e6
        + usage.ru_stime.tv_sec + usage.ru_stime.tv_usec / 1e6;
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("Lurviko-QA"); app.setApplicationName("Properties");
    QQuickStyle::setStyle("Basic");
    // REGISTER_BACKEND_TYPES
    const QString base = QString::fromLocal8Bit(qgetenv("LURVIKO_PROPERTIES_TEST_ROOT")) + "/fixture";
    require(QDir().mkpath(base), "fixture directory created");
    // Footer and Properties share one recursive snapshot, including per-child
    // contributions so selecting visible items does not count hidden siblings.
    const QString sizeRoot = base + "-size-snapshot";
    require(QDir().mkpath(sizeRoot + "/nested/deeper"), "size fixture created");
    for (const auto &entry : QList<QPair<QString, int>>{{"visible.txt", 3}, {".hidden", 7}, {"nested/deeper/data", 5}}) {
        QFile file(sizeRoot + "/" + entry.first);
        require(file.open(QIODevice::WriteOnly) && file.write(QByteArray(entry.second, 'x')) == entry.second, "size fixture bytes");
    }
    require(::symlink(QFile::encodeName(sizeRoot).constData(), QFile::encodeName(sizeRoot + "/loop").constData()) == 0, "size symlink loop fixture");
    FilePropertiesManager directoryStats, sharedProperties;
    sharedProperties.setStatsSource(&directoryStats);
    directoryStats.inspect(sizeRoot);
    sharedProperties.inspectSelection({sizeRoot + "/visible.txt", sizeRoot + "/nested"});
    require(until([&] { return directoryStats.statsReady() && sharedProperties.statsReady(); }), "in-flight size request is shared");
    require(directoryStats.totalSize() == 15 && sharedProperties.totalSize() == 8, "recursive size includes hidden files without following symlinks; subsets stay exact");
    sharedProperties.inspectSelection({sizeRoot + "/visible.txt", sizeRoot + "/.hidden", sizeRoot + "/nested", sizeRoot + "/loop"});
    require(!sharedProperties.calculating() && sharedProperties.totalSize() == 15, "all-selected Properties reuses ready footer bytes synchronously");
    require(sharedProperties.fileCount() == 3 && sharedProperties.folderCount() == 2, "shared snapshot preserves file/folder counts");
    sharedProperties.inspect(sizeRoot + "/nested");
    require(!sharedProperties.calculating() && sharedProperties.totalSize() == 5 && sharedProperties.folderCount() == 1, "single subfolder reuses snapshot excluding its root count");
    directoryStats.inspect(sizeRoot);
    sharedProperties.inspectSelection({sizeRoot + "/visible.txt", sizeRoot + "/nested"});
    sharedProperties.cancelInspection();
    require(until([&] { return directoryStats.statsReady(); }), "closing Properties does not cancel the footer owner");
    require(!sharedProperties.statsReady(), "cancelled consumer ignores shared completion");

    std::fprintf(stderr, "PASS: shared recursive footer/Properties snapshot, hidden subsets, symlink exclusion and independent consumer cancellation\n");
    // A cached local tree still costs CPU to enumerate. Automatic footer work
    // must yield while explicit Properties can boost that same worker.
    const QString idleRoot = base + "-idle-size";
    const QString idleChild = idleRoot + "/alt öğe/derin";
    require(QDir().mkpath(idleChild), "idle-size tree created");
    const QString seed = idleRoot + "/seed";
    { QFile file(seed); require(file.open(QIODevice::WriteOnly) && file.write("x") == 1, "size seed created"); }
    for (int index = 0; index < 20000; ++index)
        require(::link(QFile::encodeName(seed).constData(), QFile::encodeName(idleChild + "/item-" + QString::number(index)).constData()) == 0,
                "cached-tree hardlink fixture created");
    FilePropertiesManager idleStats, demandingProperties;
    idleStats.setProperty("backgroundStats", true);
    demandingProperties.setStatsSource(&idleStats);
    int started = 0;
    QObject::connect(&idleStats, &FilePropertiesManager::calculatingChanged, [&] { if (idleStats.calculating()) ++started; });
    QElapsedTimer scanClock; scanClock.start();
    double beforeCpu = cpuSeconds();
    idleStats.inspect(idleRoot + "/");
    require(until([&] { return idleStats.statsReady(); }, 20000), "background size scan finishes");
    const qint64 backgroundMs = scanClock.elapsed();
    double workCpu = cpuSeconds() - beforeCpu;
    std::fprintf(stderr, "SIZE CPU: %lld ms background, %.4f CPU seconds\n", backgroundMs, workCpu);
    require(workCpu < backgroundMs / 1000.0 * 0.20, "automatic recursive size stays below 20% of one core, including event-loop overhead");
    require(idleStats.totalSize() == 20001 && idleStats.fileCount() == 20001 && idleStats.folderCount() == 2,
            "low-priority scan preserves exact recursive logical totals");
    idleStats.inspect(idleRoot);
    demandingProperties.inspect(idleRoot + "/alt öğe");
    scanClock.restart();
    require(until([&] { return demandingProperties.statsReady(); }, 10000), "Properties boosts and reuses the current scan");
    require(scanClock.elapsed() < backgroundMs * 0.6 + 100 && started == 2,
            "foreground demand speeds the existing scan without starting another");
    require(demandingProperties.totalSize() == 20000 && demandingProperties.fileCount() == 20000,
            "boosted child subtotal remains exact");
    idleStats.inspect(idleRoot);
    demandingProperties.inspect(idleRoot);
    demandingProperties.cancelInspection();
    beforeCpu = cpuSeconds(); scanClock.restart();
    require(until([&] { return idleStats.statsReady(); }, 20000), "owner scan survives consumer cancellation");
    workCpu = cpuSeconds() - beforeCpu;
    require(workCpu < scanClock.elapsed() / 1000.0 * 0.20, "closing Properties releases foreground demand and restores the idle CPU budget");
    std::fprintf(stderr, "PASS: bounded background size CPU, exact totals, shared foreground promotion and cancellation demotion\n");
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

    const QString bulkBase = QString::fromLocal8Bit(qgetenv("LURVIKO_PROPERTIES_TEST_ROOT")) + "/bulk";
    const QString tree = bulkBase + "/tree", first = tree + "/first", second = tree + "/second", third = bulkBase + "/third";
    require(QDir().mkpath(tree), "bulk tree created");
    const QStringList bulkFiles{first, second, third};
    const QList<int> originalModes{0640, 0704, 0600};
    for (int i = 0; i < bulkFiles.size(); ++i) {
        QFile file(bulkFiles[i]); require(file.open(QIODevice::WriteOnly), "bulk fixture opens");
        file.write(QByteArray(5 + i, 'a' + i)); file.close();
        require(::chmod(QFile::encodeName(bulkFiles[i]).constData(), originalModes[i]) == 0, "bulk mode set");
    }
    require(::chmod(QFile::encodeName(tree).constData(), 0755) == 0, "bulk directory mode set");
    const QString bulkLink = bulkBase + "/outside-link";
    require(::symlink(QFile::encodeName(path).constData(), QFile::encodeName(bulkLink).constData()) == 0, "bulk external link created");
    manager.inspectSelection({tree, first, QUrl::fromLocalFile(first).toString(), third, bulkLink});
    require(manager.multiple() && manager.selectionCount() == 4 && manager.selectedFolders() == 1
            && manager.selectedFiles() == 2 && manager.selectedLinks() == 1, "selection counts deduplicate paths and classify links separately");
    require(manager.commonLocation().isEmpty() && until([&] { return !manager.calculating(); }), "mixed locations and asynchronous totals settle");
    require(manager.totalSize() == 18 && manager.fileCount() == 3 && manager.folderCount() == 1,
            "parent/child selection counted once and external symlink target excluded from total");
    require(manager.permissionsEditable() && manager.mixedPermissionMask() != 0, "mixed editable modes recognized");
    require(!manager.setPermissionMode(0777) && !manager.setFolderIcon("folder") && !manager.setLinkTarget(path),
            "single-item setters cannot accidentally modify one item of a bulk selection");
    require(manager.applySelectionAccess(0020, 0020, "", "", true)
            && until([&] { return !manager.accessBusy(); }), "bulk recursive bit edit finishes");
    require(manager.accessFailed() == 0 && manager.accessCompleted() == 4
            && modeOf(first) == 0660 && modeOf(second) == 0724 && modeOf(third) == 0620
            && modeOf(tree) == 0775 && modeOf(path) == 0600,
            "only requested bit changes; recursive duplicates and symlink targets stay safe");
    manager.inspectSelection(bulkFiles);
    const QString bulkOwner = manager.ownerName(), bulkGroup = manager.groupName();
    require(manager.applySelectionAccess(0, 0, bulkOwner, bulkGroup)
            && until([&] { return !manager.accessBusy(); }) && manager.accessFailed() == 0,
            "bulk ownership-only operation is supported");
    require(modeOf(first) == 0660 && modeOf(second) == 0724 && modeOf(third) == 0620,
            "ownership-only operation preserves each item's distinct mode");
    for (const auto &name : manager.availableGroups()) {
        if (name == bulkGroup) continue;
        const auto group = ::getgrnam(name.toLocal8Bit().constData()); if (!group) continue;
        const gid_t expected = group->gr_gid;
        require(manager.applySelectionAccess(0, 0, "", name)
                && until([&] { return !manager.accessBusy(); }) && manager.accessFailed() == 0, "bulk member-group changes succeed");
        for (const auto &item : bulkFiles) {
            struct stat st {}; require(::stat(QFile::encodeName(item).constData(), &st) == 0 && st.st_gid == expected,
                                       "bulk group is actually saved for every selected file");
        }
        require(manager.applySelectionAccess(0, 0, "", bulkGroup)
                && until([&] { return !manager.accessBusy(); }) && manager.accessFailed() == 0, "bulk original groups restored");
        break;
    }
    require(!manager.applySelectionAccess(0777, -1, "", "")
            && !manager.applySelectionAccess(0, 0, "lurviko-no-such-user", ""), "invalid masks and owners rejected before changing anything");
    if (::geteuid() != 0)
        require(!manager.applySelectionAccess(0, 0, "root", ""), "bulk ownership escalation needs administrator authentication");
    manager.inspectSelection({first, bulkBase + "/missing"});
    require(!manager.applySelectionAccess(0777, 0777, "", "") && modeOf(first) == 0660,
            "missing item refuses the entire batch before any mutation");
    manager.inspectSelection({first, "smb://example.invalid/share/file"});
    require(!manager.allLocal() && !manager.permissionsEditable() && !manager.applySelectionAccess(0777, 0777, "", ""),
            "remote/mixed selection cannot be mistaken for local paths");
    if (manager.administratorAvailable()) {
        manager.inspectSelection({third, tree});
        require(manager.applySelectionAccess(0777, 0777, "", "", true, true)
                && until([&] { return !manager.accessBusy(); }) && manager.accessFailed() == 0,
                "descriptor-based administrator helper handles writable trees without path-based jobs");
        require(modeOf(third) == 0777 && modeOf(first) == 0666 && modeOf(path) == 0600,
                "helper changes selected roots/descendants but never the external symlink target");
    }
    for (int i = 0; i < bulkFiles.size(); ++i) {
        QFile file(bulkFiles[i]); require(file.open(QIODevice::ReadOnly) && file.readAll() == QByteArray(5 + i, 'a' + i),
                                        "bulk permission and ownership changes preserve file contents");
    }
    std::fprintf(stderr, "PASS: bulk totals, mixed permissions, ownership changes, recursion, administrator helper and selection boundaries\n");

    QQmlApplicationEngine engine;
    engine.addImageProvider("gfilethumb", new ThumbnailProvider);
    engine.addImageProvider("systemicon", new SystemIconProvider);
    engine.addImageProvider("bundledicon", new BundledIconProvider);
    engine.addImageProvider("folderart", new FolderIconProvider);
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
    auto footerStats = pane->findChild<FilePropertiesManager *>("directorySizeProperties");
    auto footerLabel = findItem(pane, "directorySizeLabel");
    require(footerStats && footerLabel && until([&] { return footerStats->statsReady() && footerLabel->property("ready").toBool(); }), "footer prepares the real recursive directory size");
    require(footerLabel->isVisible() && !footerLabel->property("text").toString().contains(QChar(0x2026)), "footer displays a completed size beside item count");
    int visibleScanStarts = 0;
    QObject::connect(footerStats, &FilePropertiesManager::calculatingChanged, [&] { if (footerStats->calculating()) ++visibleScanStarts; });
    pane->setVisible(false); pause(30); pane->setVisible(true); pause(450);
    require(footerStats->statsReady() && visibleScanStarts == 0,
            "showing an unchanged pane reuses its ready size without a new scan");
    bool currentScanCompleted = false;
    const auto completedConnection = QObject::connect(footerStats, &FilePropertiesManager::propertiesChanged, [&] {
        if (footerStats->statsReady() && footerStats->path() == idleRoot) currentScanCompleted = true;
    });
    footerStats->inspect(idleRoot);
    eval("scheduleDirectorySize(true); scheduleDirectorySize(true)");
    require(until([&] { return footerStats->statsReady() && footerStats->path() == base; }, 20000),
            "refreshes queued during a scan produce a fresh visible-directory result");
    require(currentScanCompleted && visibleScanStarts == 2,
            "refreshes coalesce into one follow-up without cancelling the active size scan");
    QObject::disconnect(completedConnection);
    eval("selectAll(); showPropertiesForSelection()");
    auto sizeDialog = pane->findChild<QObject *>("filePropertiesDialog");
    require(sizeDialog && until([&] { return sizeDialog->property("opened").toBool(); }), "all-selected Properties opens");
    require(!eval("fileProperties.calculating").toBool() && eval("fileProperties.totalSize").toLongLong() == footerStats->totalSize(), "all-selected real Properties uses the footer snapshot without a new scan");
    QMetaObject::invokeMethod(sizeDialog, "close");
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
    auto hint = findItem(window->contentItem(), "specialPermissionsHint");
    require(hint && hint->property("text").toString().contains("setuid")
            && hint->property("text").toString().contains("setgid"), "special permission safety hint is visible in Properties");
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
    QApplication::sendEvent(window, &wheel);
    require(until([&] { return flick->property("contentY").toDouble() >= 200; }, 1200),
            "one mouse-wheel notch moves the properties content by the configured step");
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
    eval("root.selectAll(); itemContextMenu.open()"); pause(200);
    require(eval(R"js((function() {
        for (let i = 0; i < itemContextMenu.count; ++i) {
            const item = itemContextMenu.itemAt(i);
            if (item && item.shortcutAction === "properties") return item.visible && item.enabled;
        }
        return false;
    })())js").toBool(), "context menu exposes Properties for a multi-selection");
    eval("itemContextMenu.close(); root.showPropertiesForSelection()");
    require(until([&] { return popup->property("visible").toBool() && popup->property("multiple").toBool(); }),
            "Properties shortcut/action opens for multiple selected items");
    require(!apply->property("enabled").toBool() && ownerCombo->property("value").toString().isEmpty()
            && groupCombo->property("value").toString().isEmpty(), "bulk drafts initially preserve all ownership and permission values");
    require(modeInput->property("text").toString().isEmpty() && !execution->isVisible(),
            "mixed octal mode has no misleading first-item value or single-file execution control");
    auto uiProperties = eval("fileProperties").value<QObject *>();
    require(uiProperties && until([&] { return !uiProperties->property("calculating").toBool(); })
            && uiProperties->property("selectionCount").toInt() == 2
            && uiProperties->property("selectedFolders").toInt() == 1, "bulk UI uses an aggregate selection snapshot");
    auto ownerExecute = findItem(window->contentItem(), "permissionOwnerExecute");
    require(ownerExecute && ownerExecute->property("checkState").toInt() == Qt::PartiallyChecked,
            "different permission bits display indeterminate checkboxes");
    flick->setProperty("contentY", 0);
    const QPointF bitPosition = ownerExecute->mapToItem(scroll, QPointF());
    flick->setProperty("contentY", flick->property("contentY").toDouble() + bitPosition.y() - scroll->height() / 2);
    pause(120); click(ownerExecute);
    require(popup->property("permissionMask").toInt() == 0100 && apply->property("enabled").toBool(),
            "clicking an indeterminate permission edits only that bit");
    const int oldFileMode = modeOf(path), oldFolderMode = modeOf(folder);
    click(apply);
    require(until([&] { return modeOf(path) == (oldFileMode | 0100) && !uiProperties->property("accessBusy").toBool(); })
            && modeOf(folder) == oldFolderMode && !apply->property("enabled").toBool(),
            "bulk UI Apply updates selected files, preserves unrelated folder bits and clears saved drafts");
    // Exercise the editable field with the keep-values row present. This also
    // verifies group-only Apply works with a blank mixed permission mode.
    const int groupChoiceCount = eval("propertiesDialog.groupChoices.length").toInt();
    for (int i = 1; i < groupChoiceCount; ++i) {
        const QString choice = eval(QString("propertiesDialog.groupChoices[%1]").arg(i)).toString();
        if (choice == bulkGroup) continue;
        const auto group = ::getgrnam(choice.toLocal8Bit().constData()); if (!group) continue;
        const gid_t expected = group->gr_gid;
        auto groupEditor = findItem(groupCombo, "accountEditor");
        require(groupEditor, "bulk group has an editable text field");
        groupEditor->forceActiveFocus();
        groupEditor->setProperty("text", choice);
        require(QMetaObject::invokeMethod(groupEditor, "textEdited"), "bulk group text edit activates");
        require(popup->property("groupDraft").toString() == choice
                && popup->property("permissionMask").toInt() == 0 && apply->property("enabled").toBool(),
                "group-only draft is applicable without assigning a common permission mode");
        click(apply);
        require(until([&] { return !uiProperties->property("accessBusy").toBool(); }), "bulk UI group changes finish");
        for (const auto &item : {path, folder}) {
            struct stat st {}; require(::stat(QFile::encodeName(item).constData(), &st) == 0 && st.st_gid == expected,
                                       "bulk UI saves the chosen group on files and folders");
        }
        require(modeOf(path) == (oldFileMode | 0100) && modeOf(folder) == oldFolderMode,
                "bulk UI group-only change does not overwrite different permission codes");
        break;
    }
    modeInput->forceActiveFocus(); modeInput->setProperty("text", "0640");
    QMetaObject::invokeMethod(modeInput, "textEdited");
    require(popup->property("permissionMask").toInt() == 0777, "typing full octal mode explicitly edits all ordinary bits");
    modeInput->setProperty("text", "06"); QMetaObject::invokeMethod(modeInput, "textEdited");
    require(!apply->property("enabled").toBool(), "incomplete bulk octal mode cannot apply");
    modeInput->setProperty("text", "0640"); QMetaObject::invokeMethod(modeInput, "textEdited");
    modeInput->setFocus(false); pause(100);
    window->grabWindow().save("/tmp/lurviko-bulk-properties-dark.png");
    QMetaObject::invokeMethod(popup, "close");
    std::fprintf(stderr, "PASS: bulk Properties UI, mixed-state editing, Apply and octal validation\n");
    return 0;
}
