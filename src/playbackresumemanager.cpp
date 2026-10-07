#include "playbackresumemanager.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

namespace {
QString resumeStoragePath()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.local/share");
    const QString dir = QDir(base).filePath(QStringLiteral("g-File"));
    QDir().mkpath(dir);
    return QDir(dir).filePath(QStringLiteral("playback-resume.json"));
}

QJsonObject loadRoot()
{
    QFile file(resumeStoragePath());
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    return document.isObject() ? document.object() : QJsonObject{};
}

bool saveRoot(const QJsonObject &root)
{
    QSaveFile file(resumeStoragePath());
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    return file.commit();
}
}

PlaybackResumeManager::PlaybackResumeManager(QObject *parent)
    : QObject(parent)
{
}

QString PlaybackResumeManager::storagePath() const
{
    return resumeStoragePath();
}

QString PlaybackResumeManager::normalizeIdentity(const QString &key)
{
    QString value = key.trimmed();
    if (value.isEmpty())
        return {};

    const QUrl url(value);
    if (url.isValid() && url.isLocalFile())
        value = url.toLocalFile();

    // Local gallery items use an absolute path while some callers may hand us
    // the equivalent file:// URL. Canonicalize both to one stable identity so
    // a process restart cannot change the resume key.
    if (QDir::isAbsolutePath(value)) {
        QFileInfo info(value);
        const QString canonical = info.exists() ? info.canonicalFilePath() : QString();
        value = canonical.isEmpty() ? QDir::cleanPath(info.absoluteFilePath()) : canonical;
    }
    return value;
}

QString PlaybackResumeManager::storageKey(const QString &key)
{
    const QString normalized = normalizeIdentity(key);
    return QString::fromLatin1(QCryptographicHash::hash(normalized.toUtf8(), QCryptographicHash::Sha256).toHex());
}

qint64 PlaybackResumeManager::positionFor(const QString &key) const
{
    const QString normalized = normalizeIdentity(key);
    if (normalized.isEmpty())
        return -1;

    const QJsonObject root = loadRoot();
    const QJsonObject entries = root.value(QStringLiteral("entries")).toObject();
    const QJsonObject entry = entries.value(storageKey(normalized)).toObject();
    if (entry.isEmpty())
        return -1;

    const qint64 position = qint64(entry.value(QStringLiteral("position")).toDouble(-1));
    const qint64 duration = qint64(entry.value(QStringLiteral("duration")).toDouble(0));
    if (position < 3000)
        return -1;

    // A stale/missing duration must not make an otherwise valid persisted
    // position disappear after restart. Completion cleanup happens on save.
    if (duration > 0 && (position >= duration - 60000
            || double(position) / double(duration) >= 0.95))
        return -1;
    return position;
}

void PlaybackResumeManager::save(const QString &key, qint64 position, qint64 duration)
{
    const QString normalized = normalizeIdentity(key);
    if (normalized.isEmpty() || duration <= 0)
        return;

    // MediaPlayer commonly resets position to zero while the application is
    // tearing down. Never let that transient value erase the last good point.
    if (position < 3000)
        return;

    if (position >= duration - 60000
            || double(position) / double(duration) >= 0.95) {
        clear(normalized);
        return;
    }

    QJsonObject root = loadRoot();
    QJsonObject entries = root.value(QStringLiteral("entries")).toObject();
    QJsonObject entry;
    entry.insert(QStringLiteral("source"), normalized);
    entry.insert(QStringLiteral("position"), double(position));
    entry.insert(QStringLiteral("duration"), double(duration));
    entry.insert(QStringLiteral("updatedAt"), double(QDateTime::currentMSecsSinceEpoch()));
    entries.insert(storageKey(normalized), entry);
    root.insert(QStringLiteral("version"), 2);
    root.insert(QStringLiteral("entries"), entries);
    saveRoot(root);
}

void PlaybackResumeManager::clear(const QString &key)
{
    const QString normalized = normalizeIdentity(key);
    if (normalized.isEmpty())
        return;

    QJsonObject root = loadRoot();
    QJsonObject entries = root.value(QStringLiteral("entries")).toObject();
    entries.remove(storageKey(normalized));
    root.insert(QStringLiteral("version"), 2);
    root.insert(QStringLiteral("entries"), entries);
    saveRoot(root);
}


QVariantMap PlaybackResumeManager::trackPreferencesFor(const QString &key) const
{
    const QString normalized = normalizeIdentity(key);
    if (normalized.isEmpty())
        return {};

    const QJsonObject root = loadRoot();
    const QJsonObject preferences = root.value(QStringLiteral("preferences")).toObject();
    const QJsonObject pref = preferences.value(storageKey(normalized)).toObject();
    if (pref.isEmpty())
        return {};

    QVariantMap result;
    result.insert(QStringLiteral("audioTrack"), pref.value(QStringLiteral("audioTrack")).toInt(-1));
    result.insert(QStringLiteral("subtitleKind"), pref.value(QStringLiteral("subtitleKind")).toString(QStringLiteral("off")));
    result.insert(QStringLiteral("subtitleTrack"), pref.value(QStringLiteral("subtitleTrack")).toInt(-1));
    result.insert(QStringLiteral("subtitlePath"), pref.value(QStringLiteral("subtitlePath")).toString());
    return result;
}

void PlaybackResumeManager::saveTrackPreferences(const QString &key, int audioTrack,
                                                  const QString &subtitleKind, int subtitleTrack,
                                                  const QString &subtitlePath)
{
    const QString normalized = normalizeIdentity(key);
    if (normalized.isEmpty())
        return;

    QString kind = subtitleKind.trimmed().toLower();
    if (kind != QStringLiteral("embedded") && kind != QStringLiteral("external"))
        kind = QStringLiteral("off");

    QJsonObject root = loadRoot();
    QJsonObject preferences = root.value(QStringLiteral("preferences")).toObject();
    QJsonObject pref;
    pref.insert(QStringLiteral("source"), normalized);
    pref.insert(QStringLiteral("audioTrack"), audioTrack);
    pref.insert(QStringLiteral("subtitleKind"), kind);
    pref.insert(QStringLiteral("subtitleTrack"), kind == QStringLiteral("embedded") ? subtitleTrack : -1);
    pref.insert(QStringLiteral("subtitlePath"), kind == QStringLiteral("external") ? subtitlePath : QString());
    pref.insert(QStringLiteral("updatedAt"), double(QDateTime::currentMSecsSinceEpoch()));
    preferences.insert(storageKey(normalized), pref);
    root.insert(QStringLiteral("version"), 2);
    root.insert(QStringLiteral("preferences"), preferences);
    saveRoot(root);
}
