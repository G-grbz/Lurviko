#include "favoritesmodel.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

FavoritesModel::FavoritesModel(QObject *parent)
    : QAbstractListModel(parent)
{
    load();
}

int FavoritesModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant FavoritesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return {};
    const Favorite &item = m_items.at(index.row());
    if (role == NameRole) return item.name;
    if (role == LocationRole) return item.location;
    if (role == IconRole) return item.icon;
    return {};
}

QHash<int, QByteArray> FavoritesModel::roleNames() const
{
    return {{NameRole, "name"}, {LocationRole, "location"}, {IconRole, "favoriteIcon"}};
}

QString FavoritesModel::normalize(const QString &location)
{
    if (location.contains(QStringLiteral("://")))
        return QUrl::fromUserInput(location).toString();
    return QUrl::fromLocalFile(QDir(location).absolutePath()).toString();
}

QString FavoritesModel::displayName(const QString &location)
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

QString FavoritesModel::safeIcon(const QString &icon)
{
    const QString value = icon.trimmed();
    static const QStringList bundled = {
        QStringLiteral("folder.svg"), QStringLiteral("home-folder.svg"),
        QStringLiteral("desktop.svg"), QStringLiteral("downloads.svg"),
        QStringLiteral("documents.svg"), QStringLiteral("image.svg"),
        QStringLiteral("audio.svg"), QStringLiteral("video.svg"),
        QStringLiteral("archive.svg"), QStringLiteral("drive.svg"),
        QStringLiteral("favorite.svg"), QStringLiteral("network.svg"),
        QStringLiteral("cloud.svg"), QStringLiteral("trash.svg")
    };
    if (bundled.contains(value))
        return value;
    if (value.startsWith(QStringLiteral("system:"))) {
        static const QRegularExpression valid(QStringLiteral("^system:[A-Za-z0-9._+-]+$"));
        if (valid.match(value).hasMatch())
            return value;
    }
    return QStringLiteral("favorite.svg");
}

void FavoritesModel::addFavorite(const QString &location, const QString &name, const QString &icon)
{
    const QString normalized = normalize(location);
    if (normalized.isEmpty() || contains(normalized))
        return;
    const int row = m_items.size();
    beginInsertRows(QModelIndex(), row, row);
    m_items.push_back({name.trimmed().isEmpty() ? displayName(normalized) : name.trimmed(),
                       normalized, safeIcon(icon)});
    endInsertRows();
    save();
    emit countChanged();
}

void FavoritesModel::removeFavorite(const QString &location)
{
    const QString normalized = normalize(location);
    int found = -1;
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).location == normalized) { found = i; break; }
    }
    if (found < 0)
        return;
    beginRemoveRows(QModelIndex(), found, found);
    m_items.removeAt(found);
    endRemoveRows();
    save();
    emit countChanged();
}

bool FavoritesModel::updateFavorite(int index, const QString &name, const QString &icon)
{
    if (index < 0 || index >= m_items.size())
        return false;
    Favorite &item = m_items[index];
    item.name = name.trimmed().isEmpty() ? displayName(item.location) : name.trimmed();
    item.icon = safeIcon(icon);
    emit dataChanged(this->index(index), this->index(index), {NameRole, IconRole});
    save();
    return true;
}

bool FavoritesModel::contains(const QString &location) const
{
    const QString normalized = normalize(location);
    for (const Favorite &item : m_items) {
        if (item.location == normalized)
            return true;
    }
    return false;
}

void FavoritesModel::load()
{
    QSettings settings;
    const QVariant itemsValue = settings.value(QStringLiteral("favorites/items"));
    if (itemsValue.isValid()) {
        const QVariantList list = itemsValue.toList();
        for (const QVariant &value : list) {
            const QVariantMap map = value.toMap();
            const QString location = normalize(map.value(QStringLiteral("location")).toString());
            if (location.isEmpty())
                continue;
            m_items.push_back({map.value(QStringLiteral("name"), displayName(location)).toString(),
                               location,
                               safeIcon(map.value(QStringLiteral("icon"), QStringLiteral("favorite.svg")).toString())});
        }
        return;
    }

    const QStringList legacy = settings.value(QStringLiteral("favorites/locations")).toStringList();
    for (const QString &locationValue : legacy) {
        const QString location = normalize(locationValue);
        if (!location.isEmpty())
            m_items.push_back({displayName(location), location, QStringLiteral("favorite.svg")});
    }
    if (!legacy.isEmpty())
        save();
}

void FavoritesModel::save() const
{
    QVariantList list;
    for (const Favorite &item : m_items) {
        QVariantMap map;
        map.insert(QStringLiteral("name"), item.name);
        map.insert(QStringLiteral("location"), item.location);
        map.insert(QStringLiteral("icon"), item.icon);
        list.push_back(map);
    }
    QSettings settings;
    settings.setValue(QStringLiteral("favorites/items"), list);
}
