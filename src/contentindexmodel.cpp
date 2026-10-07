#include "contentindexmodel.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QPointer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QUrl>
#include <QThread>
#include <QVariantMap>
#include <QtConcurrent>

#include <algorithm>
#include <utility>

namespace {
constexpr int kIndexVersion = 8;
constexpr int kMaxWatchedDirectories = 4096;
constexpr qint64 kMinimumImageBytes = 24 * 1024;
constexpr qint64 kMinimumVideoBytes = 1024 * 1024;

const QStringList &allCategoryKeys()
{
    static const QStringList keys = {
        QStringLiteral("documents"), QStringLiteral("appimage"), QStringLiteral("archives"),
        QStringLiteral("images"), QStringLiteral("music"), QStringLiteral("videos")
    };
    return keys;
}

QStringList defaultIgnoreRulesForCategory(const QString &category)
{
    QStringList rules = {
        QStringLiteral("path:.git"), QStringLiteral("path:node_modules"),
        QStringLiteral("path:__pycache__"), QStringLiteral("path:.venv"),
        QStringLiteral("path:venv"), QStringLiteral("path:python-venv"),
        QStringLiteral("path:.tooling")
    };
    if (category == QStringLiteral("images") || category == QStringLiteral("videos")) {
        rules.append({
            QStringLiteral("path:assets"), QStringLiteral("path:asset"), QStringLiteral("path:public"),
            QStringLiteral("path:static"), QStringLiteral("path:resources"), QStringLiteral("path:res"),
            QStringLiteral("path:src"), QStringLiteral("path:dist"), QStringLiteral("path:build"),
            QStringLiteral("path:generated"), QStringLiteral("path:metadata"),
            QStringLiteral("path:vendor"), QStringLiteral("path:wwwroot"), QStringLiteral("path:icons"),
            QStringLiteral("path:sprites"), QStringLiteral("path:avatar"), QStringLiteral("path:avatars"),
            QStringLiteral("path:site-packages"), QStringLiteral("path:appdir"), QStringLiteral("path:skins"),
            QStringLiteral("path:visualizations"), QStringLiteral("path:qml"), QStringLiteral("path:ui"),
            QStringLiteral("path:img"), QStringLiteral("path:docs"), QStringLiteral("path:music-artwork"),
            QStringLiteral("path:manual-artwork"), QStringLiteral("path:*thumbnail*"), QStringLiteral("path:*thumb*"),
            QStringLiteral("path:*cache*"), QStringLiteral("path:*preview*")
        });
    }
    if (category == QStringLiteral("archives")) {
        rules.append({
            QStringLiteral("path:/@"), QStringLiteral("path:npm"),
            QStringLiteral("path:.yarn/cache"), QStringLiteral("path:.pub-cache")
        });
    }
    return rules;
}

bool isKnownExtensionRule(const QString &category, const QString &rule)
{
    static const QHash<QString, QSet<QString>> extensions = {
        {QStringLiteral("documents"), {QStringLiteral("pdf"), QStringLiteral("txt"), QStringLiteral("rtf"),
            QStringLiteral("odt"), QStringLiteral("ods"), QStringLiteral("odp"), QStringLiteral("doc"),
            QStringLiteral("docx"), QStringLiteral("xls"), QStringLiteral("xlsx"), QStringLiteral("ppt"),
            QStringLiteral("pptx"), QStringLiteral("csv"), QStringLiteral("tsv"), QStringLiteral("epub"),
            QStringLiteral("mobi"), QStringLiteral("pages"), QStringLiteral("numbers"), QStringLiteral("key"),
            QStringLiteral("djvu"), QStringLiteral("xps")}},
        {QStringLiteral("images"), {QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
            QStringLiteral("webp"), QStringLiteral("gif"), QStringLiteral("bmp"), QStringLiteral("tif"),
            QStringLiteral("tiff"), QStringLiteral("svg"), QStringLiteral("avif"), QStringLiteral("heic"),
            QStringLiteral("heif"), QStringLiteral("ico"), QStringLiteral("jxl")}},
        {QStringLiteral("music"), {QStringLiteral("mp3"), QStringLiteral("flac"), QStringLiteral("wav"),
            QStringLiteral("ogg"), QStringLiteral("opus"), QStringLiteral("m4a"), QStringLiteral("aac"),
            QStringLiteral("wma"), QStringLiteral("alac"), QStringLiteral("ape"), QStringLiteral("aiff"),
            QStringLiteral("aif")}},
        {QStringLiteral("videos"), {QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("avi"),
            QStringLiteral("mov"), QStringLiteral("webm"), QStringLiteral("m4v"), QStringLiteral("mpg"),
            QStringLiteral("mpeg"), QStringLiteral("ts"), QStringLiteral("m2ts"), QStringLiteral("mts"),
            QStringLiteral("flv"), QStringLiteral("wmv"), QStringLiteral("ogv"), QStringLiteral("3gp")}},
        {QStringLiteral("archives"), {QStringLiteral("zip"), QStringLiteral("rar"), QStringLiteral("7z"),
            QStringLiteral("tar"), QStringLiteral("tar.gz"), QStringLiteral("tgz"), QStringLiteral("tar.bz2"),
            QStringLiteral("tbz"), QStringLiteral("tbz2"), QStringLiteral("tar.xz"), QStringLiteral("txz"),
            QStringLiteral("tar.zst"), QStringLiteral("gz"), QStringLiteral("bz2"), QStringLiteral("xz"),
            QStringLiteral("zst"), QStringLiteral("lz"), QStringLiteral("lz4"), QStringLiteral("cab"),
            QStringLiteral("arj"), QStringLiteral("ace"), QStringLiteral("deb"), QStringLiteral("rpm"),
            QStringLiteral("apk")}},
        {QStringLiteral("appimage"), {QStringLiteral("appimage")}}
    };
    return extensions.value(category).contains(rule);
}

bool matchesPathIgnoreRule(const QString &path, QString rule)
{
    rule = rule.trimmed().toLower();
    if (!rule.startsWith(QStringLiteral("path:")))
        return false;
    QString token = rule.mid(5).trimmed();
    if (token.isEmpty())
        return false;
    const QString fullPath = QDir::cleanPath(path).toLower();
    if (token.contains(QLatin1Char('/')))
        return fullPath.contains(token);
    const bool wildcard = token.startsWith(QLatin1Char('*')) || token.endsWith(QLatin1Char('*'));
    token.remove(QLatin1Char('*'));
    for (const QString &part : fullPath.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        if ((!wildcard && part == token) || (wildcard && !token.isEmpty() && part.contains(token)))
            return true;
    }
    return false;
}

bool matchesIgnoreRules(const QFileInfo &info, const QString &category, const QStringList &rules)
{
    const QString fileName = info.fileName().toLower();
    const QString suffix = info.suffix().toLower();
    const QString fullPath = QDir::cleanPath(info.absoluteFilePath()).toLower();
    for (QString rule : rules) {
        rule = rule.trimmed().toLower();
        if (rule.isEmpty())
            continue;
        if (rule.startsWith(QStringLiteral("path:"))) {
            if (matchesPathIgnoreRule(fullPath, rule))
                return true;
            continue;
        }
        bool forceExtension = false;
        if (rule.startsWith(QStringLiteral("ext:"))) {
            rule = rule.mid(4).trimmed();
            forceExtension = true;
        } else if (rule.startsWith(QStringLiteral("name:"))) {
            const QString token = rule.mid(5).trimmed();
            if (!token.isEmpty() && fileName.contains(token))
                return true;
            continue;
        }
        if (rule.startsWith(QStringLiteral("*."))) {
            rule.remove(0, 2);
            forceExtension = true;
        } else if (rule.startsWith(QLatin1Char('.'))) {
            rule.remove(0, 1);
            forceExtension = true;
        }
        if (rule.isEmpty())
            continue;
        if (forceExtension || isKnownExtensionRule(category, rule)) {
            if (suffix == rule || fileName.endsWith(QStringLiteral(".") + rule))
                return true;
        } else if (fileName.contains(rule)) {
            return true;
        }
    }
    return false;
}

QString normalizedPath(const QString &path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

bool isHiddenWithinHome(const QString &path)
{
    const QString relative = QDir(QDir::homePath()).relativeFilePath(path);
    for (const QString &part : relative.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        if (part.startsWith(QLatin1Char('.')) && part != QStringLiteral(".") && part != QStringLiteral(".."))
            return true;
    }
    return false;
}

bool isAppStateRoot(const QString &path)
{
    const QString relative = QDir(QDir::homePath()).relativeFilePath(path);
    const QString first = relative.section(QLatin1Char('/'), 0, 0).toLower();
    static const QSet<QString> roots = {
        QStringLiteral(".local"), QStringLiteral(".config"), QStringLiteral(".rustup"),
        QStringLiteral(".cargo"), QStringLiteral(".npm"), QStringLiteral(".nuget"),
        QStringLiteral(".yarn"), QStringLiteral(".gradle"), QStringLiteral(".codex"),
        QStringLiteral(".var"), QStringLiteral(".steam"), QStringLiteral(".mozilla"),
        QStringLiteral(".jriver"), QStringLiteral(".qbtheme")
    };
    return roots.contains(first);
}





bool shouldIncludeIndexedFile(const QFileInfo &info, const QString &category, const QStringList &ignoreRules)
{
    if (matchesIgnoreRules(info, category, ignoreRules))
        return false;
    const QString name = info.fileName();
    const int conflictMarker = name.indexOf(QStringLiteral(".sync-conflict-"), 0, Qt::CaseInsensitive);
    if (conflictMarker >= 0) {
        const int extension = name.lastIndexOf(QLatin1Char('.'));
        if (extension > conflictMarker) {
            const QString original = name.left(conflictMarker) + name.mid(extension);
            if (QFileInfo::exists(QDir(info.absolutePath()).filePath(original)))
                return false;
        }
    }
    if (category == QStringLiteral("images"))
        return info.size() >= kMinimumImageBytes;
    if (category == QStringLiteral("videos"))
        return info.size() >= kMinimumVideoBytes;
    return true;
}
}

ContentIndexModel::ContentIndexModel(QObject *parent)
    : QAbstractListModel(parent)
{
    m_categories = {
        // Private is a virtual encrypted category.  It participates in Discover
        // ordering, but is deliberately excluded from allCategoryKeys() so the
        // normal filesystem indexer never scans or manages its contents.
        {QStringLiteral("private"), QStringLiteral("Sana Özel"), QStringLiteral("Private"), QStringLiteral("private.svg"), {}, {}, true, false},
        {QStringLiteral("documents"), QStringLiteral("Belgeler"), QStringLiteral("Documents"), QStringLiteral("documents.svg"), {}, {}, true, false},
        {QStringLiteral("appimage"), QStringLiteral("AppImage"), QStringLiteral("AppImage"), QStringLiteral("appimage.svg"), {}, {}, true, false},
        {QStringLiteral("archives"), QStringLiteral("Arşivler"), QStringLiteral("Archives"), QStringLiteral("archive.svg"), {}, {}, true, false},
        {QStringLiteral("images"), QStringLiteral("Resimler"), QStringLiteral("Images"), QStringLiteral("image.svg"), {}, {}, true, false},
        {QStringLiteral("music"), QStringLiteral("Müzikler"), QStringLiteral("Music"), QStringLiteral("audio.svg"), {}, {}, true, false},
        {QStringLiteral("videos"), QStringLiteral("Videolar"), QStringLiteral("Videos"), QStringLiteral("video.svg"), {}, {}, true, false}
    };

    m_scanPool = new QThreadPool;
    m_scanPool->setMaxThreadCount(1);
    m_scanPool->setThreadPriority(QThread::LowPriority);
    m_scanPool->setExpiryTimeout(1500);
    m_ioPool = new QThreadPool;
    m_ioPool->setMaxThreadCount(1);
    m_ioPool->setThreadPriority(QThread::LowPriority);
    m_ioPool->setExpiryTimeout(1500);
    loadCategorySettings();
    loadMediaRootSettings();
    loadIgnoreRuleSettings();
    for (auto it = m_mediaRoots.constBegin(); it != m_mediaRoots.constEnd(); ++it) {
        for (const QString &root : it.value())
            m_mediaRootAvailability.insert(root, QFileInfo(root).isDir());
    }

    m_mediaRootProbeTimer.setInterval(4000);
    m_mediaRootProbeTimer.setSingleShot(false);
    connect(&m_mediaRootProbeTimer, &QTimer::timeout, this, &ContentIndexModel::probeMediaRootAvailability);
    m_mediaRootProbeTimer.start();

    m_changeDebounce.setSingleShot(true);
    m_changeDebounce.setInterval(260);
    connect(&m_changeDebounce, &QTimer::timeout, this, &ContentIndexModel::startPendingScan);

    m_saveDebounce.setSingleShot(true);
    m_saveDebounce.setInterval(900);
    connect(&m_saveDebounce, &QTimer::timeout, this, &ContentIndexModel::saveIndex);

    // Register recursive directory watches incrementally. QFileSystemWatcher::addPaths()
    // can block the GUI for a noticeable amount of time when thousands of paths
    // are installed in one call. Small batches keep the event loop responsive.
    m_watchInstallTimer.setSingleShot(true);
    m_watchInstallTimer.setInterval(12);
    connect(&m_watchInstallTimer, &QTimer::timeout, this, &ContentIndexModel::installWatcherBatch);

    connect(&m_saveWatcher, &QFutureWatcher<void>::finished, this, [this] {
        if (m_savePending) {
            m_savePending = false;
            saveIndex();
        }
    });

    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString &path) {
        const QString clean = normalizedPath(path);
        if (!clean.isEmpty())
            m_pendingRoots.insert(clean);
        m_changeDebounce.start();
    });

    connect(&m_loadWatcher, &QFutureWatcher<ScanResult>::finished, this, [this] {
        const ScanResult cached = m_loadWatcher.result();
        // A live scan always wins over an older disk cache.
        if (m_scanRunning || cached.files.isEmpty())
            return;
        m_files = cached.files;
        m_counts = cached.counts;
        notifyCountsChanged();
    });
    startIndexLoad();

    connect(&m_scanWatcher, &QFutureWatcher<ScanResult>::finished, this, [this] {
        const ScanResult result = m_scanWatcher.result();
        const QString subtree = m_activeSubtree;
        const QString category = m_activeCategory;
        m_scanRunning = false;
        m_activeSubtree.clear();
        m_activeCategory.clear();

        if (m_fullRescanRequested) {
            m_fullRescanRequested = false;
            const bool showProgress = m_fullRescanShowProgress;
            m_fullRescanShowProgress = false;
            startFullScan(showProgress);
            return;
        }

        if (!category.isEmpty())
            applyCategoryScan(category, result);
        else if (subtree.isEmpty())
            applyFullScan(result);
        else
            applySubtreeScan(subtree, result);

        if (!m_pendingCategoryRefreshes.isEmpty()) {
            const QString next = *m_pendingCategoryRefreshes.constBegin();
            m_pendingCategoryRefreshes.remove(next);
            startCategoryScan(next, true);
            return;
        }
        if (!m_pendingRoots.isEmpty()) {
            startPendingScan();
            return;
        }
        setIndexing(false);
        setScanProgressVisible(false);
    });

    // Cached results appear immediately. Give the first window frame priority
    // before starting the disk-intensive recursive home scan.
    QTimer::singleShot(5000, this, [this] { startFullScan(); });
}

ContentIndexModel::~ContentIndexModel()
{
    // Closing the window must not be held hostage by a recursive home scan or
    // a large JSON cache write.  Scan workers observe this flag frequently and
    // all workers use value snapshots/QPointer rather than a raw model pointer.
    if (m_scanCancel)
        m_scanCancel->store(true, std::memory_order_relaxed);
    m_changeDebounce.stop();
    m_saveDebounce.stop();
    m_watchInstallTimer.stop();
    m_mediaRootProbeTimer.stop();
    m_loadWatcher.disconnect(this);
    m_scanWatcher.disconnect(this);
    m_saveWatcher.disconnect(this);
}

int ContentIndexModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_categories.size();
}

QVariant ContentIndexModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_categories.size())
        return {};
    const Category &category = m_categories.at(index.row());
    switch (role) {
    case KeyRole: return category.key;
    case TitleTrRole: return category.customTitle.isEmpty() ? category.titleTr : category.customTitle;
    case TitleEnRole: return category.customTitle.isEmpty() ? category.titleEn : category.customTitle;
    case IconRole: return category.customIcon.isEmpty() ? category.icon : category.customIcon;
    case CountRole: return countForCategory(category.key);
    case VisibleRole: return category.visible;
    case IndexHiddenRole: return category.indexHidden;
    default: return {};
    }
}

QHash<int, QByteArray> ContentIndexModel::roleNames() const
{
    return {
        {KeyRole, "categoryKey"},
        {TitleTrRole, "categoryTitleTr"},
        {TitleEnRole, "categoryTitleEn"},
        {IconRole, "categoryIcon"},
        {CountRole, "categoryCount"},
        {VisibleRole, "categoryVisible"},
        {IndexHiddenRole, "categoryIndexHidden"}
    };
}

QVariantList ContentIndexModel::filesForCategory(const QString &key) const
{
    QVector<IndexedFile> matches;
    matches.reserve(countForCategory(key));
    for (auto it = m_files.constBegin(); it != m_files.constEnd(); ++it) {
        if (it->category == key)
            matches.push_back(*it);
    }
    std::sort(matches.begin(), matches.end(), [](const IndexedFile &a, const IndexedFile &b) {
        if (a.modifiedMs != b.modifiedMs)
            return a.modifiedMs > b.modifiedMs;
        return QString::localeAwareCompare(a.path, b.path) < 0;
    });

    QVariantList result;
    result.reserve(matches.size());
    for (const IndexedFile &entry : matches) {
        const QFileInfo info(entry.path);
        QVariantMap item;
        item.insert(QStringLiteral("path"), entry.path);
        item.insert(QStringLiteral("url"), QUrl::fromLocalFile(entry.path).toString());
        item.insert(QStringLiteral("name"), info.fileName());
        item.insert(QStringLiteral("parentPath"), info.absolutePath());
        item.insert(QStringLiteral("suffix"), info.suffix().toLower());
        item.insert(QStringLiteral("size"), entry.size);
        item.insert(QStringLiteral("modifiedMs"), entry.modifiedMs);
        result.push_back(item);
    }
    return result;
}

int ContentIndexModel::countForCategory(const QString &key) const
{
    if (m_scanProgressVisible && m_scanRunning && m_activeSubtree.isEmpty() && !m_liveCounts.isEmpty()) {
        if (m_activeCategory.isEmpty() || m_activeCategory == key)
            return m_liveCounts.value(key, 0);
    }
    return m_counts.value(key, 0);
}

qint64 ContentIndexModel::bytesForCategory(const QString &key) const
{
    qint64 total = 0;
    for (auto it = m_files.constBegin(); it != m_files.constEnd(); ++it) {
        if (it->category == key)
            total += qMax<qint64>(0, it->size);
    }
    return total;
}

qint64 ContentIndexModel::homeStorageTotalBytes() const
{
    QStorageInfo storage(QDir::homePath());
    return storage.isValid() && storage.isReady() ? qMax<qint64>(0, storage.bytesTotal()) : 0;
}

qint64 ContentIndexModel::homeStorageAvailableBytes() const
{
    QStorageInfo storage(QDir::homePath());
    return storage.isValid() && storage.isReady() ? qMax<qint64>(0, storage.bytesAvailable()) : 0;
}

QString ContentIndexModel::titleForCategory(const QString &key, const QString &language) const
{
    for (const Category &category : m_categories) {
        if (category.key == key) {
            if (!category.customTitle.isEmpty())
                return category.customTitle;
            return language == QStringLiteral("tr") ? category.titleTr : category.titleEn;
        }
    }
    return key;
}

int ContentIndexModel::scanProgressPercent() const
{
    if (!m_indexing)
        return 100;
    const int totalKnown = m_scanProcessedDirectories + m_scanPendingDirectories;
    if (totalKnown <= 0)
        return 0;
    return qBound(0, int((qint64(m_scanProcessedDirectories) * 100) / totalKnown), 99);
}

void ContentIndexModel::moveCategory(int from, int to)
{
    if (from < 0 || to < 0 || from >= m_categories.size() || to >= m_categories.size() || from == to)
        return;
    const int destination = to > from ? to + 1 : to;
    beginMoveRows(QModelIndex(), from, from, QModelIndex(), destination);
    m_categories.move(from, to);
    endMoveRows();
    saveCategorySettings();
}

bool ContentIndexModel::updateCategoryAppearance(const QString &key, const QString &title, const QString &icon)
{
    const QString cleanTitle = title.trimmed();
    const QString cleanIcon = icon.trimmed();
    if (cleanTitle.isEmpty() || cleanIcon.isEmpty())
        return false;
    for (int i = 0; i < m_categories.size(); ++i) {
        Category &category = m_categories[i];
        if (category.key != key)
            continue;
        if (category.customTitle == cleanTitle && category.customIcon == cleanIcon)
            return true;
        category.customTitle = cleanTitle;
        category.customIcon = cleanIcon;
        emit dataChanged(index(i), index(i), {TitleTrRole, TitleEnRole, IconRole});
        saveCategorySettings();
        return true;
    }
    return false;
}

void ContentIndexModel::setCategoryVisible(const QString &key, bool visible)
{
    for (int i = 0; i < m_categories.size(); ++i) {
        if (m_categories[i].key != key)
            continue;
        if (m_categories[i].visible == visible)
            return;
        m_categories[i].visible = visible;
        emit dataChanged(index(i), index(i), {VisibleRole});
        saveCategorySettings();
        return;
    }
}

void ContentIndexModel::setCategoryIndexHidden(const QString &key, bool enabled)
{
    const QString normalized = key.trimmed().toLower();
    for (int i = 0; i < m_categories.size(); ++i) {
        if (m_categories[i].key != normalized)
            continue;
        if (m_categories[i].indexHidden == enabled)
            return;
        m_categories[i].indexHidden = enabled;
        emit dataChanged(index(i), index(i), {IndexHiddenRole});
        saveCategorySettings();
        emit categoryIndexSettingsChanged(normalized);
        refreshCategory(normalized);
        return;
    }
}

bool ContentIndexModel::categoryIndexHidden(const QString &key) const
{
    const QString normalized = key.trimmed().toLower();
    for (const Category &category : m_categories) {
        if (category.key == normalized)
            return category.indexHidden;
    }
    return false;
}

bool ContentIndexModel::categorySingleClickOpen(const QString &key) const
{
    const QString normalized = key.trimmed().toLower();
    for (const Category &category : m_categories) {
        if (category.key == normalized)
            return category.singleClickOpen;
    }
    return false;
}

void ContentIndexModel::setCategorySingleClickOpen(const QString &key, bool enabled)
{
    const QString normalized = key.trimmed().toLower();
    for (Category &category : m_categories) {
        if (category.key != normalized)
            continue;
        if (category.singleClickOpen == enabled)
            return;
        category.singleClickOpen = enabled;
        ++m_categoryInteractionRevision;
        saveCategorySettings();
        emit categoryInteractionRevisionChanged();
        return;
    }
}

QSet<QString> ContentIndexModel::hiddenCategories() const
{
    QSet<QString> enabled;
    for (const Category &category : m_categories) {
        if (category.indexHidden)
            enabled.insert(category.key);
    }
    return enabled;
}

bool ContentIndexModel::isKnownCategory(const QString &category) const
{
    return allCategoryKeys().contains(category.trimmed().toLower());
}

QStringList ContentIndexModel::mediaRoots(const QString &category) const
{
    const QString key = category.trimmed().toLower();
    return isKnownCategory(key) ? m_mediaRoots.value(key) : QStringList{};
}

QString ContentIndexModel::defaultMediaRoot() const
{
    return QDir::homePath();
}

QStringList ContentIndexModel::ignoreRules(const QString &category) const
{
    const QString key = category.trimmed().toLower();
    return isKnownCategory(key) ? m_ignoreRules.value(key) : QStringList{};
}

QStringList ContentIndexModel::defaultIgnoreRules(const QString &category) const
{
    const QString key = category.trimmed().toLower();
    return isKnownCategory(key) ? defaultIgnoreRulesForCategory(key) : QStringList{};
}

bool ContentIndexModel::pathWithinRoot(const QString &path, const QString &root)
{
    const QString cleanPath = normalizedPath(path);
    const QString cleanRoot = normalizedPath(root);
    if (cleanPath.isEmpty() || cleanRoot.isEmpty())
        return false;
    return cleanPath == cleanRoot || cleanPath.startsWith(cleanRoot + QLatin1Char('/'));
}

bool ContentIndexModel::pathAllowedForMediaCategory(const QString &path, const QString &category) const
{
    const QStringList roots = m_mediaRoots.value(category);
    for (const QString &root : roots) {
        if (pathWithinRoot(path, root))
            return true;
    }
    return false;
}

bool ContentIndexModel::setCategoryIndexSettings(const QString &category, const QVariantList &roots,
                                                  bool indexHidden, const QVariantList &ignoreRulesValues)
{
    const QString key = category.trimmed().toLower();
    if (!isKnownCategory(key))
        return false;

    QStringList cleanedRoots;
    QSet<QString> seenRoots;
    for (const QVariant &value : roots) {
        QString path = value.toString().trimmed();
        if (path.startsWith(QStringLiteral("file:")))
            path = QUrl(path).toLocalFile();
        if (path.isEmpty())
            continue;
        path = normalizedPath(path);
        if (path.isEmpty() || seenRoots.contains(path))
            continue;
        seenRoots.insert(path);
        cleanedRoots.push_back(path);
    }
    std::sort(cleanedRoots.begin(), cleanedRoots.end(), [](const QString &a, const QString &b) {
        return QString::localeAwareCompare(a, b) < 0;
    });

    QStringList cleanedRules;
    QSet<QString> seenRules;
    for (const QVariant &value : ignoreRulesValues) {
        QString rule = value.toString().trimmed().toLower();
        if (rule.isEmpty() || seenRules.contains(rule))
            continue;
        seenRules.insert(rule);
        cleanedRules.push_back(rule);
    }
    std::sort(cleanedRules.begin(), cleanedRules.end(), [](const QString &a, const QString &b) {
        return QString::localeAwareCompare(a, b) < 0;
    });

    bool changed = false;
    if (m_mediaRoots.value(key) != cleanedRoots) {
        m_mediaRoots.insert(key, cleanedRoots);
        changed = true;
        emit mediaRootsChanged(key);
    }
    if (m_ignoreRules.value(key) != cleanedRules) {
        m_ignoreRules.insert(key, cleanedRules);
        changed = true;
    }
    for (int i = 0; i < m_categories.size(); ++i) {
        if (m_categories[i].key != key)
            continue;
        if (m_categories[i].indexHidden != indexHidden) {
            m_categories[i].indexHidden = indexHidden;
            emit dataChanged(index(i), index(i), {IndexHiddenRole});
            changed = true;
        }
        break;
    }

    QSet<QString> configured;
    for (auto it = m_mediaRoots.constBegin(); it != m_mediaRoots.constEnd(); ++it) {
        for (const QString &root : it.value()) {
            configured.insert(root);
            if (!m_mediaRootAvailability.contains(root))
                m_mediaRootAvailability.insert(root, QFileInfo(root).isDir());
        }
    }
    for (auto it = m_mediaRootAvailability.begin(); it != m_mediaRootAvailability.end();) {
        if (!configured.contains(it.key()))
            it = m_mediaRootAvailability.erase(it);
        else
            ++it;
    }

    if (changed) {
        QStringList obsoleteWatches;
        for (const QString &watched : m_watcher.directories()) {
            bool stillCovered = false;
            for (auto rootsIt = m_mediaRoots.constBegin(); rootsIt != m_mediaRoots.constEnd() && !stillCovered; ++rootsIt) {
                for (const QString &configuredRoot : rootsIt.value()) {
                    if (pathWithinRoot(watched, configuredRoot)) {
                        stillCovered = true;
                        break;
                    }
                }
            }
            if (!stillCovered)
                obsoleteWatches.push_back(watched);
        }
        if (!obsoleteWatches.isEmpty())
            m_watcher.removePaths(obsoleteWatches);
    }

    if (!changed)
        return true;
    saveMediaRootSettings();
    saveIgnoreRuleSettings();
    saveCategorySettings();
    emit categoryIndexSettingsChanged(key);
    refreshCategory(key);
    return true;
}

bool ContentIndexModel::setMediaRoots(const QString &category, const QVariantList &roots)
{
    const QString key = category.trimmed().toLower();
    QVariantList rules;
    for (const QString &rule : m_ignoreRules.value(key))
        rules.push_back(rule);
    return setCategoryIndexSettings(key, roots, categoryIndexHidden(key), rules);
}

void ContentIndexModel::refresh()
{
    startFullScan(true);
}

void ContentIndexModel::refreshCategory(const QString &category)
{
    const QString key = category.trimmed().toLower();
    if (!isKnownCategory(key))
        return;
    if (m_scanRunning) {
        m_pendingCategoryRefreshes.insert(key);
        return;
    }
    startCategoryScan(key, true);
}

QString ContentIndexModel::categoryForPath(const QString &path)
{
    const QString name = QFileInfo(path).fileName().toLower();
    if (name.endsWith(QStringLiteral(".appimage")))
        return QStringLiteral("appimage");

    static const QStringList archives = {
        QStringLiteral(".zip"), QStringLiteral(".rar"), QStringLiteral(".7z"),
        QStringLiteral(".tar"), QStringLiteral(".tar.gz"), QStringLiteral(".tgz"),
        QStringLiteral(".tar.bz2"), QStringLiteral(".tbz"), QStringLiteral(".tbz2"),
        QStringLiteral(".tar.xz"), QStringLiteral(".txz"), QStringLiteral(".tar.zst"),
        QStringLiteral(".gz"), QStringLiteral(".bz2"), QStringLiteral(".xz"),
        QStringLiteral(".zst"), QStringLiteral(".lz"), QStringLiteral(".lz4"),
        QStringLiteral(".cab"), QStringLiteral(".arj"), QStringLiteral(".ace"),
        QStringLiteral(".deb"), QStringLiteral(".rpm"), QStringLiteral(".apk")
    };
    for (const QString &ext : archives) {
        if (name.endsWith(ext))
            return QStringLiteral("archives");
    }

    const QString suffix = QFileInfo(path).suffix().toLower();
    static const QSet<QString> images = {
        QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("webp"),
        QStringLiteral("gif"), QStringLiteral("bmp"), QStringLiteral("tif"), QStringLiteral("tiff"),
        QStringLiteral("svg"), QStringLiteral("avif"), QStringLiteral("heic"), QStringLiteral("heif"),
        QStringLiteral("ico"), QStringLiteral("jxl")
    };
    static const QSet<QString> music = {
        QStringLiteral("mp3"), QStringLiteral("flac"), QStringLiteral("wav"), QStringLiteral("ogg"),
        QStringLiteral("opus"), QStringLiteral("m4a"), QStringLiteral("aac"), QStringLiteral("wma"),
        QStringLiteral("alac"), QStringLiteral("ape"), QStringLiteral("aiff"), QStringLiteral("aif")
    };
    static const QSet<QString> videos = {
        QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("avi"), QStringLiteral("mov"),
        QStringLiteral("webm"), QStringLiteral("m4v"), QStringLiteral("mpg"), QStringLiteral("mpeg"),
        QStringLiteral("ts"), QStringLiteral("m2ts"), QStringLiteral("mts"), QStringLiteral("flv"),
        QStringLiteral("wmv"), QStringLiteral("ogv"), QStringLiteral("3gp")
    };
    static const QSet<QString> documents = {
        QStringLiteral("pdf"), QStringLiteral("txt"), QStringLiteral("rtf"),
        QStringLiteral("odt"), QStringLiteral("ods"), QStringLiteral("odp"), QStringLiteral("doc"),
        QStringLiteral("docx"), QStringLiteral("xls"), QStringLiteral("xlsx"), QStringLiteral("ppt"),
        QStringLiteral("pptx"), QStringLiteral("csv"), QStringLiteral("tsv"), QStringLiteral("epub"),
        QStringLiteral("mobi"), QStringLiteral("pages"), QStringLiteral("numbers"),
        QStringLiteral("key"), QStringLiteral("djvu"), QStringLiteral("xps")
    };

    if (images.contains(suffix)) return QStringLiteral("images");
    if (music.contains(suffix)) return QStringLiteral("music");
    if (videos.contains(suffix)) return QStringLiteral("videos");
    if (documents.contains(suffix)) return QStringLiteral("documents");
    return {};
}

bool ContentIndexModel::shouldSkipDirectory(const QString &path)
{
    const QString clean = QDir::cleanPath(path);
    const QString home = QDir::cleanPath(QDir::homePath());
    const QString appData = QDir(home).filePath(QStringLiteral(".local/share/g-File"));
    return clean == QDir(home).filePath(QStringLiteral(".cache"))
        || clean == QDir(home).filePath(QStringLiteral(".local/share/Trash"))
        || clean == appData
        || (clean != home && isAppStateRoot(clean))
        || clean.contains(QStringLiteral("/.cache/"))
        || clean.contains(QStringLiteral("/.local/share/Trash/"))
        || clean.startsWith(appData + QLatin1Char('/'));
}

ContentIndexModel::ScanResult ContentIndexModel::scanTree(const QString &rootPath,
                                                         const QSet<QString> &hiddenCategories,
                                                         const QHash<QString, QStringList> &categoryRoots,
                                                         const QHash<QString, QStringList> &ignoreRules,
                                                         const QSet<QString> &categoryFilter,
                                                         const ProgressCallback &progress,
                                                         const std::shared_ptr<std::atomic_bool> &cancel,
                                                         bool background)
{
    ScanResult result;
    const QString root = normalizedPath(rootPath);
    if (root.isEmpty() || !QFileInfo(root).isDir() || shouldSkipDirectory(root))
        return result;

    struct PendingDirectory { QString path; bool hidden; };
    QVector<PendingDirectory> stack;
    const bool rootHidden = isHiddenWithinHome(root);
    if (rootHidden && hiddenCategories.isEmpty())
        return result;
    stack.push_back({root, rootHidden});
    int processedDirectories = 0;
    int visitedEntries = 0;
    int matchedFiles = 0;
    QElapsedTimer progressTimer;
    progressTimer.start();
    QElapsedTimer workBudget;
    workBudget.start();
    const auto yieldBackgroundScan = [&] {
        // Cached categories remain usable during automatic reconciliation.
        // Limit it to short bursts so startup scans do not compete with
        // input/rendering and the visible directory's thumbnail workers.
        if (background && workBudget.elapsed() >= 8) {
            QThread::msleep(24);
            workBudget.restart();
        }
    };

    auto publishProgress = [&] (bool force = false) {
        if (!progress)
            return;
        if (!force && progressTimer.elapsed() < 120)
            return;
        progressTimer.restart();
        progress(processedDirectories, stack.size(), visitedEntries, matchedFiles, result.counts);
    };

    while (!stack.isEmpty()) {
        yieldBackgroundScan();
        if (cancel && cancel->load(std::memory_order_relaxed))
            return {};
        const PendingDirectory pending = stack.takeLast();
        const QString &directoryPath = pending.path;
        if (shouldSkipDirectory(directoryPath))
            continue;

        ++processedDirectories;
        result.directories.push_back(directoryPath);
        const QDir dir(directoryPath);
        QDir::Filters filters = QDir::AllEntries | QDir::NoDotAndDotDot;
        if (!hiddenCategories.isEmpty())
            filters |= QDir::Hidden | QDir::System;
        const QFileInfoList entries = dir.entryInfoList(filters, QDir::NoSort);

        for (const QFileInfo &info : entries) {
            yieldBackgroundScan();
            if (cancel && cancel->load(std::memory_order_relaxed))
                return {};
            ++visitedEntries;
            if (info.isSymLink())
                continue;
            if (info.isDir()) {
                const bool childHidden = pending.hidden || info.isHidden();
                const QString childPath = normalizedPath(info.absoluteFilePath());
                bool relevantCategory = false;
                bool neededByAnyCategory = false;
                for (auto rootsIt = categoryRoots.constBegin(); rootsIt != categoryRoots.constEnd() && !neededByAnyCategory; ++rootsIt) {
                    const QString &candidateCategory = rootsIt.key();
                    if (!categoryFilter.isEmpty() && !categoryFilter.contains(candidateCategory))
                        continue;
                    for (const QString &configuredRoot : rootsIt.value()) {
                        const bool childWithinRoot = ContentIndexModel::pathWithinRoot(childPath, configuredRoot);
                        const bool rootWithinChild = ContentIndexModel::pathWithinRoot(configuredRoot, childPath);
                        if (!childWithinRoot && !rootWithinChild)
                            continue;
                        relevantCategory = true;
                        if (rootWithinChild && !childWithinRoot) {
                            neededByAnyCategory = true;
                            break;
                        }
                        bool ignoredForCategory = false;
                        for (const QString &rule : ignoreRules.value(candidateCategory)) {
                            if (matchesPathIgnoreRule(childPath, rule)) {
                                ignoredForCategory = true;
                                break;
                            }
                        }
                        if (!ignoredForCategory) {
                            neededByAnyCategory = true;
                            break;
                        }
                    }
                }
                if ((!childHidden || !hiddenCategories.isEmpty())
                        && !shouldSkipDirectory(childPath)
                        && (!relevantCategory || neededByAnyCategory))
                    stack.push_back({childPath, childHidden});
                continue;
            }
            if (!info.isFile())
                continue;
            const QString category = categoryForPath(info.absoluteFilePath());
            if (category.isEmpty() || (!categoryFilter.isEmpty() && !categoryFilter.contains(category))
                    || !shouldIncludeIndexedFile(info, category, ignoreRules.value(category))
                    || ((pending.hidden || info.isHidden()) && !hiddenCategories.contains(category)))
                continue;

            bool allowed = false;
            const QString filePath = normalizedPath(info.absoluteFilePath());
            const QStringList roots = categoryRoots.value(category);
            for (const QString &categoryRoot : roots) {
                if (ContentIndexModel::pathWithinRoot(filePath, categoryRoot)) {
                    allowed = true;
                    break;
                }
            }
            if (!allowed)
                continue;
            IndexedFile item;
            item.path = normalizedPath(info.absoluteFilePath());
            item.category = category;
            item.size = info.size();
            item.modifiedMs = info.lastModified().toMSecsSinceEpoch();
            result.files.insert(item.path, item);
            result.counts[category] = result.counts.value(category, 0) + 1;
            ++matchedFiles;
        }
        publishProgress();
    }
    publishProgress(true);
    return result;
}

void ContentIndexModel::loadCategorySettings()
{
    QSettings settings;
    const QStringList order = settings.value(QStringLiteral("contentIndex/categoryOrder")).toStringList();
    const QStringList hidden = settings.value(QStringLiteral("contentIndex/hiddenCategories")).toStringList();
    Q_UNUSED(hidden);
    const QStringList indexHidden = settings.value(QStringLiteral("contentIndex/indexHiddenCategories")).toStringList();
    const QStringList singleClick = settings.value(QStringLiteral("contentIndex/singleClickCategories")).toStringList();
    const QVariantMap customTitles = settings.value(QStringLiteral("contentIndex/customCategoryTitles")).toMap();
    const QVariantMap customIcons = settings.value(QStringLiteral("contentIndex/customCategoryIcons")).toMap();
    for (Category &category : m_categories) {
        category.visible = true;
        category.indexHidden = indexHidden.contains(category.key);
        category.singleClickOpen = singleClick.contains(category.key);
        category.customTitle = customTitles.value(category.key).toString().trimmed();
        category.customIcon = customIcons.value(category.key).toString().trimmed();
    }

    if (order.isEmpty())
        return;
    QVector<Category> sorted;
    sorted.reserve(m_categories.size());
    // Existing installations predate the virtual Private category.  Keep its
    // historical first-card position until the user explicitly drags it.
    if (!order.contains(QStringLiteral("private"))) {
        for (const Category &category : std::as_const(m_categories)) {
            if (category.key == QStringLiteral("private")) {
                sorted.push_back(category);
                break;
            }
        }
    }
    for (const QString &key : order) {
        for (const Category &category : std::as_const(m_categories)) {
            if (category.key == key) {
                sorted.push_back(category);
                break;
            }
        }
    }
    for (const Category &category : std::as_const(m_categories)) {
        bool exists = false;
        for (const Category &candidate : std::as_const(sorted)) {
            if (candidate.key == category.key) { exists = true; break; }
        }
        if (!exists)
            sorted.push_back(category);
    }
    m_categories = sorted;
}

void ContentIndexModel::saveCategorySettings() const
{
    QStringList order;
    QStringList hidden;
    QStringList indexHidden;
    QStringList singleClick;
    QVariantMap customTitles;
    QVariantMap customIcons;
    for (const Category &category : m_categories) {
        order.push_back(category.key);
        if (!category.visible)
            hidden.push_back(category.key);
        if (category.indexHidden)
            indexHidden.push_back(category.key);
        if (category.singleClickOpen)
            singleClick.push_back(category.key);
        if (!category.customTitle.isEmpty())
            customTitles.insert(category.key, category.customTitle);
        if (!category.customIcon.isEmpty())
            customIcons.insert(category.key, category.customIcon);
    }
    QSettings settings;
    settings.setValue(QStringLiteral("contentIndex/categoryOrder"), order);
    settings.setValue(QStringLiteral("contentIndex/hiddenCategories"), hidden);
    settings.setValue(QStringLiteral("contentIndex/indexHiddenCategories"), indexHidden);
    settings.setValue(QStringLiteral("contentIndex/singleClickCategories"), singleClick);
    settings.setValue(QStringLiteral("contentIndex/customCategoryTitles"), customTitles);
    settings.setValue(QStringLiteral("contentIndex/customCategoryIcons"), customIcons);
}

void ContentIndexModel::loadMediaRootSettings()
{
    QSettings settings;
    const QString home = normalizedPath(QDir::homePath());
    for (const QString &category : allCategoryKeys()) {
        const QString settingKey = QStringLiteral("contentIndex/mediaRoots/") + category;
        QStringList roots;
        if (settings.contains(settingKey))
            roots = settings.value(settingKey).toStringList();
        else
            roots = {home};

        QStringList cleaned;
        QSet<QString> seen;
        for (const QString &value : roots) {
            const QString path = normalizedPath(value);
            if (path.isEmpty() || seen.contains(path))
                continue;
            seen.insert(path);
            cleaned.push_back(path);
        }
        std::sort(cleaned.begin(), cleaned.end(), [](const QString &a, const QString &b) {
            return QString::localeAwareCompare(a, b) < 0;
        });
        m_mediaRoots.insert(category, cleaned);
    }
}

void ContentIndexModel::saveMediaRootSettings() const
{
    QSettings settings;
    for (const QString &category : allCategoryKeys())
        settings.setValue(QStringLiteral("contentIndex/mediaRoots/") + category, m_mediaRoots.value(category));
}

void ContentIndexModel::loadIgnoreRuleSettings()
{
    QSettings settings;
    for (const QString &category : allCategoryKeys()) {
        const QString settingKey = QStringLiteral("contentIndex/ignoreRules/") + category;
        QStringList rules = settings.contains(settingKey)
                ? settings.value(settingKey).toStringList()
                : defaultIgnoreRulesForCategory(category);
        QStringList cleaned;
        QSet<QString> seen;
        for (QString rule : rules) {
            rule = rule.trimmed().toLower();
            if (rule.isEmpty() || seen.contains(rule))
                continue;
            seen.insert(rule);
            cleaned.push_back(rule);
        }
        std::sort(cleaned.begin(), cleaned.end(), [](const QString &a, const QString &b) {
            return QString::localeAwareCompare(a, b) < 0;
        });
        m_ignoreRules.insert(category, cleaned);
    }
}

void ContentIndexModel::saveIgnoreRuleSettings() const
{
    QSettings settings;
    for (const QString &category : allCategoryKeys())
        settings.setValue(QStringLiteral("contentIndex/ignoreRules/") + category, m_ignoreRules.value(category));
}

QString ContentIndexModel::mediaRootsSignature() const
{
    QStringList parts;
    const QSet<QString> hidden = hiddenCategories();
    for (const QString &category : allCategoryKeys()) {
        QStringList roots = m_mediaRoots.value(category);
        QStringList rules = m_ignoreRules.value(category);
        std::sort(roots.begin(), roots.end(), [](const QString &a, const QString &b) {
            return QString::localeAwareCompare(a, b) < 0;
        });
        std::sort(rules.begin(), rules.end(), [](const QString &a, const QString &b) {
            return QString::localeAwareCompare(a, b) < 0;
        });
        parts.push_back(category + QLatin1Char('=') + roots.join(QLatin1Char('|'))
                        + QStringLiteral("#hidden=") + (hidden.contains(category) ? QStringLiteral("1") : QStringLiteral("0"))
                        + QStringLiteral("#ignore=") + rules.join(QLatin1Char('|')));
    }
    return parts.join(QLatin1Char(';'));
}

QString ContentIndexModel::indexFilePath() const
{
    const QString base = QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
                             .filePath(QStringLiteral("g-File"));
    QDir().mkpath(base);
    return QDir(base).filePath(QStringLiteral("content-index.json"));
}

ContentIndexModel::ScanResult ContentIndexModel::loadIndexFile(const QString &path, const QString &homePath, const QString &mediaRootsSignature)
{
    ScanResult result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return result;
    // Version 2 could contain millions of stale entries. Check the compact JSON
    // trailer before parsing so an obsolete cache is discarded cheaply.
    file.seek(qMax<qint64>(0, file.size() - 128));
    if (!file.readAll().contains(QByteArrayLiteral("\"version\":") + QByteArray::number(kIndexVersion)))
        return result;
    file.seek(0);
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject() || doc.object().value(QStringLiteral("version")).toInt() != kIndexVersion
            || doc.object().value(QStringLiteral("home")).toString() != homePath
            || doc.object().value(QStringLiteral("mediaRootsSignature")).toString() != mediaRootsSignature)
        return result;
    const QJsonArray items = doc.object().value(QStringLiteral("items")).toArray();
    result.files.reserve(items.size());
    for (const QJsonValue &value : items) {
        const QJsonObject object = value.toObject();
        IndexedFile item;
        item.path = object.value(QStringLiteral("path")).toString();
        item.category = object.value(QStringLiteral("category")).toString();
        item.size = qint64(object.value(QStringLiteral("size")).toDouble());
        item.modifiedMs = qint64(object.value(QStringLiteral("modifiedMs")).toDouble());
        if (!item.path.isEmpty() && !item.category.isEmpty()) {
            result.files.insert(item.path, item);
            result.counts[item.category] = result.counts.value(item.category, 0) + 1;
        }
    }
    return result;
}

void ContentIndexModel::startIndexLoad()
{
    const QString path = indexFilePath();
    const QString homePath = QDir::homePath();
    const QString rootsSignature = mediaRootsSignature();
    m_loadWatcher.setFuture(QtConcurrent::run(m_ioPool, [path, homePath, rootsSignature] {
        return ContentIndexModel::loadIndexFile(path, homePath, rootsSignature);
    }));
}

void ContentIndexModel::loadIndex()
{
    const ScanResult cached = loadIndexFile(indexFilePath(), QDir::homePath(), mediaRootsSignature());
    m_files = cached.files;
    m_counts = cached.counts;
}

void ContentIndexModel::saveIndex() const
{
    auto *self = const_cast<ContentIndexModel *>(this);
    if (self->m_saveWatcher.isRunning()) {
        self->m_savePending = true;
        return;
    }

    // QHash is implicitly shared, so taking this snapshot is cheap on the GUI
    // thread. JSON construction + disk IO happen on a worker thread.
    const QHash<QString, IndexedFile> snapshot = m_files;
    const QString outputPath = indexFilePath();
    const QString rootsSignature = mediaRootsSignature();
    self->m_saveWatcher.setFuture(QtConcurrent::run(self->m_ioPool, [snapshot, outputPath, rootsSignature] {
        QJsonArray items;
        for (auto it = snapshot.constBegin(); it != snapshot.constEnd(); ++it) {
            QJsonObject object;
            object.insert(QStringLiteral("path"), it->path);
            object.insert(QStringLiteral("category"), it->category);
            object.insert(QStringLiteral("size"), double(it->size));
            object.insert(QStringLiteral("modifiedMs"), double(it->modifiedMs));
            items.push_back(object);
        }
        QJsonObject root;
        root.insert(QStringLiteral("version"), kIndexVersion);
        root.insert(QStringLiteral("home"), QDir::homePath());
        root.insert(QStringLiteral("mediaRootsSignature"), rootsSignature);
        root.insert(QStringLiteral("generatedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
        root.insert(QStringLiteral("items"), items);
        QSaveFile file(outputPath);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
            file.commit();
        }
    }));
}

void ContentIndexModel::setIndexing(bool value)
{
    if (m_indexing == value)
        return;
    m_indexing = value;
    emit indexingChanged();
}

void ContentIndexModel::setScanProgressVisible(bool visible)
{
    if (m_scanProgressVisible == visible)
        return;
    m_scanProgressVisible = visible;
    emit scanProgressVisibilityChanged();
}

void ContentIndexModel::startFullScan(bool showProgress)
{
    if (m_scanRunning) {
        m_fullRescanRequested = true;
        m_fullRescanShowProgress = m_fullRescanShowProgress || showProgress;
        if (m_scanCancel)
            m_scanCancel->store(true, std::memory_order_relaxed);
        return;
    }
    setScanProgressVisible(showProgress);
    setIndexing(true);
    m_scanRunning = true;
    m_activeSubtree.clear();
    m_activeCategory.clear();
    m_liveCounts.clear();
    m_scanProcessedDirectories = 0;
    m_scanVisitedEntries = 0;
    m_scanMatchedFiles = 0;
    m_pendingRoots.clear();
    const quint64 generation = ++m_scanGeneration;
    const QSet<QString> enabledHidden = hiddenCategories();
    const QHash<QString, QStringList> categoryRoots = m_mediaRoots;
    const QHash<QString, QStringList> ignoreRules = m_ignoreRules;

    QSet<QString> candidates;
    for (auto it = categoryRoots.constBegin(); it != categoryRoots.constEnd(); ++it) {
        for (const QString &value : it.value()) {
            const QString root = normalizedPath(value);
            if (!root.isEmpty() && QFileInfo(root).isDir())
                candidates.insert(root);
        }
    }
    QStringList sortedCandidates = candidates.values();
    std::sort(sortedCandidates.begin(), sortedCandidates.end(), [](const QString &a, const QString &b) {
        if (a.size() != b.size())
            return a.size() < b.size();
        return QString::localeAwareCompare(a, b) < 0;
    });
    QStringList scanRoots;
    for (const QString &candidate : sortedCandidates) {
        bool covered = false;
        for (const QString &existing : std::as_const(scanRoots)) {
            if (pathWithinRoot(candidate, existing)) {
                covered = true;
                break;
            }
        }
        if (!covered)
            scanRoots.push_back(candidate);
    }
    m_scanPendingDirectories = scanRoots.size();

    m_scanCancel = std::make_shared<std::atomic_bool>(false);
    const auto cancel = m_scanCancel;
    emit scanProgressChanged();

    QPointer<ContentIndexModel> self(this);
    m_scanWatcher.setFuture(QtConcurrent::run(m_scanPool, [self, generation, enabledHidden, categoryRoots,
                                                            ignoreRules, scanRoots, cancel, showProgress] {
        auto mergeResult = [](ScanResult &target, const ScanResult &source) {
            for (auto it = source.files.constBegin(); it != source.files.constEnd(); ++it)
                target.files.insert(it.key(), it.value());
            for (auto it = source.counts.constBegin(); it != source.counts.constEnd(); ++it)
                target.counts[it.key()] = target.counts.value(it.key(), 0) + it.value();
            QSet<QString> known;
            for (const QString &directory : std::as_const(target.directories))
                known.insert(directory);
            for (const QString &directory : source.directories) {
                if (!known.contains(directory)) {
                    known.insert(directory);
                    target.directories.push_back(directory);
                }
            }
        };

        ScanResult aggregate;
        int completedRoots = 0;
        for (const QString &rootPath : scanRoots) {
            if (cancel && cancel->load(std::memory_order_relaxed))
                return ScanResult{};
            const ProgressCallback progress = [self, generation, completedRoots, scanRoots](int processedDirectories,
                                                                                             int pendingDirectories,
                                                                                             int visitedEntries,
                                                                                             int matchedFiles,
                                                                                             const QHash<QString, int> &counts) {
                if (!self)
                    return;
                QMetaObject::invokeMethod(self, [self, generation, completedRoots, scanRoots, processedDirectories,
                                                 pendingDirectories, visitedEntries, matchedFiles, counts] {
                    if (!self || self->m_scanGeneration != generation || !self->m_scanRunning
                            || !self->m_activeSubtree.isEmpty() || !self->m_activeCategory.isEmpty())
                        return;
                    self->m_scanProcessedDirectories = processedDirectories + completedRoots;
                    self->m_scanPendingDirectories = pendingDirectories + qMax(0, int(scanRoots.size()) - completedRoots - 1);
                    self->m_scanVisitedEntries = qMax(self->m_scanVisitedEntries, visitedEntries);
                    self->m_scanMatchedFiles = qMax(self->m_scanMatchedFiles, matchedFiles);
                    self->m_liveCounts = counts;
                    emit self->scanProgressChanged();
                    if (self->m_scanProgressVisible && !self->m_categories.isEmpty())
                        emit self->dataChanged(self->index(0), self->index(self->m_categories.size() - 1), {CountRole});
                }, Qt::QueuedConnection);
            };
            mergeResult(aggregate, ContentIndexModel::scanTree(rootPath, enabledHidden, categoryRoots,
                                                                ignoreRules, {}, progress, cancel, !showProgress));
            ++completedRoots;
        }
        return aggregate;
    }));
}

void ContentIndexModel::startCategoryScan(const QString &category, bool showProgress)
{
    const QString key = category.trimmed().toLower();
    if (!isKnownCategory(key))
        return;
    if (m_scanRunning) {
        m_pendingCategoryRefreshes.insert(key);
        return;
    }

    setScanProgressVisible(showProgress);
    setIndexing(true);
    m_scanRunning = true;
    m_activeSubtree.clear();
    m_activeCategory = key;
    m_liveCounts.clear();
    m_scanProcessedDirectories = 0;
    m_scanVisitedEntries = 0;
    m_scanMatchedFiles = 0;
    const quint64 generation = ++m_scanGeneration;

    QHash<QString, QStringList> categoryRoots;
    categoryRoots.insert(key, m_mediaRoots.value(key));
    QHash<QString, QStringList> ignoreRules;
    ignoreRules.insert(key, m_ignoreRules.value(key));
    QSet<QString> enabledHidden;
    if (categoryIndexHidden(key))
        enabledHidden.insert(key);
    const QSet<QString> categoryFilter = {key};

    QStringList candidates;
    for (const QString &value : m_mediaRoots.value(key)) {
        const QString root = normalizedPath(value);
        if (!root.isEmpty() && QFileInfo(root).isDir())
            candidates.push_back(root);
    }
    std::sort(candidates.begin(), candidates.end(), [](const QString &a, const QString &b) {
        if (a.size() != b.size())
            return a.size() < b.size();
        return QString::localeAwareCompare(a, b) < 0;
    });
    QStringList scanRoots;
    for (const QString &candidate : candidates) {
        bool covered = false;
        for (const QString &existing : std::as_const(scanRoots)) {
            if (pathWithinRoot(candidate, existing)) {
                covered = true;
                break;
            }
        }
        if (!covered)
            scanRoots.push_back(candidate);
    }
    m_scanPendingDirectories = scanRoots.size();
    m_scanCancel = std::make_shared<std::atomic_bool>(false);
    const auto cancel = m_scanCancel;
    emit scanProgressChanged();

    QPointer<ContentIndexModel> self(this);
    m_scanWatcher.setFuture(QtConcurrent::run(m_scanPool, [self, generation, key, enabledHidden, categoryRoots,
                                                            ignoreRules, categoryFilter, scanRoots, cancel, showProgress] {
        auto mergeResult = [](ScanResult &target, const ScanResult &source) {
            for (auto it = source.files.constBegin(); it != source.files.constEnd(); ++it)
                target.files.insert(it.key(), it.value());
            for (auto it = source.counts.constBegin(); it != source.counts.constEnd(); ++it)
                target.counts[it.key()] = target.counts.value(it.key(), 0) + it.value();
            QSet<QString> known;
            for (const QString &directory : std::as_const(target.directories))
                known.insert(directory);
            for (const QString &directory : source.directories) {
                if (!known.contains(directory)) {
                    known.insert(directory);
                    target.directories.push_back(directory);
                }
            }
        };

        ScanResult aggregate;
        int completedRoots = 0;
        for (const QString &rootPath : scanRoots) {
            if (cancel && cancel->load(std::memory_order_relaxed))
                return ScanResult{};
            const ProgressCallback progress = [self, generation, key, completedRoots, scanRoots](int processedDirectories,
                                                                                                  int pendingDirectories,
                                                                                                  int visitedEntries,
                                                                                                  int matchedFiles,
                                                                                                  const QHash<QString, int> &counts) {
                if (!self)
                    return;
                QMetaObject::invokeMethod(self, [self, generation, key, completedRoots, scanRoots, processedDirectories,
                                                 pendingDirectories, visitedEntries, matchedFiles, counts] {
                    if (!self || self->m_scanGeneration != generation || !self->m_scanRunning
                            || self->m_activeCategory != key)
                        return;
                    self->m_scanProcessedDirectories = processedDirectories + completedRoots;
                    self->m_scanPendingDirectories = pendingDirectories + qMax(0, int(scanRoots.size()) - completedRoots - 1);
                    self->m_scanVisitedEntries = visitedEntries;
                    self->m_scanMatchedFiles = matchedFiles;
                    self->m_liveCounts = counts;
                    emit self->scanProgressChanged();
                }, Qt::QueuedConnection);
            };
            mergeResult(aggregate, ContentIndexModel::scanTree(rootPath, enabledHidden, categoryRoots,
                                                                ignoreRules, categoryFilter, progress, cancel, !showProgress));
            ++completedRoots;
        }
        return aggregate;
    }));
}

void ContentIndexModel::startPendingScan()
{
    if (m_scanRunning)
        return;
    if (m_pendingRoots.isEmpty()) {
        setIndexing(false);
        setScanProgressVisible(false);
        return;
    }

    QString root = *m_pendingRoots.constBegin();
    m_pendingRoots.remove(root);
    // If a parent and child are both pending, scanning the parent covers both.
    for (auto it = m_pendingRoots.begin(); it != m_pendingRoots.end();) {
        if (it->startsWith(root + QLatin1Char('/')))
            it = m_pendingRoots.erase(it);
        else if (root.startsWith(*it + QLatin1Char('/'))) {
            root = *it;
            it = m_pendingRoots.erase(it);
        } else {
            ++it;
        }
    }

    setScanProgressVisible(false);
    setIndexing(true);
    m_scanRunning = true;
    m_activeSubtree = root;
    m_activeCategory.clear();
    const QSet<QString> enabledHidden = hiddenCategories();
    const QHash<QString, QStringList> categoryRoots = m_mediaRoots;
    const QHash<QString, QStringList> ignoreRules = m_ignoreRules;
    m_scanCancel = std::make_shared<std::atomic_bool>(false);
    const auto cancel = m_scanCancel;
    m_scanWatcher.setFuture(QtConcurrent::run(m_scanPool, [root, enabledHidden, categoryRoots, ignoreRules, cancel] {
        return ContentIndexModel::scanTree(root, enabledHidden, categoryRoots, ignoreRules, {}, {}, cancel, true);
    }));
}

void ContentIndexModel::probeMediaRootAvailability()
{
    QSet<QString> configured;
    QSet<QString> changedCategories;
    for (auto it = m_mediaRoots.constBegin(); it != m_mediaRoots.constEnd(); ++it) {
        for (const QString &root : it.value()) {
            configured.insert(root);
            const bool available = QFileInfo(root).isDir();
            if (!m_mediaRootAvailability.contains(root) || m_mediaRootAvailability.value(root) != available) {
                m_mediaRootAvailability.insert(root, available);
                changedCategories.insert(it.key());
            }
        }
    }
    for (auto it = m_mediaRootAvailability.begin(); it != m_mediaRootAvailability.end();) {
        if (!configured.contains(it.key()))
            it = m_mediaRootAvailability.erase(it);
        else
            ++it;
    }
    for (const QString &category : changedCategories) {
        if (m_scanRunning)
            m_pendingCategoryRefreshes.insert(category);
        else
            startCategoryScan(category, false);
    }
}

void ContentIndexModel::notifyCountsChanged(const QSet<QString> &changedCategories)
{
    if (!m_categories.isEmpty())
        emit dataChanged(index(0), index(m_categories.size() - 1), {CountRole});
    emit indexChanged();
    emit categoryFilesChanged(changedCategories.isEmpty()
            ? allCategoryKeys() : changedCategories.values());
}

void ContentIndexModel::applyFullScan(const ScanResult &result)
{
    bool changed = m_files.size() != result.files.size();
    if (!changed) {
        for (auto it = m_files.constBegin(); it != m_files.constEnd(); ++it) {
            const auto fresh = result.files.constFind(it.key());
            if (fresh == result.files.constEnd() || fresh->category != it->category
                    || fresh->size != it->size || fresh->modifiedMs != it->modifiedMs) {
                changed = true;
                break;
            }
        }
    }
    if (changed) {
        m_files = result.files;
        m_counts = result.counts;
    }
    m_liveCounts.clear();
    m_scanProcessedDirectories = result.directories.size();
    m_scanPendingDirectories = 0;
    m_scanVisitedEntries = qMax(m_scanVisitedEntries, m_files.size());
    m_scanMatchedFiles = m_files.size();
    emit scanProgressChanged();
    rebuildWatchers(result.directories);
    // An unchanged startup scan must not tear down/reload an open category
    // or rewrite the same JSON cache. Publish only actual file changes.
    if (changed) {
        m_saveDebounce.start();
        notifyCountsChanged();
    }
}

void ContentIndexModel::applyCategoryScan(const QString &category, const ScanResult &result)
{
    bool changed = false;
    int oldCount = 0;
    for (auto it = m_files.constBegin(); it != m_files.constEnd(); ++it) {
        if (it->category != category)
            continue;
        ++oldCount;
        const auto fresh = result.files.constFind(it.key());
        if (fresh == result.files.constEnd() || fresh->category != it->category
                || fresh->size != it->size || fresh->modifiedMs != it->modifiedMs)
            changed = true;
    }
    changed = changed || oldCount != result.files.size();

    if (changed) {
        for (auto it = m_files.begin(); it != m_files.end();) {
            if (it->category == category)
                it = m_files.erase(it);
            else
                ++it;
        }
        for (auto it = result.files.constBegin(); it != result.files.constEnd(); ++it)
            m_files.insert(it.key(), it.value());
        const int count = result.counts.value(category, 0);
        if (count > 0)
            m_counts[category] = count;
        else
            m_counts.remove(category);
        notifyCountsChanged({category});
    }
    // Persist the new category settings signature even when the resulting file
    // set is identical to the previous one. Otherwise the next launch would
    // discard a perfectly valid cache after a rules-only change.
    m_saveDebounce.start();

    m_liveCounts.clear();
    m_scanProcessedDirectories = result.directories.size();
    m_scanPendingDirectories = 0;
    m_scanMatchedFiles = result.files.size();
    emit scanProgressChanged();
    addWatchers(result.directories);
}

void ContentIndexModel::applySubtreeScan(const QString &rootPath, const ScanResult &result)
{
    const QString prefix = QDir::cleanPath(rootPath) + QLatin1Char('/');
    bool changed = false;
    QSet<QString> changedCategories;
    int oldCount = 0;
    for (auto it = m_files.constBegin(); it != m_files.constEnd(); ++it) {
        if (it.key() != rootPath && !it.key().startsWith(prefix))
            continue;
        ++oldCount;
        const auto fresh = result.files.constFind(it.key());
        if (fresh == result.files.constEnd() || fresh->category != it->category
                || fresh->size != it->size || fresh->modifiedMs != it->modifiedMs) {
            changed = true;
            changedCategories.insert(it->category);
            if (fresh != result.files.constEnd())
                changedCategories.insert(fresh->category);
        }
    }
    for (auto it = result.files.constBegin(); it != result.files.constEnd(); ++it) {
        if (!m_files.contains(it.key()))
            changedCategories.insert(it->category);
    }
    changed = changed || oldCount != result.files.size();
    if (!changed) {
        addWatchers(result.directories);
        return;
    }

    for (auto it = m_files.begin(); it != m_files.end();) {
        if (it.key() == rootPath || it.key().startsWith(prefix)) {
            const QString category = it->category;
            const int nextCount = qMax(0, m_counts.value(category, 0) - 1);
            if (nextCount == 0)
                m_counts.remove(category);
            else
                m_counts[category] = nextCount;
            it = m_files.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = result.files.constBegin(); it != result.files.constEnd(); ++it) {
        m_files.insert(it.key(), it.value());
        m_counts[it->category] = m_counts.value(it->category, 0) + 1;
    }

    addWatchers(result.directories);

    m_saveDebounce.start();
    notifyCountsChanged(changedCategories);
}

void ContentIndexModel::rebuildWatchers(const QStringList &directories)
{
    // Watch visible directories first. Explicitly enabled hidden trees can be
    // enormous, so keep a fixed ceiling and remove watches made obsolete by
    // changed indexing preferences.
    QStringList selected;
    selected.reserve(qMin(kMaxWatchedDirectories, directories.size()));
    QSet<QString> selectedSet;
    for (int pass = 0; pass < 2 && selected.size() < kMaxWatchedDirectories; ++pass) {
        for (const QString &path : directories) {
            if (isHiddenWithinHome(path) != (pass == 1) || selectedSet.contains(path))
                continue;
            selected.push_back(path);
            selectedSet.insert(path);
            if (selected.size() == kMaxWatchedDirectories)
                break;
        }
    }

    m_watchQueue.clear();
    m_queuedWatchers.clear();
    QStringList obsolete;
    for (const QString &path : m_watcher.directories()) {
        if (!selectedSet.contains(path))
            obsolete.push_back(path);
    }
    if (!obsolete.isEmpty())
        m_watcher.removePaths(obsolete);
    queueWatchers(selected);
}

void ContentIndexModel::addWatchers(const QStringList &directories)
{
    queueWatchers(directories);
}

void ContentIndexModel::queueWatchers(const QStringList &directories)
{
    QSet<QString> known = m_queuedWatchers;
    const QStringList watchedDirectories = m_watcher.directories();
    for (const QString &path : watchedDirectories)
        known.insert(path);

    for (const QString &path : directories) {
        if (known.size() >= kMaxWatchedDirectories)
            break;
        if (path.isEmpty() || known.contains(path))
            continue;
        m_watchQueue.push_back(path);
        m_queuedWatchers.insert(path);
        known.insert(path);
    }

    if (!m_watchQueue.isEmpty() && !m_watchInstallTimer.isActive())
        m_watchInstallTimer.start();
}

void ContentIndexModel::installWatcherBatch()
{
    constexpr int kWatchBatchSize = 16;
    if (m_watchQueue.isEmpty())
        return;

    QStringList batch;
    batch.reserve(qMin(kWatchBatchSize, m_watchQueue.size()));
    for (int i = 0; i < kWatchBatchSize && !m_watchQueue.isEmpty(); ++i) {
        const QString path = m_watchQueue.takeFirst();
        m_queuedWatchers.remove(path);
        if (QFileInfo(path).isDir())
            batch.push_back(path);
    }

    if (!batch.isEmpty())
        m_watcher.addPaths(batch);

    if (!m_watchQueue.isEmpty())
        m_watchInstallTimer.start();
}
