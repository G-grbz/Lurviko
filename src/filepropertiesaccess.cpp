#include "filepropertiesmanager.h"
#include "fileaccesssecurity.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QTimer>
#include <QLocale>
#include <QSettings>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QProcess>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtConcurrent/QtConcurrentRun>
#include <KUser>
#include <algorithm>
#include <cerrno>
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
QString administratorHelperPath()
{
    const QString directory = QCoreApplication::applicationDirPath();
    const QString installed = QDir(directory).filePath(QStringLiteral(LURVIKO_ACCESS_HELPER_RELATIVE_PATH));
    if (QFileInfo(installed).isExecutable()) return installed;
    return QDir(directory).filePath(QStringLiteral("lurviko-access-helper"));
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

FilePropertiesManager::~FilePropertiesManager() { cancelAccess(); cancelInspection(); }
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
bool FilePropertiesManager::administratorAvailable() const { return !QStandardPaths::findExecutable(QStringLiteral("pkexec")).isEmpty()
    && QFileInfo(administratorHelperPath()).isExecutable(); }

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
    return applySelectionAccess(mode, 0777, owner, group, recursive, administrator);
}

bool FilePropertiesManager::applySelectionAccess(int mode, int modeMask, const QString &owner,
                                                 const QString &group, bool recursive, bool administrator)
{
    if (m_accessBusy || !m_localFile || m_selection.isEmpty() || mode < 0 || mode > 0777
            || modeMask < 0 || modeMask > 0777) return false;
    if (administrator && !administratorAvailable()) {
        emit error(securityMessage("Install polkit and the Lurviko access helper to change access using administrator authentication.",
                                   "Yönetici yetkisiyle erişimi değiştirmek için polkit ve Lurviko izin yardımcısını kurun.")); return false;
    }
    // Keep unchanged numeric ownership names usable even when NSS cannot
    // resolve a removed account. New selections must be real system accounts.
    uid_t uid = static_cast<uid_t>(-1);
    gid_t gid = static_cast<gid_t>(-1);
    if (!owner.isEmpty() && (multiple() || recursive || owner != m_ownerName)) {
        const auto account = ::getpwnam(owner.toLocal8Bit().constData());
        if (!account && owner != m_ownerName) { emit error(tr("Unknown user.")); return false; }
        if (account) uid = account->pw_uid;
    }
    if (!group.isEmpty() && (multiple() || recursive || group != m_groupName)) {
        const auto account = ::getgrnam(group.toLocal8Bit().constData());
        if (!account && group != m_groupName) { emit error(tr("Unknown group.")); return false; }
        if (account) gid = account->gr_gid;
    }
    QStringList paths;
    for (const auto &path : m_selection) {
        struct stat st {};
        if (::lstat(QFile::encodeName(path).constData(), &st) != 0) {
            emit error(securityMessage("A selected item is no longer available. No changes were made.",
                                       "Seçili bir öğe artık mevcut değil. Hiçbir değişiklik yapılmadı.")); return false;
        }
        if (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode)) {
            if (multiple()) continue;
            emit error(securityMessage("Symbolic link or special-file permissions cannot be changed here.",
                                       "Sembolik bağlantı veya özel dosya izinleri burada değiştirilemez.")); return false;
        }
        if (!administrator && ((st.st_uid != ::geteuid() && ::geteuid() != 0)
                || (uid != static_cast<uid_t>(-1) && uid != st.st_uid && ::geteuid() != 0))) {
            emit error(securityMessage("Use administrator authentication to change the selected items' access or owner.",
                                       "Seçili öğelerin erişimini veya sahibini değiştirmek için yönetici yetkisini kullan.")); return false;
        }
        paths.append(path);
    }
    if (paths.isEmpty()) return false;
    // A selected descendant is already handled by its selected ancestor. Do not
    // apply twice or turn a recursive document into an executable on the second pass.
    if (recursive) {
        const auto roots = paths;
        paths.erase(std::remove_if(paths.begin(), paths.end(), [&roots](const QString &path) {
            for (const auto &root : roots)
                if (path != root && path.startsWith(root + QLatin1Char('/')) && QFileInfo(root).isDir()) return true;
            return false;
        }), paths.end());
    }
    m_accessTask = std::make_shared<FileAccessTask>();
    m_accessPath = m_path;
    m_accessPaths = paths;
    m_accessGeneration = m_generation;
    m_accessModeMask = modeMask;
    m_accessOwner = owner;
    m_accessGroup = group;
    m_accessMode = mode;
    m_accessOwnerId = uid == uid_t(-1) ? -1 : qint64(uid);
    m_accessGroupId = gid == gid_t(-1) ? -1 : qint64(gid);
    m_accessError.clear();
    m_accessBusy = true;
    m_accessTimer->start();
    emit accessProgressChanged();
    if (administrator) { startAdministratorAccess(recursive); return true; }
    auto *watcher = new QFutureWatcher<void>(this);
    const auto task = m_accessTask;
    connect(watcher, &QFutureWatcher<void>::finished, this, [this, watcher, task]() {
        watcher->deleteLater(); m_accessError = task->firstError; finishAccess(task->cancelled);
    });
    watcher->setFuture(QtConcurrent::run([paths, mode, modeMask, uid, gid, recursive, task]() {
        for (const auto &path : paths) {
            if (task->cancelled) break;
            struct stat st {};
            const bool directory = ::lstat(QFile::encodeName(path).constData(), &st) == 0 && S_ISDIR(st.st_mode);
            FileAccessSecurity::applyLocalAccess(path, mode, uid, gid, recursive && directory, task, modeMask);
        }
    }));
    return true;
}

void FilePropertiesManager::cancelAccess()
{
    if (!m_accessBusy || !m_accessTask) return;
    m_accessTask->cancelled = true;
    if (m_accessProcess) {
        // The authorized helper stops cooperatively; an ordinary user cannot
        // signal a root process. EOF also cancels if this application exits.
        m_accessProcess->write("cancel\n");
        m_accessProcess->closeWriteChannel();
    }
}

void FilePropertiesManager::finishAccess(bool cancelled)
{
    if (!m_accessBusy) return;
    m_accessBusy = false;
    m_accessTimer->stop();
    m_accessProcess.clear();
    if (m_generation == m_accessGeneration) { refreshPermissions(); emit propertiesChanged(); }
    emit accessProgressChanged();
    if (accessCompleted()) emit permissionsApplied(m_accessPath);
    emit accessFinished(!cancelled && accessFailed() == 0, cancelled, accessCompleted(), accessFailed());
}

void FilePropertiesManager::startAdministratorAccess(bool recursive)
{
    // polkit supplies root authority, but all actual mutations use pinned
    // descriptors in the helper. Never enumerate/check as the unprivileged UI
    // or dispatch separate, path-based administrator stat/chmod/chown jobs.
    auto *process = new QProcess(this);
    m_accessProcess = process;
    const auto task = m_accessTask;
    const auto output = std::make_shared<QByteArray>();
    const auto finalReport = std::make_shared<bool>(false);
    QJsonArray identities;
    for (const auto &path : m_accessPaths) {
        struct stat st {};
        if (::lstat(QFile::encodeName(path).constData(), &st) != 0) {
            ++task->failed;
            m_accessError = securityMessage("A selected item is no longer available. No changes were made.",
                                           "Seçili bir öğe artık mevcut değil. Hiçbir değişiklik yapılmadı.");
            process->deleteLater(); finishAccess(false); return;
        }
        identities.append(QJsonObject{{"device", QString::number(qulonglong(st.st_dev))},
                                      {"inode", QString::number(qulonglong(st.st_ino))}});
    }
    const QJsonObject request{{"paths", QJsonArray::fromStringList(m_accessPaths)},
        {"identities", identities},
        {"mode", m_accessMode}, {"mask", m_accessModeMask}, {"recursive", recursive},
        {"owner", m_accessOwnerId}, {"group", m_accessGroupId}};
    connect(process, &QProcess::started, this, [process, request] {
        process->write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
        // Keep stdin open as the lifetime/cancellation pipe.
    });
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process, task, output, finalReport] {
        if (m_accessTask != task || !m_accessBusy) return;
        output->append(process->readAllStandardOutput());
        if (output->size() > 1024 * 1024) { task->cancelled = true; process->closeWriteChannel(); return; }
        for (int newline; (newline = output->indexOf('\n')) >= 0;) {
            const auto report = QJsonDocument::fromJson(output->left(newline)).object();
            output->remove(0, newline + 1);
            if (!report.contains("changed") || !report.contains("finished")) continue;
            task->changed = qMax(0, report.value("changed").toInt());
            task->failed = qMax(0, report.value("failed").toInt());
            task->skipped = qMax(0, report.value("skipped").toInt());
            if (report.value("cancelled").toBool()) task->cancelled = true;
            if (report.value("finished").toBool()) {
                *finalReport = true;
                m_accessError = report.value("error").toString();
                if (!report.value("code").toString().isEmpty())
                    m_accessError = securityMessage("Administrator access could not be applied safely.",
                                                    "Yönetici yetkisiyle izin değişikliği güvenli biçimde uygulanamadı.")
                        + (m_accessError.isEmpty() ? QString() : QStringLiteral("\n") + m_accessError);
            }
        }
        emit accessProgressChanged();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process, task](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || m_accessTask != task || !m_accessBusy) return;
        ++task->failed;
        m_accessError = securityMessage("Administrator authentication could not be started.",
                                        "Yönetici kimlik doğrulaması başlatılamadı.");
        process->deleteLater(); finishAccess(false);
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, process, task, finalReport](int code, QProcess::ExitStatus status) {
        process->deleteLater();
        if (m_accessTask != task || !m_accessBusy) return;
        if (!*finalReport) {
            if (code == 126 && status == QProcess::NormalExit) task->cancelled = true;
            else {
                ++task->failed;
                m_accessError = securityMessage("Administrator authentication or the access helper failed.",
                                                "Yönetici kimlik doğrulaması veya izin yardımcısı başarısız oldu.");
            }
        } else if (status != QProcess::NormalExit || (code != 0 && !task->failed && !task->cancelled)) {
            ++task->failed;
        }
        finishAccess(task->cancelled);
    });
    process->start(QStandardPaths::findExecutable(QStringLiteral("pkexec")),
                   {QStringLiteral("--disable-internal-agent"), administratorHelperPath()});
}
