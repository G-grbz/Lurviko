#pragma once

#include <QDir>
#include <QString>
#include <QUrl>

namespace IconReference {
// Local artwork can be persisted alongside bundled/theme names. Keep remote
// URLs outside this path and encode filename punctuation before QML loads it.
inline QString localFile(const QString &selection)
{
    QString value = selection.trimmed();
    if (value.startsWith(QStringLiteral("system:")))
        value.remove(0, 7);
    if (value.contains(QChar::Null))
        return {};
    if (QDir::isAbsolutePath(value))
        return QUrl::fromLocalFile(QDir::cleanPath(value)).toString(QUrl::FullyEncoded);
    const QUrl url(value, QUrl::StrictMode);
    if (url.isValid() && url.isLocalFile() && QDir::isAbsolutePath(url.path())
            && (url.host().isEmpty() || url.host() == QStringLiteral("localhost"))
            && url.userInfo().isEmpty() && url.port() == -1
            && !url.path().contains(QChar::Null) && !url.hasQuery() && !url.hasFragment())
        return url.toString(QUrl::FullyEncoded);
    return {};
}
}
