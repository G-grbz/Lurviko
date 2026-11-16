#include "filedialogintegration.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryFile>

namespace {

const QString fileChooserKey = QStringLiteral("org.freedesktop.impl.portal.FileChooser");

QString desktopName()
{
    const QString current = qEnvironmentVariable("XDG_CURRENT_DESKTOP");
    const QString first = current.split(QLatin1Char(':'), Qt::SkipEmptyParts).value(0).toLower();
    return first;
}

QString configFileName()
{
    const QString desktop = desktopName();
    return desktop.isEmpty() ? QStringLiteral("portals.conf") : desktop + QStringLiteral("-portals.conf");
}

// Where xdg-desktop-portal reads *.portal files (DATADIR unless overridden).
QString portalDirectory()
{
    const QString overridden = qEnvironmentVariable("XDG_DESKTOP_PORTAL_DIR");
    return overridden.isEmpty() ? QStringLiteral("/usr/share/xdg-desktop-portal/portals") : overridden;
}

QStringList dataDirectories()
{
    QStringList directories{QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)};
    QString system = qEnvironmentVariable("XDG_DATA_DIRS");
    if (system.isEmpty())
        system = QStringLiteral("/usr/local/share:/usr/share");
    directories += system.split(QLatin1Char(':'), Qt::SkipEmptyParts);
    return directories;
}

QString readText(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll());
}

QString keyValue(const QString &line, const QString &key)
{
    static const QRegularExpression pattern(QStringLiteral("^\\s*([^=\\s]+)\\s*=\\s*(.*)$"));
    const auto match = pattern.match(line);
    return match.hasMatch() && match.captured(1) == key ? match.captured(2).trimmed() : QString();
}

bool isKey(const QString &line, const QString &key)
{
    static const QRegularExpression pattern(QStringLiteral("^\\s*([^=\\s]+)\\s*="));
    const auto match = pattern.match(line);
    return match.hasMatch() && match.captured(1) == key;
}

} // namespace

FileDialogIntegration::FileDialogIntegration(QObject *parent)
    : QObject(parent)
{
    refresh();
}

QString FileDialogIntegration::configPath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/xdg-desktop-portal/") + configFileName();
}

QString FileDialogIntegration::portalDefinition()
{
    return QStringLiteral("[portal]\n"
                          "DBusName=org.freedesktop.impl.portal.desktop.lurviko\n"
                          "Interfaces=org.freedesktop.impl.portal.FileChooser\n");
}

QString FileDialogIntegration::createdHeader()
{
    return QStringLiteral("# Created by Lurviko (Settings → file dialogs). Delete this file to "
                          "restore the system defaults.");
}

void FileDialogIntegration::refresh()
{
    // The first service file D-Bus finds must start an existing program;
    // otherwise every Open/Save dialog would silently fail to appear.
    bool service = false;
    for (const QString &directory : dataDirectories()) {
        const QString text = readText(directory + QStringLiteral("/dbus-1/services/org.freedesktop.impl.portal.desktop.lurviko.service"));
        if (text.isEmpty())
            continue;
        for (const QString &line : text.split(QLatin1Char('\n'))) {
            const QString exec = keyValue(line, QStringLiteral("Exec"));
            if (exec.isEmpty())
                continue;
            const QString program = QProcess::splitCommand(exec).value(0);
            const QString path = QDir::isAbsolutePath(program) ? program : QStandardPaths::findExecutable(program);
            const QFileInfo info(path);
            service = info.isFile() && info.isExecutable();
        }
        break;
    }
    m_installed = service && QFileInfo::exists(portalDirectory() + QLatin1Char('/') + QLatin1String(portalFileName));

    m_desktopApps = readText(environmentScriptPath()).startsWith(createdHeader().left(20));

    m_enabled = false;
    bool inPreferred = false;
    const QStringList lines = readText(configPath()).split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1Char('[')))
            inPreferred = trimmed == QLatin1String("[preferred]");
        else if (inPreferred && isKey(line, fileChooserKey))
            m_enabled = keyValue(line, fileChooserKey).split(QLatin1Char(';')).value(0).trimmed() == QLatin1String("lurviko");
    }
    emit changed();
}

QString FileDialogIntegration::systemConfigText() const
{
    QStringList directories;
    QString configDirs = qEnvironmentVariable("XDG_CONFIG_DIRS");
    if (configDirs.isEmpty())
        configDirs = QStringLiteral("/etc/xdg");
    for (const QString &directory : configDirs.split(QLatin1Char(':'), Qt::SkipEmptyParts))
        directories << directory + QStringLiteral("/xdg-desktop-portal");
    QString dataDirs = qEnvironmentVariable("XDG_DATA_DIRS");
    if (dataDirs.isEmpty())
        dataDirs = QStringLiteral("/usr/local/share:/usr/share");
    for (const QString &directory : dataDirs.split(QLatin1Char(':'), Qt::SkipEmptyParts))
        directories << directory + QStringLiteral("/xdg-desktop-portal");
    for (const QString &name : {configFileName(), QStringLiteral("portals.conf")}) {
        for (const QString &directory : std::as_const(directories)) {
            const QString text = readText(directory + QLatin1Char('/') + name);
            if (!text.isEmpty())
                return text;
        }
    }
    const QString desktop = desktopName();
    return QStringLiteral("[preferred]\ndefault=%1\n").arg(desktop.isEmpty() ? QStringLiteral("gtk") : desktop);
}

QString FileDialogIntegration::updatedConfig(const QString &existing, bool enable, bool *deleteFile)
{
    if (deleteFile)
        *deleteFile = false;
    QStringList lines = existing.split(QLatin1Char('\n'));
    while (!lines.isEmpty() && lines.constLast().trimmed().isEmpty())
        lines.removeLast();

    int preferredStart = -1;
    int preferredEnd = lines.size();
    for (int i = 0; i < lines.size(); ++i) {
        const QString trimmed = lines.at(i).trimmed();
        if (!trimmed.startsWith(QLatin1Char('[')))
            continue;
        if (preferredStart >= 0) {
            preferredEnd = i;
            break;
        }
        if (trimmed == QLatin1String("[preferred]"))
            preferredStart = i;
    }

    QString previous;
    QString fallback;
    int keyLine = -1;
    if (preferredStart >= 0) {
        for (int i = preferredStart + 1; i < preferredEnd; ++i) {
            if (isKey(lines.at(i), fileChooserKey)) {
                previous = keyValue(lines.at(i), fileChooserKey);
                keyLine = i;
            } else if (isKey(lines.at(i), QStringLiteral("default"))) {
                fallback = keyValue(lines.at(i), QStringLiteral("default"));
            }
        }
    }
    QStringList chain = previous.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (QString &item : chain)
        item = item.trimmed();
    chain.removeAll(QStringLiteral("lurviko"));

    if (!enable) {
        if (lines.value(0) == createdHeader()) {
            if (deleteFile)
                *deleteFile = true;
            return {};
        }
        if (keyLine >= 0) {
            if (chain.isEmpty())
                lines.removeAt(keyLine);
            else
                lines[keyLine] = fileChooserKey + QLatin1Char('=') + chain.join(QLatin1Char(';'));
        }
        return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
    }

    if (chain.isEmpty() && !fallback.isEmpty())
        chain = fallback.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    chain.prepend(QStringLiteral("lurviko"));
    const QString line = fileChooserKey + QLatin1Char('=') + chain.join(QLatin1Char(';'));
    if (keyLine >= 0) {
        lines[keyLine] = line;
    } else if (preferredStart >= 0) {
        int insertAt = preferredEnd;
        while (insertAt > preferredStart + 1 && lines.at(insertAt - 1).trimmed().isEmpty())
            --insertAt;
        lines.insert(insertAt, line);
    } else {
        if (!lines.isEmpty())
            lines << QString();
        lines << QStringLiteral("[preferred]") << line;
    }
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

bool FileDialogIntegration::plasmaSession() const
{
    const QStringList desktops = qEnvironmentVariable("XDG_CURRENT_DESKTOP").split(QLatin1Char(':'));
    return desktops.contains(QStringLiteral("KDE"), Qt::CaseInsensitive);
}

QString FileDialogIntegration::environmentScriptPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/plasma-workspace/env/lurviko-file-dialogs.sh");
}

QString FileDialogIntegration::environmentScript()
{
    return createdHeader() + QStringLiteral(
        "\n# KDE/Qt and GTK apps ask the desktop portal (and with it Lurviko) for Open/Save dialogs.\n"
        "export PLASMA_INTEGRATION_USE_PORTAL=1\n"
        "export GTK_USE_PORTAL=1\n"
        "case \",${GDK_DEBUG},\" in\n"
        "    *,portals,*) ;;\n"
        "    *) export GDK_DEBUG=\"${GDK_DEBUG:+$GDK_DEBUG,}portals\" ;;\n"
        "esac\n");
}

void FileDialogIntegration::setDesktopAppsEnabled(bool enable)
{
    const QString path = environmentScriptPath();
    if (enable) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(environmentScript().toUtf8()) < 0 || !file.commit()) {
            setError(QStringLiteral("writeFailed"));
            return;
        }
    } else if (readText(path).startsWith(createdHeader().left(20)) && !QFile::remove(path)) {
        setError(QStringLiteral("writeFailed"));
        return;
    }
    m_error.clear();
    refresh();
}

void FileDialogIntegration::setEnabled(bool enable)
{
    if (enable && !m_installed) {
        setError(QStringLiteral("notInstalled"));
        return;
    }
    const QString path = configPath();
    const bool exists = QFileInfo::exists(path);
    if (!enable && !exists) {
        refresh();
        return;
    }
    QString base = exists ? readText(path) : createdHeader() + QLatin1Char('\n') + systemConfigText();
    bool remove = false;
    const QString text = updatedConfig(base, enable, &remove);
    if (remove) {
        if (!QFile::remove(path)) {
            setError(QStringLiteral("writeFailed"));
            return;
        }
    } else {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(text.toUtf8()) < 0 || !file.commit()) {
            setError(QStringLiteral("writeFailed"));
            return;
        }
    }
    m_error.clear();
    // Routing other apps to the portal only makes sense while it is Lurviko.
    if (!enable && m_desktopApps)
        setDesktopAppsEnabled(false);
    restartPortal();
    refresh();
}

void FileDialogIntegration::restartPortal()
{
    // dbus-broker reads activation files only on reload; make a reinstalled
    // service (a corrected Exec path) effective without a new login.
    QDBusConnection::sessionBus().asyncCall(QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
        QStringLiteral("org.freedesktop.DBus"), QStringLiteral("ReloadConfig")));
    // The portal reads portals.conf at startup. try-restart leaves a portal
    // that is not running alone; it starts with the new choice on demand.
    QProcess::startDetached(QStringLiteral("systemctl"),
                            {QStringLiteral("--user"), QStringLiteral("try-restart"),
                             QStringLiteral("xdg-desktop-portal.service")});
}

void FileDialogIntegration::installPortalDefinition()
{
    if (m_busy)
        return;
    // An AppImage has no package that installs the D-Bus activation file:
    // write the user's own, pointing at this AppImage.
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    if (!appImage.isEmpty() && QFileInfo(appImage).isExecutable()) {
        const QString service = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + QStringLiteral("/dbus-1/services/org.freedesktop.impl.portal.desktop.lurviko.service");
        QDir().mkpath(QFileInfo(service).absolutePath());
        QSaveFile file(service);
        const QString text = QStringLiteral("[D-BUS Service]\nName=org.freedesktop.impl.portal.desktop.lurviko\n"
                                            "Exec=\"%1\" --file-chooser-portal\n").arg(appImage);
        if (!file.open(QIODevice::WriteOnly) || file.write(text.toUtf8()) < 0 || !file.commit()) {
            setError(QStringLiteral("installFailed"));
            return;
        }
        QDBusConnection::sessionBus().asyncCall(QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
            QStringLiteral("org.freedesktop.DBus"), QStringLiteral("ReloadConfig")));
    }
    if (QFileInfo::exists(portalDirectory() + QLatin1Char('/') + QLatin1String(portalFileName))) {
        m_error.clear();
        refresh();
        return;
    }
    auto *source = new QTemporaryFile(this);
    if (!source->open() || source->write(portalDefinition().toUtf8()) < 0 || !source->flush()) {
        delete source;
        setError(QStringLiteral("installFailed"));
        return;
    }
    source->close();
    auto *process = new QProcess(this);
    m_busy = true;
    emit changed();
    connect(process, &QProcess::finished, this, [this, process, source](int code, QProcess::ExitStatus status) {
        process->deleteLater();
        source->deleteLater();
        m_busy = false;
        if (status != QProcess::NormalExit || code != 0)
            setError(QStringLiteral("installFailed"));
        else
            m_error.clear();
        refresh();
    });
    process->start(QStringLiteral("pkexec"),
                   {QStringLiteral("install"), QStringLiteral("-D"), QStringLiteral("-m"), QStringLiteral("0644"),
                    source->fileName(), portalDirectory() + QLatin1Char('/') + QLatin1String(portalFileName)});
}

void FileDialogIntegration::setError(const QString &message)
{
    m_error = message;
    emit changed();
}
