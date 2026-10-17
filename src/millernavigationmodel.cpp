#include "millernavigationmodel.h"
#include <QDir>
#include <QScopedValueRollback>
#include <QSettings>
#include <QUrl>

MillerNavigationModel::MillerNavigationModel(QObject *parent) : QAbstractListModel(parent)
{
    m_defaultWidth = qBound(180, QSettings().value("BrowserView/millerColumnWidth", 280).toInt(), 640);
}
int MillerNavigationModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : m_columns.size(); }
QVariant MillerNavigationModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_columns.size()) return {};
    const auto &column = m_columns[index.row()];
    switch (role) {
    case DirectoryRole: return QVariant::fromValue(column.directory.data());
    case LocationRole: return column.location;
    case SelectionRole: return column.selected;
    case WidthRole: return column.width;
    default: return {};
    }
}
QHash<int, QByteArray> MillerNavigationModel::roleNames() const
{
    return {{DirectoryRole,"columnDirectory"}, {LocationRole,"columnLocation"},
            {SelectionRole,"columnSelection"}, {WidthRole,"columnWidth"}};
}
DirectoryModel *MillerNavigationModel::activeDirectory() const { return directoryAt(m_active) ? directoryAt(m_active) : m_source.data(); }
QString MillerNavigationModel::location() const { return activeDirectory() ? activeDirectory()->location() : QString(); }
DirectoryModel *MillerNavigationModel::directoryAt(int column) const { return column >= 0 && column < m_columns.size() ? m_columns[column].directory.data() : nullptr; }
QString MillerNavigationModel::locationAt(int column) const { return column >= 0 && column < m_columns.size() ? m_columns[column].location : QString(); }
QString MillerNavigationModel::selectedUrl(int column) const { return column >= 0 && column < m_columns.size() ? m_columns[column].selected : QString(); }
QString MillerNavigationModel::normalized(const QString &location)
{
    if (location.isEmpty()) return {};
    const QUrl url = location.startsWith('/') ? QUrl::fromLocalFile(location) : QUrl(location);
    return url.adjusted(QUrl::NormalizePathSegments | QUrl::StripTrailingSlash).toString();
}
QString MillerNavigationModel::parentOf(const QString &location)
{
    QUrl url(location);
    url.setPath(QDir::cleanPath(url.path() + "/.."));
    return normalized(url.toString());
}
void MillerNavigationModel::setSourceDirectory(DirectoryModel *source)
{
    if (m_source == source) return;
    if (m_source) disconnect(m_source, nullptr, this, nullptr);
    m_source = source;
    if (source) observe(source);
    resetBase(source ? source->location() : QString());
    emit sourceDirectoryChanged();
}
void MillerNavigationModel::observe(DirectoryModel *directory)
{
    const auto options = [this, directory] { synchronizeOptions(directory); };
    connect(directory, &DirectoryModel::showHiddenChanged, this, options);
    connect(directory, &DirectoryModel::hiddenPlacementChanged, this, options);
    connect(directory, &DirectoryModel::sortModeChanged, this, options);
    connect(directory, &DirectoryModel::sortAscendingChanged, this, options);
    connect(directory, &DirectoryModel::googleAccessTokenChanged, this, options);
    connect(directory, &DirectoryModel::oneDriveAccessTokenChanged, this, options);
    connect(directory, &DirectoryModel::contentIndexSourceChanged, this, options);
    connect(directory, &DirectoryModel::folderPreviewsEnabledChanged, this, options);
    connect(directory, &DirectoryModel::heavyIoBusyChanged, this, options);
    connect(directory, &DirectoryModel::locationChanged, this, [this, directory] {
        if (m_updating) return;
        for (int i = 0; i < m_columns.size(); ++i) {
            if (m_columns[i].directory != directory) continue;
            m_columns[i].location = normalized(directory->location());
            emit dataChanged(index(i), index(i), {LocationRole});
            if (i == m_active) emit locationChanged();
            break;
        }
    });
}
void MillerNavigationModel::configure(DirectoryModel *directory)
{
    if (!m_source || directory == m_source) return;
    directory->setShowHidden(m_source->showHidden());
    directory->setHiddenPlacement(m_source->hiddenPlacement());
    directory->setSortMode(m_source->sortMode());
    directory->setSortAscending(m_source->sortAscending());
    directory->setGoogleAccessToken(m_source->googleAccessToken());
    directory->setOneDriveAccessToken(m_source->oneDriveAccessToken());
    directory->setContentIndexSource(m_source->contentIndexSource());
    directory->setFolderPreviewsEnabled(m_enabled ? false : m_source->folderPreviewsEnabled());
    directory->setHeavyIoBusy(m_source->heavyIoBusy());
}
void MillerNavigationModel::synchronizeOptions(DirectoryModel *changed)
{
    if (m_updating || !m_source) return;
    QScopedValueRollback<bool> guard(m_updating, true);
    if (changed != m_source && changed == activeDirectory()) {
        m_source->setShowHidden(changed->showHidden());
        m_source->setHiddenPlacement(changed->hiddenPlacement());
        m_source->setSortMode(changed->sortMode());
        m_source->setSortAscending(changed->sortAscending());
    }
    for (const auto &column : std::as_const(m_columns)) configure(column.directory);
}
void MillerNavigationModel::resetBase(const QString &target)
{
    const QScopedValueRollback<bool> guard(m_updating, true);
    beginResetModel();
    for (const auto &column : std::as_const(m_columns)) if (column.directory && column.directory != m_source) column.directory->deleteLater();
    m_columns.clear(); m_active = 0;
    if (m_source) {
        m_source->setLocation(target);
        m_columns.append({m_source, normalized(m_source->location()), {}, m_defaultWidth});
    }
    endResetModel();
    emit countChanged(); emit activeDirectoryChanged(); emit locationChanged();
}
void MillerNavigationModel::setEnabled(bool enabled)
{
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    // A display-mode switch must not reset selection, scroll or an active
    // search. Grid/List can use the same DirectoryModel as the active column.
    synchronizeOptions(m_source);
    emit enabledChanged();
}
void MillerNavigationModel::trim(int count)
{
    if (count >= m_columns.size()) return;
    beginRemoveRows({}, count, m_columns.size() - 1);
    while (m_columns.size() > count) {
        auto column = m_columns.takeLast();
        if (column.directory && column.directory != m_source) column.directory->deleteLater();
    }
    endRemoveRows(); emit countChanged();
}
void MillerNavigationModel::append(const QString &target)
{
    auto *directory = new DirectoryModel(this);
    { QScopedValueRollback<bool> guard(m_updating, true); configure(directory); directory->setLocation(target); }
    observe(directory);
    const int row = m_columns.size(); beginInsertRows({}, row, row);
    m_columns.append({directory, target, {}, m_defaultWidth});
    endInsertRows(); emit countChanged();
}
void MillerNavigationModel::setLocation(const QString &value)
{
    const QString target = normalized(value);
    if (!m_source || target.isEmpty() || target == normalized(location())) return;
    if (!m_enabled || target.startsWith("category:") || m_columns.isEmpty()) { resetBase(target); return; }
    for (int i = 0; i < m_columns.size(); ++i) if (m_columns[i].location == target) { activateColumn(i); return; }
    const QString parent = parentOf(target);
    for (int i = 0; i < m_columns.size(); ++i) if (m_columns[i].location == parent) {
        // Replacing a sibling can destroy the former active model. Publish
        // the final model once, without a transient parent navigation event.
        m_active = i;
        selectUrl(i, target, true);
        m_active = i + 1;
        emit activeDirectoryChanged(); emit locationChanged();
        return;
    }
    resetBase(target);
}
void MillerNavigationModel::activateColumn(int column)
{
    if (column < 0 || column >= m_columns.size() || column == m_active) return;
    m_active = column; emit activeDirectoryChanged(); emit locationChanged();
}
void MillerNavigationModel::selectUrl(int column, const QString &value, bool directory)
{
    if (!m_enabled || column < 0 || column >= m_columns.size()) return;
    if (column < m_active) activateColumn(column);
    const QString url = normalized(value);
    m_columns[column].selected = url;
    emit dataChanged(index(column), index(column), {SelectionRole});
    // Cloud item IDs are not path hierarchies. The existing directory model
    // has already identified the selected entry as a folder for us.
    if (directory && !url.isEmpty() && url != m_columns[column].location) {
        if (column + 1 < m_columns.size() && m_columns[column + 1].location == url) return;
        trim(column + 1); append(url);
    } else trim(column + 1);
}
void MillerNavigationModel::setColumnWidth(int column, int width, bool persist)
{
    if (column < 0 || column >= m_columns.size()) return;
    width = qBound(180, width, 640);
    m_columns[column].width = width;
    emit dataChanged(index(column), index(column), {WidthRole});
    if (persist) { m_defaultWidth = width; QSettings().setValue("BrowserView/millerColumnWidth", width); }
}
void MillerNavigationModel::refreshAll()
{
    for (const auto &column : std::as_const(m_columns)) if (column.directory) column.directory->requestRefresh();
}
void MillerNavigationModel::prepareSearch()
{
    // Existing tab search sessions live in the original, persistent model.
    // Temporary column models must not take ownership of those sessions.
    if (activeDirectory() != m_source) resetBase(location());
}
