#pragma once

#include <QDBusArgument>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QFileSystemWatcher>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

class QQmlEngine;

// Wire types of the FileChooser portal (see org.freedesktop.portal.FileChooser).
struct PortalFilterPattern {
    uint type = 0;  // 0: glob, 1: MIME type
    QString pattern;
};
struct PortalFilter {
    QString label;
    QList<PortalFilterPattern> patterns;
};
struct PortalChoiceOption {
    QString id;
    QString label;
};
struct PortalChoice {
    QString id;
    QString label;
    QList<PortalChoiceOption> options;
    QString value;
};
struct PortalChoicePair {
    QString id;
    QString value;
};
Q_DECLARE_METATYPE(PortalFilterPattern)
Q_DECLARE_METATYPE(PortalFilter)
Q_DECLARE_METATYPE(PortalChoiceOption)
Q_DECLARE_METATYPE(PortalChoice)
Q_DECLARE_METATYPE(PortalChoicePair)

QDBusArgument &operator<<(QDBusArgument &argument, const PortalFilterPattern &value);
const QDBusArgument &operator>>(const QDBusArgument &argument, PortalFilterPattern &value);
QDBusArgument &operator<<(QDBusArgument &argument, const PortalFilter &value);
const QDBusArgument &operator>>(const QDBusArgument &argument, PortalFilter &value);
QDBusArgument &operator<<(QDBusArgument &argument, const PortalChoiceOption &value);
const QDBusArgument &operator>>(const QDBusArgument &argument, PortalChoiceOption &value);
QDBusArgument &operator<<(QDBusArgument &argument, const PortalChoice &value);
const QDBusArgument &operator>>(const QDBusArgument &argument, PortalChoice &value);
QDBusArgument &operator<<(QDBusArgument &argument, const PortalChoicePair &value);
const QDBusArgument &operator>>(const QDBusArgument &argument, PortalChoicePair &value);

class QWindow;

// xdg-desktop-portal FileChooser backend.
//
// xdg-desktop-portal forwards OpenFile/SaveFile/SaveFiles requests of
// sandboxed apps, browsers and (with PLASMA_INTEGRATION_USE_PORTAL=1) Qt apps
// to the backend named for org.freedesktop.impl.portal.FileChooser in
// portals.conf. Lurviko answers them with its own browser in a picker window
// (qml/FileChooserWindow.qml). The process is D-Bus activated
// (lurviko --file-chooser-portal), independent of the main window's
// single-instance process, and exits after a few idle minutes.

// One OpenFile/SaveFile/SaveFiles call, exposed to the picker window.
class FileChooserRequest final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString mode READ mode CONSTANT)              // "open", "save" or "saveFiles"
    Q_PROPERTY(QString title READ title CONSTANT)
    Q_PROPERTY(QString appId READ appId CONSTANT)
    Q_PROPERTY(QString acceptLabel READ acceptLabel CONSTANT)
    Q_PROPERTY(bool multiple READ multiple CONSTANT)
    Q_PROPERTY(bool directory READ directory CONSTANT)
    // [{label, globs: [...], mimes: [...]}]
    Q_PROPERTY(QVariantList filters READ filters CONSTANT)
    Q_PROPERTY(int currentFilter READ currentFilter CONSTANT)
    // [{id, label, options: [{id, label}], value}]; no options = checkbox.
    Q_PROPERTY(QVariantList choices READ choices CONSTANT)
    Q_PROPERTY(QString currentName READ currentName CONSTANT)
    // Where the picker starts: the caller's folder, or the folder last used
    // for this app when the caller names none (or only the home folder).
    Q_PROPERTY(QString currentFolder READ currentFolder CONSTANT)  // file:// URL or ""
    Q_PROPERTY(QString currentFile READ currentFile CONSTANT)      // file:// URL or ""
    Q_PROPERTY(QStringList files READ files CONSTANT)              // SaveFiles names
    Q_PROPERTY(bool finished READ finished NOTIFY completed)

public:
    struct Filter {
        QString label;
        QList<QPair<uint, QString>> patterns;  // 0 = glob, 1 = MIME type
    };
    struct Choice {
        QString id;
        QString label;
        QList<QPair<QString, QString>> options;
        QString value;
    };

    FileChooserRequest(const QString &mode, const QString &appId, const QString &title,
                       const QVariantMap &options, QObject *parent = nullptr);

    QString mode() const { return m_mode; }
    QString title() const { return m_title; }
    QString appId() const { return m_appId; }
    QString acceptLabel() const { return m_acceptLabel; }
    bool multiple() const { return m_multiple; }
    bool directory() const { return m_directory; }
    QVariantList filters() const;
    int currentFilter() const { return m_currentFilter; }
    QVariantList choices() const;
    QString currentName() const { return m_currentName; }
    QString currentFolder() const { return m_currentFolder; }
    QString currentFile() const { return m_currentFile; }
    QStringList files() const { return m_files; }

    // "folder" + "name" as a file:// URL; "" if the name is not a plain name.
    Q_INVOKABLE QString childUrl(const QString &folderUrl, const QString &name) const;
    Q_INVOKABLE bool exists(const QString &url) const;
    Q_INVOKABLE bool isDirectory(const QString &url) const;
    // browsedFolder (the folder shown when accepting) is remembered as the
    // start folder of the next request from the same app.
    Q_INVOKABLE void accept(const QStringList &uris, const QVariantMap &choices, int filterIndex,
                            const QString &browsedFolder = QString());
    Q_INVOKABLE void cancel();
    // Ends the request without a user decision (portal response 2).
    void abort();

    bool finished() const { return m_finished; }
    // Portal result: 0 success, 1 cancelled, 2 other.
    uint response() const { return m_response; }
    QVariantMap results() const { return m_results; }

    // Parsed from portal option values (QDBusArgument), public for tests.
    static QList<Filter> parseFilters(const QVariant &value);
    static Filter parseFilter(const QVariant &value);
    static QList<Choice> parseChoices(const QVariant &value);
    // Last accepted folder for appId (falling back to any app's), if it
    // still exists; "" otherwise. Stored in QSettings under FileChooser/.
    static QString rememberedFolder(const QString &appId);
    static void rememberFolder(const QString &appId, const QString &folderUrl);

signals:
    void completed();
    // org.freedesktop.impl.portal.Request.Close from the caller.
    void closeRequested();

private:
    void finish(uint response, const QVariantMap &results);

    QString m_mode;
    QString m_appId;
    QString m_title;
    QString m_acceptLabel;
    bool m_multiple = false;
    bool m_directory = false;
    QList<Filter> m_filters;
    int m_currentFilter = -1;
    QList<Choice> m_choices;
    QString m_currentName;
    QString m_currentFolder;
    QString m_currentFile;
    QStringList m_files;
    bool m_finished = false;
    uint m_response = 2;
    QVariantMap m_results;
};

// The org.freedesktop.impl.portal.Request object at the request handle.
class FileChooserRequestHandle final : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.impl.portal.Request")
public:
    explicit FileChooserRequestHandle(FileChooserRequest *request);
public slots:
    void Close();
private:
    QPointer<FileChooserRequest> m_request;
};

class FileChooserPortal final : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.impl.portal.FileChooser")
    Q_PROPERTY(uint version READ version CONSTANT)

public:
    static constexpr const char *serviceName = "org.freedesktop.impl.portal.desktop.lurviko";
    static constexpr const char *objectPath = "/org/freedesktop/portal/desktop";

    explicit FileChooserPortal(QQmlEngine *engine, QObject *parent = nullptr);
    uint version() const { return 4; }
    // Registers the object and claims the service name on the session bus.
    bool registerOnSessionBus();
    // Exits the process after this long without an open picker (0 = never).
    void setIdleTimeout(int milliseconds);
    // Exits (once no picker is open) when this program file is replaced, so
    // the next dialog is served by the newly installed version instead of
    // the old process until its idle timeout.
    void exitWhenReplaced(const QString &executable);
    // Opens a picker without D-Bus (tests); the window is returned.
    QWindow *open(FileChooserRequest *request, const QString &parentWindow = QString());

public slots:
    uint OpenFile(const QDBusObjectPath &handle, const QString &app_id, const QString &parent_window,
                  const QString &title, const QVariantMap &options, QVariantMap &results);
    uint SaveFile(const QDBusObjectPath &handle, const QString &app_id, const QString &parent_window,
                  const QString &title, const QVariantMap &options, QVariantMap &results);
    uint SaveFiles(const QDBusObjectPath &handle, const QString &app_id, const QString &parent_window,
                   const QString &title, const QVariantMap &options, QVariantMap &results);

private:
    uint handleRequest(const QString &mode, const QDBusObjectPath &handle, const QString &appId,
                       const QString &parentWindow, const QString &title, const QVariantMap &options);
    void updateIdle();

    QQmlEngine *m_engine = nullptr;
    int m_open = 0;
    QTimer m_idle;
    QFileSystemWatcher m_executableWatcher;
    bool m_replaced = false;
};

void registerFileChooserPortalTypes();

class QApplication;
// Entry point of "lurviko --file-chooser-portal".
int runFileChooserPortal(QApplication &app);
