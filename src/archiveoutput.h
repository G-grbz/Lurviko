#pragma once
#include <QDir>
#include <QFile>
#include <QTemporaryFile>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/syscall.h>
#include <linux/fs.h>
#include <unistd.h>

// Write to an exclusively-created sibling, then publish without replacing any
// existing directory entry. Cancellation/failure removes only our staging file.
class ArchiveOutput
{
public:
    ArchiveOutput(const QString &directory, const QString &base, const QString &extension)
        : m_directory(directory), m_base(base), m_extension(extension),
          m_file(QDir(directory).filePath(".lurviko-archive-XXXXXX" + extension))
    { if (m_file.open()) m_file.close(); }
    QString path() const { return m_file.fileName(); }
    bool valid() const { return !path().isEmpty() && QFile::exists(path()); }
    QString publish(QString *error) {
        for (int index = 0; index < 10000; ++index) {
            const QString name = index == 0 ? m_base + m_extension
                : QStringLiteral("%1 (%2)%3").arg(m_base).arg(index).arg(m_extension);
            const QString destination = QDir(m_directory).filePath(name);
            const QByteArray from = QFile::encodeName(path()), to = QFile::encodeName(destination);
#ifdef SYS_renameat2
            // Supports filesystems without hard links, without an existence-
            // check/rename race. Keep the safe link fallback for older kernels.
            if (::syscall(SYS_renameat2, AT_FDCWD, from.constData(), AT_FDCWD, to.constData(), RENAME_NOREPLACE) == 0) {
                m_file.setAutoRemove(false);
                return destination;
            }
            if (errno != ENOSYS && errno != EINVAL && errno != EOPNOTSUPP) {
                if (errno == EEXIST) continue;
                *error = QString::fromLocal8Bit(std::strerror(errno)); return {};
            }
#endif
            if (::link(from.constData(), to.constData()) == 0) return destination;
            if (errno != EEXIST) { *error = QString::fromLocal8Bit(std::strerror(errno)); return {}; }
        }
        *error = QStringLiteral("Too many conflicting archive names.");
        return {};
    }
private:
    QString m_directory, m_base, m_extension;
    QTemporaryFile m_file;
};
