#pragma once

#include <QObject>
#include <QHash>
#include <functional>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

class TmdbMetadataManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString apiToken READ apiToken WRITE setApiToken NOTIFY apiTokenChanged)
    Q_PROPERTY(bool configured READ configured NOTIFY configuredChanged)
    Q_PROPERTY(QString preferredLanguage READ preferredLanguage WRITE setPreferredLanguage NOTIFY preferredLanguageChanged)
    Q_PROPERTY(QString cacheDirectory READ cacheDirectory CONSTANT)
    Q_PROPERTY(bool hoverEnabled READ hoverEnabled WRITE setHoverEnabled NOTIFY hoverEnabledChanged)
    Q_PROPERTY(bool hoverAnimationsEnabled READ hoverAnimationsEnabled WRITE setHoverAnimationsEnabled NOTIFY hoverAnimationsEnabledChanged)
    Q_PROPERTY(bool dlnaArtworkEnabled READ dlnaArtworkEnabled WRITE setDlnaArtworkEnabled NOTIFY dlnaArtworkEnabledChanged)
    Q_PROPERTY(bool localArtworkEnabled READ localArtworkEnabled WRITE setLocalArtworkEnabled NOTIFY localArtworkEnabledChanged)
    Q_PROPERTY(int hoverDelayMs READ hoverDelayMs WRITE setHoverDelayMs NOTIFY hoverDelayMsChanged)
    Q_PROPERTY(int artworkConcurrency READ artworkConcurrency WRITE setArtworkConcurrency NOTIFY artworkConcurrencyChanged)

public:
    explicit TmdbMetadataManager(QObject *parent = nullptr);
    ~TmdbMetadataManager() override;

    QString apiToken() const;
    void setApiToken(const QString &token);
    bool configured() const;
    QString preferredLanguage() const;
    void setPreferredLanguage(const QString &language);
    QString cacheDirectory() const;
    bool hoverEnabled() const;
    void setHoverEnabled(bool enabled);
    bool hoverAnimationsEnabled() const;
    void setHoverAnimationsEnabled(bool enabled);
    bool dlnaArtworkEnabled() const;
    void setDlnaArtworkEnabled(bool enabled);
    bool localArtworkEnabled() const;
    void setLocalArtworkEnabled(bool enabled);
    int hoverDelayMs() const;
    void setHoverDelayMs(int delayMs);
    int artworkConcurrency() const;
    void setArtworkConcurrency(int concurrency);

    Q_INVOKABLE QVariantMap lookup(const QString &filePath,
                                   const QString &displayName,
                                   const QString &language);
    Q_INVOKABLE QVariantMap lookupTyped(const QString &sourcePath,
                                        const QString &title,
                                        int year,
                                        const QString &mediaType,
                                        const QString &language);
    Q_INVOKABLE QVariantMap lookupById(const QString &sourcePath,
                                       int tmdbId,
                                       const QString &title,
                                       int year,
                                       const QString &mediaType,
                                       const QString &language);
    Q_INVOKABLE QVariantMap cachedLookup(const QString &sourcePath,
                                         const QString &language) const;
    Q_INVOKABLE QVariantMap lookupSeasonArtwork(const QString &sourcePath,
                                                int seriesId,
                                                int seasonNumber,
                                                const QString &seriesTitle,
                                                const QString &language);
    Q_INVOKABLE void clearForFile(const QString &filePath);
    Q_INVOKABLE void pruneMissingFiles();
    Q_INVOKABLE void clearCache();

signals:
    void apiTokenChanged();
    void configuredChanged();
    void preferredLanguageChanged();
    void hoverEnabledChanged();
    void hoverAnimationsEnabledChanged();
    void dlnaArtworkEnabledChanged();
    void localArtworkEnabledChanged();
    void hoverDelayMsChanged();
    void artworkConcurrencyChanged();
    void metadataReady(const QString &filePath, const QVariantMap &metadata);
    void metadataFailed(const QString &filePath, const QString &message);

private:
    struct ParsedName {
        QString title;
        int year = 0;
        bool tvSeries = false;
    };

    struct PendingLookup {
        QString sourcePath;
        QString displayName;
        QString language;
        QString locale;
        QString region;
        QString cacheKey;
        ParsedName parsed;
        bool allowTypeFallback = true;
    };

    using JsonCallback = std::function<void(const QJsonObject &, const QString &)>;
    using DownloadCallback = std::function<void(bool)>;

    QString normalizedLocalPath(const QString &filePath) const;
    QString cacheKeyFor(const QString &sourcePath, const QString &language) const;
    QString sourceKeyFor(const QString &sourcePath) const;
    QString metadataPathForKey(const QString &key) const;
    QString imagePathFor(const QString &sourceKey, const QString &kind, const QString &tmdbPath) const;
    QVariantMap readCache(const QString &sourcePath, const QString &language) const;
    void writeCache(const PendingLookup &pending, const QVariantMap &metadata) const;
    void removeCacheFile(const QString &jsonPath) const;

    ParsedName parseName(const QString &filePath, const QString &displayName) const;
    QString compareKey(const QString &value) const;
    QJsonObject chooseSearchResult(const QJsonArray &results,
                                   const ParsedName &parsed,
                                   const QString &language) const;
    QString normalizeLanguageCode(const QString &language) const;
    QString localeForLanguage(const QString &language) const;
    QString regionForLanguage(const QString &language) const;

    void startLookup(const PendingLookup &pending);
    void fetchDetails(const PendingLookup &pending, int movieId);
    void finishMetadata(const PendingLookup &pending,
                        int movieId,
                        const QJsonObject &localized,
                        const QJsonObject &english,
                        const QJsonObject &images);

    void getJson(const QUrl &url, const JsonCallback &callback);
    void downloadImage(const QString &tmdbPath,
                       const QString &size,
                       const QString &targetPath,
                       const DownloadCallback &callback);

    QString chooseLocalizedString(const QJsonObject &localized,
                                  const QJsonObject &english,
                                  const QString &key) const;
    QStringList genreNames(const QJsonObject &localized, const QJsonObject &english) const;
    QStringList castNames(const QJsonObject &details) const;
    QStringList crewNames(const QJsonObject &details, const QStringList &jobs, int maximum) const;
    QString certification(const QJsonObject &details, const QString &region, bool tvSeries) const;
    QString chooseImagePath(const QJsonArray &images, const QString &language) const;
    QString chooseBackdropPath(const QJsonArray &images, const QString &language) const;
    QVariantMap loadingResult(const PendingLookup &pending, const QString &state) const;

    QString m_apiToken;
    QString m_preferredLanguage = QStringLiteral("tr");
    QString m_baseDirectory;
    QString m_metadataDirectory;
    QString m_imageDirectory;
    QString m_settingsFile;
    bool m_hoverEnabled = true;
    bool m_hoverAnimationsEnabled = true;
    bool m_dlnaArtworkEnabled = true;
    bool m_localArtworkEnabled = true;
    int m_hoverDelayMs = 360;
    int m_artworkConcurrency = 6;
    QNetworkAccessManager m_network;
    QHash<QString, PendingLookup> m_pending;
    QTimer m_pruneTimer;
};
