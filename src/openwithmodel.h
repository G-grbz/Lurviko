#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVector>
#include <QVariantList>
#include <QStringList>

class OpenWithModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(QString mimeType READ mimeType NOTIFY contextChanged)
    Q_PROPERTY(QString itemUrl READ itemUrl NOTIFY contextChanged)
    Q_PROPERTY(QString defaultDesktopEntry READ defaultDesktopEntry NOTIFY defaultChanged)
    Q_PROPERTY(QVariantList quickApps READ quickApps NOTIFY quickAppsChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        IconNameRole,
        DesktopEntryRole,
        IsDefaultRole
    };

    explicit OpenWithModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString mimeType() const { return m_mimeType; }
    QString itemUrl() const { return m_itemUrl; }
    QString defaultDesktopEntry() const { return m_defaultDesktopEntry; }
    QVariantList quickApps() const { return m_quickApps; }

    Q_INVOKABLE void setContext(const QString &itemUrl, const QString &mimeType);
    Q_INVOKABLE void setContextMany(const QStringList &itemUrls);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void openWith(const QString &desktopEntry);
    Q_INVOKABLE void openDefault(const QString &itemUrl, const QString &mimeType = QString());
    Q_INVOKABLE void openDefaults(const QStringList &itemUrls);
    Q_INVOKABLE bool setDefaultApplication(const QString &desktopEntry);
    Q_INVOKABLE QString nameForDesktopEntry(const QString &desktopEntry) const;
    Q_INVOKABLE int indexForDesktopEntry(const QString &desktopEntry) const;

signals:
    void countChanged();
    void contextChanged();
    void defaultChanged();
    void quickAppsChanged();
    void error(const QString &message);

private:
    struct AppEntry {
        QString name;
        QString iconName;
        QString desktopEntry;
        bool isDefault = false;
    };

    QVector<AppEntry> m_apps;
    QString m_mimeType;
    QString m_itemUrl;
    QStringList m_itemUrls;
    QString m_defaultDesktopEntry;
    QVariantList m_quickApps;

    void rebuildQuickApps();
    void rememberApplication(const QString &desktopEntry);
};
