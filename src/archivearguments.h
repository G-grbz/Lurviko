#pragma once
#include <QFileInfo>
#include <QStringList>
namespace ArchiveArguments {
inline QString extractionFolderName(const QString &fileName)
{
    QString name = QFileInfo(fileName).fileName();
    const QStringList suffixes = {".tar.gz", ".tar.bz2", ".tar.xz", ".tar.zst", ".tgz", ".tbz2",
                                 ".txz", ".zip", ".rar", ".7z", ".tar", ".gz", ".bz2", ".xz", ".zst"};
    for (const QString &suffix : suffixes) {
        if (name.endsWith(suffix, Qt::CaseInsensitive)) {
            name.chop(suffix.size());
            break;
        }
    }
    // A valid archive filename such as "...zip" becomes ".." after removing
    // its suffix. Never let "extract into subfolder" escape the chosen parent.
    return name.isEmpty() || name == "." || name == ".." ? QStringLiteral("Archive") : name;
}
inline QStringList sourceOperand(const QFileInfo &info)
{
    // QProcess passes arguments directly. './' prevents option and @file
    // interpretation while preserving spaces, Unicode and mixed directories.
    return {QStringLiteral("-C"), info.absolutePath(), QStringLiteral("./") + info.fileName()};
}
inline QStringList extraction(const QString &archivePath, const QString &destination)
{
    // bsdtar's default path/symlink guards stay enabled. Safe writes replace
    // regular files atomically rather than following their destination links.
    return {QStringLiteral("-xf"), archivePath, QStringLiteral("-C"), destination,
            QStringLiteral("--safe-writes")};
}
}
