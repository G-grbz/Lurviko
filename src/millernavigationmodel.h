#pragma once
#include <QAbstractListModel>
#include <QPointer>
#include "directorymodel.h"

// Owns only the additional directory models. BrowserPane's original model is
// retained for Grid/List, categories, search and the first Miller column.
class MillerNavigationModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(DirectoryModel *sourceDirectory READ sourceDirectory WRITE setSourceDirectory NOTIFY sourceDirectoryChanged)
    Q_PROPERTY(DirectoryModel *activeDirectory READ activeDirectory NOTIFY activeDirectoryChanged)
    Q_PROPERTY(QString location READ location WRITE setLocation NOTIFY locationChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(int activeColumn READ activeColumn NOTIFY activeDirectoryChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    enum Role { DirectoryRole = Qt::UserRole + 1, LocationRole, SelectionRole, WidthRole };
    explicit MillerNavigationModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    DirectoryModel *sourceDirectory() const { return m_source; }
    DirectoryModel *activeDirectory() const;
    QString location() const;
    bool enabled() const { return m_enabled; }
    int activeColumn() const { return m_active; }
    void setSourceDirectory(DirectoryModel *source);
    void setLocation(const QString &location);
    void setEnabled(bool enabled);
    Q_INVOKABLE void activateColumn(int column);
    Q_INVOKABLE void selectUrl(int column, const QString &url, bool directory);
    Q_INVOKABLE void setColumnWidth(int column, int width, bool persist = false);
    Q_INVOKABLE QString selectedUrl(int column) const;
    Q_INVOKABLE DirectoryModel *directoryAt(int column) const;
    Q_INVOKABLE QString locationAt(int column) const;
    Q_INVOKABLE void refreshAll();
    Q_INVOKABLE void prepareSearch();
signals:
    void sourceDirectoryChanged();
    void activeDirectoryChanged();
    void locationChanged();
    void enabledChanged();
    void countChanged();
private:
    struct Column { QPointer<DirectoryModel> directory; QString location, selected; int width = 280; };
    static QString normalized(const QString &location);
    static QString parentOf(const QString &location);
    void resetBase(const QString &location);
    void trim(int count);
    void append(const QString &location);
    void configure(DirectoryModel *directory);
    void synchronizeOptions(DirectoryModel *changed);
    void observe(DirectoryModel *directory);
    QPointer<DirectoryModel> m_source;
    QList<Column> m_columns;
    int m_active = 0, m_defaultWidth = 280;
    bool m_enabled = false, m_updating = false;
};
