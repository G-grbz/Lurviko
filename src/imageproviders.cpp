#include "imageproviders.h"

#include "bundlediconprovider.h"
#include "thumbnailcache.h"
#include "thumbnailprovider.h"

#include <QQmlEngine>

void installImageProviders(QQmlEngine &engine)
{
    engine.addImageProvider(QStringLiteral("gfilethumb"), new ThumbnailProvider);
    // Size cap and expiry for Lurviko's private preview cache; idle thread,
    // at most once a day.
    ThumbnailCache::scheduleMaintenance();
    engine.addImageProvider(QStringLiteral("systemicon"), new SystemIconProvider);
    engine.addImageProvider(QStringLiteral("bundledicon"), new BundledIconProvider);
    engine.addImageProvider(QStringLiteral("folderart"), new FolderIconProvider);
    engine.addImageProvider(QStringLiteral("fileart"), new ArtworkIconProvider);
}
