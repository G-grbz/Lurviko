#pragma once

#include <QObject>
#include <QVariantMap>
#include <QCache>

class MusicMetadataManager : public QObject
{
    Q_OBJECT
public:
    explicit MusicMetadataManager(QObject *parent = nullptr);

    Q_INVOKABLE QVariantMap load(const QString &url) const;
    Q_INVOKABLE void request(const QString &url);
    Q_INVOKABLE void clear();

signals:
    void metadataReady(const QString &url, const QVariantMap &metadata);

private:
    static QString localPathForUrl(const QString &url);
    static QString readTextFile(const QString &path);
    static QVariantMap parseLyrics(const QString &text);
    static QVariantMap ffprobeMetadata(const QString &path);
    static QString fingerprint(const QString &url);
    void startPending();
    QCache<QString, QVariantMap> m_cache{8192}; // KiB; bounded lyric cache.
    QString m_pendingUrl;
    quint64 m_generation = 0;
    bool m_workerActive = false;
};
