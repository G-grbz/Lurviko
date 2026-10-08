#pragma once

#include <QObject>
#include <QHash>
#include <QList>
#include <QSet>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtGlobal>

class MusicLibraryManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY tracksChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool scanPaused READ scanPaused NOTIFY scanPausedChanged)
    Q_PROPERTY(int scanTotal READ scanTotal NOTIFY scanProgressChanged)
    Q_PROPERTY(int scanCompleted READ scanCompleted NOTIFY scanProgressChanged)
    Q_PROPERTY(int scanRemaining READ scanRemaining NOTIFY scanProgressChanged)
    Q_PROPERTY(double scanProgress READ scanProgress NOTIFY scanProgressChanged)
    Q_PROPERTY(int favoriteCount READ favoriteCount NOTIFY tracksChanged)

public:
    explicit MusicLibraryManager(QObject *parent = nullptr);

    QVariantList tracks() const { return m_tracks; }
    bool loading() const { return m_loading; }
    bool scanPaused() const { return m_scanPaused; }
    int scanTotal() const { return m_scanTotal; }
    int scanCompleted() const { return m_scanCompleted; }
    int scanRemaining() const { return qMax(0, m_scanTotal - m_scanCompleted); }
    double scanProgress() const {
        return m_scanTotal > 0 ? qBound(0.0, double(m_scanCompleted) / double(m_scanTotal), 1.0) : 1.0;
    }
    int favoriteCount() const;

    Q_INVOKABLE void setItems(const QVariantList &items);
    Q_INVOKABLE bool isFavorite(const QString &url) const;
    Q_INVOKABLE void toggleFavorite(const QString &url);
    Q_INVOKABLE void recordPlayed(const QString &url);
    Q_INVOKABLE void pauseMetadataScan();
    Q_INVOKABLE void resumeMetadataScan();

signals:
    void tracksChanged();
    void loadingChanged();
    void scanPausedChanged();
    void scanProgressChanged();

private:
    struct CachedMeta {
        qint64 modifiedMs = 0;
        qint64 size = 0;
        QString title;
        QString artist;
        QString album;
        QString year;
        QString genre;
    };

    static QString localPathForUrl(const QString &url);
    static QVariantMap probeMetadata(const QVariantMap &item);
    static QVariantMap fallbackMetadata(const QVariantMap &item);
    static QVariantMap mapFromCached(const CachedMeta &cached);
    static QString normalizedUrl(const QVariantMap &item);
    static QString cacheKeyForItem(const QVariantMap &item);
    static QVariantMap mergedTrack(const QVariantMap &item, const QVariantMap &metadata,
                                   const QSet<QString> &favorites,
                                   const QHash<QString, QVariantMap> &history);

    QString statePath() const;
    void loadState();
    void saveState() const;
    void setLoading(bool loading);
    void setScanPaused(bool paused);
    void setScanProgress(int total, int completed);
    void startNextMetadataScan();
    void storeMetadata(const QVariantMap &item, const QVariantMap &metadata);

    QVariantList m_tracks;
    QSet<QString> m_favorites;
    QHash<QString, QVariantMap> m_history;
    QHash<QString, CachedMeta> m_metadataCache;
    QList<QVariantMap> m_scanQueue;
    QString m_sourceSignature;
    quint64 m_generation = 0;
    bool m_loading = false;
    bool m_scanPaused = false;
    bool m_scanWorkerActive = false;
    QTimer m_trackNotifyTimer;
    int m_scanTotal = 0;
    int m_scanCompleted = 0;
};
