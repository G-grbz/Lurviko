#pragma once

#include <QAbstractListModel>
#include <QVector>
#include <QStringList>
#include <QVariantList>

class QuickAccessModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(QString lastLocation READ lastLocation NOTIFY lastLocationChanged)
    Q_PROPERTY(QVariantList recentLocations READ recentLocations NOTIFY recentLocationsChanged)

public:
    enum Roles {
        TitleRole = Qt::UserRole + 1,
        LocationRole,
        IconRole,
        FixedRole
    };

    explicit QuickAccessModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString lastLocation() const { return m_lastLocation; }
    QVariantList recentLocations() const;

    Q_INVOKABLE bool addShortcut(const QString &location,
                                 const QString &title = QString(),
                                 const QString &icon = QStringLiteral("folder.svg"));
    Q_INVOKABLE bool updateShortcut(int index, const QString &title,
                                    const QString &location, const QString &icon);
    Q_INVOKABLE bool removeShortcut(int index);
    Q_INVOKABLE bool moveShortcut(int from, int to);
    Q_INVOKABLE bool contains(const QString &location) const;
    Q_INVOKABLE void setLastLocation(const QString &location);
    Q_INVOKABLE bool pruneRecentLocations();

signals:
    void countChanged();
    void lastLocationChanged();
    void recentLocationsChanged();

private:
    struct Shortcut {
        QString title;
        QString location;
        QString icon;
    };

    void load();
    void save() const;
    void loadDefaults();
    static QString normalize(const QString &location);
    static QString defaultTitle(const QString &location);
    static QString safeIcon(const QString &icon);
    static bool isReachableHistoryLocation(const QString &location);
    int shortcutIndexForRow(int row) const;

    QString m_lastLocation;
    QString m_fixedTitle;
    QString m_fixedIcon = QStringLiteral("folder.svg");
    QStringList m_recentLocations;
    QVector<Shortcut> m_shortcuts;
    int m_fixedRow = 0;
};
