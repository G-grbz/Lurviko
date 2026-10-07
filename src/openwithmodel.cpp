#include "openwithmodel.h"

#include <QMimeDatabase>
#include <QProcess>
#include <QSettings>
#include <QSet>
#include <QUrl>
#include <algorithm>

#include <KApplicationTrader>
#include <KService>
#include <KIO/ApplicationLauncherJob>
#include <KIO/OpenUrlJob>
#include <KJob>

OpenWithModel::OpenWithModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int OpenWithModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_apps.size();
}

QVariant OpenWithModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_apps.size())
        return {};

    const AppEntry &entry = m_apps.at(index.row());
    switch (role) {
    case NameRole: return entry.name;
    case IconNameRole: return entry.iconName;
    case DesktopEntryRole: return entry.desktopEntry;
    case IsDefaultRole: return entry.isDefault;
    default: return {};
    }
}

QHash<int, QByteArray> OpenWithModel::roleNames() const
{
    return {
        {NameRole, "name"},
        {IconNameRole, "iconName"},
        {DesktopEntryRole, "desktopEntry"},
        {IsDefaultRole, "isDefault"}
    };
}

void OpenWithModel::setContext(const QString &itemUrl, const QString &mimeType)
{
    const bool changed = m_itemUrl != itemUrl || m_mimeType != mimeType || m_itemUrls != QStringList{itemUrl};
    m_itemUrl = itemUrl;
    m_itemUrls = itemUrl.isEmpty() ? QStringList{} : QStringList{itemUrl};
    m_mimeType = mimeType;
    if (changed)
        emit contextChanged();
    refresh();
}

void OpenWithModel::setContextMany(const QStringList &itemUrls)
{
    QStringList clean;
    clean.reserve(itemUrls.size());
    for (const QString &url : itemUrls) {
        if (!url.trimmed().isEmpty() && !clean.contains(url))
            clean.push_back(url);
    }

    const QString first = clean.isEmpty() ? QString() : clean.constFirst();
    const bool changed = m_itemUrls != clean || m_itemUrl != first || !m_mimeType.isEmpty();
    m_itemUrls = clean;
    m_itemUrl = first;
    m_mimeType.clear();
    if (changed)
        emit contextChanged();
    refresh();
}

void OpenWithModel::rebuildQuickApps()
{
    QVariantList quick;
    const int count = std::min(4, int(m_apps.size()));
    for (int i = 0; i < count; ++i) {
        const AppEntry &entry = m_apps.at(i);
        QVariantMap map;
        map.insert(QStringLiteral("name"), entry.name);
        map.insert(QStringLiteral("iconName"), entry.iconName);
        map.insert(QStringLiteral("desktopEntry"), entry.desktopEntry);
        map.insert(QStringLiteral("isDefault"), entry.isDefault);
        quick.push_back(map);
    }
    if (quick != m_quickApps) {
        m_quickApps = quick;
        emit quickAppsChanged();
    }
}

void OpenWithModel::refresh()
{
    beginResetModel();
    m_apps.clear();
    m_defaultDesktopEntry.clear();

    QStringList mimeTypes;
    QMimeDatabase db;
    if (m_itemUrls.size() == 1 && !m_mimeType.trimmed().isEmpty()
            && m_mimeType != QStringLiteral("application/octet-stream")) {
        mimeTypes.push_back(m_mimeType.trimmed());
    } else {
        for (const QString &itemUrl : m_itemUrls) {
            const QUrl url = QUrl::fromUserInput(itemUrl);
            const QString mime = db.mimeTypeForUrl(url).name();
            if (!mime.isEmpty() && !mimeTypes.contains(mime))
                mimeTypes.push_back(mime);
        }
    }

    if (!mimeTypes.isEmpty()) {
        if (mimeTypes.size() == 1) {
            const KService::Ptr preferred = KApplicationTrader::preferredService(mimeTypes.constFirst());
            if (preferred)
                m_defaultDesktopEntry = preferred->desktopEntryName();
        }

        QSet<QString> availableEntries;
        QHash<QString, AppEntry> appByDesktop;
        QStringList applicationOrder;

        for (const QString &mime : mimeTypes) {
            const KService::List services = KApplicationTrader::queryByMimeType(mime);
            for (const KService::Ptr &service : services) {
                if (!service || !service->isApplication() || service->noDisplay())
                    continue;
                const QString desktopEntry = service->desktopEntryName();
                if (desktopEntry.isEmpty())
                    continue;

                if (!availableEntries.contains(desktopEntry)) {
                    availableEntries.insert(desktopEntry);
                    applicationOrder.push_back(desktopEntry);
                }
                if (!appByDesktop.contains(desktopEntry)) {
                    AppEntry entry;
                    entry.name = service->name();
                    entry.iconName = service->icon();
                    entry.desktopEntry = desktopEntry;
                    entry.isDefault = desktopEntry == m_defaultDesktopEntry;
                    appByDesktop.insert(desktopEntry, entry);
                }
            }
        }

        // “Open With” is intentionally a forced choice: for mixed MIME types,
        // offer the union of applications known for any selected file rather than
        // requiring one desktop entry to advertise every MIME type. The chosen
        // application receives the complete URL list.
        for (const QString &desktopEntry : applicationOrder) {
            if (appByDesktop.contains(desktopEntry))
                m_apps.push_back(appByDesktop.value(desktopEntry));
        }

        QSettings settings;
        const QStringList recent = settings.value(QStringLiteral("openWith/recentApplications")).toStringList();
        std::stable_sort(m_apps.begin(), m_apps.end(), [&recent, this](const AppEntry &a, const AppEntry &b) {
            const int aiRaw = recent.indexOf(a.desktopEntry);
            const int biRaw = recent.indexOf(b.desktopEntry);
            const int ai = aiRaw < 0 ? 100000 : aiRaw;
            const int bi = biRaw < 0 ? 100000 : biRaw;
            if (ai != bi)
                return ai < bi;
            if (a.isDefault != b.isDefault)
                return a.isDefault;
            return false;
        });
    }

    endResetModel();
    emit countChanged();
    emit defaultChanged();
    rebuildQuickApps();
}

void OpenWithModel::rememberApplication(const QString &desktopEntry)
{
    if (desktopEntry.isEmpty())
        return;
    QSettings settings;
    QStringList recent = settings.value(QStringLiteral("openWith/recentApplications")).toStringList();
    recent.removeAll(desktopEntry);
    recent.prepend(desktopEntry);
    while (recent.size() > 12)
        recent.removeLast();
    settings.setValue(QStringLiteral("openWith/recentApplications"), recent);
}

void OpenWithModel::openWith(const QString &desktopEntry)
{
    if (m_itemUrls.isEmpty() || desktopEntry.isEmpty())
        return;

    const KService::Ptr service = KService::serviceByDesktopName(desktopEntry);
    if (!service) {
        emit error(QStringLiteral("Application could not be found."));
        return;
    }

    QList<QUrl> urls;
    urls.reserve(m_itemUrls.size());
    for (const QString &itemUrl : m_itemUrls) {
        const QUrl url = QUrl::fromUserInput(itemUrl);
        if (url.isValid())
            urls.push_back(url);
    }
    if (urls.isEmpty())
        return;

    auto *job = new KIO::ApplicationLauncherJob(service, this);
    job->setUrls(urls);
    connect(job, &KJob::result, this, [this, job]() {
        if (job->error())
            emit error(job->errorString());
    });
    job->start();

    rememberApplication(desktopEntry);
    refresh();
}

void OpenWithModel::openDefault(const QString &itemUrl, const QString &mimeType)
{
    if (itemUrl.isEmpty())
        return;

    const QUrl url = QUrl::fromUserInput(itemUrl);
    if (!url.isValid()) {
        emit error(QStringLiteral("Invalid URL"));
        return;
    }

    QString resolvedMime = mimeType.trimmed();
    if (resolvedMime.isEmpty() || resolvedMime == QStringLiteral("application/octet-stream"))
        resolvedMime = QMimeDatabase().mimeTypeForUrl(url).name();

    const KService::Ptr preferred = resolvedMime.isEmpty()
        ? KService::Ptr{}
        : KApplicationTrader::preferredService(resolvedMime);

    if (preferred && preferred->isApplication()) {
        auto *job = new KIO::ApplicationLauncherJob(preferred, this);
        job->setUrls({url});
        connect(job, &KJob::result, this, [this, job]() {
            if (job->error())
                emit error(job->errorString());
        });
        job->start();
        return;
    }

    auto *job = new KIO::OpenUrlJob(url, this);
    job->setRunExecutables(false);
    connect(job, &KJob::result, this, [this, job]() {
        if (job->error())
            emit error(job->errorString());
    });
    job->start();
}

void OpenWithModel::openDefaults(const QStringList &itemUrls)
{
    struct LaunchGroup {
        KService::Ptr service;
        QList<QUrl> urls;
    };

    QHash<QString, LaunchGroup> groups;
    QList<QUrl> fallbackUrls;
    QMimeDatabase db;

    for (const QString &itemUrl : itemUrls) {
        const QUrl url = QUrl::fromUserInput(itemUrl);
        if (!url.isValid())
            continue;
        const QString mime = db.mimeTypeForUrl(url).name();
        const KService::Ptr preferred = mime.isEmpty() ? KService::Ptr{} : KApplicationTrader::preferredService(mime);
        if (preferred && preferred->isApplication()) {
            const QString key = preferred->desktopEntryName();
            if (!groups.contains(key)) {
                LaunchGroup group;
                group.service = preferred;
                groups.insert(key, group);
            }
            groups[key].urls.push_back(url);
        } else {
            fallbackUrls.push_back(url);
        }
    }

    for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
        auto *job = new KIO::ApplicationLauncherJob(it.value().service, this);
        job->setUrls(it.value().urls);
        connect(job, &KJob::result, this, [this, job]() {
            if (job->error())
                emit error(job->errorString());
        });
        job->start();
    }

    for (const QUrl &url : fallbackUrls) {
        auto *job = new KIO::OpenUrlJob(url, this);
        job->setRunExecutables(false);
        connect(job, &KJob::result, this, [this, job]() {
            if (job->error())
                emit error(job->errorString());
        });
        job->start();
    }
}

bool OpenWithModel::setDefaultApplication(const QString &desktopEntry)
{
    if (m_mimeType.isEmpty() || desktopEntry.isEmpty())
        return false;

    const KService::Ptr service = KService::serviceByDesktopName(desktopEntry);
    if (!service) {
        emit error(QStringLiteral("Application could not be found."));
        return false;
    }

    KApplicationTrader::setPreferredService(m_mimeType, service);
    m_defaultDesktopEntry = desktopEntry;
    for (AppEntry &entry : m_apps)
        entry.isDefault = entry.desktopEntry == desktopEntry;

    if (!m_apps.isEmpty())
        emit dataChanged(index(0, 0), index(m_apps.size() - 1, 0), {IsDefaultRole});
    emit defaultChanged();
    rebuildQuickApps();

    QProcess::startDetached(QStringLiteral("kbuildsycoca6"), QStringList{});
    return true;
}

QString OpenWithModel::nameForDesktopEntry(const QString &desktopEntry) const
{
    for (const AppEntry &entry : m_apps) {
        if (entry.desktopEntry == desktopEntry)
            return entry.name;
    }
    const KService::Ptr service = KService::serviceByDesktopName(desktopEntry);
    return service ? service->name() : QString();
}

int OpenWithModel::indexForDesktopEntry(const QString &desktopEntry) const
{
    for (int i = 0; i < m_apps.size(); ++i) {
        if (m_apps.at(i).desktopEntry == desktopEntry)
            return i;
    }
    return -1;
}
