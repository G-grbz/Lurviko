#pragma once

#include <QObject>
#include <QVariantMap>

class PlaybackResumeManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString storagePath READ storagePath CONSTANT)
public:
    explicit PlaybackResumeManager(QObject *parent = nullptr);

    Q_INVOKABLE qint64 positionFor(const QString &key) const;
    Q_INVOKABLE void save(const QString &key, qint64 position, qint64 duration);
    Q_INVOKABLE void clear(const QString &key);
    Q_INVOKABLE QVariantMap trackPreferencesFor(const QString &key) const;
    Q_INVOKABLE void saveTrackPreferences(const QString &key, int audioTrack,
                                           const QString &subtitleKind, int subtitleTrack,
                                           const QString &subtitlePath);
    QString storagePath() const;

private:
    static QString normalizeIdentity(const QString &key);
    static QString storageKey(const QString &key);
};
