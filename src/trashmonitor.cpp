#include "trashmonitor.h"

#include <QDir>
#include <QFileInfo>
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

    const bool hasItems = !empty;
    if (m_hasItems != hasItems) {
        m_hasItems = hasItems;
        emit hasItemsChanged();
    }
}
