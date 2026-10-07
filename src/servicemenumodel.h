#pragma once

#include <QAbstractListModel>
#include <QAction>
#include <QList>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QVariant>
#include <memory>

class QMenu;
class KFileItemActions;

class ServiceMenuModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(QString itemUrl READ itemUrl NOTIFY contextChanged)
    Q_PROPERTY(int favoriteCount READ favoriteCount NOTIFY favoriteCountChanged)
    Q_PROPERTY(QVariantList favoriteEntries READ favoriteEntries NOTIFY favoriteEntriesChanged)

public:
    enum Roles {
        LabelRole = Qt::UserRole + 1,
        IconNameRole,
        KeyRole,
        FavoriteRole
    };

    explicit ServiceMenuModel(QObject *parent = nullptr);
    ~ServiceMenuModel() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString itemUrl() const { return m_itemUrl; }
    int favoriteCount() const;
    QVariantList favoriteEntries() const;

    Q_INVOKABLE void setContext(const QString &itemUrl, const QString &mimeType);
    Q_INVOKABLE void setContexts(const QStringList &itemUrls, const QStringList &mimeTypes);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void trigger(int row);
    Q_INVOKABLE void triggerKey(const QString &key);
    Q_INVOKABLE void toggleFavorite(int row);
    Q_INVOKABLE void toggleFavoriteKey(const QString &key);

signals:
    void countChanged();
    void favoriteCountChanged();
    void favoriteEntriesChanged();
    void contextChanged();
    void error(const QString &message);

private:
    struct Entry {
        QString text;
        QString iconName;
        QString key;
        QPointer<QAction> action;
    };

    void rebuild();
    void collectActions(const QList<QAction *> &actions, const QString &prefix = QString());
    void loadFavorites();
    void saveFavorites() const;
    static QString cleanText(QString text);

    QVector<Entry> m_entries;
    QString m_itemUrl;
    QString m_mimeType;
    QStringList m_itemUrls;
    QStringList m_mimeTypes;
    QSet<QString> m_favoriteKeys;
    std::unique_ptr<KFileItemActions> m_fileItemActions;
    std::unique_ptr<QMenu> m_menu;
};
