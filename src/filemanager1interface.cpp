#include "filemanager1interface.h"

FileManager1Interface::FileManager1Interface(QObject *parent)
    : QObject(parent)
{
}

void FileManager1Interface::ShowFolders(const QStringList &uris, const QString &startupId)
{
    emit showFoldersRequested(uris, startupId);
}

void FileManager1Interface::ShowItems(const QStringList &uris, const QString &startupId)
{
    emit showItemsRequested(uris, startupId);
}

void FileManager1Interface::ShowItemProperties(const QStringList &uris, const QString &startupId)
{
    emit showItemPropertiesRequested(uris, startupId);
}
