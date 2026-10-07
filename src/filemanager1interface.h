#pragma once

#include <QObject>
#include <QStringList>

class FileManager1Interface final : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.FileManager1")

public:
    explicit FileManager1Interface(QObject *parent = nullptr);

public slots:
    Q_SCRIPTABLE void ShowFolders(const QStringList &uris, const QString &startupId);
    Q_SCRIPTABLE void ShowItems(const QStringList &uris, const QString &startupId);
    Q_SCRIPTABLE void ShowItemProperties(const QStringList &uris, const QString &startupId);

signals:
    void showFoldersRequested(const QStringList &uris, const QString &startupId);
    void showItemsRequested(const QStringList &uris, const QString &startupId);
    void showItemPropertiesRequested(const QStringList &uris, const QString &startupId);
};
