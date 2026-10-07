#include "servicemenumodel.h"

#include <QAction>
#include <QIcon>
#include <QMenu>
#include <QSettings>
#include <QVariantMap>
#include <QUrl>

#include <KFileItem>
#include <KFileItemActions>
#include <KFileItemListProperties>

ServiceMenuModel::ServiceMenuModel(QObject *parent)
    : QAbstractListModel(parent)
{
    loadFavorites();
}

ServiceMenuModel::~ServiceMenuModel()
{
    m_fileItemActions.reset();
    m_menu.reset();
}

int ServiceMenuModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}


int ServiceMenuModel::favoriteCount() const
{
    int count = 0;
    for (const Entry &entry : m_entries) {
        if (m_favoriteKeys.contains(entry.key))
            ++count;
    }
    return count;
}


QVariantList ServiceMenuModel::favoriteEntries() const
{
    QVariantList result;
    for (const Entry &entry : m_entries) {
        if (!m_favoriteKeys.contains(entry.key))
            continue;
        QVariantMap item;
        item.insert(QStringLiteral("label"), entry.text);
        item.insert(QStringLiteral("actionKey"), entry.key);
        item.insert(QStringLiteral("iconName"), entry.iconName);
        result.push_back(item);
    }
    return result;
}

QVariant ServiceMenuModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};

    const Entry &entry = m_entries.at(index.row());
    switch (role) {
    case LabelRole: return entry.text;
    case IconNameRole: return entry.iconName;
    case KeyRole: return entry.key;
    case FavoriteRole: return m_favoriteKeys.contains(entry.key);
    default: return {};
    }
}

QHash<int, QByteArray> ServiceMenuModel::roleNames() const
{
    return {
        {LabelRole, "label"},
        {IconNameRole, "iconName"},
        {KeyRole, "actionKey"},
        {FavoriteRole, "favorite"}
    };
}

void ServiceMenuModel::setContext(const QString &itemUrl, const QString &mimeType)
{
    setContexts(itemUrl.isEmpty() ? QStringList{} : QStringList{itemUrl},
                itemUrl.isEmpty() ? QStringList{} : QStringList{mimeType});
}

void ServiceMenuModel::setContexts(const QStringList &itemUrls, const QStringList &mimeTypes)
{
    QStringList normalizedUrls;
    QStringList normalizedMimes;
    normalizedUrls.reserve(itemUrls.size());
    normalizedMimes.reserve(itemUrls.size());

    for (qsizetype i = 0; i < itemUrls.size(); ++i) {
        const QString value = itemUrls.at(i).trimmed();
        if (value.isEmpty())
            continue;
        normalizedUrls.push_back(value);
        normalizedMimes.push_back(i < mimeTypes.size() ? mimeTypes.at(i) : QString());
    }

    if (m_itemUrls == normalizedUrls && m_mimeTypes == normalizedMimes)
        return;

    m_itemUrls = normalizedUrls;
    m_mimeTypes = normalizedMimes;
    m_itemUrl = m_itemUrls.isEmpty() ? QString() : m_itemUrls.constFirst();
    m_mimeType = m_mimeTypes.isEmpty() ? QString() : m_mimeTypes.constFirst();
    emit contextChanged();
    rebuild();
}

void ServiceMenuModel::clear()
{
    if (m_itemUrl.isEmpty() && m_entries.isEmpty())
        return;

    beginResetModel();
    m_entries.clear();
    m_fileItemActions.reset();
    m_menu.reset();
    m_itemUrl.clear();
    m_mimeType.clear();
    m_itemUrls.clear();
    m_mimeTypes.clear();
    endResetModel();
    emit contextChanged();
    emit countChanged();
    emit favoriteCountChanged();
    emit favoriteEntriesChanged();
}

void ServiceMenuModel::trigger(int row)
{
    if (row < 0 || row >= m_entries.size())
        return;

    QAction *action = m_entries.at(row).action.data();
    if (!action || !action->isEnabled())
        return;

    action->trigger();
}

void ServiceMenuModel::triggerKey(const QString &key)
{
    for (const Entry &entry : m_entries) {
        if (entry.key != key)
            continue;
        QAction *action = entry.action.data();
        if (action && action->isEnabled())
            action->trigger();
        return;
    }
}

void ServiceMenuModel::toggleFavorite(int row)
{
    if (row < 0 || row >= m_entries.size())
        return;

    const QString key = m_entries.at(row).key;
    if (key.isEmpty())
        return;

    if (m_favoriteKeys.contains(key))
        m_favoriteKeys.remove(key);
    else
        m_favoriteKeys.insert(key);

    saveFavorites();
    const QModelIndex idx = index(row, 0);
    emit dataChanged(idx, idx, {FavoriteRole});
    emit favoriteCountChanged();
    emit favoriteEntriesChanged();
}

void ServiceMenuModel::toggleFavoriteKey(const QString &key)
{
    for (int row = 0; row < m_entries.size(); ++row) {
        if (m_entries.at(row).key != key)
            continue;
        toggleFavorite(row);
        return;
    }
}

void ServiceMenuModel::rebuild()
{
    beginResetModel();
    m_entries.clear();
    m_fileItemActions.reset();
    m_menu.reset();

    if (!m_itemUrls.isEmpty()) {
        KFileItemList items;
        items.reserve(m_itemUrls.size());
        for (qsizetype i = 0; i < m_itemUrls.size(); ++i) {
            const QUrl url = QUrl::fromUserInput(m_itemUrls.at(i));
            if (!url.isValid())
                continue;
            const QString mime = i < m_mimeTypes.size() ? m_mimeTypes.at(i) : QString();
            items.push_back(KFileItem(url, mime));
        }

        if (!items.isEmpty()) {
            // KFileItemListProperties computes the common capabilities for the
            // whole selection.  Service actions therefore only appear when the
            // selected files/folders can actually be handled together.
            KFileItemListProperties properties(items);

            m_menu = std::make_unique<QMenu>();
            m_fileItemActions = std::make_unique<KFileItemActions>(this);
            m_fileItemActions->setItemListProperties(properties);

            connect(m_fileItemActions.get(), &KFileItemActions::error,
                    this, &ServiceMenuModel::error);

            const auto sources = KFileItemActions::MenuActionSources(
                KFileItemActions::MenuActionSource::Services);
            m_fileItemActions->addActionsTo(m_menu.get(), sources);
            collectActions(m_menu->actions());
        }
    }

    endResetModel();
    emit countChanged();
    emit favoriteCountChanged();
    emit favoriteEntriesChanged();
}

void ServiceMenuModel::collectActions(const QList<QAction *> &actions, const QString &prefix)
{
    for (QAction *action : actions) {
        if (!action || !action->isVisible() || action->isSeparator())
            continue;

        const QString raw = cleanText(action->text());
        if (raw.isEmpty())
            continue;

        const QString identity = action->objectName() + QLatin1Char(' ') + action->data().toString();
        if (identity.contains(QStringLiteral("dolphin"), Qt::CaseInsensitive))
            continue;

        if (QMenu *subMenu = action->menu()) {
            const QString nextPrefix = prefix.isEmpty() ? raw : prefix + QStringLiteral(" › ") + raw;
            collectActions(subMenu->actions(), nextPrefix);
            continue;
        }

        Entry entry;
        entry.text = prefix.isEmpty() ? raw : prefix + QStringLiteral(" › ") + raw;
        entry.iconName = action->icon().name();
        entry.key = action->objectName() + QStringLiteral("|") + action->data().toString()
                    + QStringLiteral("|") + entry.text;
        entry.action = action;
        m_entries.push_back(entry);
    }
}

void ServiceMenuModel::loadFavorites()
{
    const QStringList values = QSettings().value(QStringLiteral("serviceMenus/favorites")).toStringList();
    m_favoriteKeys.clear();
    for (const QString &value : values)
        m_favoriteKeys.insert(value);
}

void ServiceMenuModel::saveFavorites() const
{
    QStringList values;
    values.reserve(m_favoriteKeys.size());
    for (const QString &value : m_favoriteKeys)
        values.push_back(value);
    values.sort(Qt::CaseInsensitive);
    QSettings().setValue(QStringLiteral("serviceMenus/favorites"), values);
}

QString ServiceMenuModel::cleanText(QString text)
{
    text.replace(QStringLiteral("&&"), QStringLiteral("\x01"));
    text.remove(QLatin1Char('&'));
    text.replace(QChar(0x01), QLatin1Char('&'));
    return text.trimmed();
}
