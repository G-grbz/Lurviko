#pragma once
#include <QFileInfo>
#include <QStringList>
namespace ArchiveArguments {
inline QStringList sourceOperand(const QFileInfo &info)
{
    // QProcess passes arguments directly. './' prevents option and @file
    // interpretation while preserving spaces, Unicode and mixed directories.
    return {QStringLiteral("-C"), info.absolutePath(), QStringLiteral("./") + info.fileName()};
}
}
