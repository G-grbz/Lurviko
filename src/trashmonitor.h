#pragma once

#include <QObject>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QString>

class TrashMonitor : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool hasItems READ hasItems NOTIFY hasItemsChanged)

public:
    explicit TrashMonitor(QObject *parent = nullptr);

    bool hasItems() const { return m_hasItems; }

    Q_INVOKABLE void refresh();

signals:
    void hasItemsChanged();

private:
    QString trashConfigPath() const;
    void ensureWatches();
    void scheduleRefresh();

    QFileSystemWatcher m_watcher;
    QTimer m_debounceTimer;
    QTimer m_safetyTimer;
    bool m_hasItems = false;
};
