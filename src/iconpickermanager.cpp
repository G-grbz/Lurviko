#include "iconpickermanager.h"

#include <KIconDialog>
#include <KIconLoader>

#include <QIcon>
#include <QDir>
#include <QFileDialog>
#include <QSet>
#include <QUrl>
#include <algorithm>
#include <limits>

QString IconPickerManager::chooseSystemIcon(const QString &initialIcon, const QString &title)
{
    KIconDialog dialog;
    dialog.setup(KIconLoader::Desktop, KIconLoader::Any, false, 48, false);
    const QUrl initialUrl(initialIcon);
    if (!initialIcon.isEmpty())
        dialog.setSelectedIcon(initialUrl.isLocalFile() ? initialUrl.toLocalFile() : initialIcon);
    if (!title.isEmpty())
        dialog.setWindowTitle(title);
    // KDE emits the exact selection, including an external file's path. Do
    // not infer a theme name from the browsed file's basename.
    QString selection;
    connect(&dialog, &KIconDialog::newIconName, &dialog,
            [&selection](const QString &icon) { selection = icon; });
    const QString result = dialog.openDialog();
    return selection.isEmpty() ? result : selection;
}

QString IconPickerManager::chooseIconFile(const QString &initialIcon, const QString &title)
{
    const QUrl initialUrl(initialIcon);
    const QString initialPath = initialUrl.isLocalFile() ? initialUrl.toLocalFile() : initialIcon;
    return QFileDialog::getOpenFileName(nullptr, title,
        QDir::isAbsolutePath(initialPath) ? initialPath : QDir::homePath(),
        QStringLiteral("SVG / PNG / JPEG / WebP / ICO (*.svg *.svgz *.png *.jpg *.jpeg *.webp *.ico *.xpm)"));
}

int IconPickerManager::desktopIconSize() const
{
    const int size = KIconLoader::global()->currentSize(KIconLoader::Desktop);
    return size > 0 ? size : KIconLoader::SizeMedium;
}


QVariantList IconPickerManager::availableSystemIconSizes(const QString &iconName, int maximumSize) const
{
    const QString name = iconName.trimmed().isEmpty() ? QStringLiteral("folder") : iconName.trimmed();
    QIcon icon = QIcon::fromTheme(name);
    if (icon.isNull() && name != QStringLiteral("folder"))
        icon = QIcon::fromTheme(QStringLiteral("folder"));

    maximumSize = qBound(8, maximumSize, 512);

    QSet<int> unique;
    const QList<QSize> sizes = icon.availableSizes(QIcon::Normal, QIcon::Off);
    for (const QSize &size : sizes) {
        if (!size.isValid())
            continue;
        const int edge = qMin(size.width(), size.height());
        if (edge >= 8 && edge <= maximumSize)
            unique.insert(edge);
    }

    // Some SVG-based icon engines do not expose their fixed-directory sizes
    // through QIcon::availableSizes(). KDE themes still conventionally provide
    // these native buckets; use only as a compatibility fallback.
    if (unique.isEmpty()) {
        const int fallback[] = {16, 22, 24, 32, 48, 64, 96, 128, 256};
        for (int value : fallback) {
            if (value <= maximumSize)
                unique.insert(value);
        }
    }

    QList<int> ordered = unique.values();
    std::sort(ordered.begin(), ordered.end());
    QVariantList result;
    result.reserve(ordered.size());
    for (int value : ordered)
        result.push_back(value);
    return result;
}

int IconPickerManager::nearestSystemIconSize(const QString &iconName, int requestedSize, int maximumSize) const
{
    const QVariantList sizes = availableSystemIconSizes(iconName, maximumSize);
    if (sizes.isEmpty())
        return qMax(1, requestedSize);

    int best = sizes.first().toInt();
    int bestDistance = std::numeric_limits<int>::max();
    for (const QVariant &entry : sizes) {
        const int candidate = entry.toInt();
        const int distance = qAbs(candidate - requestedSize);
        if (distance < bestDistance || (distance == bestDistance && candidate > best)) {
            best = candidate;
            bestDistance = distance;
        }
    }
    return best;
}
