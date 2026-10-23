#include <QApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVirtualObject>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QMimeData>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QtTest/QSignalSpy>
#include <cstdio>
#include <functional>
#include "backend_test_types.h"
static void require(bool ok, const char *message) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); } }
static void pause(int ms) { QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec(); }
static bool until(const std::function<bool()> &check) { QElapsedTimer timer; timer.start(); while (!check() && timer.elapsed() < 15000) pause(10); return check(); }
static void write(const QString &path, const QByteArray &data) { QFile f(path); require(f.open(QIODevice::WriteOnly), "fixture opens"); require(f.write(data) == data.size(), "fixture writes"); }
static QVariant eval(QObject *pane, const QString &code) {
    auto *scope = pane->findChild<MillerNavigationModel *>("millerNavigation");
    QQmlExpression expression(qmlContext(scope), pane, code);
    auto value = expression.evaluate(); require(!expression.hasError(), qPrintable(expression.error().toString())); return value;
}
class ArkService final : public QDBusVirtualObject {
public:
    std::function<void(QString)> extract;
    QString lastDestination;
    QString introspect(const QString &) const override { return "<interface name=\"org.kde.ark.DndExtract\"><method name=\"extractSelectedFilesTo\"><arg type=\"s\" direction=\"in\"/></method></interface>"; }
    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override {
        if (message.member() != "extractSelectedFilesTo") return false;
        // The real Ark API replies before its extraction job finishes.
        connection.send(message.createReply());
        lastDestination = message.arguments().first().toString();
        extract(lastDestination);
        return true;
    }
};
int main(int argc, char **argv) {
    QApplication app(argc, argv); app.setOrganizationName("Lurviko-QA"); app.setApplicationName("ArchiveDrop"); QQuickStyle::setStyle("Basic");
    // REGISTER_BACKEND_TYPES
    const QString base = qEnvironmentVariable("LURVIKO_ARCHIVE_DROP_TEST_ROOT");
    require(QDir().mkpath(base + "/source/zz-test"), "archive source creates");
    write(base + "/source/zz-test/a.txt", "small archive payload");
    write(base + "/source/zz-test/b.txt", "second payload");
    const QString zip = base + "/tiny.zip";
    QProcess create; create.start("bsdtar", {"-a", "-cf", zip, "-C", base + "/source", "zz-test"});
    require(create.waitForFinished() && create.exitCode() == 0, "tiny ZIP creates");
    require(QFileInfo(zip).size() < 2048, "archive is small enough for the fast-job regression");
    QQmlApplicationEngine engine;
    engine.addImageProvider("gfilethumb", new ThumbnailProvider);
    engine.addImageProvider("systemicon", new SystemIconProvider);
    engine.addImageProvider("bundledicon", new BundledIconProvider);
    engine.addImageProvider("folderart", new FolderIconProvider);
    engine.loadData(R"qml(
import QtQuick
import QtQuick.Controls
import Lurviko.App
import Lurviko.Backend
ApplicationWindow {
 width: 1100; height: 700; visible: true
 LanguageManager { id: language; objectName: "language"; language: "en" }
 FavoritesModel { id: favorites } QuickAccessModel { id: shortcuts } AdminEditManager { id: admin }
 QtObject { id: auth; property string googleAccessToken: ""; property string oneDriveAccessToken: "" }
 QtObject { id: prefs } QtObject { id: cloud; property string accessToken: ""; signal refreshRequested() }
 BrowsePage { objectName: "page"; anchors.fill: parent; lang: language; contentIndexModel: null; musicPlayer: null;
   favoritesModel: favorites; quickAccessModel: shortcuts; adminEditor: admin; cloudAuth: auth;
   cloudIntegrationPreferences: prefs; googleDrive: cloud; oneDrive: cloud }
}
)qml");
    require(!engine.rootObjects().isEmpty(), "browser loads");
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    auto *page = window->findChild<QQuickItem *>("page");
    auto *pane = page->property("activePane").value<QObject *>();
    auto *operations = qobject_cast<FileOperations *>(eval(pane, "fileOps").value<QObject *>());
    require(operations, "shared file operations found");
    QStringList toasts;
    QSignalSpy notifications(pane, SIGNAL(requestToast(QString)));
    require(notifications.isValid(), "UI notification signal connects");
    QObject::connect(operations, &FileOperations::archiveDropItemsChanged, &app,
        [&](const QString &, const QStringList &urls, bool first) { if (first) toasts.append(urls.join('|')); });
    auto *language = window->findChild<LanguageManager *>("language");
    ArkService ark;
    auto bus = QDBusConnection::sessionBus();
    require(bus.registerVirtualObject("/DndExtract/Test", &ark), "Ark fixture registers");
    auto dropAt = [&](QPointF point, const QString &expectedTarget = QString()) {
        QMimeData mime;
        mime.setData("application/x-kde-ark-dndextract-service", bus.baseService().toUtf8());
        mime.setData("application/x-kde-ark-dndextract-path", "/DndExtract/Test");
        QDragEnterEvent enter(point.toPoint(), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QDropEvent drop(point, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        app.sendEvent(window, &enter);
        require(until([&] { return pane->property("dropTargetUrl").toString() == expectedTarget; }),
                "Ark drag highlights the actual hovered folder before extraction");
        if (!expectedTarget.isEmpty()) {
            auto *entry = qobject_cast<QQuickItem *>(eval(pane,
                QString("root.directoryView().itemAtIndex(root.directory.indexOfUrl('%1'))").arg(expectedTarget)).value<QObject *>());
            auto *badge = entry ? entry->findChild<QQuickItem *>("folderDropBadge") : nullptr;
            require(badge && badge->isVisible(), "hovered folder displays the bundled drop badge");
            const auto *glyph = badge->findChild<QQuickItem *>("folderDropBadgeIcon");
            require(glyph && glyph->property("source").toUrl().toString().contains("plus.svg"), "drop badge uses the supplied plus glyph");
            QQuickItem *artwork = nullptr;
            for (auto *sibling : badge->parentItem()->childItems()) {
                if (sibling != badge && sibling->property("source").isValid()) { artwork = sibling; break; }
            }
            require(artwork && qAbs(badge->x() - artwork->x()) < 1 && qAbs(badge->y() - artwork->y()) < 1,
                    "drop badge sits at the icon's upper-left corner");
            require(eval(pane, "AppTheme.dropTargetColor !== AppTheme.accent").toBool(), "drop color differs from selection color");
        }
        app.sendEvent(window, &drop);
        require(pane->property("dropTargetUrl").toString().isEmpty(), "drop feedback clears after extraction starts");
    };
    for (bool kio : {false, true}) {
        const QString destination = base + (kio ? "/kio" : "/integrated");
        require(QDir().mkpath(destination + "/zz-test"), "existing empty merge folder creates");
        for (int i = 0; i < 100; ++i)
            require(QDir().mkpath(destination + QString("/item-%1").arg(i, 3, 10, QChar('0'))), "offscreen row creates");
        eval(pane, kio ? "AppTheme.setKioProgressEnabled(true)" : "AppTheme.setKioProgressEnabled(false)");
        language->setLanguage(kio ? "tr" : "en");
        pane->setProperty("currentLocation", QUrl::fromLocalFile(destination).toString());
        pane->setProperty("viewMode", "grid");
        auto directory = [&] { return qobject_cast<DirectoryModel *>(pane->property("directory").value<QObject *>()); };
        require(until([&] { return directory() && !directory()->loading() && directory()->rowCount() == 101; }), "destination loads");
        directory()->setSortMode("name"); directory()->setSortAscending(true);
        // The existing folder sorts after 100 other folders, off screen.
        const QString target = QUrl::fromLocalFile(destination + "/zz-test").toString();
        require(directory()->itemAt(directory()->indexOfUrl(target)).value("childCount").toInt() == 0, "merge folder starts with zero children");
        eval(pane, "grid.contentY = grid.originY");
        const int beforeToasts = toasts.size();
        const int beforeNotifications = notifications.size();
        bool extracted = false;
        ark.extract = [&](const QString &output) {
            auto *process = new QProcess(&app);
            QObject::connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), &app,
                [&, process](int code, QProcess::ExitStatus status) { extracted = code == 0 && status == QProcess::NormalExit; process->deleteLater(); });
            process->start("bsdtar", {"-xf", zip, "-C", output, "--safe-writes"});
        };
        // Deliver the actual external Ark MIME payload to the real DropArea.
        const auto *item = qobject_cast<QQuickItem *>(pane);
        const QPointF point = item->mapToScene(QPointF(item->width() - 25, item->height() - 80));
        dropAt(point);
        require(until([&] { return extracted; }), "real ZIP extraction finishes");
        require(until([&] {
            const int row = directory()->indexOfUrl(target);
            return row >= 0 && directory()->itemAt(row).value("childCount").toInt() == 2
                && pane->property("selectedUrl").toString() == target;
        }), "existing merged folder count and selection update without manual refresh");
        require(until([&] { return eval(pane, "grid.contentY > grid.originY + 10").toBool(); }), "viewport scrolls to extracted result");
        require(toasts.size() == beforeToasts + 1 && toasts.last().contains(target), "small extraction produces one observed-result notification in either KIO mode");
        require(notifications.size() == beforeNotifications + 1
                && notifications.last().first().toString() == (kio ? QString::fromUtf8("Arşiv içeriği eklendi.") : QString("Archive contents added.")),
                "UI toast is visible and localized with system progress both on and off");
        eval(pane, "root.selectIndex(0, Qt.NoModifier)");
        const QString manualSelection = pane->property("selectedUrl").toString();
        write(destination + "/zz-test/c.txt", "late entry");
        require(until([&] { return directory()->itemAt(directory()->indexOfUrl(target)).value("childCount").toInt() == 3; }), "later child changes refresh folder count");
        require(pane->property("selectedUrl").toString() == manualSelection && toasts.size() == beforeToasts + 1,
                "follow-up updates do not steal selection or repeat the notification");
    }
    // The pane-wide Ark receiver must route to a scrolled folder card/row,
    // rather than swallowing the folder's own drop target. Exercise actual
    // external MIME events and extraction, including paths needing URL encoding.
    for (const QString &mode : {QString("grid"), QString("list"), QString("columns")}) {
        const QString destination = base + "/folder-drop-" + mode;
        const QString folder = destination + QString::fromUtf8("/zz target Türkçe");
        require(QDir().mkpath(folder), "folder drop destination creates");
        for (int i = 0; i < 35; ++i)
            require(QDir().mkpath(destination + QString("/item-%1").arg(i, 3, 10, QChar('0'))), "scroll fixture creates");
        write(destination + "/plain.txt", "regular file is not a destination folder");
        pane->setProperty("currentLocation", QUrl::fromLocalFile(destination).toString());
        pane->setProperty("viewMode", mode);
        auto directory = [&] { return qobject_cast<DirectoryModel *>(pane->property("directory").value<QObject *>()); };
        require(until([&] { return directory() && !directory()->loading() && directory()->rowCount() == 37; }), "folder drop view loads");
        directory()->setSortMode("name"); directory()->setSortAscending(true);
        require(until([&] { return eval(pane, "root.directoryView().visible && root.directoryView().count === root.directory.count").toBool(); }),
                "drop view binds the loaded directory model");
        auto itemPoint = [&](const QString &url) {
            const int row = directory()->indexOfUrl(url);
            require(row >= 0, "drop item exists in model");
            std::fprintf(stderr, "DROP: view=%s row=%d url=%s\n", qPrintable(mode), row, qPrintable(url));
            eval(pane, "root.directoryView().forceLayout()");
            eval(pane, QString("root.directoryView().positionViewAtIndex(%1, ListView.Center)").arg(row));
            QQuickItem *entry = nullptr;
            require(until([&] {
                entry = qobject_cast<QQuickItem *>(eval(pane, QString("root.directoryView().itemAtIndex(%1)").arg(row)).value<QObject *>());
                return entry && entry->isVisible();
            }), "scrolled drop delegate becomes visible");
            pause(100);
            return entry->mapToScene(QPointF(entry->width() / 2, entry->height() / 2));
        };
        bool extracted = false;
        ark.extract = [&](const QString &output) {
            auto *process = new QProcess(&app);
            QObject::connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), &app,
                [&, process](int code, QProcess::ExitStatus status) { extracted = code == 0 && status == QProcess::NormalExit; process->deleteLater(); });
            process->start("bsdtar", {"-xf", zip, "-C", output, "--safe-writes"});
        };
        ark.lastDestination.clear();
        dropAt(itemPoint(QUrl::fromLocalFile(folder).toString()), QUrl::fromLocalFile(folder).toString());
        require(until([&] { return extracted; }), "dropping onto a folder extracts successfully");
        require(QDir::cleanPath(ark.lastDestination) == QDir::cleanPath(folder), "Ark receives hovered folder, not current directory");
        require(QFileInfo::exists(folder + "/zz-test/a.txt") && !QFileInfo::exists(destination + "/zz-test"),
                "ZIP contents are created inside the hovered folder only");
        // A file must continue to mean the containing directory.
        extracted = false; ark.lastDestination.clear();
        dropAt(itemPoint(QUrl::fromLocalFile(destination + "/plain.txt").toString()));
        require(until([&] { return extracted; }), "dropping onto a file extracts into its directory");
        require(QDir::cleanPath(ark.lastDestination) == QDir::cleanPath(destination)
                    && QFileInfo::exists(destination + "/zz-test/a.txt"),
                "file drop retains the current-directory fallback");
        QMimeData regularFile;
        regularFile.setUrls({QUrl::fromLocalFile(base + "/source/zz-test/a.txt")});
        const QPointF folderPoint = itemPoint(QUrl::fromLocalFile(folder).toString());
        QDragEnterEvent regularEnter(folderPoint.toPoint(), Qt::CopyAction, &regularFile, Qt::LeftButton, Qt::NoModifier);
        app.sendEvent(window, &regularEnter);
        require(until([&] { return pane->property("dropTargetUrl").toString() == QUrl::fromLocalFile(folder).toString(); }),
                "regular external file drag uses the same folder feedback");
        QDragLeaveEvent leave;
        app.sendEvent(window, &leave);
        require(until([&] { return pane->property("dropTargetUrl").toString().isEmpty(); }), "cancelled drag clears folder feedback");
    }
    bus.unregisterObject("/DndExtract/Test");
    std::fprintf(stderr, "PASS: native Ark drops, small ZIP merge/counts/focus in both KIO modes, scrolled folder and file drop targets in grid/list/columns\n");
}
