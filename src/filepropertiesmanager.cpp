#include "filepropertiesmanager.h"

#include <QDir>
#include <QCoreApplication>
#include <QDirIterator>
#include <QFileInfo>
#include <QFileDevice>
#include <QFutureWatcher>
#include <QFile>
#include <QLocale>
#include <QSettings>
#include <QUrl>
#include <QSet>
#include <QtConcurrent/QtConcurrentRun>
#include <QElapsedTimer>
#include <QThread>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
QString securityMessage(const char *english, const char *turkish)
{
    const QString fallback = QLocale::system().language() == QLocale::Turkish ? QStringLiteral("tr") : QStringLiteral("en");
    return QString::fromUtf8(QSettings().value(QStringLiteral("ui/language"), fallback).toString() == QStringLiteral("tr") ? turkish : english);
}

struct FolderStats {
    qint64 bytes = 0;
    int files = 0;
    int folders = 0;
    QVariantMap children;
};

QString localPathFromInput(const QString &value, const QString &baseDirectory)
{
    const QString trimmed = value.trimmed();
    const QUrl candidate(trimmed);
    if (candidate.isValid() && candidate.isLocalFile())
        return candidate.toLocalFile();
    if (QDir::isAbsolutePath(trimmed))
        return QDir::cleanPath(trimmed);
    return QDir(baseDirectory).absoluteFilePath(trimmed);
}

bool atomicReplaceLink(const QString &path, const QString &target, bool symbolic, QString *errorText)
{
    const QFileInfo linkInfo(path);
    const QString tempPath = linkInfo.absolutePath() + QStringLiteral("/.gfile-link-%1-%2.tmp")
        .arg(QCoreApplication::applicationPid())
        .arg(QDateTime::currentMSecsSinceEpoch());
    const QByteArray nativeTemp = QFile::encodeName(tempPath);
    const QByteArray nativeTarget = QFile::encodeName(target);
    int rc = symbolic ? ::symlink(nativeTarget.constData(), nativeTemp.constData())
                      : ::link(nativeTarget.constData(), nativeTemp.constData());
    if (rc != 0) {
        if (errorText)
            *errorText = QString::fromLocal8Bit(std::strerror(errno));
        return false;
    }

    const QByteArray nativePath = QFile::encodeName(path);
    if (::rename(nativeTemp.constData(), nativePath.constData()) != 0) {
        const int savedErrno = errno;
        QFile::remove(tempPath);
        if (errorText)
            *errorText = QString::fromLocal8Bit(std::strerror(savedErrno));
        return false;
    }
    return true;
}
}

FilePropertiesManager::FilePropertiesManager(QObject *parent)
    : QObject(parent)
{
    initializeAccess();
}

void FilePropertiesManager::setCalculating(bool value)
{
    if (m_calculating == value)
        return;
    m_calculating = value;
    emit calculatingChanged();
}

QString FilePropertiesManager::readFolderIcon(const QString &path)
{
    const QString metadataPath = QDir(path).filePath(QStringLiteral(".directory"));
    if (!QFileInfo::exists(metadataPath))
        return QStringLiteral("folder");
    QSettings metadata(metadataPath, QSettings::IniFormat);
    metadata.beginGroup(QStringLiteral("Desktop Entry"));
    const QString icon = metadata.value(QStringLiteral("Icon"), QStringLiteral("folder")).toString().trimmed();
    metadata.endGroup();
    return icon.isEmpty() ? QStringLiteral("folder") : icon;
}

void FilePropertiesManager::cancelInspection()
{
    ++m_generation;
    disconnect(m_statsConnection);
    m_statsReady = false;
    m_childStats.clear();
    if (m_statsCancelled) m_statsCancelled->store(true);
    setCalculating(false);
}

// Share the visible directory's recursive snapshot with Properties. Each
// immediate child's contribution includes hidden descendants, so subsets and
// selections that hide dotfiles never accidentally inherit the entire total.
bool FilePropertiesManager::reuseStatsOrWait(const QStringList &paths, bool selection, int generation)
{
    auto *source = m_statsSource.data();
    if (!source || source->m_path.isEmpty() || paths.isEmpty()) return false;
    for (const auto &path : paths)
        if (path != source->m_path && QFileInfo(path).absolutePath() != source->m_path) return false;
    if (source->calculating()) {
        setCalculating(true);
        m_statsConnection = connect(source, &FilePropertiesManager::calculatingChanged, this,
            [this, paths, selection, generation] {
                if (generation != m_generation || !m_statsSource || m_statsSource->calculating()) return;
                disconnect(m_statsConnection);
                if (selection) inspectSelection(paths); else inspect(paths.first());
            }, Qt::QueuedConnection);
        return true;
    }
    if (!source->m_statsReady) return false;
    qint64 bytes = 0; int files = 0, folders = 0;
    const bool entireRoot = paths.contains(source->m_path);
    for (const auto &path : paths) {
        if (entireRoot && path != source->m_path) continue;
        if (path == source->m_path) {
            bytes += source->m_totalSize; files += source->m_fileCount;
            folders += source->m_folderCount + (selection ? 1 : 0);
        } else {
            if (!source->m_childStats.contains(path)) return false;
            const auto child = source->m_childStats.value(path).toMap();
            bytes += child.value("bytes").toLongLong(); files += child.value("files").toInt();
            folders += child.value("folders").toInt();
            if (!selection && child.value("directory").toBool()) --folders;
        }
    }
    m_totalSize = bytes; m_fileCount = files; m_folderCount = folders;
    m_statsReady = true;
    setCalculating(false); emit propertiesChanged();
    return true;
}

void FilePropertiesManager::inspect(const QString &urlOrPath)
{
    if (!m_accessBusy) {
        m_accessError.clear();
        emit accessProgressChanged();
    }
    cancelInspection();
    m_statsCancelled = std::make_shared<std::atomic_bool>(false);
    const auto cancelled = m_statsCancelled;
    const int generation = m_generation;
    const QUrl url = QUrl::fromUserInput(urlOrPath);
    m_localFile = url.isLocalFile();
    const QString local = url.isLocalFile() ? url.toLocalFile() : urlOrPath;
    const QFileInfo info(local);

    m_linkType.clear();
    m_linkTarget.clear();
    m_linkTargetExists = false;
    m_inode = 0;
    m_linkCount = 1;

    m_path = info.absoluteFilePath();
    m_selection = {m_path};
    m_commonLocation = info.absolutePath();
    m_selectedFolders = info.isDir() && !info.isSymLink() ? 1 : 0;
    m_selectedLinks = info.isSymLink() ? 1 : 0;
    m_selectedFiles = !m_selectedFolders && !m_selectedLinks ? 1 : 0;
    m_name = info.fileName();
    m_directory = info.isDir();
    m_totalSize = m_directory ? 0 : info.size();
    m_fileCount = m_directory ? 0 : 1;
    m_folderCount = 0;
    m_created = info.birthTime();
    m_modified = info.lastModified();
    m_accessed = info.lastRead();
    m_iconName = m_directory ? readFolderIcon(m_path) : QString();

    struct stat st {};
    const QByteArray nativePath = QFile::encodeName(m_path);
    if (::lstat(nativePath.constData(), &st) == 0) {
        m_inode = static_cast<qulonglong>(st.st_ino);
        m_linkCount = static_cast<int>(st.st_nlink);
    }

    if (info.isSymLink()) {
        m_linkType = QStringLiteral("symlink");
        m_linkTarget = info.symLinkTarget();
        m_linkTargetExists = QFileInfo(m_linkTarget).exists();
    } else if (info.suffix().compare(QStringLiteral("desktop"), Qt::CaseInsensitive) == 0) {
        QSettings desktop(m_path, QSettings::IniFormat);
        desktop.beginGroup(QStringLiteral("Desktop Entry"));
        const QString desktopType = desktop.value(QStringLiteral("Type")).toString().trimmed();
        if (desktopType.compare(QStringLiteral("Link"), Qt::CaseInsensitive) == 0) {
            m_linkType = QStringLiteral("shortcut");
            m_linkTarget = desktop.value(QStringLiteral("URL")).toString().trimmed();
            const QUrl targetUrl(m_linkTarget);
            m_linkTargetExists = !targetUrl.isLocalFile() || QFileInfo(targetUrl.toLocalFile()).exists();
        }
        desktop.endGroup();
    }
    if (m_linkType.isEmpty() && S_ISREG(st.st_mode) && st.st_nlink > 1)
        m_linkType = QStringLiteral("hardlink");

    refreshPermissions();
    emit propertiesChanged();

    if (!m_directory || (info.isSymLink() && !m_backgroundStats) || !info.exists()) {
        setCalculating(false);
        return;
    }

    if (reuseStatsOrWait({m_path}, false, generation)) return;
    setCalculating(true);
    auto *watcher = new QFutureWatcher<FolderStats>(this);
    connect(watcher, &QFutureWatcher<FolderStats>::finished, this, [this, watcher, generation]() {
        const FolderStats stats = watcher->result();
        watcher->deleteLater();
        if (generation != m_generation)
            return;
        m_totalSize = stats.bytes;
        m_fileCount = stats.files;
        m_folderCount = stats.folders;
        m_childStats = stats.children;
        m_statsReady = true;
        setCalculating(false);
        emit propertiesChanged();
    });
    const QString root = m_path;
    const bool background = m_backgroundStats;
    watcher->setFuture(QtConcurrent::run([root, cancelled, background]() {
        FolderStats stats;
        QElapsedTimer budget; budget.start();
        const QDir rootDirectory(root);
        QDirIterator it(root,
                        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                        QDirIterator::Subdirectories);
        while (!cancelled->load() && it.hasNext()) {
            it.next();
            const QFileInfo item = it.fileInfo();
            const QString relative = rootDirectory.relativeFilePath(item.absoluteFilePath());
            const QString topName = relative.section(QLatin1Char('/'), 0, 0);
            const QString topPath = rootDirectory.filePath(topName);
            auto child = stats.children.value(topPath).toMap();
            const bool topLevel = !relative.contains(QLatin1Char('/'));
            if (topLevel) child.insert("directory", item.isDir() && !item.isSymLink());
            if (!item.isSymLink()) {
                if (item.isDir()) {
                    ++stats.folders;
                    child.insert("folders", child.value("folders").toInt() + 1);
                } else if (item.isFile()) {
                    ++stats.files; stats.bytes += item.size();
                    child.insert("files", child.value("files").toInt() + 1);
                    child.insert("bytes", child.value("bytes").toLongLong() + item.size());
                }
            }
            stats.children.insert(topPath, child);
            if (background && budget.elapsed() >= 8) { QThread::msleep(8); budget.restart(); }
        }
        return stats;
    }));
}

void FilePropertiesManager::inspectSelection(const QStringList &urls)
{
    QStringList selection;
    bool local = true;
    for (const auto &input : urls) {
        const QUrl url = QUrl::fromUserInput(input);
        const QString item = url.isLocalFile() ? QDir::cleanPath(url.toLocalFile()) : url.toString();
        if (item.isEmpty() || selection.contains(item)) continue;
        selection.append(item);
        local &= url.isLocalFile();
    }
    if (selection.size() == 1) { inspect(urls.first()); return; }
    cancelInspection();
    m_statsCancelled = std::make_shared<std::atomic_bool>(false);
    const auto cancelled = m_statsCancelled;
    const int generation = m_generation;
    m_selection = selection;
    m_localFile = local && !selection.isEmpty();
    m_path = m_localFile ? selection.first() : QString();
    m_name = securityMessage("%1 selected items", "%1 seçili öğe").arg(selection.size());
    m_commonLocation.clear();
    m_selectedFiles = m_selectedFolders = m_selectedLinks = 0;
    m_totalSize = m_fileCount = m_folderCount = 0;
    m_created = m_modified = m_accessed = {};
    m_iconName.clear(); m_linkType.clear(); m_linkTarget.clear();
    m_linkTargetExists = false; m_inode = 0; m_linkCount = 1;
    m_directory = false;
    if (!m_accessBusy) { m_accessError.clear(); emit accessProgressChanged(); }
    if (m_localFile) {
        m_commonLocation = QFileInfo(selection.first()).absolutePath();
        for (const auto &path : selection) {
            if (QFileInfo(path).absolutePath() != m_commonLocation) m_commonLocation.clear();
            struct stat st {};
            if (::lstat(QFile::encodeName(path).constData(), &st) != 0) continue;
            if (S_ISDIR(st.st_mode)) ++m_selectedFolders;
            else if (S_ISLNK(st.st_mode)) ++m_selectedLinks;
            else if (S_ISREG(st.st_mode)) ++m_selectedFiles;
        }
    }
    m_directory = m_selectedFolders > 0;
    refreshPermissions();
    emit propertiesChanged();
    if (!m_localFile) { setCalculating(false); return; }
    if (reuseStatsOrWait(selection, true, generation)) return;
    setCalculating(true);
    auto *watcher = new QFutureWatcher<FolderStats>(this);
    connect(watcher, &QFutureWatcher<FolderStats>::finished, this, [this, watcher, generation] {
        const auto stats = watcher->result(); watcher->deleteLater();
        if (generation != m_generation) return;
        m_totalSize = stats.bytes; m_fileCount = stats.files; m_folderCount = stats.folders;
        m_statsReady = true;
        setCalculating(false); emit propertiesChanged();
    });
    watcher->setFuture(QtConcurrent::run([selection, cancelled] {
        FolderStats stats;
        QSet<QString> visited;
        auto count = [&](const QString &path, const QFileInfo &info) {
            if (visited.contains(path) || info.isSymLink()) return;
            visited.insert(path);
            if (info.isDir()) ++stats.folders;
            else if (info.isFile()) { ++stats.files; stats.bytes += info.size(); }
        };
        for (const auto &path : selection) {
            if (cancelled->load()) break;
            const QFileInfo info(path);
            if (info.isSymLink()) continue;
            count(path, info);
            if (!info.isDir()) continue;
            QDirIterator it(path, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                            QDirIterator::Subdirectories);
            while (!cancelled->load() && it.hasNext()) { it.next(); count(it.filePath(), it.fileInfo()); }
        }
        return stats;
    }));
}

void FilePropertiesManager::refreshPermissions()
{
    m_permissionMode = 0;
    m_ownerName.clear();
    m_groupName.clear();
    m_permissionsEditable = false;
    m_permissionsText.clear();
    m_mixedPermissionMask = 0;
    if (multiple()) {
        bool first = true, mixedOwner = false, mixedGroup = false;
        m_permissionsEditable = m_localFile;
        for (const auto &path : m_selection) {
            struct stat item {};
            if (!m_localFile || ::lstat(QFile::encodeName(path).constData(), &item) != 0) {
                m_permissionsEditable = false; continue;
            }
            if (!S_ISREG(item.st_mode) && !S_ISDIR(item.st_mode)) continue;
            const auto user = ::getpwuid(item.st_uid);
            const QString owner = user ? QString::fromLocal8Bit(user->pw_name) : QString::number(item.st_uid);
            const auto account = ::getgrgid(item.st_gid);
            const QString group = account ? QString::fromLocal8Bit(account->gr_name) : QString::number(item.st_gid);
            const int mode = item.st_mode & 0777;
            if (first) { m_permissionMode = mode; m_ownerName = owner; m_groupName = group; first = false; }
            else {
                m_mixedPermissionMask |= m_permissionMode ^ mode;
                m_permissionMode &= mode;
                mixedOwner |= owner != m_ownerName; mixedGroup |= group != m_groupName;
            }
            m_permissionsEditable &= ::geteuid() == 0 || ::geteuid() == item.st_uid;
        }
        if (mixedOwner) m_ownerName.clear();
        if (mixedGroup) m_groupName.clear();
        m_permissionsEditable &= !first;
        m_permissionsText = first ? QString() : m_mixedPermissionMask
            ? securityMessage("Mixed permissions", "Farklı izinler")
            : QStringLiteral("0%1").arg(m_permissionMode, 3, 8, QLatin1Char('0'));
        emit permissionsChanged(); return;
    }
    struct stat st {};
    const QByteArray nativePath = QFile::encodeName(m_path);
    if (m_localFile && ::lstat(nativePath.constData(), &st) == 0) {
        m_permissionMode = static_cast<int>(st.st_mode & 0777);
        const auto owner = ::getpwuid(st.st_uid);
        m_ownerName = owner ? QString::fromLocal8Bit(owner->pw_name) : QString::number(st.st_uid);
        const auto group = ::getgrgid(st.st_gid);
        m_groupName = group ? QString::fromLocal8Bit(group->gr_name) : QString::number(st.st_gid);
        m_permissionsEditable = (S_ISREG(st.st_mode) || S_ISDIR(st.st_mode))
                                && (::geteuid() == 0 || ::geteuid() == st.st_uid);
        m_permissionsText = QStringLiteral("---------");
        const char letters[] = "rwxrwxrwx";
        for (int index = 0; index < 9; ++index)
            if (st.st_mode & (0400 >> index))
                m_permissionsText[index] = QLatin1Char(letters[index]);
        if (st.st_mode & S_ISUID) m_permissionsText[2] = QLatin1Char(st.st_mode & S_IXUSR ? 's' : 'S');
        if (st.st_mode & S_ISGID) m_permissionsText[5] = QLatin1Char(st.st_mode & S_IXGRP ? 's' : 'S');
        if (st.st_mode & S_ISVTX) m_permissionsText[8] = QLatin1Char(st.st_mode & S_IXOTH ? 't' : 'T');
    }
    emit permissionsChanged();
}

bool FilePropertiesManager::setPermissionMode(int mode)
{
    if (m_accessBusy || multiple())
        return false;
    if (!m_localFile || m_path.isEmpty() || mode < 0 || mode > 0777)
        return false;
    struct stat st {};
    const QByteArray nativePath = QFile::encodeName(m_path);
    if (::lstat(nativePath.constData(), &st) != 0) {
        emit error(tr("Could not read file permissions: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
        return false;
    }
    // Symbolic links have no editable POSIX mode. Never change their targets
    // through this dialog, including when the path was replaced after inspect().
    if ((!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode))
        || (::geteuid() != 0 && ::geteuid() != st.st_uid)) {
        emit error(tr("You do not own this item, or its permissions cannot be changed here."));
        return false;
    }
    // Ordinary rwx editor only: deliberately preserve existing setuid/setgid/sticky.
    const mode_t requested = static_cast<mode_t>(mode) | (st.st_mode & (S_ISUID | S_ISGID | S_ISVTX));
    if (::fchmodat(AT_FDCWD, nativePath.constData(), requested, AT_SYMLINK_NOFOLLOW) != 0) {
        emit error(tr("Could not change file permissions: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
        return false;
    }
    refreshPermissions();
    emit propertiesChanged();
    emit permissionsApplied(m_path);
    return true;
}

bool FilePropertiesManager::setFolderIcon(const QString &iconName)
{
    if (multiple() || !m_directory || m_path.isEmpty() || iconName.trimmed().isEmpty())
        return false;
    const QString metadataPath = QDir(m_path).filePath(QStringLiteral(".directory"));
    QSettings metadata(metadataPath, QSettings::IniFormat);
    metadata.beginGroup(QStringLiteral("Desktop Entry"));
    metadata.setValue(QStringLiteral("Icon"), iconName.trimmed());
    metadata.endGroup();
    metadata.sync();
    if (metadata.status() != QSettings::NoError) {
        emit error(QStringLiteral("Folder icon could not be saved."));
        return false;
    }
    m_iconName = iconName.trimmed();
    emit propertiesChanged();
    emit folderIconChanged(m_path, m_iconName);
    return true;
}

bool FilePropertiesManager::setLinkTarget(const QString &target)
{
    const QString trimmed = target.trimmed();
    if (multiple() || m_path.isEmpty() || m_linkType.isEmpty() || trimmed.isEmpty())
        return false;

    if (m_linkType == QStringLiteral("shortcut")) {
        QSettings desktop(m_path, QSettings::IniFormat);
        desktop.beginGroup(QStringLiteral("Desktop Entry"));
        desktop.setValue(QStringLiteral("URL"), trimmed);
        desktop.endGroup();
        desktop.sync();
        if (desktop.status() != QSettings::NoError) {
            emit error(QStringLiteral("Shortcut target could not be saved."));
            return false;
        }
        inspect(m_path);
        return true;
    }

    QString errorText;
    if (m_linkType == QStringLiteral("symlink")) {
        QString linkValue = trimmed;
        const QUrl candidate(trimmed);
        if (candidate.isValid() && candidate.isLocalFile())
            linkValue = candidate.toLocalFile();
        if (!atomicReplaceLink(m_path, linkValue, true, &errorText)) {
            emit error(QStringLiteral("Symbolic link target could not be changed: %1").arg(errorText));
            return false;
        }
        inspect(m_path);
        return true;
    }

    if (m_linkType == QStringLiteral("hardlink")) {
        const QString targetPath = localPathFromInput(trimmed, QFileInfo(m_path).absolutePath());
        const QFileInfo targetInfo(targetPath);
        if (!targetInfo.exists() || !targetInfo.isFile() || targetInfo.isSymLink()) {
            emit error(QStringLiteral("Hard link target must be an existing regular file."));
            return false;
        }
        if (!atomicReplaceLink(m_path, targetPath, false, &errorText)) {
            emit error(QStringLiteral("Hard link could not be changed: %1").arg(errorText));
            return false;
        }
        inspect(m_path);
        return true;
    }

    return false;
}

QString FilePropertiesManager::formatBytes(qint64 bytes) const
{
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}
