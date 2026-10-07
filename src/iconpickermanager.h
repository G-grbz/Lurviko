#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

class IconPickerManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int desktopIconSize READ desktopIconSize CONSTANT)
public:
    explicit IconPickerManager(QObject *parent = nullptr) : QObject(parent) {}

    Q_INVOKABLE QString chooseSystemIcon(const QString &initialIcon = QStringLiteral("folder"),
                                         const QString &title = QString());
    int desktopIconSize() const;
    Q_INVOKABLE QVariantList availableSystemIconSizes(const QString &iconName = QStringLiteral("folder"), int maximumSize = 512) const;
    Q_INVOKABLE int nearestSystemIconSize(const QString &iconName, int requestedSize, int maximumSize = 512) const;
};
