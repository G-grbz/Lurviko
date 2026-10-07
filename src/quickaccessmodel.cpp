#include "quickaccessmodel.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <utility>

QuickAccessModel::QuickAccessModel(QObject *parent)
    : QAbstractListModel(parent)
{
    load();
}


QVariantList QuickAccessModel::recentLocations() const
{
    QVariantList result;
    result.reserve(m_recentLocations.size());
    for (const QString &location : m_recentLocations) {
        QVariantMap entry;
        entry.insert(QStringLiteral("title"), defaultTitle(location));
        entry.insert(QStringLiteral("location"), location);
        result.push_back(entry);
    }
    return result;
}

int QuickAccessModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_shortcuts.size() + 1;
}

QVariant QuickAccessModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return {};

    if (index.row() == m_fixedRow) {
        switch (role) {
        case TitleRole: return m_fixedTitle;
        case LocationRole: return m_lastLocation;
        case IconRole: return m_fixedIcon;
        case FixedRole: return true;
        default: return {};
        }
    }

    const int shortcutIndex = shortcutIndexForRow(index.row());
    if (shortcutIndex < 0 || shortcutIndex >= m_shortcuts.size())
        return {};
    const Shortcut &shortcut = m_shortcuts.at(shortcutIndex);
    switch (role) {
    case TitleRole: return shortcut.title;
    case LocationRole: return shortcut.location;
    case IconRole: return shortcut.icon;
    case FixedRole: return false;
    default: return {};
    }
}

QHash<int, QByteArray> QuickAccessModel::roleNames() const
{
    return {
        {TitleRole, "shortcutTitle"},
        {LocationRole, "shortcutLocation"},
        {IconRole, "shortcutIcon"},
        {FixedRole, "shortcutFixed"}
    };
}

int QuickAccessModel::shortcutIndexForRow(int row) const
{
    if (row < 0 || row >= rowCount() || row == m_fixedRow)
        return -1;
    return row < m_fixedRow ? row : row - 1;
}

QString QuickAccessModel::normalize(const QString &location)
{
    const QString value = location.trimmed();
    if (value.isEmpty())
        return {};
    if (value.contains(QStringLiteral("://")))
        return QUrl::fromUserInput(value).toString();
    return QUrl::fromLocalFile(QDir(value).absolutePath()).toString();
}

QString QuickAccessModel::defaultTitle(const QString &location)
{
    const QUrl url(location);
    if (url.isLocalFile()) {
        const QString path = url.toLocalFile();
        if (path == QStringLiteral("/"))
            return QStringLiteral("Root");
        const QString name = QFileInfo(path).fileName();
        return name.isEmpty() ? path : name;
    }
    if (!url.host().isEmpty())
        return url.host();
    return url.toDisplayString();
}


bool QuickAccessModel::isReachableHistoryLocation(const QString &location)
{
    const QUrl url(location);

    // Local history entries can be checked reliably and synchronously. This
    // also removes paths that belonged to a removable disk after it has been
    // unplugged or unmounted.
    if (url.isLocalFile()) {
        const QFileInfo info(url.toLocalFile());
        return info.exists() && info.isDir();
    }

    // admin:/// mirrors a local filesystem path, so it is safe to validate it
    // the same way without requiring a privileged operation.
    if (url.scheme() == QStringLiteral("admin")) {
        const QFileInfo info(url.path());
        return info.exists() && info.isDir();
    }

    // Network/cloud reachability is intentionally not guessed here. A remote
    // endpoint can be temporarily offline, require authentication, or wake on
    // access; pruning it synchronously would destroy valid history entries.
    const QString scheme = url.scheme().toLower();
    if (scheme == QStringLiteral("smb") || scheme == QStringLiteral("sftp")
            || scheme == QStringLiteral("gdrive") || scheme == QStringLiteral("onedrive")
            || scheme == QStringLiteral("trash"))
        return true;

    return !location.trimmed().isEmpty();
}

bool QuickAccessModel::pruneRecentLocations()
{
    QStringList filtered;
    filtered.reserve(m_recentLocations.size());
    const QString homeLocation = normalize(QDir::homePath());

    for (const QString &location : std::as_const(m_recentLocations)) {
        // Home is a permanent navigation anchor, not a meaningful "last"
        // location. Never let it enter the Last Location history.
        if (location == homeLocation)
            continue;
        if (isReachableHistoryLocation(location) && !filtered.contains(location)) {
            filtered.push_back(location);
            if (filtered.size() >= 10)
                break;
        }
    }

    const bool historyChanged = filtered != m_recentLocations;
    if (historyChanged)
        m_recentLocations = filtered;

    QString nextLast = m_lastLocation;
    if (nextLast == homeLocation || !isReachableHistoryLocation(nextLast)) {
        nextLast = m_recentLocations.isEmpty() ? QString() : m_recentLocations.first();
    }

    const bool lastChanged = nextLast != m_lastLocation;
    if (lastChanged)
        m_lastLocation = nextLast;

    if (!historyChanged && !lastChanged)
        return false;

    QSettings settings;
    settings.setValue(QStringLiteral("navigation/lastLocation"), m_lastLocation);
    settings.setValue(QStringLiteral("navigation/recentLocations"), m_recentLocations);

    if (lastChanged) {
        emit dataChanged(index(m_fixedRow), index(m_fixedRow), {LocationRole});
        emit lastLocationChanged();
    }
    if (historyChanged || lastChanged)
        emit recentLocationsChanged();
    return true;
}

QString QuickAccessModel::safeIcon(const QString &icon)
{
    const QString trimmed = icon.trimmed();
    static const QStringList allowed = {
        QStringLiteral("folder.svg"), QStringLiteral("home-folder.svg"),
        QStringLiteral("desktop.svg"), QStringLiteral("downloads.svg"),
        QStringLiteral("documents.svg"), QStringLiteral("image.svg"),
        QStringLiteral("audio.svg"), QStringLiteral("video.svg"),
        QStringLiteral("archive.svg"), QStringLiteral("drive.svg"),
        QStringLiteral("favorite.svg"), QStringLiteral("network.svg"),
        QStringLiteral("cloud.svg"), QStringLiteral("trash.svg")
    };
    if (allowed.contains(trimmed))
        return trimmed;
    if (trimmed.startsWith(QStringLiteral("system:"))) {
        const QString name = trimmed.mid(7);
        static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9._+-]+$"));
        if (valid.match(name).hasMatch())
            return trimmed;
    }
    return QStringLiteral("folder.svg");
}

bool QuickAccessModel::addShortcut(const QString &location, const QString &title, const QString &icon)
{
    const QString normalized = normalize(location);
    if (normalized.isEmpty() || contains(normalized))
        return false;

    const int row = m_shortcuts.size() + 1;
    beginInsertRows(QModelIndex(), row, row);
    m_shortcuts.push_back({title.trimmed().isEmpty() ? defaultTitle(normalized) : title.trimmed(),
                           normalized, safeIcon(icon)});
    endInsertRows();
    save();
    emit countChanged();
    return true;
}

bool QuickAccessModel::updateShortcut(int index, const QString &title,
                                      const QString &location, const QString &icon)
{
    if (index < 0 || index >= rowCount())
        return false;

    if (index == m_fixedRow) {
        m_fixedTitle = title.trimmed();
        m_fixedIcon = safeIcon(icon);
        emit dataChanged(this->index(index), this->index(index), {TitleRole, IconRole});
        save();
        return true;
    }

    const int itemIndex = shortcutIndexForRow(index);
    if (itemIndex < 0)
        return false;
    const QString normalized = normalize(location);
    if (normalized.isEmpty())
        return false;
    for (int i = 0; i < m_shortcuts.size(); ++i) {
        if (i != itemIndex && m_shortcuts.at(i).location == normalized)
            return false;
    }

    Shortcut &shortcut = m_shortcuts[itemIndex];
    shortcut.title = title.trimmed().isEmpty() ? defaultTitle(normalized) : title.trimmed();
    shortcut.location = normalized;
    shortcut.icon = safeIcon(icon);
    emit dataChanged(this->index(index), this->index(index),
                     {TitleRole, LocationRole, IconRole});
    save();
    return true;
}

bool QuickAccessModel::removeShortcut(int index)
{
    if (index < 0 || index >= rowCount() || index == m_fixedRow)
        return false;
    const int itemIndex = shortcutIndexForRow(index);
    if (itemIndex < 0)
        return false;
    beginRemoveRows(QModelIndex(), index, index);
    m_shortcuts.removeAt(itemIndex);
    if (index < m_fixedRow)
        --m_fixedRow;
    endRemoveRows();
    save();
    emit countChanged();
    return true;
}

bool QuickAccessModel::moveShortcut(int from, int to)
{
    if (from < 0 || to < 0 || from >= rowCount() || to >= rowCount() || from == to)
        return false;

    QVector<int> displayOrder;
    displayOrder.reserve(rowCount());
    for (int row = 0; row < rowCount(); ++row)
        displayOrder.push_back(row == m_fixedRow ? -1 : shortcutIndexForRow(row));
    displayOrder.move(from, to);

    const QVector<Shortcut> oldShortcuts = m_shortcuts;
    QVector<Shortcut> reordered;
    reordered.reserve(oldShortcuts.size());
    int nextFixedRow = 0;
    for (int row = 0; row < displayOrder.size(); ++row) {
        const int token = displayOrder.at(row);
        if (token < 0) {
            nextFixedRow = row;
            continue;
        }
        if (token < oldShortcuts.size())
            reordered.push_back(oldShortcuts.at(token));
    }

    const int destination = to > from ? to + 1 : to;
    beginMoveRows(QModelIndex(), from, from, QModelIndex(), destination);
    m_shortcuts = reordered;
    m_fixedRow = nextFixedRow;
    endMoveRows();
    save();
    return true;
}

bool QuickAccessModel::contains(const QString &location) const
{
    const QString normalized = normalize(location);
    for (const Shortcut &shortcut : m_shortcuts) {
        if (shortcut.location == normalized)
            return true;
    }
    return false;
}

void QuickAccessModel::setLastLocation(const QString &location)
{
    if (location.startsWith(QStringLiteral("category:")))
        return;

    const QString normalized = normalize(location);
    if (normalized.isEmpty())
        return;

    // The Home directory is always available from the sidebar already.
    // Visiting it must not replace the user's meaningful Last Location or
    // appear in that location's recent-history popup.
    if (normalized == normalize(QDir::homePath()))
        return;

    // Keep stale local paths out of the list as navigation progresses.
    pruneRecentLocations();

    if (!isReachableHistoryLocation(normalized))
        return;

    const bool lastChanged = normalized != m_lastLocation;
    const bool historyChanged = m_recentLocations.isEmpty()
            || m_recentLocations.first() != normalized;

    // A location is unique in history. Revisiting it moves it to the front
    // instead of consuming another one of the ten slots.
    m_recentLocations.removeAll(normalized);
    m_recentLocations.prepend(normalized);
    while (m_recentLocations.size() > 10)
        m_recentLocations.removeLast();

    m_lastLocation = normalized;
    QSettings settings;
    settings.setValue(QStringLiteral("navigation/lastLocation"), m_lastLocation);
    settings.setValue(QStringLiteral("navigation/recentLocations"), m_recentLocations);

    if (lastChanged) {
        emit dataChanged(index(m_fixedRow), index(m_fixedRow), {LocationRole});
        emit lastLocationChanged();
    }
    if (historyChanged || lastChanged)
        emit recentLocationsChanged();
}

void QuickAccessModel::loadDefaults()
{
    const auto append = [this](QStandardPaths::StandardLocation type, const QString &title, const QString &icon) {
        const QString path = QStandardPaths::writableLocation(type);
        if (!path.isEmpty())
            m_shortcuts.push_back({title, normalize(path), icon});
    };
    append(QStandardPaths::DesktopLocation, QStringLiteral("Desktop"), QStringLiteral("desktop.svg"));
    append(QStandardPaths::DocumentsLocation, QStringLiteral("Documents"), QStringLiteral("documents.svg"));
    const QString home = QDir::homePath();
    m_shortcuts.push_back({QStringLiteral("Downloads"), normalize(QDir(home).filePath(QStringLiteral("Downloads"))), QStringLiteral("downloads.svg")});
    append(QStandardPaths::PicturesLocation, QStringLiteral("Pictures"), QStringLiteral("image.svg"));
    append(QStandardPaths::MusicLocation, QStringLiteral("Music"), QStringLiteral("audio.svg"));
    append(QStandardPaths::MoviesLocation, QStringLiteral("Videos"), QStringLiteral("video.svg"));
}

void QuickAccessModel::load()
{
    const QSettings settings;
    const int savedFixedRow = settings.value(QStringLiteral("quickAccess/fixedRow"), 0).toInt();
    m_fixedTitle = settings.value(QStringLiteral("quickAccess/fixedTitle")).toString().trimmed();
    m_fixedIcon = safeIcon(settings.value(QStringLiteral("quickAccess/fixedIcon"), QStringLiteral("folder.svg")).toString());
    m_lastLocation = normalize(settings.value(QStringLiteral("navigation/lastLocation")).toString());
    const QString homeLocation = normalize(QDir::homePath());
    if (m_lastLocation == homeLocation)
        m_lastLocation.clear();

    const QStringList storedRecent = settings.value(QStringLiteral("navigation/recentLocations")).toStringList();
    for (const QString &entry : storedRecent) {
        const QString normalized = normalize(entry);
        if (!normalized.isEmpty() && normalized != homeLocation
                && !m_recentLocations.contains(normalized))
            m_recentLocations.push_back(normalized);
        if (m_recentLocations.size() >= 10)
            break;
    }
    if (!m_lastLocation.isEmpty()) {
        m_recentLocations.removeAll(m_lastLocation);
        m_recentLocations.prepend(m_lastLocation);
        while (m_recentLocations.size() > 10)
            m_recentLocations.removeLast();
    }

    // Clean stale local history at startup as well, so a removed directory or
    // an unmounted disk never survives until the popup is opened.
    pruneRecentLocations();

    const QVariant value = settings.value(QStringLiteral("quickAccess/items"));
    if (!value.isValid()) {
        loadDefaults();
        m_fixedRow = qBound(0, savedFixedRow, static_cast<int>(m_shortcuts.size()));
        save();
        return;
    }

    const QVariantList items = value.toList();
    bool migrated = false;
    for (const QVariant &item : items) {
        const QVariantMap map = item.toMap();
        QString location = normalize(map.value(QStringLiteral("location")).toString());
        const QString title = map.value(QStringLiteral("title")).toString();
        const QString icon = safeIcon(map.value(QStringLiteral("icon")).toString());
        const QString normalizedHome = normalize(QDir::homePath());
        if (icon == QStringLiteral("downloads.svg") && location == normalizedHome) {
            location = normalize(QDir(QDir::homePath()).filePath(QStringLiteral("Downloads")));
            migrated = true;
        }
        if (!location.isEmpty())
            m_shortcuts.push_back({title, location, icon});
    }
    m_fixedRow = qBound(0, savedFixedRow, static_cast<int>(m_shortcuts.size()));
    if (migrated)
        save();
}

void QuickAccessModel::save() const
{
    QVariantList items;
    items.reserve(m_shortcuts.size());
    for (const Shortcut &shortcut : m_shortcuts) {
        QVariantMap item;
        item.insert(QStringLiteral("title"), shortcut.title);
        item.insert(QStringLiteral("location"), shortcut.location);
        item.insert(QStringLiteral("icon"), shortcut.icon);
        items.push_back(item);
    }
    QSettings settings;
    settings.setValue(QStringLiteral("quickAccess/items"), items);
    settings.setValue(QStringLiteral("quickAccess/fixedRow"), m_fixedRow);
    settings.setValue(QStringLiteral("quickAccess/fixedTitle"), m_fixedTitle);
    settings.setValue(QStringLiteral("quickAccess/fixedIcon"), m_fixedIcon);
}
