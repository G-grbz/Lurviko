#include "filepropertiesmanager.h"

#include <QDir>
#include <QFile>
#include <QFutureWatcher>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <KUser>
#include <KProtocolInfo>
#include <KIO/ListJob>
#include <KIO/SimpleJob>
#include <KIO/StatJob>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

struct FileAccessTask {
    std::atomic<bool> cancelled{false};
    std::atomic<int> changed{0};
    std::atomic<int> failed{0};
    QString firstError; // Read by the UI only after the future has finished.
};

namespace {
QStringList sortedNames(QStringList names, const QString &current)
{
    if (!current.isEmpty()) names.append(current);
    names.removeDuplicates();
    names.sort(Qt::CaseInsensitive);
    return names;
}

void recordFailure(const std::shared_ptr<FileAccessTask> &task, const QString &path, int error)
{
    ++task->failed;
    if (task->firstError.isEmpty())
        task->firstError = path + QStringLiteral(": ") + QString::fromLocal8Bit(std::strerror(error));
}

void applyLocalAccess(const QString &path, int mode, uid_t owner, gid_t group, bool recursive,
                      const std::shared_ptr<FileAccessTask> &task)
{
    const auto apply = [&](int parent, const QByteArray &name, const QString &display, bool descendant) {
        if (task->cancelled) return;
        struct stat st {};
        if (::fstatat(parent, name.constData(), &st, AT_SYMLINK_NOFOLLOW) != 0) {
            recordFailure(task, display, errno); return;
        }
        if (S_ISLNK(st.st_mode) || (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode))) return;
        if ((owner != static_cast<uid_t>(-1) && owner != st.st_uid)
            || (group != static_cast<gid_t>(-1) && group != st.st_gid)) {
            if (::fchownat(parent, name.constData(), owner, group, AT_SYMLINK_NOFOLLOW) != 0) {
                recordFailure(task, display, errno); return;
            }
            // chown may clear setuid/setgid. Do not restore cleared privilege bits.
            if (::fstatat(parent, name.constData(), &st, AT_SYMLINK_NOFOLLOW) != 0) {
                recordFailure(task, display, errno); return;
            }
            if (S_ISLNK(st.st_mode)) { recordFailure(task, display, ELOOP); return; }
        }
        int requested = mode;
        // Match Dolphin's recursive +X behavior: ordinary documents stay
        // non-executable; directories and existing programs receive execute bits.
        if (descendant && S_ISREG(st.st_mode) && !(st.st_mode & 0111) && (mode & 0111))
            requested &= ~0111;
        requested |= st.st_mode & (S_ISUID | S_ISGID | S_ISVTX);
        if (::fchmodat(parent, name.constData(), requested, AT_SYMLINK_NOFOLLOW) != 0)
            recordFailure(task, display, errno);
        else
            ++task->changed;
    };
    if (!recursive) { apply(AT_FDCWD, QFile::encodeName(path), path, false); return; }

    const int root = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (root < 0) { recordFailure(task, path, errno); return; }
    DIR *rootStream = ::fdopendir(root);
    if (!rootStream) { const int error = errno; ::close(root); recordFailure(task, path, error); return; }
    struct Frame { DIR *stream; QString path; };
    std::vector<Frame> stack{{rootStream, path}};
    while (!stack.empty() && !task->cancelled) {
        const Frame frame = stack.back();
        errno = 0;
        dirent *entry = ::readdir(frame.stream);
        if (!entry) {
            if (errno) recordFailure(task, frame.path, errno);
            // Open directory descriptors keep traversal inside the selected
            // tree. Change directories after their children, even for 0000.
            apply(::dirfd(frame.stream), QByteArray("."), frame.path, stack.size() > 1);
            ::closedir(frame.stream);
            stack.pop_back();
            continue;
        }
        const QByteArray name(entry->d_name);
        if (name == "." || name == "..") continue;
        const QString display = QDir(frame.path).filePath(QFile::decodeName(name));
        struct stat st {};
        const int parent = ::dirfd(frame.stream);
        if (::fstatat(parent, name.constData(), &st, AT_SYMLINK_NOFOLLOW) != 0) {
            recordFailure(task, display, errno); continue;
        }
        if (S_ISLNK(st.st_mode)) continue;
        if (S_ISDIR(st.st_mode)) {
            const int fd = ::openat(parent, name.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (fd < 0) { recordFailure(task, display, errno); continue; }
            DIR *stream = ::fdopendir(fd);
            if (!stream) { const int error = errno; ::close(fd); recordFailure(task, display, error); continue; }
            stack.push_back({stream, display});
        } else {
            apply(parent, name, display, true);
        }
    }
    for (const auto &frame : stack) ::closedir(frame.stream);
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
int FilePropertiesManager::accessFailed() const { return m_accessTask ? m_accessTask->failed.load() : 0; }
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
        applyLocalAccess(path, mode, uid, gid, recursive, task);
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
