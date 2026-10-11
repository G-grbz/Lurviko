#include "trashmonitor.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QIcon>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

TrashMonitor::TrashMonitor(QObject *parent)
    : QObject(parent)
{
    m_debounceTimer.setSingleShot(true);
    m_debounceTimer.setInterval(70);
    connect(&m_debounceTimer, &QTimer::timeout, this, &TrashMonitor::refresh);

    // KIO maintains ~/.config/trashrc [Status] Empty for the global Trash
    // place. KDE's own Places implementation reads the same status, so using
    // it avoids scanning every mounted volume just to paint one sidebar icon.
    m_safetyTimer.setInterval(60000);
    connect(&m_safetyTimer, &QTimer::timeout, this, &TrashMonitor::refresh);
    m_safetyTimer.start();

    connect(&m_watcher, &QFileSystemWatcher::fileChanged,
            this, [this](const QString &) { scheduleRefresh(); });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged,
            this, [this](const QString &) { scheduleRefresh(); });

    ensureWatches();
    QTimer::singleShot(0, this, &TrashMonitor::refresh);
}

QString TrashMonitor::trashConfigPath() const
{
    const QString configHome = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    return QDir(configHome).filePath(QStringLiteral("trashrc"));
}

void TrashMonitor::ensureWatches()
{
    const QString path = trashConfigPath();
    const QString parent = QFileInfo(path).absolutePath();

    if (QFileInfo(parent).isDir() && !m_watcher.directories().contains(parent))
        m_watcher.addPath(parent);
    if (QFileInfo::exists(path) && !m_watcher.files().contains(path))
        m_watcher.addPath(path);

    const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    // Watch infrastructure too: it may be created after startup. No recursive
    // listing or periodic KIO request is needed to detect a home-trash change.
    for (const QString &directory : {data, data + QStringLiteral("/Trash"),
                                    data + QStringLiteral("/Trash/files")}) {
        if (QFileInfo(directory).isDir() && !m_watcher.directories().contains(directory))
            m_watcher.addPath(directory);
    }
    if (!QFileInfo(data).isDir()) {
        QString ancestor = QFileInfo(data).absolutePath();
        while (!QFileInfo(ancestor).isDir() && ancestor != QStringLiteral("/"))
            ancestor = QFileInfo(ancestor).absolutePath();
        if (!m_watcher.directories().contains(ancestor))
            m_watcher.addPath(ancestor);
    }
}

void TrashMonitor::scheduleRefresh()
{
    m_debounceTimer.start();
}

void TrashMonitor::refresh()
{
    ensureWatches();

    QSettings settings(trashConfigPath(), QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("Status"));
    const bool empty = settings.value(QStringLiteral("Empty"), true).toBool();
    settings.endGroup();

    const QString files = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/Trash/files");
    QDirIterator payloads(files, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    // trashrc remains authoritative for other volumes. Check just the first
    // home payload as well, so missing/stale config cannot paint a full bin empty.
    const bool hasItems = !empty || payloads.hasNext();
    if (m_hasItems != hasItems) {
        m_hasItems = hasItems;
        emit hasItemsChanged();
    }
}

QString TrashMonitor::iconName(const QString &customName, bool full) const
{
    const QString fallback = full ? QStringLiteral("user-trash-full") : QStringLiteral("user-trash");
    const QString custom = customName.trimmed();
    if (custom.isEmpty())
        return fallback;

    static const QRegularExpression standard(QStringLiteral("^user-trash(?:-full)?(.*)$"));
    const auto match = standard.match(custom);
    const QString candidate = match.hasMatch()
        ? fallback + match.captured(1)
        : (full ? custom + QStringLiteral("-full") : custom);
    // A saved empty icon (or a missing theme variant such as user-trash2)
    // must not override the actual full/empty state.
    return QIcon::hasThemeIcon(candidate) ? candidate : fallback;
}
