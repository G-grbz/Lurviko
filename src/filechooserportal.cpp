#include "filechooserportal.h"
#include "imageproviders.h"

#include <QApplication>
#include <KWindowSystem>

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMetaType>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QSettings>
#include <QUrl>

QDBusArgument &operator<<(QDBusArgument &argument, const PortalFilterPattern &value)
{
    argument.beginStructure();
    argument << value.type << value.pattern;
    argument.endStructure();
    return argument;
}
const QDBusArgument &operator>>(const QDBusArgument &argument, PortalFilterPattern &value)
{
    argument.beginStructure();
    argument >> value.type >> value.pattern;
    argument.endStructure();
    return argument;
}
QDBusArgument &operator<<(QDBusArgument &argument, const PortalFilter &value)
{
    argument.beginStructure();
    argument << value.label << value.patterns;
    argument.endStructure();
    return argument;
}
const QDBusArgument &operator>>(const QDBusArgument &argument, PortalFilter &value)
{
    argument.beginStructure();
    argument >> value.label >> value.patterns;
    argument.endStructure();
    return argument;
}
QDBusArgument &operator<<(QDBusArgument &argument, const PortalChoiceOption &value)
{
    argument.beginStructure();
    argument << value.id << value.label;
    argument.endStructure();
    return argument;
}
const QDBusArgument &operator>>(const QDBusArgument &argument, PortalChoiceOption &value)
{
    argument.beginStructure();
    argument >> value.id >> value.label;
    argument.endStructure();
    return argument;
}
QDBusArgument &operator<<(QDBusArgument &argument, const PortalChoice &value)
{
    argument.beginStructure();
    argument << value.id << value.label << value.options << value.value;
    argument.endStructure();
    return argument;
}
const QDBusArgument &operator>>(const QDBusArgument &argument, PortalChoice &value)
{
    argument.beginStructure();
    argument >> value.id >> value.label >> value.options >> value.value;
    argument.endStructure();
    return argument;
}
QDBusArgument &operator<<(QDBusArgument &argument, const PortalChoicePair &value)
{
    argument.beginStructure();
    argument << value.id << value.value;
    argument.endStructure();
    return argument;
}
const QDBusArgument &operator>>(const QDBusArgument &argument, PortalChoicePair &value)
{
    argument.beginStructure();
    argument >> value.id >> value.value;
    argument.endStructure();
    return argument;
}

void registerFileChooserPortalTypes()
{
    static bool registered = false;
    if (registered)
        return;
    registered = true;
    qDBusRegisterMetaType<PortalFilterPattern>();
    qDBusRegisterMetaType<QList<PortalFilterPattern>>();
    qDBusRegisterMetaType<PortalFilter>();
    qDBusRegisterMetaType<QList<PortalFilter>>();
    qDBusRegisterMetaType<PortalChoiceOption>();
    qDBusRegisterMetaType<QList<PortalChoiceOption>>();
    qDBusRegisterMetaType<PortalChoice>();
    qDBusRegisterMetaType<QList<PortalChoice>>();
    qDBusRegisterMetaType<PortalChoicePair>();
    qDBusRegisterMetaType<QList<PortalChoicePair>>();
}

namespace {

template<typename T>
T castOption(const QVariant &value)
{
    if (value.userType() == qMetaTypeId<QDBusArgument>())
        return qdbus_cast<T>(value.value<QDBusArgument>());
    return value.value<T>();
}

// "ay" paths are NUL-terminated byte strings in the file system encoding.
QString urlFromPathBytes(const QVariant &value)
{
    QByteArray bytes = value.toByteArray();
    while (bytes.endsWith('\0'))
        bytes.chop(1);
    if (bytes.isEmpty())
        return {};
    return QUrl::fromLocalFile(QFile::decodeName(bytes)).toString();
}

QStringList namesFromPathBytesArray(const QVariant &value)
{
    QList<QByteArray> list;
    if (value.userType() == qMetaTypeId<QDBusArgument>()) {
        const QDBusArgument argument = value.value<QDBusArgument>();
        argument.beginArray();
        while (!argument.atEnd()) {
            QByteArray bytes;
            argument >> bytes;
            list.append(bytes);
        }
        argument.endArray();
    } else {
        list = value.value<QList<QByteArray>>();
    }
    QStringList names;
    for (QByteArray bytes : std::as_const(list)) {
        while (bytes.endsWith('\0'))
            bytes.chop(1);
        if (!bytes.isEmpty())
            names.append(QFileInfo(QFile::decodeName(bytes)).fileName());
    }
    return names;
}

FileChooserRequest::Filter fromPortal(const PortalFilter &filter)
{
    FileChooserRequest::Filter result;
    result.label = filter.label;
    for (const PortalFilterPattern &pattern : filter.patterns)
        result.patterns.append({pattern.type, pattern.pattern});
    return result;
}

PortalFilter toPortal(const FileChooserRequest::Filter &filter)
{
    PortalFilter result;
    result.label = filter.label;
    for (const auto &pattern : filter.patterns)
        result.patterns.append({pattern.first, pattern.second});
    return result;
}

bool sameFilter(const FileChooserRequest::Filter &a, const FileChooserRequest::Filter &b)
{
    return a.label == b.label && a.patterns == b.patterns;
}

} // namespace

FileChooserRequest::FileChooserRequest(const QString &mode, const QString &appId, const QString &title,
                                       const QVariantMap &options, QObject *parent)
    : QObject(parent), m_mode(mode), m_appId(appId), m_title(title)
{
    registerFileChooserPortalTypes();
    m_acceptLabel = options.value(QStringLiteral("accept_label")).toString();
    m_multiple = mode == QLatin1String("open") && options.value(QStringLiteral("multiple")).toBool();
    m_directory = mode == QLatin1String("open") && options.value(QStringLiteral("directory")).toBool();
    m_filters = parseFilters(options.value(QStringLiteral("filters")));
    if (options.contains(QStringLiteral("current_filter"))) {
        const Filter current = parseFilter(options.value(QStringLiteral("current_filter")));
        for (int i = 0; i < m_filters.size(); ++i) {
            if (sameFilter(m_filters.at(i), current)) {
                m_currentFilter = i;
                break;
            }
        }
        // The specification allows a current filter that is not in the list.
        if (m_currentFilter < 0 && !current.patterns.isEmpty()) {
            m_filters.append(current);
            m_currentFilter = m_filters.size() - 1;
        }
    }
    if (m_currentFilter < 0 && !m_filters.isEmpty())
        m_currentFilter = 0;
    m_choices = parseChoices(options.value(QStringLiteral("choices")));
    m_currentName = options.value(QStringLiteral("current_name")).toString();
    m_currentFolder = urlFromPathBytes(options.value(QStringLiteral("current_folder")));
    m_currentFile = urlFromPathBytes(options.value(QStringLiteral("current_file")));
    if (!m_currentFile.isEmpty()) {
        const QFileInfo file(QUrl(m_currentFile).toLocalFile());
        if (m_currentFolder.isEmpty())
            m_currentFolder = QUrl::fromLocalFile(file.absolutePath()).toString();
        if (m_currentName.isEmpty())
            m_currentName = file.fileName();
    }
    if (mode == QLatin1String("saveFiles"))
        m_files = namesFromPathBytesArray(options.value(QStringLiteral("files")));
    // Most callers either name no folder or fall back to the home folder
    // (Qt, Electron), which would always open the picker at the default
    // location. Those start where the user last chose a file instead; a
    // specific folder chosen by the caller is kept.
    const QString requested = m_currentFolder.isEmpty() ? QString() : QDir::cleanPath(QUrl(m_currentFolder).toLocalFile());
    if (requested.isEmpty() || requested == QDir::cleanPath(QDir::homePath())) {
        const QString remembered = rememberedFolder(m_appId);
        if (!remembered.isEmpty())
            m_currentFolder = remembered;
    }
}

namespace {
QString appFolderKey(const QString &appId)
{
    QString id = appId;
    id.replace(QLatin1Char('/'), QLatin1Char('_')).replace(QLatin1Char('\\'), QLatin1Char('_'));
    return QStringLiteral("FileChooser/appFolders/") + (id.isEmpty() ? QStringLiteral("_host") : id);
}
}

QString FileChooserRequest::rememberedFolder(const QString &appId)
{
    QSettings settings;
    for (const QString &key : {appFolderKey(appId), QStringLiteral("FileChooser/lastFolder")}) {
        const QUrl folder(settings.value(key).toString());
        if (folder.isLocalFile() && QFileInfo(folder.toLocalFile()).isDir())
            return folder.toString();
    }
    return {};
}

void FileChooserRequest::rememberFolder(const QString &appId, const QString &folderUrl)
{
    const QUrl folder(folderUrl);
    if (!folder.isLocalFile() || !QFileInfo(folder.toLocalFile()).isDir())
        return;
    const QString value = QUrl::fromLocalFile(QDir::cleanPath(folder.toLocalFile())).toString();
    QSettings settings;
    settings.setValue(appFolderKey(appId), value);
    settings.setValue(QStringLiteral("FileChooser/lastFolder"), value);
}

QList<FileChooserRequest::Filter> FileChooserRequest::parseFilters(const QVariant &value)
{
    QList<Filter> result;
    if (!value.isValid())
        return result;
    registerFileChooserPortalTypes();
    const auto filters = castOption<QList<PortalFilter>>(value);
    for (const PortalFilter &filter : filters)
        result.append(fromPortal(filter));
    return result;
}

FileChooserRequest::Filter FileChooserRequest::parseFilter(const QVariant &value)
{
    if (!value.isValid())
        return {};
    registerFileChooserPortalTypes();
    return fromPortal(castOption<PortalFilter>(value));
}

QList<FileChooserRequest::Choice> FileChooserRequest::parseChoices(const QVariant &value)
{
    QList<Choice> result;
    if (!value.isValid())
        return result;
    registerFileChooserPortalTypes();
    const auto choices = castOption<QList<PortalChoice>>(value);
    for (const PortalChoice &choice : choices) {
        Choice item;
        item.id = choice.id;
        item.label = choice.label;
        item.value = choice.value;
        for (const PortalChoiceOption &option : choice.options)
            item.options.append({option.id, option.label});
        result.append(item);
    }
    return result;
}

QVariantList FileChooserRequest::filters() const
{
    QVariantList result;
    for (const Filter &filter : m_filters) {
        QStringList globs, mimes;
        for (const auto &pattern : filter.patterns)
            (pattern.first == 1 ? mimes : globs).append(pattern.second);
        result.append(QVariantMap{{QStringLiteral("label"), filter.label},
                                  {QStringLiteral("globs"), globs},
                                  {QStringLiteral("mimes"), mimes}});
    }
    return result;
}

QVariantList FileChooserRequest::choices() const
{
    QVariantList result;
    for (const Choice &choice : m_choices) {
        QVariantList options;
        for (const auto &option : choice.options)
            options.append(QVariantMap{{QStringLiteral("id"), option.first},
                                       {QStringLiteral("label"), option.second}});
        result.append(QVariantMap{{QStringLiteral("id"), choice.id},
                                  {QStringLiteral("label"), choice.label},
                                  {QStringLiteral("options"), options},
                                  {QStringLiteral("value"), choice.value}});
    }
    return result;
}

QString FileChooserRequest::childUrl(const QString &folderUrl, const QString &name) const
{
    QString text = name.trimmed();
    if (text.isEmpty() || text == QLatin1String(".") || text == QLatin1String(".."))
        return {};
    QString path;
    if (text.startsWith(QLatin1String("~/")))
        path = QDir::homePath() + text.mid(1);
    else if (QDir::isAbsolutePath(text))
        path = text;
    else {
        const QUrl folder(folderUrl);
        if (!folder.isLocalFile())
            return {};
        path = folder.toLocalFile() + QLatin1Char('/') + text;
    }
    path = QDir::cleanPath(path);
    if (path.endsWith(QLatin1Char('/')))
        return {};
    return QUrl::fromLocalFile(path).toString();
}

bool FileChooserRequest::exists(const QString &url) const
{
    const QUrl value(url);
    return value.isLocalFile() && QFileInfo::exists(value.toLocalFile());
}

bool FileChooserRequest::isDirectory(const QString &url) const
{
    const QUrl value(url);
    return value.isLocalFile() && QFileInfo(value.toLocalFile()).isDir();
}

void FileChooserRequest::accept(const QStringList &uris, const QVariantMap &choices, int filterIndex,
                                const QString &browsedFolder)
{
    if (!m_finished)
        rememberFolder(m_appId, browsedFolder);
    QVariantMap results;
    results.insert(QStringLiteral("uris"), uris);
    if (!m_choices.isEmpty()) {
        QList<PortalChoicePair> selected;
        for (const Choice &choice : std::as_const(m_choices))
            selected.append({choice.id, choices.value(choice.id, choice.value).toString()});
        results.insert(QStringLiteral("choices"), QVariant::fromValue(selected));
    }
    if (filterIndex >= 0 && filterIndex < m_filters.size())
        results.insert(QStringLiteral("current_filter"), QVariant::fromValue(toPortal(m_filters.at(filterIndex))));
    finish(0, results);
}

void FileChooserRequest::cancel()
{
    finish(1, {});
}

void FileChooserRequest::abort()
{
    finish(2, {});
}

void FileChooserRequest::finish(uint response, const QVariantMap &results)
{
    if (m_finished)
        return;
    m_finished = true;
    m_response = response;
    m_results = results;
    emit completed();
}

FileChooserRequestHandle::FileChooserRequestHandle(FileChooserRequest *request)
    : QObject(request), m_request(request)
{
}

void FileChooserRequestHandle::Close()
{
    if (!m_request)
        return;
    emit m_request->closeRequested();
    // The caller withdrew the request: "other", not a user cancellation.
    QMetaObject::invokeMethod(m_request, [request = m_request] {
        if (request)
            request->abort();
    }, Qt::QueuedConnection);
}

FileChooserPortal::FileChooserPortal(QQmlEngine *engine, QObject *parent)
    : QObject(parent), m_engine(engine)
{
    registerFileChooserPortalTypes();
    m_idle.setSingleShot(true);
    connect(&m_idle, &QTimer::timeout, qApp, &QCoreApplication::quit);
}

bool FileChooserPortal::registerOnSessionBus()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;
    if (!bus.registerObject(QString::fromLatin1(objectPath), this,
                            QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllProperties))
        return false;
    return bus.registerService(QString::fromLatin1(serviceName));
}

void FileChooserPortal::setIdleTimeout(int milliseconds)
{
    m_idle.setInterval(milliseconds);
    updateIdle();
}

void FileChooserPortal::exitWhenReplaced(const QString &executable)
{
    if (executable.isEmpty() || !m_executableWatcher.addPath(executable))
        return;
    connect(&m_executableWatcher, &QFileSystemWatcher::fileChanged, this, [this] {
        m_replaced = true;
        updateIdle();
    });
}

void FileChooserPortal::updateIdle()
{
    if (m_replaced && m_open == 0) {
        // Release the D-Bus name now; activation starts the new version.
        QTimer::singleShot(0, qApp, &QCoreApplication::quit);
        return;
    }
    if (m_open == 0 && m_idle.interval() > 0)
        m_idle.start();
    else
        m_idle.stop();
}

uint FileChooserPortal::OpenFile(const QDBusObjectPath &handle, const QString &app_id,
                                 const QString &parent_window, const QString &title,
                                 const QVariantMap &options, QVariantMap &)
{
    return handleRequest(QStringLiteral("open"), handle, app_id, parent_window, title, options);
}

uint FileChooserPortal::SaveFile(const QDBusObjectPath &handle, const QString &app_id,
                                 const QString &parent_window, const QString &title,
                                 const QVariantMap &options, QVariantMap &)
{
    return handleRequest(QStringLiteral("save"), handle, app_id, parent_window, title, options);
}

uint FileChooserPortal::SaveFiles(const QDBusObjectPath &handle, const QString &app_id,
                                  const QString &parent_window, const QString &title,
                                  const QVariantMap &options, QVariantMap &)
{
    return handleRequest(QStringLiteral("saveFiles"), handle, app_id, parent_window, title, options);
}

uint FileChooserPortal::handleRequest(const QString &mode, const QDBusObjectPath &handle,
                                      const QString &appId, const QString &parentWindow,
                                      const QString &title, const QVariantMap &options)
{
    auto *request = new FileChooserRequest(mode, appId, title, options, this);
    QDBusConnection bus = connection();
    const QDBusMessage call = message();
    setDelayedReply(true);

    const QString handlePath = handle.path();
    if (!handlePath.isEmpty() && handlePath != QLatin1String("/"))
        bus.registerObject(handlePath, new FileChooserRequestHandle(request), QDBusConnection::ExportAllSlots);

    connect(request, &FileChooserRequest::completed, this, [bus, call, request, handlePath]() mutable {
        if (!handlePath.isEmpty() && handlePath != QLatin1String("/"))
            bus.unregisterObject(handlePath);
        bus.send(call.createReply({QVariant::fromValue(request->response()),
                                   QVariant::fromValue(request->results())}));
        request->deleteLater();
    });
    if (!open(request, parentWindow))
        request->abort();
    return 2;  // ignored: the delayed reply above carries the result
}

QWindow *FileChooserPortal::open(FileChooserRequest *request, const QString &parentWindow)
{
    QQmlComponent component(m_engine, "Lurviko.App", "FileChooserWindow");
    QObject *object = component.createWithInitialProperties(
        {{QStringLiteral("request"), QVariant::fromValue<QObject *>(request)}});
    auto *window = qobject_cast<QQuickWindow *>(object);
    if (!window) {
        qWarning().noquote() << "File chooser window failed:" << component.errorString();
        delete object;
        return nullptr;
    }
    QQmlEngine::setObjectOwnership(window, QQmlEngine::CppOwnership);

    // Keep the picker on top of (and grouped with) the calling window:
    // "x11:<hex XID>" or "wayland:<xdg-foreign handle>".
    if (parentWindow.startsWith(QLatin1String("x11:"))) {
        bool ok = false;
        const WId id = parentWindow.mid(4).toULongLong(&ok, 16);
        if (ok && id)
            KWindowSystem::setMainWindow(window, id);
    } else if (parentWindow.startsWith(QLatin1String("wayland:")) && parentWindow.size() > 8) {
        KWindowSystem::setMainWindow(window, parentWindow.mid(8));
    }

    ++m_open;
    updateIdle();
    connect(request, &FileChooserRequest::completed, window, [this, window]() {
        window->close();
        window->deleteLater();
        --m_open;
        updateIdle();
    });
    window->show();
    window->raise();
    window->requestActivate();
    return window;
}

int runFileChooserPortal(QApplication &app)
{
    // Pickers come and go; the process stays for the idle timeout below.
    app.setQuitOnLastWindowClosed(false);
    QQmlEngine engine;
    installImageProviders(engine);
    FileChooserPortal portal(&engine);
    if (!portal.registerOnSessionBus()) {
        qWarning().noquote() << "Could not claim" << FileChooserPortal::serviceName
                             << "on the session bus:" << QDBusConnection::sessionBus().lastError().message();
        return 1;
    }
    // Keep a warm process for follow-up dialogs, then free its memory.
    portal.setIdleTimeout(10 * 60 * 1000);
    portal.exitWhenReplaced(QCoreApplication::applicationFilePath());
    return app.exec();
}
