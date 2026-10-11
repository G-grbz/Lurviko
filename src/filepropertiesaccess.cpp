#include "filepropertiesmanager.h"
#include "fileaccesssecurity.h"

#include <QDir>
#include <QFile>
#include <QFutureWatcher>
#include <QTimer>
#include <QLocale>
#include <QSettings>
#include <QtConcurrent/QtConcurrentRun>
#include <KUser>
#include <KProtocolInfo>
#include <KIO/ListJob>
#include <KIO/SimpleJob>
#include <KIO/StatJob>
#include <algorithm>
#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
QString securityMessage(const char *english, const char *turkish)
{
    const QString fallback = QLocale::system().language() == QLocale::Turkish ? QStringLiteral("tr") : QStringLiteral("en");
    return QString::fromUtf8(QSettings().value(QStringLiteral("ui/language"), fallback).toString() == QStringLiteral("tr")
                                ? turkish : english);
}
QStringList sortedNames(QStringList names, const QString &current)
{
    if (!current.isEmpty()) names.append(current);
    names.removeDuplicates();
    names.sort(Qt::CaseInsensitive);
    return names;
}
}

void FilePropertiesManager::initializeAccess()
{
    m_accessTimer = new QTimer(this);
    m_accessTimer->setInterval(100);
    connect(m_accessTimer, &QTimer::timeout, this, &FilePropertiesManager::accessProgressChanged);
}

FilePropertiesManager::~FilePropertiesManager() { cancelAccess(); }
int FilePropertiesManager::accessCompleted() const { return m_accessTask ? m_accessTask->changed.load() : 0; }
int FilePropertiesManager::accessSkipped() const { return m_accessTask ? m_accessTask->skipped.load() : 0; }
int FilePropertiesManager::accessFailed() const { return m_accessTask ? m_accessTask->failed.load() : 0; }
QString FilePropertiesManager::specialPermissionsHint() const
{
    return securityMessage("Special bits (setuid, setgid and sticky) cannot be edited here. Existing bits are preserved; ownership changes may clear setuid/setgid. Recursive local changes stay on the selected filesystem.",
                           "Özel bitler (setuid, setgid ve sticky) burada değiştirilemez. Mevcut bitler korunur; sahiplik değişikliği setuid/setgid bitlerini silebilir. Yerel özyinelemeli değişiklikler seçilen dosya sistemiyle sınırlıdır.");
}
QString FilePropertiesManager::accessSkippedSummary() const
{
    return securityMessage(" · Skipped mount entries: %1", " · Atlanan bağlama öğeleri: %1").arg(accessSkipped());
}
bool FilePropertiesManager::administratorAvailable() const { return KProtocolInfo::isKnownProtocol(QStringLiteral("admin")); }

QStringList FilePropertiesManager::availableOwners(bool administrator) const
{
    return sortedNames(administrator || ::geteuid() == 0 ? KUser::allUserNames() : QStringList{}, m_ownerName);
}
QStringList FilePropertiesManager::availableGroups(bool administrator) const
{
    return sortedNames(administrator || ::geteuid() == 0 ? KUserGroup::allGroupNames() : KUser().groupNames(), m_groupName);
}

bool FilePropertiesManager::applyAccess(int mode, const QString &owner, const QString &group, bool recursive, bool administrator)
{
    if (m_accessBusy || !m_localFile || m_path.isEmpty() || mode < 0 || mode > 0777) return false;
    struct stat st {};
    const bool exists = ::lstat(QFile::encodeName(m_path).constData(), &st) == 0;
    if (exists && (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode))) {
        emit error(tr("Symbolic link or special-file permissions cannot be changed here.")); return false;
    }
    if (administrator && !administratorAvailable()) {
        emit error(tr("Install kio-admin to change access using administrator authentication.")); return false;
    }
    // Keep unchanged numeric ownership names usable even when NSS cannot
    // resolve a removed account. New selections must be real system accounts.
    uid_t uid = static_cast<uid_t>(-1);
    gid_t gid = static_cast<gid_t>(-1);
    if (owner != m_ownerName && !owner.isEmpty()) {
        const auto account = ::getpwnam(owner.toLocal8Bit().constData());
        if (!account) { emit error(tr("Unknown user.")); return false; }
        uid = account->pw_uid;
    }
    if (group != m_groupName && !group.isEmpty()) {
        const auto account = ::getgrnam(group.toLocal8Bit().constData());
        if (!account) { emit error(tr("Unknown group.")); return false; }
        gid = account->gr_gid;
    }
    if (!administrator && (!exists || (st.st_uid != ::geteuid() && ::geteuid() != 0)
                            || (uid != static_cast<uid_t>(-1) && uid != st.st_uid && ::geteuid() != 0))) {
        emit error(tr("Use administrator authentication to change this item's access or owner.")); return false;
    }
    m_accessTask = std::make_shared<FileAccessTask>();
    m_accessPath = m_path;
    m_accessOwner = owner;
    m_accessGroup = group;
    m_accessMode = mode;
    m_accessError.clear();
    m_accessBusy = true;
    m_accessTimer->start();
    emit accessProgressChanged();
    if (administrator) { startAdministratorAccess(recursive); return true; }
    // For recursive ownership the selected unchanged owner/group are still
    // applied to descendants, subject to the caller's own OS permissions.
    if (recursive) {
        if (!owner.isEmpty()) { if (const auto user = ::getpwnam(owner.toLocal8Bit().constData())) uid = user->pw_uid; }
        if (!group.isEmpty()) { if (const auto item = ::getgrnam(group.toLocal8Bit().constData())) gid = item->gr_gid; }
    }
    auto *watcher = new QFutureWatcher<void>(this);
    const auto task = m_accessTask;
    const QString path = m_path;
    connect(watcher, &QFutureWatcher<void>::finished, this, [this, watcher, task]() {
        watcher->deleteLater(); m_accessError = task->firstError; finishAccess(task->cancelled);
    });
    watcher->setFuture(QtConcurrent::run([path, mode, uid, gid, recursive, task]() {
        FileAccessSecurity::applyLocalAccess(path, mode, uid, gid, recursive, task);
    }));
    return true;
}

void FilePropertiesManager::cancelAccess()
{
    if (!m_accessBusy || !m_accessTask) return;
    m_accessTask->cancelled = true;
    if (m_accessJob) {
        m_accessJob->kill(KJob::Quietly); m_accessJob.clear(); finishAccess(true);
    }
}

void FilePropertiesManager::finishAccess(bool cancelled)
{
    if (!m_accessBusy) return;
    m_accessBusy = false;
    m_accessTimer->stop();
    m_accessJob.clear();
    m_accessEntries.clear();
    if (m_path == m_accessPath) { refreshPermissions(); emit propertiesChanged(); }
    emit accessProgressChanged();
    if (accessCompleted()) emit permissionsApplied(m_accessPath);
    emit accessFinished(!cancelled && accessFailed() == 0, cancelled, accessCompleted(), accessFailed());
}

void FilePropertiesManager::startAdministratorAccess(bool recursive)
{
    QUrl root = QUrl::fromLocalFile(m_accessPath);
    root.setScheme(QStringLiteral("admin"));
    m_accessEntries = {{root, false}};
    if (!recursive) { continueAdministratorAccess(); return; }
    // Complete a no-follow local preflight before any privileged list/stat/job.
    // An unreadable tree is not assumed safe merely because admin:// can read it.
    struct stat selected {};
    if (::lstat(QFile::encodeName(m_accessPath).constData(), &selected) != 0) {
        ++m_accessTask->failed;
        m_accessError = securityMessage("Cannot safely inspect this tree before administrator changes.",
                                       "Yönetici değişikliklerinden önce bu ağaç güvenle denetlenemiyor.");
        emit error(m_accessError); finishAccess(false); return;
    }
    uid_t targetOwner = selected.st_uid;
    if (!m_accessOwner.isEmpty()) {
        if (const auto user = ::getpwnam(m_accessOwner.toLocal8Bit().constData()))
            targetOwner = user->pw_uid;
    }
    auto *watcher = new QFutureWatcher<FileAccessSecurity::TreeCheck>(this);
    const auto task = m_accessTask;
    const QString path = m_accessPath;
    connect(watcher, &QFutureWatcher<FileAccessSecurity::TreeCheck>::finished, this, [this, watcher, task, root] {
        const auto check = watcher->result(); watcher->deleteLater();
        if (task->cancelled) { finishAccess(true); return; }
        if (!check.safe) {
            ++task->failed;
            m_accessError = securityMessage(
                "Recursive administrator changes were refused. All directories must belong to root or the selected owner, must not allow group/other writes, and must be locally readable for safety checks.",
                "Özyinelemeli yönetici değişiklikleri reddedildi. Tüm dizinler root veya seçilen kullanıcıya ait olmalı, grup/diğerleri tarafından yazılamamalı ve güvenlik denetimi için yerel olarak okunabilmelidir.")
                + QStringLiteral("\n") + check.path;
            emit error(m_accessError); finishAccess(false); return;
        }
        listAdministratorTree(root);
    });
    watcher->setFuture(QtConcurrent::run([path, targetOwner, task] {
        return FileAccessSecurity::checkAdministratorTree(path, targetOwner, &task->cancelled);
    }));
}

void FilePropertiesManager::listAdministratorTree(const QUrl &root)
{
    if (m_accessTask->cancelled) { finishAccess(true); return; }
    auto *job = KIO::listRecursive(root, KIO::HideProgressInfo);
    m_accessJob = job;
    connect(job, &KIO::ListJob::entries, this, [this, root](KIO::Job *, const KIO::UDSEntryList &entries) {
        for (const auto &entry : entries) {
            if (!entry.stringValue(KIO::UDSEntry::UDS_LINK_DEST).isEmpty()) continue;
            const QString name = entry.stringValue(KIO::UDSEntry::UDS_NAME);
            const QString clean = QDir::cleanPath(name);
            if (clean == "." || clean == ".." || clean.startsWith("../") || QDir::isAbsolutePath(clean)) continue;
            QUrl url = root; url.setPath(QDir(root.path()).filePath(clean));
            m_accessEntries.append({url, true});
        }
    });
    connect(job, &KJob::result, this, [this, job]() {
        m_accessJob.clear();
        if (job->error()) { ++m_accessTask->failed; m_accessError = job->errorString(); finishAccess(false); return; }
        std::sort(m_accessEntries.begin(), m_accessEntries.end(), [](const AccessEntry &a, const AccessEntry &b) {
            return a.url.path().count('/') < b.url.path().count('/');
        });
        continueAdministratorAccess();
    });
}

void FilePropertiesManager::continueAdministratorAccess()
{
    if (!m_accessBusy) return;
    if (m_accessTask->cancelled) { finishAccess(true); return; }
    if (m_accessEntries.isEmpty()) { finishAccess(false); return; }
    const auto entry = m_accessEntries.takeLast();
    auto *stat = KIO::stat(entry.url, KIO::HideProgressInfo);
    m_accessJob = stat;
    connect(stat, &KJob::result, this, [this, stat, entry]() {
        m_accessJob.clear();
        if (stat->error()) { ++m_accessTask->failed; m_accessError = stat->errorString(); finishAccess(false); return; }
        const auto info = stat->statResult();
        const mode_t type = info.numberValue(KIO::UDSEntry::UDS_FILE_TYPE);
        if (!info.stringValue(KIO::UDSEntry::UDS_LINK_DEST).isEmpty() || S_ISLNK(type)) {
            QTimer::singleShot(0, this, &FilePropertiesManager::continueAdministratorAccess); return;
        }
        if (!S_ISREG(type) && !S_ISDIR(type)) {
            QTimer::singleShot(0, this, &FilePropertiesManager::continueAdministratorAccess); return;
        }
        const int oldMode = info.numberValue(KIO::UDSEntry::UDS_ACCESS);
        const QString oldOwner = info.stringValue(KIO::UDSEntry::UDS_USER);
        const QString oldGroup = info.stringValue(KIO::UDSEntry::UDS_GROUP);
        const bool ownershipChanged = (!m_accessOwner.isEmpty() && m_accessOwner != oldOwner)
                                      || (!m_accessGroup.isEmpty() && m_accessGroup != oldGroup);
        int mode = m_accessMode;
        if (entry.descendant && S_ISREG(type) && !(oldMode & 0111) && (mode & 0111)) mode &= ~0111;
        mode |= oldMode & (ownershipChanged ? S_ISVTX : (S_ISUID | S_ISGID | S_ISVTX));
        const auto chmod = [this, entry, mode]() {
            if (m_accessTask->cancelled) { finishAccess(true); return; }
            auto *job = KIO::chmod(entry.url, mode);
            m_accessJob = job;
            connect(job, &KJob::result, this, [this, job]() {
                m_accessJob.clear();
                if (job->error()) { ++m_accessTask->failed; m_accessError = job->errorString(); finishAccess(false); return; }
                ++m_accessTask->changed;
                QTimer::singleShot(0, this, &FilePropertiesManager::continueAdministratorAccess);
            });
        };
        if (!ownershipChanged) { chmod(); return; }
        auto *job = KIO::chown(entry.url, m_accessOwner.isEmpty() ? oldOwner : m_accessOwner,
                              m_accessGroup.isEmpty() ? oldGroup : m_accessGroup);
        m_accessJob = job;
        connect(job, &KJob::result, this, [this, job, chmod]() {
            m_accessJob.clear();
            if (job->error()) { ++m_accessTask->failed; m_accessError = job->errorString(); finishAccess(false); return; }
            chmod();
        });
    });
}
