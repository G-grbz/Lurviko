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
#include <QtConcurrent/QtConcurrentRun>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
struct FolderStats {
    qint64 bytes = 0;
    int files = 0;
    int folders = 0;
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

void FilePropertiesManager::inspect(const QString &urlOrPath)
{
    if (!m_accessBusy) {
        m_accessError.clear();
        emit accessProgressChanged();
    }
    ++m_generation;
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

    if (!m_directory || !info.exists()) {
        setCalculating(false);
        return;
    }

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
        setCalculating(false);
        emit propertiesChanged();
    });
    const QString root = m_path;
    watcher->setFuture(QtConcurrent::run([root]() {
        FolderStats stats;
        QDirIterator it(root,
                        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            const QFileInfo item = it.fileInfo();
            if (item.isSymLink())
                continue;
            if (item.isDir()) {
                ++stats.folders;
            } else if (item.isFile()) {
                ++stats.files;
                stats.bytes += item.size();
            }
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
    if (m_accessBusy)
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
    if (!m_directory || m_path.isEmpty() || iconName.trimmed().isEmpty())
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
    if (m_path.isEmpty() || m_linkType.isEmpty() || trimmed.isEmpty())
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
