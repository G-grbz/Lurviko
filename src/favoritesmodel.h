#pragma once

#include <QAbstractListModel>
#include <QVector>

class FavoritesModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles { NameRole = Qt::UserRole + 1, LocationRole, IconRole };

    explicit FavoritesModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void addFavorite(const QString &location,
                                 const QString &name = QString(),
                                 const QString &icon = QStringLiteral("favorite.svg"));
    Q_INVOKABLE void removeFavorite(const QString &location);
    Q_INVOKABLE bool updateFavorite(int index, const QString &name, const QString &icon);
    Q_INVOKABLE bool contains(const QString &location) const;

signals:
    void countChanged();

private:
    struct Favorite {
        QString name;
        QString location;
        QString icon;
    };

    void load();
    void save() const;
    static QString normalize(const QString &location);
    static QString displayName(const QString &location);
    static QString safeIcon(const QString &icon);

    QVector<Favorite> m_items;
};
