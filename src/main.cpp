#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QIcon>
#include <QLockFile>
#include <QMessageBox>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QQmlApplicationEngine>
#include <QUrl>
#include <QWindow>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSplashScreen>
#include <QEventLoop>
#include <QTimer>
#include <QDataStream>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusReply>
#include <QtQml>
#include <KApplicationTrader>
#include <KService>
#include <KWindowSystem>
#include <KStartupInfo>
#include <memory>
#include <unistd.h>

#include "storagemodel.h"
#include "appmigration.h"
#include "updatechecker.h"
#include "videochapterreader.h"
#include "directorymodel.h"
#include "fileoperations.h"
#include "trashmonitor.h"
#include "favoritesmodel.h"
#include "quickaccessmodel.h"
#include "contentindexmodel.h"
#include "languagemanager.h"
#include "keyboardshortcutmanager.h"
#include "cloudauthmanager.h"
#include "thumbnailprovider.h"
#include "bundlediconprovider.h"
#include "admineditmanager.h"
#include "openwithmodel.h"
#include "servicemenumodel.h"
#include "googledrivemanager.h"
#include "onedrivemanager.h"
#include "iconpickermanager.h"
#include "filepropertiesmanager.h"
#include "filemanager1interface.h"
#include "tmdbmetadatamanager.h"
#include "dlnamediamanager.h"
#include "frameratematchclient.h"
#include "mpriscontroller.h"
#include "musicmetadatamanager.h"
#include "musiclibrarymanager.h"
#include "playbackresumemanager.h"
#include "videoplayerinputmanager.h"
#include "subtitleaimanager.h"
#include "subtitlestylemanager.h"
#include "privatevaultmanager.h"
#include "popupdismissfilter.h"

static QUrl normalizeExternalUrl(const QUrl &input);

static QString launchTarget(const QString &argument)
{
    const QUrl input = QUrl::fromUserInput(argument);
    if (!input.isValid())
        return {};

    const QString localPath = input.isLocalFile() ? input.toLocalFile() : QString();
    if (!localPath.endsWith(QStringLiteral(".desktop"), Qt::CaseInsensitive))
        return normalizeExternalUrl(input).toString();

    QFile desktopFile(localPath);
    if (!desktopFile.open(QIODevice::ReadOnly | QIODevice::Text))
        return input.toString();

    const QList<QByteArray> lines = desktopFile.readAll().split('\n');
    bool desktopEntryGroup = false;
    QString execCommand;
    for (QByteArray rawLine : lines) {
        rawLine = rawLine.trimmed();
        if (rawLine.startsWith('[')) {
            desktopEntryGroup = rawLine == QByteArrayLiteral("[Desktop Entry]");
            continue;
        }
        if (!desktopEntryGroup)
            continue;
        if (rawLine.startsWith("Exec=")) {
            execCommand = QString::fromUtf8(rawLine.mid(5)).trimmed();
            continue;
        }
        if (!rawLine.startsWith("URL=") && !rawLine.startsWith("URL[$e]=")
                && !rawLine.startsWith("X-Lurviko-Target=")
                && !rawLine.startsWith("X-GFile-Target="))
            continue;

        QString target = QString::fromUtf8(rawLine.mid(rawLine.indexOf('=') + 1)).trimmed();
        target.replace(QStringLiteral("${HOME}"), QDir::homePath());
        target.replace(QStringLiteral("$HOME"), QDir::homePath());
        if (target.startsWith(QLatin1Char('~')))
            target.replace(0, 1, QDir::homePath());
        const QUrl targetUrl = QUrl::fromUserInput(target);
        if (targetUrl.isValid())
            return normalizeExternalUrl(targetUrl).toString();
    }

    const QStringList commandParts = QProcess::splitCommand(execCommand);
    if (commandParts.size() > 1
            && (QFileInfo(commandParts.constFirst()).fileName() == QStringLiteral("lurviko")
                || QFileInfo(commandParts.constFirst()).fileName() == QStringLiteral("g-file"))) {
        for (int i = 1; i < commandParts.size(); ++i) {
            if (commandParts.at(i).startsWith(QLatin1Char('%')))
                continue;
            const QUrl targetUrl = QUrl::fromUserInput(commandParts.at(i));
            if (targetUrl.isValid())
                return normalizeExternalUrl(targetUrl).toString();
        }
    }

    return normalizeExternalUrl(input).toString();
}

static QUrl normalizeExternalUrl(const QUrl &input)
{
    if (!input.isValid() || input.isEmpty())
        return input;

    if (!input.isLocalFile())
        return input;

    const QString localPath = QDir::cleanPath(input.toLocalFile());
    static const QRegularExpression kioFuseTrash(
        QStringLiteral(R"(^/run/user/\d+/kio-fuse-[^/]+/trash/?$)"));
    if (kioFuseTrash.match(localPath).hasMatch())
        return QUrl(QStringLiteral("trash:/"));

    return QUrl::fromLocalFile(localPath);
}

static QString singleInstanceServerName()
{
    return QStringLiteral("lurviko-%1").arg(static_cast<qulonglong>(geteuid()));
}

static QStringList commandLineTargets(const QStringList &arguments)
{
    QStringList targets;
    for (int i = 1; i < arguments.size(); ++i) {
        const QString argument = arguments.at(i);
        if (argument.startsWith(QLatin1Char('-')))
            continue;
        const QString target = launchTarget(argument);
        if (!target.isEmpty())
            targets.append(target);
    }
    return targets;
}

static bool isLurvikoPreferredFileManager()
{
    const KService::Ptr preferred = KApplicationTrader::preferredService(QStringLiteral("inode/directory"));
    if (!preferred)
        return false;

    const QString storageId = preferred->storageId();
    return storageId == QStringLiteral("lurviko.desktop")
        || storageId == QStringLiteral("g-file.desktop")
        || storageId == QStringLiteral("aether-files.desktop");
}

static QUrl parentLocationForItem(const QUrl &input)
{
    const QUrl normalized = normalizeExternalUrl(input);
    if (!normalized.isValid() || normalized.isEmpty())
        return {};

    if (normalized.isLocalFile()) {
        const QFileInfo info(normalized.toLocalFile());
        return QUrl::fromLocalFile(info.absoluteDir().absolutePath());
    }

    QUrl parent = normalized.adjusted(QUrl::RemoveFilename);
    if (parent.path().isEmpty())
        parent.setPath(QStringLiteral("/"));
    return parent;
}

static bool isExistingLocalFileTarget(const QString &target, QUrl *itemUrl = nullptr, QUrl *parentUrl = nullptr)
{
    const QUrl normalized = normalizeExternalUrl(QUrl::fromUserInput(target));
    if (!normalized.isValid() || !normalized.isLocalFile())
        return false;

    const QFileInfo info(normalized.toLocalFile());
    if (!info.exists() || !info.isFile())
        return false;

    if (itemUrl)
        *itemUrl = normalized;
    if (parentUrl)
        *parentUrl = QUrl::fromLocalFile(info.absoluteDir().absolutePath());
    return true;
}

static bool recentDownloadForFolder(const QString &target, QUrl *itemUrl = nullptr,
                                    QUrl *parentUrl = nullptr)
{
    const QUrl normalized = normalizeExternalUrl(QUrl::fromUserInput(target));
    if (!normalized.isValid() || !normalized.isLocalFile())
        return false;

    const QString folderPath = QDir::cleanPath(normalized.toLocalFile());
    const QString homePath = QDir::cleanPath(QDir::homePath());
    QStringList downloadPaths = {
        QDir(homePath).filePath(QStringLiteral("Downloads")),
        QDir(homePath).filePath(QStringLiteral("İndirilenler")),
        QDir(homePath).filePath(QStringLiteral("Indirilenler"))
    };
    const QString configuredDownload = QDir::cleanPath(
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    // Some user-dirs configurations use $HOME for DOWNLOAD. Treating the
    // whole home directory as a download folder would select unrelated files.
    if (!configuredDownload.isEmpty() && configuredDownload != homePath)
        downloadPaths.append(configuredDownload);

    bool isDownloadFolder = false;
    for (const QString &path : std::as_const(downloadPaths)) {
        if (folderPath == QDir::cleanPath(path)) {
            isDownloadFolder = true;
            break;
        }
    }
    if (!isDownloadFolder)
        return false;

    const QFileInfo folderInfo(folderPath);
    if (!folderInfo.exists() || !folderInfo.isDir())
        return false;

    QFileInfo newest;
    QDateTime newestActivity;
    const QFileInfoList entries = QDir(folderPath).entryInfoList(
        QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
        QDir::NoSort);
    for (const QFileInfo &entry : entries) {
        const QString lowerName = entry.fileName().toLower();
        if (entry.fileName().startsWith(QLatin1Char('.'))
                || lowerName.endsWith(QStringLiteral(".part"))
                || lowerName.endsWith(QStringLiteral(".crdownload")))
            continue;
        QDateTime activity = entry.lastModified();
        if (entry.birthTime().isValid() && (!activity.isValid() || entry.birthTime() > activity))
            activity = entry.birthTime();
        if (entry.metadataChangeTime().isValid()
                && (!activity.isValid() || entry.metadataChangeTime() > activity))
            activity = entry.metadataChangeTime();
        if (!newestActivity.isValid() || activity > newestActivity) {
            newest = entry;
            newestActivity = activity;
        }
    }

    // Folder-only KIO notifications do not carry the completed file URL.
    // Select the newest completed download while it is still recent; normal
    // folder launches later in the day keep their usual empty selection.
    if (!newest.exists() || !newestActivity.isValid()
            || newestActivity.secsTo(QDateTime::currentDateTime()) > 30 * 60)
        return false;

    if (itemUrl)
        *itemUrl = QUrl::fromLocalFile(newest.absoluteFilePath());
    if (parentUrl)
        *parentUrl = QUrl::fromLocalFile(folderPath);
    return true;
}

static bool forwardToRunningInstance(const QStringList &targets)
{
    QLocalSocket socket;
    socket.connectToServer(singleInstanceServerName(), QIODevice::WriteOnly);
    if (!socket.waitForConnected(180))
        return false;

    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream << targets;
    socket.write(payload);
    socket.flush();
    socket.waitForBytesWritten(500);
    socket.disconnectFromServer();
    return true;
}

int main(int argc, char *argv[])
{
    // Prefer Qt Multimedia's FFmpeg backend for local media playback.  It's
    // the highest-fidelity cross-codec path on Linux and avoids backend
    // differences between desktop environments. Respect an explicit user
    // override when QT_MEDIA_BACKEND is already set.
    if (qEnvironmentVariableIsEmpty("QT_MEDIA_BACKEND"))
        qputenv("QT_MEDIA_BACKEND", QByteArrayLiteral("ffmpeg"));

    // The FFmpeg backend can select a hardware decoder that produces audio
    // but a black VideoOutput on some Linux/Wayland + NVIDIA combinations,
    // especially for 4K AV1.  Prefer the reliable software-decoding path by
    // default.  Users can still override this before launch when they want to
    // opt back into a specific hardware backend (for example cuda).
    if (qEnvironmentVariableIsEmpty("QT_FFMPEG_DECODING_HW_DEVICE_TYPES"))
        qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", QByteArrayLiteral(","));
    if (qEnvironmentVariableIsEmpty("QT_DISABLE_HW_TEXTURES_CONVERSION"))
        qputenv("QT_DISABLE_HW_TEXTURES_CONVERSION", QByteArrayLiteral("1"));

    QApplication app(argc, argv);
    QFont interfaceFont = app.font();
    interfaceFont.setHintingPreference(QFont::PreferVerticalHinting);
    app.setFont(interfaceFont);
    QCoreApplication::setOrganizationName("Lurviko");
    QCoreApplication::setOrganizationDomain("lurviko.local");
    QCoreApplication::setApplicationName("Lurviko");
    QCoreApplication::setApplicationVersion(LURVIKO_VERSION);
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Lurviko"));
    QApplication::setDesktopFileName(QStringLiteral("lurviko"));
    const QIcon bundledIcon(QStringLiteral(":/qt/qml/Lurviko/App/assets/icons/logo.png"));
    QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("lurviko"), bundledIcon));

    const QString configRoot = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    const bool migrationOnly = app.arguments().contains(QStringLiteral("--migrate-only"));
    // An old build must be closed before moving the files it can still write.
    QLocalSocket legacyInstance;
    legacyInstance.connectToServer(QStringLiteral("g-file-%1").arg(static_cast<qulonglong>(geteuid())));
    if (legacyInstance.waitForConnected(30)) {
        const QString message = QStringLiteral("Please close g-File before starting Lurviko so your settings can be migrated safely.");
        if (migrationOnly) qCritical().noquote() << message;
        else QMessageBox::warning(nullptr, QStringLiteral("Lurviko"), message);
        return 1;
    }
    QDir().mkpath(configRoot);
    QLockFile migrationLock(QDir(configRoot).filePath(QStringLiteral("lurviko-migration.lock")));
    if (!migrationLock.tryLock(10000)) {
        QMessageBox::warning(nullptr, QStringLiteral("Lurviko"), QStringLiteral("Another Lurviko process is migrating your settings. Please try again shortly."));
        return 1;
    }
    QString migrationError;
    if (!migrateLegacyUserData(configRoot,
            QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation),
            QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation), &migrationError)) {
        if (migrationOnly) qCritical().noquote() << migrationError;
        else QMessageBox::warning(nullptr, QStringLiteral("Lurviko"), migrationError);
        return 1;
    }
    migrationLock.unlock();
    if (migrationOnly) return 0;

    // Native glyph rasterization is noticeably sharper for the small UI labels
    // used by Lurviko on fractional-scale Plasma desktops.
    QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);
    QQuickStyle::setStyle("Basic");
    // Lurviko is a normal desktop application: closing its last main window
    // must terminate the process rather than leaving the single-instance
    // socket/DBus service alive in the background.
    app.setQuitOnLastWindowClosed(true);

    qmlRegisterType<StorageModel>("Lurviko.Backend", 1, 0, "StorageModel");
    qmlRegisterType<UpdateChecker>("Lurviko.Backend", 1, 0, "UpdateChecker");
    qmlRegisterType<VideoChapterReader>("Lurviko.Backend", 1, 0, "VideoChapterReader");
    qmlRegisterType<DirectoryModel>("Lurviko.Backend", 1, 0, "DirectoryModel");
    qmlRegisterType<FileOperations>("Lurviko.Backend", 1, 0, "FileOperations");
    qmlRegisterType<TrashMonitor>("Lurviko.Backend", 1, 0, "TrashMonitor");
    qmlRegisterType<FavoritesModel>("Lurviko.Backend", 1, 0, "FavoritesModel");
    qmlRegisterType<QuickAccessModel>("Lurviko.Backend", 1, 0, "QuickAccessModel");
    qmlRegisterType<ContentIndexModel>("Lurviko.Backend", 1, 0, "ContentIndexModel");
    qmlRegisterType<LanguageManager>("Lurviko.Backend", 1, 0, "LanguageManager");
    qmlRegisterType<CloudAuthManager>("Lurviko.Backend", 1, 0, "CloudAuthManager");
    qmlRegisterType<AdminEditManager>("Lurviko.Backend", 1, 0, "AdminEditManager");
    qmlRegisterType<OpenWithModel>("Lurviko.Backend", 1, 0, "OpenWithModel");
    qmlRegisterType<ServiceMenuModel>("Lurviko.Backend", 1, 0, "ServiceMenuModel");
    qmlRegisterType<GoogleDriveManager>("Lurviko.Backend", 1, 0, "GoogleDriveManager");
    qmlRegisterType<OneDriveManager>("Lurviko.Backend", 1, 0, "OneDriveManager");
    qmlRegisterType<IconPickerManager>("Lurviko.Backend", 1, 0, "IconPickerManager");
    qmlRegisterType<FilePropertiesManager>("Lurviko.Backend", 1, 0, "FilePropertiesManager");
    qmlRegisterType<TmdbMetadataManager>("Lurviko.Backend", 1, 0, "TmdbMetadataManager");
    qmlRegisterType<DlnaMediaManager>("Lurviko.Backend", 1, 0, "DlnaMediaManager");
    qmlRegisterType<FrameRateMatchClient>("Lurviko.Backend", 1, 0, "FrameRateMatchClient");
    qmlRegisterType<MprisController>("Lurviko.Backend", 1, 0, "MprisController");
    qmlRegisterType<MusicMetadataManager>("Lurviko.Backend", 1, 0, "MusicMetadataManager");
    qmlRegisterType<MusicLibraryManager>("Lurviko.Backend", 1, 0, "MusicLibraryManager");
    qmlRegisterType<SubtitleAiManager>("Lurviko.Backend", 1, 0, "SubtitleAiManager");
    qmlRegisterType<SubtitleStyleManager>("Lurviko.Backend", 1, 0, "SubtitleStyleManager");
    qmlRegisterType<PrivateVaultManager>("Lurviko.Backend", 1, 0, "PrivateVaultManager");
    qmlRegisterType<PopupDismissFilter>("Lurviko.Backend", 1, 0, "PopupDismissFilter");
    qmlRegisterSingletonType<KeyboardShortcutManager>("Lurviko.Backend", 1, 0, "KeyboardShortcuts",
        [](QQmlEngine *, QJSEngine *) -> QObject * { return new KeyboardShortcutManager; });

    PlaybackResumeManager playbackResumeManager;
    qmlRegisterSingletonInstance("Lurviko.Backend", 1, 0, "PlaybackResumeManager", &playbackResumeManager);
    VideoPlayerInputManager videoPlayerInputManager;
    qmlRegisterSingletonInstance("Lurviko.Backend", 1, 0, "VideoPlayerInputManager", &videoPlayerInputManager);

    const QStringList launchLocations = commandLineTargets(app.arguments());
    QString initialLaunchLocation = launchLocations.value(0);
    QString initialRevealItem;
    if (!initialLaunchLocation.isEmpty()) {
        QUrl initialItemUrl;
        QUrl initialParentUrl;
        if (isExistingLocalFileTarget(initialLaunchLocation, &initialItemUrl, &initialParentUrl)
                || recentDownloadForFolder(initialLaunchLocation, &initialItemUrl, &initialParentUrl)) {
            initialRevealItem = initialItemUrl.toString();
            initialLaunchLocation = initialParentUrl.toString();
        }
    }
    const bool activatedForFileManager = app.arguments().contains(QStringLiteral("--filemanager1"));

    // One Lurviko process per user. Plasma/file-association launches are
    // forwarded to the existing window instead of creating a second window.
    if (forwardToRunningInstance(launchLocations))
        return 0;

    QLocalServer singleInstanceServer;
    singleInstanceServer.setSocketOptions(QLocalServer::UserAccessOption);
    if (!singleInstanceServer.listen(singleInstanceServerName())) {
        // Cover the small race where another Lurviko started between our first
        // connect attempt and listen(). If it is a stale socket, remove it.
        if (forwardToRunningInstance(launchLocations))
            return 0;
        QLocalServer::removeServer(singleInstanceServerName());
        if (!singleInstanceServer.listen(singleInstanceServerName()))
            qWarning() << "Could not create Lurviko single-instance socket:"
                       << singleInstanceServer.errorString();
    }

    // Cold starts requested by a browser, KFind or KIO must acknowledge the
    // click immediately. Show a lightweight native card before the QML scene
    // and its models are constructed.
    std::unique_ptr<QSplashScreen> startupSplash;
    if (!launchLocations.isEmpty() || activatedForFileManager) {
        QPixmap canvas(420, 176);
        canvas.fill(Qt::transparent);
        QPainter painter(&canvas);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const bool dark = app.palette().color(QPalette::Window).lightness() < 128;
        painter.setPen(QPen(dark ? QColor(QStringLiteral("#41495B"))
                                : QColor(QStringLiteral("#D6DBE7")), 1));
        painter.setBrush(dark ? QColor(QStringLiteral("#171B24"))
                              : QColor(QStringLiteral("#F8F9FC")));
        painter.drawRoundedRect(QRectF(1, 1, 418, 174), 22, 22);

        const QPixmap logo(QStringLiteral(":/qt/qml/Lurviko/App/assets/icons/logo.png"));
        if (!logo.isNull()) {
            const QPixmap scaled = logo.scaled(82, 82, Qt::KeepAspectRatio,
                                               Qt::SmoothTransformation);
            painter.drawPixmap(QRect(28, 47, 82, 82), scaled,
                               QRect(QPoint(0, 0), scaled.size()));
        }
        painter.setPen(dark ? QColor(QStringLiteral("#F2F4FA"))
                            : QColor(QStringLiteral("#172033")));
        QFont titleFont = app.font();
        titleFont.setPointSizeF(titleFont.pointSizeF() + 5.0);
        titleFont.setWeight(QFont::DemiBold);
        painter.setFont(titleFont);
        painter.drawText(QRect(132, 49, 250, 42), Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("Lurviko"));
        painter.setPen(dark ? QColor(QStringLiteral("#AEB8CE"))
                            : QColor(QStringLiteral("#667189")));
        QFont detailFont = app.font();
        detailFont.setPointSizeF(detailFont.pointSizeF() + 0.5);
        painter.setFont(detailFont);
        painter.drawText(QRect(132, 88, 250, 32), Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("Dizin açılıyor…"));
        painter.end();

        startupSplash = std::make_unique<QSplashScreen>(canvas);
        startupSplash->show();
        app.processEvents(QEventLoop::ExcludeUserInputEvents);
    }

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("gfilethumb"), new ThumbnailProvider);
    engine.addImageProvider(QStringLiteral("systemicon"), new SystemIconProvider);
    engine.addImageProvider(QStringLiteral("bundledicon"), new BundledIconProvider);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [] { QCoreApplication::exit(-1); },
                     Qt::QueuedConnection);

    engine.setInitialProperties({
        {QStringLiteral("launchLocation"), initialLaunchLocation},
        {QStringLiteral("fileManagerActivation"), activatedForFileManager}
    });
    engine.loadFromModule("Lurviko.App", "Main");

    QObject *rootObject = engine.rootObjects().isEmpty() ? nullptr : engine.rootObjects().constFirst();
    QWindow *window = qobject_cast<QWindow *>(rootObject);
    if (window)
        window->setIcon(QApplication::windowIcon());
    if (startupSplash) {
        startupSplash->close();
    }
    if (rootObject && !initialRevealItem.isEmpty()) {
        QTimer::singleShot(0, rootObject, [rootObject, initialLaunchLocation, initialRevealItem]() {
            QMetaObject::invokeMethod(rootObject, "handleInitialExternalReveal", Qt::QueuedConnection,
                                      Q_ARG(QVariant, QVariant(initialLaunchLocation)),
                                      Q_ARG(QVariant, QVariant(initialRevealItem)));
        });
    }

    // Keep track of the last stable presentation mode. Re-activating an
    // already-visible fullscreen/maximized QWindow must not call show(), as
    // KWin may interpret that as a request to restore the normal geometry.
    auto lastStableVisibility = std::make_shared<QWindow::Visibility>(
        window ? window->visibility() : QWindow::Windowed);
    if (window) {
        QObject::connect(window, &QWindow::visibilityChanged, &app,
                         [lastStableVisibility](QWindow::Visibility visibility) {
            if (visibility == QWindow::Windowed
                    || visibility == QWindow::Maximized
                    || visibility == QWindow::FullScreen) {
                *lastStableVisibility = visibility;
            }
        });
    }

    auto activateExistingWindow = [window, lastStableVisibility](const QString &startupId = QString()) {
        if (!window)
            return;

        // FileManager1 carries the compositor/user-activation context in
        // startupId.  Reuse it before asking Plasma/KWin to activate a reused
        // window; this is especially important on Wayland where an arbitrary
        // raise() is intentionally not allowed to steal focus.
        if (!startupId.isEmpty()) {
            KStartupInfo::setNewStartupId(window, startupId.toUtf8());
            KWindowSystem::setCurrentXdgActivationToken(startupId);
        }

        const QWindow::Visibility visibility = window->visibility();
        if (visibility == QWindow::Minimized || !window->isVisible()) {
            QWindow::Visibility restoreVisibility = *lastStableVisibility;
            if (restoreVisibility != QWindow::FullScreen
                    && restoreVisibility != QWindow::Maximized
                    && restoreVisibility != QWindow::Windowed) {
                restoreVisibility = QWindow::Windowed;
            }
            window->setVisibility(restoreVisibility);
        }

        auto requestActivation = [window]() {
            if (!window)
                return;
            window->raise();
            KWindowSystem::activateWindow(window);
            window->requestActivate();
        };

        requestActivation();
        // Restoring a minimized Wayland surface and activating it in the same
        // event turn can be ignored by the compositor. Retry after the surface
        // has been mapped without changing the user's maximized/fullscreen mode.
        QTimer::singleShot(0, window, requestActivation);
        QTimer::singleShot(120, window, requestActivation);
    };

    auto dispatchLocations = [rootObject, activateExistingWindow](const QStringList &locations,
                                                                   const QString &startupId = QString()) {
        activateExistingWindow(startupId);
        if (!rootObject)
            return;
        for (const QString &location : locations) {
            QUrl itemUrl;
            QUrl parentUrl;
            if (isExistingLocalFileTarget(location, &itemUrl, &parentUrl)
                    || recentDownloadForFolder(location, &itemUrl, &parentUrl)) {
                QMetaObject::invokeMethod(rootObject, "handleExternalReveal", Qt::QueuedConnection,
                                          Q_ARG(QVariant, QVariant(parentUrl.toString())),
                                          Q_ARG(QVariant, QVariant(itemUrl.toString())));
            } else {
                QMetaObject::invokeMethod(rootObject, "handleExternalOpen", Qt::QueuedConnection,
                                          Q_ARG(QVariant, QVariant(location)));
            }
        }
    };

    auto dispatchRevealItems = [rootObject, activateExistingWindow](const QStringList &uris,
                                                                     const QString &startupId = QString()) {
        activateExistingWindow(startupId);
        if (!rootObject)
            return;
        for (const QString &uri : uris) {
            const QUrl itemUrl = normalizeExternalUrl(QUrl::fromUserInput(uri));
            const QUrl parentUrl = parentLocationForItem(itemUrl);
            if (!itemUrl.isValid() || parentUrl.isEmpty())
                continue;
            QMetaObject::invokeMethod(rootObject, "handleExternalReveal", Qt::QueuedConnection,
                                      Q_ARG(QVariant, QVariant(parentUrl.toString())),
                                      Q_ARG(QVariant, QVariant(itemUrl.toString())));
        }
    };

    FileManager1Interface fileManager1;
    QObject::connect(&fileManager1, &FileManager1Interface::showFoldersRequested,
                     &app, dispatchLocations);
    QObject::connect(&fileManager1, &FileManager1Interface::showItemsRequested,
                     &app, dispatchRevealItems);
    QObject::connect(&fileManager1, &FileManager1Interface::showItemPropertiesRequested,
                     &app, dispatchRevealItems);

    QDBusConnection sessionBus = QDBusConnection::sessionBus();
    const QString fileManagerService = QStringLiteral("org.freedesktop.FileManager1");
    const bool preferredFileManager = isLurvikoPreferredFileManager();
    if (sessionBus.isConnected() && (activatedForFileManager || preferredFileManager)) {
        const bool objectRegistered = sessionBus.registerObject(
            QStringLiteral("/org/freedesktop/FileManager1"), &fileManager1,
            QDBusConnection::ExportScriptableSlots);
        if (!objectRegistered)
            qWarning() << "Could not register org.freedesktop.FileManager1 object" << sessionBus.lastError();

        if (QDBusConnectionInterface *busInterface = sessionBus.interface()) {
            const auto queueOption = preferredFileManager
                ? QDBusConnectionInterface::ReplaceExistingService
                : QDBusConnectionInterface::DontQueueService;
            QDBusReply<QDBusConnectionInterface::RegisterServiceReply> reply =
                busInterface->registerService(fileManagerService, queueOption,
                                              QDBusConnectionInterface::AllowReplacement);
            if (!reply.isValid() || reply.value() != QDBusConnectionInterface::ServiceRegistered) {
                // If an existing daemon refuses replacement, queue behind it so
                // Lurviko acquires FileManager1 as soon as that stale owner exits.
                if (preferredFileManager) {
                    reply = busInterface->registerService(fileManagerService,
                                                          QDBusConnectionInterface::QueueService,
                                                          QDBusConnectionInterface::AllowReplacement);
                }
                if (!reply.isValid())
                    qWarning() << "Could not claim org.freedesktop.FileManager1:" << reply.error().message();
            }
        }
    }

    auto acceptPendingConnections = [&]() {
        while (QLocalSocket *socket = singleInstanceServer.nextPendingConnection()) {
            auto buffer = std::make_shared<QByteArray>();
            auto handled = std::make_shared<bool>(false);
            auto consumeRequest = [socket, buffer, handled, dispatchLocations]() {
                if (*handled)
                    return;
                buffer->append(socket->readAll());
                QDataStream stream(*buffer);
                stream.startTransaction();
                QStringList locations;
                stream >> locations;
                if (!stream.commitTransaction())
                    return;
                *handled = true;
                dispatchLocations(locations);
                socket->disconnectFromServer();
            };
            QObject::connect(socket, &QLocalSocket::readyRead, socket, consumeRequest);
            QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            // A request can arrive while QML is still loading, before the
            // newConnection signal is connected. Consume any queued bytes now.
            if (socket->bytesAvailable() > 0)
                consumeRequest();
        }
    };
    QObject::connect(&singleInstanceServer, &QLocalServer::newConnection, &app, acceptPendingConnections);
    acceptPendingConnections();

    // When the very first invocation contained multiple URLs, keep the first
    // one as the initial browser and open the rest as tabs after QML is ready.
    for (int i = 1; i < launchLocations.size(); ++i) {
        const QString target = launchLocations.at(i);
        QUrl itemUrl;
        QUrl parentUrl;
        if (isExistingLocalFileTarget(target, &itemUrl, &parentUrl)
                || recentDownloadForFolder(target, &itemUrl, &parentUrl)) {
            QMetaObject::invokeMethod(rootObject, "handleExternalReveal", Qt::QueuedConnection,
                                      Q_ARG(QVariant, QVariant(parentUrl.toString())),
                                      Q_ARG(QVariant, QVariant(itemUrl.toString())));
        } else {
            QMetaObject::invokeMethod(rootObject, "handleExternalOpen", Qt::QueuedConnection,
                                      Q_ARG(QVariant, QVariant(target)));
        }
    }

    return app.exec();
}
