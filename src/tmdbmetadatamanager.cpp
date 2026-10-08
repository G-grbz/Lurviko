#include "tmdbmetadatamanager.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include "appmigration.h"
#include <QSet>
#include <QUrlQuery>
#include <algorithm>
#include <cmath>
#include <memory>

namespace {
QSet<TmdbMetadataManager *> g_tmdbManagers;

QString trimToken(QString value)
{
    value = value.trimmed();
    while (!value.isEmpty() && QStringLiteral("-_.[](){}").contains(value.front()))
        value.remove(0, 1);
    while (!value.isEmpty() && QStringLiteral("-_.[](){}").contains(value.back()))
        value.chop(1);
    return value.trimmed();
}

QString cleanTypedReleaseTitle(QString value, int releaseYear)
{
    value = value.trimmed();
    value = QFileInfo(value).completeBaseName();

    // When DLNA already supplies the authoritative release year, only that
    // exact year may delimit the title. Do not infer a different numeric title
    // token as a year (e.g. "Blade Runner 2049" or "2001: A Space Odyssey").
    if (releaseYear > 0) {
        const QRegularExpression exactYearRe(
            QStringLiteral(R"((?:^|[\s._\-\(\[\{])%1(?=$|[\s._\-\)\]\}]))").arg(releaseYear));
        const QRegularExpressionMatch yearMatch = exactYearRe.match(value);
        if (yearMatch.hasMatch() && yearMatch.capturedStart() > 0)
            value = value.left(yearMatch.capturedStart()).trimmed();
    }

    value.replace(QRegularExpression(QStringLiteral(R"([._\-]+)")), QStringLiteral(" "));
    value.replace(QRegularExpression(QStringLiteral(R"([\[\{][^\]\}]*[\]\}])")), QStringLiteral(" "));
    value.replace(QRegularExpression(QStringLiteral(R"(\([^\)]*\))")), QStringLiteral(" "));
    value = value.simplified();

    static const QRegularExpression technicalRe(QStringLiteral(
        R"(^(?:480p|576p|720p|1080p|1080i|1440p|2160p|4320p|4k|8k|uhd|hdr10\+?|hdr|dv|dolby|vision|web|webdl|webrip|bluray|bdrip|brrip|bdremux|remux|hdtv|dvdrip|hdrip|xvid|x264|x265|h264|h265|hevc|av1|10bit|8bit|aac|ac3|eac3|dd|ddp|dts|dtshd|truehd|atmos|multi|dual|dubbed|proper|repack|extended|unrated|criterion|imax|nf|netflix|amzn|amazon|dsnp|hmax|atvp|yify|rarbg|etrg|evo|sample)$)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression audioChannelRe(
        QStringLiteral(R"(^\d(?:\.\d)?ch$)"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression bracketTagRe(
        QStringLiteral(R"(^\d{3,4}x\d{3,4}$)"), QRegularExpression::CaseInsensitiveOption);

    const QStringList tokens = value.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QStringList titleTokens;
    for (QString token : tokens) {
        token = trimToken(token);
        if (token.isEmpty())
            continue;
        if (technicalRe.match(token).hasMatch()
                || audioChannelRe.match(token).hasMatch()
                || bracketTagRe.match(token).hasMatch())
            break;
        titleTokens.append(token);
    }
    const QString cleaned = titleTokens.join(QLatin1Char(' ')).simplified();
    return cleaned.isEmpty() ? value : cleaned;
}

bool isVirtualMediaSource(const QString &value)
{
    return value.startsWith(QStringLiteral("dlna://"), Qt::CaseInsensitive);
}

QStringList uniqueNames(const QStringList &values, int maximum)
{
    QStringList result;
    for (const QString &value : values) {
        if (value.isEmpty() || result.contains(value, Qt::CaseInsensitive))
            continue;
        result.append(value);
        if (maximum > 0 && result.size() >= maximum)
            break;
    }
    return result;
}
}

TmdbMetadataManager::TmdbMetadataManager(QObject *parent)
    : QObject(parent)
{
    QString configRoot = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (configRoot.isEmpty())
        configRoot = QDir::home().filePath(QStringLiteral(".config"));

    // Application startup migrates legacy configuration before model creation.
    m_baseDirectory = QDir(configRoot).filePath(QStringLiteral("Lurviko/tmdb"));
    m_metadataDirectory = QDir(m_baseDirectory).filePath(QStringLiteral("metadata"));
    m_imageDirectory = QDir(m_baseDirectory).filePath(QStringLiteral("images"));
    m_settingsFile = QDir(m_baseDirectory).filePath(QStringLiteral("settings.ini"));
    QDir().mkpath(m_metadataDirectory);
    QDir().mkpath(m_imageDirectory);

    QSettings settings(m_settingsFile, QSettings::IniFormat);
    m_apiToken = qEnvironmentVariable("TMDB_API_TOKEN").trimmed();
    if (m_apiToken.isEmpty())
        m_apiToken = settings.value(QStringLiteral("authentication/readAccessToken")).toString().trimmed();

    const QString envLanguage = qEnvironmentVariable("TMDB_LANGUAGE").trimmed();
    m_preferredLanguage = normalizeLanguageCode(envLanguage.isEmpty()
        ? settings.value(QStringLiteral("metadata/language"), QStringLiteral("tr")).toString()
        : envLanguage);
    m_hoverEnabled = settings.value(QStringLiteral("video/hoverEnabled"), true).toBool();
    m_hoverAnimationsEnabled = settings.value(QStringLiteral("video/hoverAnimationsEnabled"), true).toBool();
    m_dlnaArtworkEnabled = settings.value(QStringLiteral("video/dlnaArtworkEnabled"), true).toBool();
    m_localArtworkEnabled = settings.value(QStringLiteral("video/localArtworkEnabled"), true).toBool();
    m_hoverDelayMs = std::clamp(settings.value(QStringLiteral("video/hoverDelayMs"), 360).toInt(), 120, 1200);
    m_artworkConcurrency = std::clamp(settings.value(QStringLiteral("video/artworkConcurrency"), 6).toInt(), 1, 8);

    g_tmdbManagers.insert(this);

    connect(&m_pruneTimer, &QTimer::timeout, this, &TmdbMetadataManager::pruneMissingFiles);
    m_pruneTimer.setInterval(60 * 1000);
    m_pruneTimer.start();
    QTimer::singleShot(0, this, &TmdbMetadataManager::pruneMissingFiles);
}

TmdbMetadataManager::~TmdbMetadataManager()
{
    g_tmdbManagers.remove(this);
}

QString TmdbMetadataManager::apiToken() const
{
    return m_apiToken;
}

void TmdbMetadataManager::setApiToken(const QString &token)
{
    const QString normalized = token.trimmed();
    if (m_apiToken == normalized)
        return;

    QSettings settings(m_settingsFile, QSettings::IniFormat);
    settings.setValue(QStringLiteral("authentication/readAccessToken"), normalized);
    settings.sync();

    const auto instances = g_tmdbManagers.values();
    for (TmdbMetadataManager *manager : instances) {
        if (!manager || manager->m_apiToken == normalized)
            continue;
        const bool wasConfigured = manager->configured();
        manager->m_apiToken = normalized;
        emit manager->apiTokenChanged();
        if (wasConfigured != manager->configured())
            emit manager->configuredChanged();
    }
}

bool TmdbMetadataManager::configured() const
{
    return !m_apiToken.isEmpty();
}

QString TmdbMetadataManager::preferredLanguage() const
{
    return m_preferredLanguage;
}

void TmdbMetadataManager::setPreferredLanguage(const QString &language)
{
    const QString normalized = normalizeLanguageCode(language);
    if (m_preferredLanguage == normalized)
        return;

    QSettings settings(m_settingsFile, QSettings::IniFormat);
    settings.setValue(QStringLiteral("metadata/language"), normalized);
    settings.sync();

    const auto instances = g_tmdbManagers.values();
    for (TmdbMetadataManager *manager : instances) {
        if (!manager || manager->m_preferredLanguage == normalized)
            continue;
        manager->m_preferredLanguage = normalized;
        emit manager->preferredLanguageChanged();
    }
}

QString TmdbMetadataManager::cacheDirectory() const
{
    return m_baseDirectory;
}

bool TmdbMetadataManager::hoverEnabled() const
{
    return m_hoverEnabled;
}

void TmdbMetadataManager::setHoverEnabled(bool enabled)
{
    if (m_hoverEnabled == enabled)
        return;
    QSettings settings(m_settingsFile, QSettings::IniFormat);
    settings.setValue(QStringLiteral("video/hoverEnabled"), enabled);
    settings.sync();
    const auto instances = g_tmdbManagers.values();
    for (TmdbMetadataManager *manager : instances) {
        if (!manager || manager->m_hoverEnabled == enabled)
            continue;
        manager->m_hoverEnabled = enabled;
        emit manager->hoverEnabledChanged();
    }
}

bool TmdbMetadataManager::hoverAnimationsEnabled() const
{
    return m_hoverAnimationsEnabled;
}

void TmdbMetadataManager::setHoverAnimationsEnabled(bool enabled)
{
    if (m_hoverAnimationsEnabled == enabled)
        return;
    QSettings settings(m_settingsFile, QSettings::IniFormat);
    settings.setValue(QStringLiteral("video/hoverAnimationsEnabled"), enabled);
    settings.sync();
    const auto instances = g_tmdbManagers.values();
    for (TmdbMetadataManager *manager : instances) {
        if (!manager || manager->m_hoverAnimationsEnabled == enabled)
            continue;
        manager->m_hoverAnimationsEnabled = enabled;
        emit manager->hoverAnimationsEnabledChanged();
    }
}

bool TmdbMetadataManager::dlnaArtworkEnabled() const
{
    return m_dlnaArtworkEnabled;
}

void TmdbMetadataManager::setDlnaArtworkEnabled(bool enabled)
{
    if (m_dlnaArtworkEnabled == enabled)
        return;
    QSettings settings(m_settingsFile, QSettings::IniFormat);
    settings.setValue(QStringLiteral("video/dlnaArtworkEnabled"), enabled);
    settings.sync();
    const auto instances = g_tmdbManagers.values();
    for (TmdbMetadataManager *manager : instances) {
        if (!manager || manager->m_dlnaArtworkEnabled == enabled)
            continue;
        manager->m_dlnaArtworkEnabled = enabled;
        emit manager->dlnaArtworkEnabledChanged();
    }
}

bool TmdbMetadataManager::localArtworkEnabled() const
{
    return m_localArtworkEnabled;
}

void TmdbMetadataManager::setLocalArtworkEnabled(bool enabled)
{
    if (m_localArtworkEnabled == enabled)
        return;
    QSettings settings(m_settingsFile, QSettings::IniFormat);
    settings.setValue(QStringLiteral("video/localArtworkEnabled"), enabled);
    settings.sync();
    const auto instances = g_tmdbManagers.values();
    for (TmdbMetadataManager *manager : instances) {
        if (!manager || manager->m_localArtworkEnabled == enabled)
            continue;
        manager->m_localArtworkEnabled = enabled;
        emit manager->localArtworkEnabledChanged();
    }
}

int TmdbMetadataManager::hoverDelayMs() const
{
    return m_hoverDelayMs;
}

void TmdbMetadataManager::setHoverDelayMs(int delayMs)
{
    const int normalized = std::clamp(delayMs, 120, 1200);
    if (m_hoverDelayMs == normalized)
        return;
    QSettings settings(m_settingsFile, QSettings::IniFormat);
    settings.setValue(QStringLiteral("video/hoverDelayMs"), normalized);
    settings.sync();
    const auto instances = g_tmdbManagers.values();
    for (TmdbMetadataManager *manager : instances) {
        if (!manager || manager->m_hoverDelayMs == normalized)
            continue;
        manager->m_hoverDelayMs = normalized;
        emit manager->hoverDelayMsChanged();
    }
}

int TmdbMetadataManager::artworkConcurrency() const
{
    return m_artworkConcurrency;
}

void TmdbMetadataManager::setArtworkConcurrency(int concurrency)
{
    const int normalized = std::clamp(concurrency, 1, 8);
    if (m_artworkConcurrency == normalized)
        return;
    QSettings settings(m_settingsFile, QSettings::IniFormat);
    settings.setValue(QStringLiteral("video/artworkConcurrency"), normalized);
    settings.sync();
    const auto instances = g_tmdbManagers.values();
    for (TmdbMetadataManager *manager : instances) {
        if (!manager || manager->m_artworkConcurrency == normalized)
            continue;
        manager->m_artworkConcurrency = normalized;
        emit manager->artworkConcurrencyChanged();
    }
}

QString TmdbMetadataManager::normalizedLocalPath(const QString &filePath) const
{
    const QString trimmed = filePath.trimmed();
    if (isVirtualMediaSource(trimmed))
        return trimmed;
    QUrl url(trimmed);
    QString path;
    if (url.isValid() && url.isLocalFile())
        path = url.toLocalFile();
    else
        path = trimmed;
    if (path.startsWith(QStringLiteral("file://")))
        path = QUrl(path).toLocalFile();
    return QDir::cleanPath(path);
}

QString TmdbMetadataManager::sourceKeyFor(const QString &sourcePath) const
{
    return QString::fromLatin1(QCryptographicHash::hash(sourcePath.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QString TmdbMetadataManager::cacheKeyFor(const QString &sourcePath, const QString &language) const
{
    return QString::fromLatin1(QCryptographicHash::hash(
        (sourcePath + QLatin1Char('\n') + language.toLower()).toUtf8(), QCryptographicHash::Sha256).toHex());
}

QString TmdbMetadataManager::metadataPathForKey(const QString &key) const
{
    return QDir(m_metadataDirectory).filePath(key + QStringLiteral(".json"));
}

QString TmdbMetadataManager::imagePathFor(const QString &sourceKey,
                                          const QString &kind,
                                          const QString &tmdbPath) const
{
    const QFileInfo info(tmdbPath);
    QString suffix = info.suffix().toLower();
    if (suffix.isEmpty() || suffix.size() > 5)
        suffix = QStringLiteral("jpg");
    const QString imageHash = QString::fromLatin1(QCryptographicHash::hash(
        tmdbPath.toUtf8(), QCryptographicHash::Sha1).toHex().left(12));
    return QDir(m_imageDirectory).filePath(
        sourceKey + QLatin1Char('-') + kind + QLatin1Char('-') + imageHash + QLatin1Char('.') + suffix);
}

QVariantMap TmdbMetadataManager::readCache(const QString &sourcePath, const QString &language) const
{
    const QString key = cacheKeyFor(sourcePath, language);
    QFile file(metadataPathForKey(key));
    if (!file.open(QIODevice::ReadOnly))
        return {};

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return {};
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("schemaVersion")).toInt() != 10)
        return {};
    if (root.value(QStringLiteral("sourcePath")).toString() != sourcePath)
        return {};

    if (!isVirtualMediaSource(sourcePath)) {
        const QFileInfo sourceInfo(sourcePath);
        if (!sourceInfo.exists())
            return {};
        const qint64 savedSize = static_cast<qint64>(root.value(QStringLiteral("sourceSize")).toDouble(-1));
        const qint64 savedModified = static_cast<qint64>(root.value(QStringLiteral("sourceModifiedMs")).toDouble(-1));
        if (savedSize != sourceInfo.size() || savedModified != sourceInfo.lastModified().toMSecsSinceEpoch())
            return {};
    }

    QVariantMap result = root.value(QStringLiteral("metadata")).toObject().toVariantMap();
    for (const auto &field : {QStringLiteral("posterUrl"), QStringLiteral("backdropUrl"), QStringLiteral("logoUrl")}) {
        if (result.contains(field)) result[field] = relocatedAppPath(result.value(field).toString());
    }
    if (!result.contains(QStringLiteral("state")))
        result.insert(QStringLiteral("state"), QStringLiteral("ready"));
    result.insert(QStringLiteral("cached"), true);
    return result;
}

void TmdbMetadataManager::writeCache(const PendingLookup &pending, const QVariantMap &metadata) const
{
    const bool virtualSource = isVirtualMediaSource(pending.sourcePath);
    const QFileInfo sourceInfo(pending.sourcePath);
    if (!virtualSource && !sourceInfo.exists())
        return;

    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), 10);
    root.insert(QStringLiteral("sourcePath"), pending.sourcePath);
    root.insert(QStringLiteral("sourceSize"), virtualSource ? -1.0 : static_cast<double>(sourceInfo.size()));
    root.insert(QStringLiteral("sourceModifiedMs"), virtualSource ? -1.0 : static_cast<double>(sourceInfo.lastModified().toMSecsSinceEpoch()));
    root.insert(QStringLiteral("language"), pending.language);
    root.insert(QStringLiteral("savedAtMs"), static_cast<double>(QDateTime::currentMSecsSinceEpoch()));
    root.insert(QStringLiteral("metadata"), QJsonObject::fromVariantMap(metadata));

    QSaveFile file(metadataPathForKey(pending.cacheKey));
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    file.commit();
}

void TmdbMetadataManager::removeCacheFile(const QString &jsonPath) const
{
    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly)) {
        QFile::remove(jsonPath);
        return;
    }
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QString sourcePath = root.value(QStringLiteral("sourcePath")).toString();
    QFile::remove(jsonPath);

    if (sourcePath.isEmpty())
        return;
    const QString prefix = sourceKeyFor(sourcePath) + QLatin1Char('-');
    QDir imageDir(m_imageDirectory);
    const QStringList files = imageDir.entryList({prefix + QStringLiteral("*")}, QDir::Files);
    for (const QString &name : files)
        imageDir.remove(name);
}

void TmdbMetadataManager::clearForFile(const QString &filePath)
{
    const QString sourcePath = normalizedLocalPath(filePath);
    if (sourcePath.isEmpty())
        return;
    QDir metadataDir(m_metadataDirectory);
    const QStringList files = metadataDir.entryList({QStringLiteral("*.json")}, QDir::Files);
    for (const QString &name : files) {
        const QString jsonPath = metadataDir.filePath(name);
        QFile file(jsonPath);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        if (root.value(QStringLiteral("sourcePath")).toString() == sourcePath)
            removeCacheFile(jsonPath);
    }
}

void TmdbMetadataManager::pruneMissingFiles()
{
    QDir metadataDir(m_metadataDirectory);
    const QStringList files = metadataDir.entryList({QStringLiteral("*.json")}, QDir::Files);
    for (const QString &name : files) {
        const QString jsonPath = metadataDir.filePath(name);
        QFile file(jsonPath);
        if (!file.open(QIODevice::ReadOnly)) {
            QFile::remove(jsonPath);
            continue;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (!doc.isObject()) {
            removeCacheFile(jsonPath);
            continue;
        }
        const QString sourcePath = doc.object().value(QStringLiteral("sourcePath")).toString();
        if (sourcePath.isEmpty()) {
            removeCacheFile(jsonPath);
            continue;
        }
        if (!isVirtualMediaSource(sourcePath) && !QFileInfo::exists(sourcePath))
            removeCacheFile(jsonPath);
    }
}

void TmdbMetadataManager::clearCache()
{
    QDir metadataDir(m_metadataDirectory);
    const QStringList metadata = metadataDir.entryList({QStringLiteral("*.json")}, QDir::Files);
    for (const QString &name : metadata)
        metadataDir.remove(name);
    QDir imageDir(m_imageDirectory);
    const QStringList images = imageDir.entryList(QDir::Files);
    for (const QString &name : images)
        imageDir.remove(name);
}

TmdbMetadataManager::ParsedName TmdbMetadataManager::parseName(const QString &filePath,
                                                               const QString &displayName) const
{
    QString base = displayName.trimmed();
    if (base.isEmpty())
        base = QFileInfo(filePath).fileName();
    base = QFileInfo(base).completeBaseName();

    ParsedName parsed;

    // Series releases are intentionally matched to the parent show, not to an
    // individual episode. Examples: "Isler Gucler - S01E03 ..." and
    // "Isler Gucler - S01 ..." both become "Isler Gucler".
    static const QRegularExpression seasonEpisodeRe(
        QStringLiteral(R"((?:^|[\s._\-])S\d{1,2}(?:[\s._\-]*E\d{1,3})?(?=$|[\s._\-]))"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression xEpisodeRe(
        QStringLiteral(R"((?:^|[\s._\-])\d{1,2}x\d{1,3}(?=$|[\s._\-]))"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression wordSeasonRe(
        QStringLiteral(R"((?:^|[\s._\-])(?:season|sezon)[\s._\-]*\d{1,2}(?=$|[\s._\-]))"),
        QRegularExpression::CaseInsensitiveOption);

    QRegularExpressionMatch seriesMatch = seasonEpisodeRe.match(base);
    if (!seriesMatch.hasMatch())
        seriesMatch = xEpisodeRe.match(base);
    if (!seriesMatch.hasMatch())
        seriesMatch = wordSeasonRe.match(base);
    if (seriesMatch.hasMatch()) {
        parsed.tvSeries = true;
        base = base.left(seriesMatch.capturedStart()).trimmed();
    }

    // Prefer an explicitly bracketed production year.  This is both more
    // reliable and avoids mistaking video resolutions for dates: "2160" used
    // to match the old 21xx year expression in names such as
    // "Soğuk (2014) ... 2160 WEBDL" and silently changed the year to 2160.
    // Years are also bounded to a realistic movie/TV range relative to today.
    const int maximumReleaseYear = QDateTime::currentDateTimeUtc().date().year() + 3;
    auto plausibleYear = [maximumReleaseYear](int year) {
        return year >= 1888 && year <= maximumReleaseYear;
    };

    static const QRegularExpression explicitYearRe(
        QStringLiteral(R"([\(\[\{](18\d{2}|19\d{2}|20\d{2}|21\d{2})[\)\]\}])"));
    QRegularExpressionMatch chosenYearMatch;
    QRegularExpressionMatchIterator explicitYears = explicitYearRe.globalMatch(base);
    while (explicitYears.hasNext()) {
        const QRegularExpressionMatch candidate = explicitYears.next();
        if (plausibleYear(candidate.captured(1).toInt()))
            chosenYearMatch = candidate;
    }

    // If there is no bracketed year, accept the final standalone plausible
    // year only when there is actual title text before it.  This keeps numeric
    // movie titles such as "1917" and "2046" intact when no separate release
    // year is present, while still handling "Movie Title 2019 1080p".
    if (!chosenYearMatch.hasMatch()) {
        static const QRegularExpression standaloneYearRe(
            QStringLiteral(R"((?:^|[\s._\-])(18\d{2}|19\d{2}|20\d{2}|21\d{2})(?=$|[\s._\-]))"));
        QRegularExpressionMatchIterator standaloneYears = standaloneYearRe.globalMatch(base);
        while (standaloneYears.hasNext()) {
            const QRegularExpressionMatch candidate = standaloneYears.next();
            const int year = candidate.captured(1).toInt();
            if (!plausibleYear(year))
                continue;
            const int yearStart = candidate.capturedStart(1);
            if (!base.left(yearStart).trimmed().isEmpty())
                chosenYearMatch = candidate;
        }
    }

    if (chosenYearMatch.hasMatch()) {
        parsed.year = chosenYearMatch.captured(1).toInt();
        int boundary = chosenYearMatch.capturedStart();
        if (chosenYearMatch.capturedStart(1) >= 0) {
            // For a standalone-year regex capturedStart() may include the
            // separator before the year. For an explicit bracketed year it is
            // already the opening bracket, which is exactly the title boundary.
            boundary = chosenYearMatch.capturedStart();
        }
        if (boundary > 0)
            base = base.left(boundary).trimmed();
    }

    base.replace(QRegularExpression(QStringLiteral(R"([._\-]+)")), QStringLiteral(" "));
    if (parsed.year > 0) {
        // Be defensive against malformed DLNA titles such as
        // "Title (2003) (2003)" that may already contain a duplicated year.
        base.replace(QRegularExpression(
            QStringLiteral(R"([\(\[\{]?\b%1\b[\)\]\}]?)").arg(parsed.year)),
            QStringLiteral(" "));
    }
    base.replace(QRegularExpression(QStringLiteral(R"([\[\{][^\]\}]*[\]\}])")), QStringLiteral(" "));
    base.replace(QRegularExpression(QStringLiteral(R"(\([^\)]*\))")), QStringLiteral(" "));
    base = base.simplified();

    static const QRegularExpression technicalRe(QStringLiteral(
        R"(^(?:480p|576p|720p|1080p|1080i|1440p|2160p|4320p|4k|8k|uhd|hdr10\+?|hdr|dv|dolby|vision|web|webdl|webrip|bluray|bdrip|brrip|bdremux|remux|hdtv|dvdrip|hdrip|xvid|x264|x265|h264|h265|hevc|av1|10bit|8bit|aac|ac3|eac3|dd|ddp|dts|dtshd|truehd|atmos|multi|dual|dubbed|proper|repack|extended|unrated|criterion|imax|nf|netflix|amzn|amazon|dsnp|hmax|atvp|yify|rarbg|etrg|evo|sample)$)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression audioChannelRe(QStringLiteral(R"(^\d(?:\.\d)?ch$)"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression bracketTagRe(QStringLiteral(R"(^\d{3,4}x\d{3,4}$)"), QRegularExpression::CaseInsensitiveOption);

    const QStringList tokens = base.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QStringList titleTokens;
    for (QString token : tokens) {
        token = trimToken(token);
        if (token.isEmpty())
            continue;
        if (technicalRe.match(token).hasMatch()
                || audioChannelRe.match(token).hasMatch()
                || bracketTagRe.match(token).hasMatch())
            break;
        titleTokens.append(token);
    }
    parsed.title = titleTokens.join(QLatin1Char(' ')).simplified();
    if (parsed.title.isEmpty())
        parsed.title = base.simplified();
    return parsed;
}

QString TmdbMetadataManager::compareKey(const QString &value) const
{
    QString result = value.normalized(QString::NormalizationForm_D).toLower();
    result.remove(QRegularExpression(QStringLiteral("[\\p{Mn}]")));
    result.replace(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}]+")), QStringLiteral(" "));
    result.replace(QRegularExpression(QStringLiteral(R"(\b(?:bolum|cilt|volume)\b)"),
                                      QRegularExpression::CaseInsensitiveOption),
                   QStringLiteral("vol"));
    return result.simplified();
}

QJsonObject TmdbMetadataManager::chooseSearchResult(const QJsonArray &results,
                                                    const ParsedName &parsed,
                                                    const QString &language) const
{
    const QString wanted = compareKey(parsed.title);
    if (wanted.isEmpty())
        return {};

    const QString titleField = parsed.tvSeries ? QStringLiteral("name") : QStringLiteral("title");
    const QString originalField = parsed.tvSeries ? QStringLiteral("original_name") : QStringLiteral("original_title");
    const QString dateField = parsed.tvSeries ? QStringLiteral("first_air_date") : QStringLiteral("release_date");
    const QString preferredLanguage = normalizeLanguageCode(language);
    const QString preferredRegion = regionForLanguage(preferredLanguage);
    double bestScore = -1000.0;
    QJsonObject best;

    const QStringList wantedTokenList = wanted.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    int resultIndex = 0;
    for (const QJsonValue &value : results) {
        const QJsonObject candidate = value.toObject();
        const QString title = compareKey(candidate.value(titleField).toString());
        const QString original = compareKey(candidate.value(originalField).toString());
        const int resultYear = candidate.value(dateField).toString().left(4).toInt();

        // A year supplied by DLNA/file naming is strong identity data. Do not
        // silently jump to a same-named production from another year. TMDB can
        // occasionally omit the date, so an undated exact-title result remains
        // eligible, but a conflicting known year is rejected.
        if (parsed.year > 0 && resultYear > 0 && resultYear != parsed.year)
            continue;

        double score = 0.0;
        if (title == wanted)
            score += 120.0;
        else if (original == wanted)
            score += 112.0;
        else if (wanted.size() >= 4
                 && ((!title.isEmpty() && (title.contains(wanted) || wanted.contains(title)))
                     || (!original.isEmpty() && (original.contains(wanted) || wanted.contains(original)))))
            score += 72.0;
        else {
            QSet<QString> wantedTokens;
            for (const QString &token : wantedTokenList)
                wantedTokens.insert(token);
            const QStringList titleTokens = title.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            int overlap = 0;
            for (const QString &token : titleTokens)
                overlap += wantedTokens.contains(token) ? 1 : 0;
            const double ratio = wantedTokens.isEmpty() ? 0.0 : double(overlap) / wantedTokens.size();
            score += 64.0 * ratio;
        }

        if (parsed.year > 0)
            score += resultYear == parsed.year ? 55.0 : 8.0;

        // TMDB search also matches localized/alternative titles that are not
        // necessarily returned in the result's title/original_title fields.
        // Example: a Turkish query such as "Bir Konuşabilse" can correctly
        // return Lost in Translation (2003), while the JSON title remains
        // English.  When the query is descriptive (2+ tokens), the production
        // year matches exactly and TMDB ranked the candidate first, trust that
        // search-rank signal.  Short one-word titles (e.g. "Gece") stay on the
        // stricter path because they are too ambiguous for this relaxation.
        if (resultIndex == 0
                && parsed.year > 0
                && resultYear == parsed.year
                && wantedTokenList.size() >= 2)
            score += 78.0;

        // This is especially important for translated titles such as the very
        // short Turkish title "Gece": several productions can have the same
        // localized title and year. Prefer a production whose original language
        // matches the user's metadata language, without penalizing translated
        // foreign titles (e.g. "Gece Uçuşu" / Red Eye).
        if (candidate.value(QStringLiteral("original_language")).toString().toLower() == preferredLanguage)
            score += 28.0;

        if (parsed.tvSeries) {
            const QJsonArray countries = candidate.value(QStringLiteral("origin_country")).toArray();
            for (const QJsonValue &country : countries) {
                if (country.toString().compare(preferredRegion, Qt::CaseInsensitive) == 0) {
                    score += 10.0;
                    break;
                }
            }
        }

        score += std::min(6.0, std::log10(1.0 + candidate.value(QStringLiteral("vote_count")).toDouble()) * 1.5);
        score += std::min(4.0, candidate.value(QStringLiteral("popularity")).toDouble() / 25.0);
        if (score > bestScore) {
            bestScore = score;
            best = candidate;
        }
        ++resultIndex;
    }

    const double threshold = parsed.year > 0 ? 120.0 : 96.0;
    return bestScore >= threshold ? best : QJsonObject{};
}

QString TmdbMetadataManager::normalizeLanguageCode(const QString &language) const
{
    QString code = language.trimmed().toLower();
    code.replace(QLatin1Char('_'), QLatin1Char('-'));
    if (code.contains(QLatin1Char('-')))
        code = code.section(QLatin1Char('-'), 0, 0);
    static const QRegularExpression codeRe(QStringLiteral(R"(^[a-z]{2}$)"));
    return codeRe.match(code).hasMatch() ? code : QStringLiteral("tr");
}

QString TmdbMetadataManager::localeForLanguage(const QString &language) const
{
    const QString code = normalizeLanguageCode(language);
    static const QHash<QString, QString> locales {
        {QStringLiteral("tr"), QStringLiteral("tr-TR")},
        {QStringLiteral("en"), QStringLiteral("en-US")},
        {QStringLiteral("de"), QStringLiteral("de-DE")},
        {QStringLiteral("fr"), QStringLiteral("fr-FR")},
        {QStringLiteral("es"), QStringLiteral("es-ES")},
        {QStringLiteral("it"), QStringLiteral("it-IT")},
        {QStringLiteral("pt"), QStringLiteral("pt-BR")},
        {QStringLiteral("ru"), QStringLiteral("ru-RU")},
        {QStringLiteral("ar"), QStringLiteral("ar-SA")},
        {QStringLiteral("ja"), QStringLiteral("ja-JP")},
        {QStringLiteral("ko"), QStringLiteral("ko-KR")},
        {QStringLiteral("zh"), QStringLiteral("zh-CN")},
        {QStringLiteral("nl"), QStringLiteral("nl-NL")},
        {QStringLiteral("pl"), QStringLiteral("pl-PL")},
        {QStringLiteral("sv"), QStringLiteral("sv-SE")},
        {QStringLiteral("da"), QStringLiteral("da-DK")},
        {QStringLiteral("fi"), QStringLiteral("fi-FI")},
        {QStringLiteral("no"), QStringLiteral("no-NO")},
        {QStringLiteral("el"), QStringLiteral("el-GR")},
        {QStringLiteral("cs"), QStringLiteral("cs-CZ")},
        {QStringLiteral("hu"), QStringLiteral("hu-HU")},
        {QStringLiteral("ro"), QStringLiteral("ro-RO")},
        {QStringLiteral("bg"), QStringLiteral("bg-BG")},
        {QStringLiteral("uk"), QStringLiteral("uk-UA")},
        {QStringLiteral("he"), QStringLiteral("he-IL")},
        {QStringLiteral("id"), QStringLiteral("id-ID")},
        {QStringLiteral("th"), QStringLiteral("th-TH")},
        {QStringLiteral("vi"), QStringLiteral("vi-VN")},
        {QStringLiteral("fa"), QStringLiteral("fa-IR")}
    };
    return locales.value(code, code);
}

QString TmdbMetadataManager::regionForLanguage(const QString &language) const
{
    const QString locale = localeForLanguage(language);
    return locale.contains(QLatin1Char('-'))
        ? locale.section(QLatin1Char('-'), 1, 1).toUpper()
        : QStringLiteral("US");
}

QVariantMap TmdbMetadataManager::loadingResult(const PendingLookup &pending, const QString &state) const
{
    QVariantMap result;
    result.insert(QStringLiteral("state"), state);
    result.insert(QStringLiteral("parsedTitle"), pending.parsed.title);
    if (pending.parsed.year > 0)
        result.insert(QStringLiteral("parsedYear"), pending.parsed.year);
    result.insert(QStringLiteral("sourcePath"), pending.sourcePath);
    result.insert(QStringLiteral("mediaType"), pending.parsed.tvSeries ? QStringLiteral("tv") : QStringLiteral("movie"));
    return result;
}

QVariantMap TmdbMetadataManager::lookup(const QString &filePath,
                                        const QString &displayName,
                                        const QString &language)
{
    const QString sourcePath = normalizedLocalPath(filePath);
    const QString normalizedLanguage = normalizeLanguageCode(language.isEmpty() ? m_preferredLanguage : language);

    PendingLookup pending;
    pending.sourcePath = sourcePath;
    pending.displayName = displayName;
    pending.language = normalizedLanguage;
    pending.locale = localeForLanguage(normalizedLanguage);
    pending.region = regionForLanguage(normalizedLanguage);
    pending.cacheKey = cacheKeyFor(sourcePath, normalizedLanguage);
    pending.parsed = parseName(sourcePath, displayName);

    if (sourcePath.isEmpty() || (!isVirtualMediaSource(sourcePath) && !QFileInfo::exists(sourcePath)))
        return loadingResult(pending, QStringLiteral("missing_file"));

    const QVariantMap cached = readCache(sourcePath, normalizedLanguage);
    if (!cached.isEmpty())
        return cached;

    if (!configured())
        return loadingResult(pending, QStringLiteral("needs_token"));
    if (pending.parsed.title.isEmpty())
        return loadingResult(pending, QStringLiteral("unmatched"));

    const QString requestKey = sourcePath + QLatin1Char('\n') + normalizedLanguage;
    if (!m_pending.contains(requestKey)) {
        m_pending.insert(requestKey, pending);
        startLookup(pending);
    }
    return loadingResult(pending, QStringLiteral("loading"));
}


QVariantMap TmdbMetadataManager::lookupTyped(const QString &sourcePathValue,
                                             const QString &title,
                                             int year,
                                             const QString &mediaType,
                                             const QString &language)
{
    const QString sourcePath = normalizedLocalPath(sourcePathValue);
    const QString normalizedLanguage = normalizeLanguageCode(language.isEmpty() ? m_preferredLanguage : language);

    PendingLookup pending;
    pending.sourcePath = sourcePath;
    pending.displayName = title.trimmed();
    pending.language = normalizedLanguage;
    pending.locale = localeForLanguage(normalizedLanguage);
    pending.region = regionForLanguage(normalizedLanguage);
    pending.cacheKey = cacheKeyFor(sourcePath, normalizedLanguage);
    // DLNA titles can be clean library names or complete release names. If
    // DLNA already supplied a release year, use that exact year as the title
    // boundary and never reinterpret numeric title words as another year.
    if (year > 0) {
        pending.parsed.title = cleanTypedReleaseTitle(title, year);
        pending.parsed.year = year;
    } else {
        pending.parsed = parseName(sourcePath, title);
    }
    const QString type = mediaType.trimmed().toLower();
    pending.parsed.tvSeries = type == QStringLiteral("tv")
                           || type == QStringLiteral("show")
                           || type == QStringLiteral("series");
    pending.allowTypeFallback = false;

    if (sourcePath.isEmpty() || (!isVirtualMediaSource(sourcePath) && !QFileInfo::exists(sourcePath)))
        return loadingResult(pending, QStringLiteral("missing_file"));

    const QVariantMap cached = readCache(sourcePath, normalizedLanguage);
    if (!cached.isEmpty())
        return cached;

    if (!configured())
        return loadingResult(pending, QStringLiteral("needs_token"));
    if (pending.parsed.title.isEmpty())
        return loadingResult(pending, QStringLiteral("unmatched"));

    const QString requestKey = sourcePath + QLatin1Char('\n') + normalizedLanguage;
    if (!m_pending.contains(requestKey)) {
        m_pending.insert(requestKey, pending);
        startLookup(pending);
    }
    return loadingResult(pending, QStringLiteral("loading"));
}

QVariantMap TmdbMetadataManager::lookupById(const QString &sourcePathValue,
                                            int tmdbId,
                                            const QString &title,
                                            int year,
                                            const QString &mediaType,
                                            const QString &language)
{
    const QString sourcePath = normalizedLocalPath(sourcePathValue);
    const QString normalizedLanguage = normalizeLanguageCode(language.isEmpty() ? m_preferredLanguage : language);

    PendingLookup pending;
    pending.sourcePath = sourcePath;
    pending.displayName = title.trimmed();
    pending.language = normalizedLanguage;
    pending.locale = localeForLanguage(normalizedLanguage);
    pending.region = regionForLanguage(normalizedLanguage);
    pending.cacheKey = cacheKeyFor(sourcePath, normalizedLanguage);
    // DLNA titles can be clean library names or complete release names. If
    // DLNA already supplied a release year, use that exact year as the title
    // boundary and never reinterpret numeric title words as another year.
    if (year > 0) {
        pending.parsed.title = cleanTypedReleaseTitle(title, year);
        pending.parsed.year = year;
    } else {
        pending.parsed = parseName(sourcePath, title);
    }
    const QString type = mediaType.trimmed().toLower();
    pending.parsed.tvSeries = type == QStringLiteral("tv")
                           || type == QStringLiteral("show")
                           || type == QStringLiteral("series");
    pending.allowTypeFallback = false;

    if (sourcePath.isEmpty() || (!isVirtualMediaSource(sourcePath) && !QFileInfo::exists(sourcePath)))
        return loadingResult(pending, QStringLiteral("missing_file"));
    const QVariantMap cached = readCache(sourcePath, normalizedLanguage);
    if (!cached.isEmpty())
        return cached;
    if (!configured())
        return loadingResult(pending, QStringLiteral("needs_token"));
    if (tmdbId <= 0)
        return loadingResult(pending, QStringLiteral("unmatched"));

    const QString requestKey = sourcePath + QLatin1Char('\n') + normalizedLanguage;
    if (!m_pending.contains(requestKey)) {
        m_pending.insert(requestKey, pending);
        fetchDetails(pending, tmdbId);
    }
    return loadingResult(pending, QStringLiteral("loading"));
}

QVariantMap TmdbMetadataManager::cachedLookup(const QString &sourcePathValue,
                                              const QString &language) const
{
    const QString sourcePath = normalizedLocalPath(sourcePathValue);
    if (sourcePath.isEmpty())
        return {};
    const QString normalizedLanguage = normalizeLanguageCode(language.isEmpty() ? m_preferredLanguage : language);
    return readCache(sourcePath, normalizedLanguage);
}

QVariantMap TmdbMetadataManager::lookupSeasonArtwork(const QString &filePath,
                                                      int seriesId,
                                                      int seasonNumber,
                                                      const QString &seriesTitle,
                                                      const QString &language)
{
    const QString sourcePath = normalizedLocalPath(filePath);
    const QString normalizedLanguage = normalizeLanguageCode(language.isEmpty() ? m_preferredLanguage : language);

    PendingLookup pending;
    pending.sourcePath = sourcePath;
    pending.displayName = seriesTitle;
    pending.language = normalizedLanguage;
    pending.locale = localeForLanguage(normalizedLanguage);
    pending.region = regionForLanguage(normalizedLanguage);
    pending.cacheKey = cacheKeyFor(sourcePath, normalizedLanguage);
    pending.parsed.title = seriesTitle.trimmed();
    pending.parsed.tvSeries = true;

    if (sourcePath.isEmpty() || (!isVirtualMediaSource(sourcePath) && !QFileInfo::exists(sourcePath)))
        return loadingResult(pending, QStringLiteral("missing_file"));

    const QVariantMap cached = readCache(sourcePath, normalizedLanguage);
    if (!cached.isEmpty())
        return cached;

    if (!configured())
        return loadingResult(pending, QStringLiteral("needs_token"));
    if (seriesId <= 0 || seasonNumber < 0)
        return loadingResult(pending, QStringLiteral("unmatched"));

    const QString requestKey = sourcePath + QLatin1Char('\n') + normalizedLanguage;
    if (m_pending.contains(requestKey))
        return loadingResult(pending, QStringLiteral("loading"));
    m_pending.insert(requestKey, pending);

    auto finish = [this, pending, requestKey, seriesId, seasonNumber](const QString &tmdbPath,
                                                                     bool isBackdrop) {
        QVariantMap metadata;
        metadata.insert(QStringLiteral("state"), QStringLiteral("ready"));
        metadata.insert(QStringLiteral("tmdbId"), seriesId);
        metadata.insert(QStringLiteral("mediaType"), QStringLiteral("tv"));
        metadata.insert(QStringLiteral("seasonNumber"), seasonNumber);
        metadata.insert(QStringLiteral("title"), pending.parsed.title);
        metadata.insert(QStringLiteral("sourcePath"), pending.sourcePath);
        metadata.insert(QStringLiteral("cached"), false);

        if (tmdbPath.isEmpty()) {
            m_pending.remove(requestKey);
            writeCache(pending, metadata);
            emit metadataReady(pending.sourcePath, metadata);
            return;
        }

        const QString sourceKey = sourceKeyFor(pending.sourcePath);
        // v6 uses higher-resolution artwork and a new cache key so older
        // low-resolution files cannot be reused accidentally.
        const QString kind = isBackdrop ? QStringLiteral("season-backdrop-v6") : QStringLiteral("season-poster-v6");
        const QString targetPath = imagePathFor(sourceKey, kind, tmdbPath);
        const QString size = isBackdrop ? QStringLiteral("w1280") : QStringLiteral("w780");
        downloadImage(tmdbPath, size, targetPath,
                      [this, pending, requestKey, metadata, targetPath, isBackdrop](bool ok) mutable {
            QVariantMap result = metadata;
            if (ok) {
                const QString localUrl = QUrl::fromLocalFile(targetPath).toString();
                result.insert(QStringLiteral("artworkUrl"), localUrl);
                result.insert(isBackdrop ? QStringLiteral("backdropUrl") : QStringLiteral("posterUrl"), localUrl);
            }
            m_pending.remove(requestKey);
            writeCache(pending, result);
            emit metadataReady(pending.sourcePath, result);
        });
    };

    QUrl seasonImages(QStringLiteral("https://api.themoviedb.org/3/tv/%1/season/%2/images")
                          .arg(seriesId).arg(seasonNumber));
    QUrlQuery seasonQuery;
    seasonQuery.addQueryItem(QStringLiteral("include_image_language"),
                             normalizedLanguage + QStringLiteral(",en,null"));
    seasonImages.setQuery(seasonQuery);

    getJson(seasonImages, [this, pending, requestKey, seriesId, normalizedLanguage, finish](const QJsonObject &json,
                                                                                           const QString &error) {
        if (!error.isEmpty()) {
            m_pending.remove(requestKey);
            emit metadataFailed(pending.sourcePath, error);
            return;
        }

        const QString poster = chooseImagePath(json.value(QStringLiteral("posters")).toArray(), normalizedLanguage);
        if (!poster.isEmpty()) {
            finish(poster, false);
            return;
        }

        // Some seasons have no dedicated poster. Fall back to the show's
        // clean backdrop instead of leaving the folder card blank.
        QUrl seriesImages(QStringLiteral("https://api.themoviedb.org/3/tv/%1/images").arg(seriesId));
        QUrlQuery seriesQuery;
        seriesQuery.addQueryItem(QStringLiteral("include_image_language"),
                                 normalizedLanguage + QStringLiteral(",en,null"));
        seriesImages.setQuery(seriesQuery);
        getJson(seriesImages, [this, pending, requestKey, normalizedLanguage, finish](const QJsonObject &seriesJson,
                                                                                      const QString &seriesError) {
            if (!seriesError.isEmpty()) {
                m_pending.remove(requestKey);
                emit metadataFailed(pending.sourcePath, seriesError);
                return;
            }
            const QString backdrop = chooseBackdropPath(seriesJson.value(QStringLiteral("backdrops")).toArray(), normalizedLanguage);
            if (!backdrop.isEmpty()) {
                finish(backdrop, true);
                return;
            }
            const QString poster = chooseImagePath(seriesJson.value(QStringLiteral("posters")).toArray(), normalizedLanguage);
            finish(poster, false);
        });
    });

    return loadingResult(pending, QStringLiteral("loading"));
}

void TmdbMetadataManager::startLookup(const PendingLookup &pending)
{
    auto searchUrl = [this](const PendingLookup &lookup,
                            bool tvSeries,
                            bool relaxed,
                            const QString &queryOverride = QString()) {
        QUrl url(QStringLiteral("https://api.themoviedb.org/3/search/%1")
                     .arg(tvSeries ? QStringLiteral("tv") : QStringLiteral("movie")));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("query"),
                           queryOverride.trimmed().isEmpty() ? lookup.parsed.title : queryOverride.trimmed());
        query.addQueryItem(QStringLiteral("include_adult"), QStringLiteral("false"));

        if (relaxed) {
            // TMDB can index localized/alternative titles differently depending
            // on the requested locale.  A second, broad English search keeps
            // the same local year verification in chooseSearchResult(), but
            // avoids false negatives such as Turkish/localized titles whose
            // first search result is exposed under an English title.
            query.addQueryItem(QStringLiteral("language"), QStringLiteral("en-US"));
        } else {
            query.addQueryItem(QStringLiteral("language"), lookup.locale);
            if (!tvSeries)
                query.addQueryItem(QStringLiteral("region"), lookup.region);
            if (lookup.parsed.year > 0) {
                query.addQueryItem(tvSeries ? QStringLiteral("first_air_date_year") : QStringLiteral("year"),
                                   QString::number(lookup.parsed.year));
            }
        }
        url.setQuery(query);
        return url;
    };

    auto finishUnmatched = [this](const PendingLookup &lookup) {
        const QString requestKey = lookup.sourcePath + QLatin1Char('\n') + lookup.language;
        m_pending.remove(requestKey);
        QVariantMap data = loadingResult(lookup, QStringLiteral("unmatched"));
        data.insert(QStringLiteral("message"), QStringLiteral("No confident TMDB match"));
        writeCache(lookup, data);
        emit metadataReady(lookup.sourcePath, data);
    };

    using SearchDone = std::function<void(bool)>;
    auto tryType = std::make_shared<std::function<void(PendingLookup, bool, SearchDone)>>();
    *tryType = [this, searchUrl](PendingLookup lookup,
                                          bool tvSeries,
                                          SearchDone done) {
        lookup.parsed.tvSeries = tvSeries;

        QStringList queryVariants;
        queryVariants.append(lookup.parsed.title.trimmed());
        const QString folded = compareKey(lookup.parsed.title);
        if (!folded.isEmpty()
                && folded.compare(lookup.parsed.title.trimmed(), Qt::CaseInsensitive) != 0)
            queryVariants.append(folded);
        queryVariants.removeDuplicates();

        // phase 0: normal localized + server-side year constrained search
        // phase 1+: broad English search, first with the original cleaned title
        // and then (when useful) with a diacritic/punctuation-folded variant.
        const int totalPhases = 1 + queryVariants.size();
        auto runPhase = std::make_shared<std::function<void(int)>>();
        *runPhase = [this, lookup, tvSeries, searchUrl, queryVariants, totalPhases, runPhase, done](int phase) {
            if (phase >= totalPhases) {
                *runPhase = {};
                done(false);
                return;
            }

            const bool relaxed = phase > 0;
            const QString queryText = relaxed ? queryVariants.value(phase - 1) : lookup.parsed.title;
            getJson(searchUrl(lookup, tvSeries, relaxed, queryText),
                    [this, lookup, tvSeries, phase, runPhase, done](const QJsonObject &json,
                                                                    const QString &error) {
                if (!error.isEmpty()) {
                    // Preserve the old behavior for a real network/API error;
                    // a relaxed title retry is only for valid but unmatched
                    // search responses.
                    const QString requestKey = lookup.sourcePath + QLatin1Char('\n') + lookup.language;
                    m_pending.remove(requestKey);
                    emit metadataFailed(lookup.sourcePath, error);
                    *runPhase = {};
                    done(true);
                    return;
                }

                ParsedName parsed = lookup.parsed;
                parsed.tvSeries = tvSeries;
                const QJsonObject result = chooseSearchResult(
                    json.value(QStringLiteral("results")).toArray(), parsed, lookup.language);
                const int mediaId = result.value(QStringLiteral("id")).toInt();
                if (mediaId > 0) {
                    PendingLookup matched = lookup;
                    matched.parsed.tvSeries = tvSeries;
                    *runPhase = {};
                    fetchDetails(matched, mediaId);
                    done(true);
                    return;
                }

                (*runPhase)(phase + 1);
            });
        };

        (*runPhase)(0);
    };

    const bool forceTv = pending.parsed.tvSeries;
    (*tryType)(pending, forceTv, [this, pending, forceTv, tryType, finishUnmatched](bool handled) {
        if (handled)
            return;

        // A plain filename can still be a TV title without an Sxx marker. Try
        // the exact same robust search sequence against TV only after movie
        // lookup (including its relaxed variants) has genuinely failed.
        if (!forceTv && pending.allowTypeFallback) {
            PendingLookup tvFallback = pending;
            tvFallback.parsed.tvSeries = true;
            (*tryType)(tvFallback, true, [tvFallback, finishUnmatched](bool tvHandled) {
                if (!tvHandled)
                    finishUnmatched(tvFallback);
            });
            return;
        }

        finishUnmatched(pending);
    });
}

void TmdbMetadataManager::fetchDetails(const PendingLookup &pending, int mediaId)
{
    const QString namespaceName = pending.parsed.tvSeries ? QStringLiteral("tv") : QStringLiteral("movie");
    auto detailUrl = [mediaId, namespaceName, pending](const QString &locale) {
        QUrl url(QStringLiteral("https://api.themoviedb.org/3/%1/%2").arg(namespaceName).arg(mediaId));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("language"), locale);
        query.addQueryItem(QStringLiteral("append_to_response"),
                           pending.parsed.tvSeries ? QStringLiteral("credits,content_ratings")
                                                   : QStringLiteral("credits,release_dates"));
        url.setQuery(query);
        return url;
    };

    getJson(detailUrl(pending.locale), [this, pending, mediaId, detailUrl, namespaceName](const QJsonObject &localized, const QString &localizedError) {
        if (!localizedError.isEmpty()) {
            const QString requestKey = pending.sourcePath + QLatin1Char('\n') + pending.language;
            m_pending.remove(requestKey);
            emit metadataFailed(pending.sourcePath, localizedError);
            return;
        }

        auto fetchImages = [this, pending, mediaId, localized, namespaceName](const QJsonObject &english) {
            QUrl imagesUrl(QStringLiteral("https://api.themoviedb.org/3/%1/%2/images").arg(namespaceName).arg(mediaId));
            QUrlQuery query;
            query.addQueryItem(QStringLiteral("include_image_language"),
                               pending.language == QStringLiteral("en")
                                   ? QStringLiteral("en,null")
                                   : pending.language + QStringLiteral(",en,null"));
            imagesUrl.setQuery(query);
            getJson(imagesUrl, [this, pending, mediaId, localized, english](const QJsonObject &images, const QString &imageError) {
                const QString requestKey = pending.sourcePath + QLatin1Char('\n') + pending.language;
                if (!imageError.isEmpty()) {
                    m_pending.remove(requestKey);
                    emit metadataFailed(pending.sourcePath, imageError);
                    return;
                }
                finishMetadata(pending, mediaId, localized, english, images);
            });
        };

        if (pending.language == QStringLiteral("en")) {
            fetchImages(localized);
            return;
        }

        getJson(detailUrl(QStringLiteral("en-US")), [fetchImages, localized](const QJsonObject &english, const QString &) {
            fetchImages(english.isEmpty() ? localized : english);
        });
    });
}

QString TmdbMetadataManager::chooseLocalizedString(const QJsonObject &localized,
                                                    const QJsonObject &english,
                                                    const QString &key) const
{
    const QString local = localized.value(key).toString().trimmed();
    return !local.isEmpty() ? local : english.value(key).toString().trimmed();
}

QStringList TmdbMetadataManager::genreNames(const QJsonObject &localized, const QJsonObject &english) const
{
    auto names = [](const QJsonArray &array) {
        QStringList result;
        for (const QJsonValue &value : array) {
            const QString name = value.toObject().value(QStringLiteral("name")).toString().trimmed();
            if (!name.isEmpty())
                result.append(name);
        }
        return result;
    };
    QStringList result = names(localized.value(QStringLiteral("genres")).toArray());
    if (result.isEmpty())
        result = names(english.value(QStringLiteral("genres")).toArray());
    return result;
}

QStringList TmdbMetadataManager::castNames(const QJsonObject &details) const
{
    QStringList result;
    const QJsonArray cast = details.value(QStringLiteral("credits")).toObject().value(QStringLiteral("cast")).toArray();
    for (const QJsonValue &value : cast) {
        const QString name = value.toObject().value(QStringLiteral("name")).toString().trimmed();
        if (!name.isEmpty())
            result.append(name);
        if (result.size() >= 6)
            break;
    }
    return uniqueNames(result, 6);
}

QStringList TmdbMetadataManager::crewNames(const QJsonObject &details,
                                           const QStringList &jobs,
                                           int maximum) const
{
    QStringList result;
    const QJsonArray crew = details.value(QStringLiteral("credits")).toObject().value(QStringLiteral("crew")).toArray();
    for (const QString &wantedJob : jobs) {
        for (const QJsonValue &value : crew) {
            const QJsonObject person = value.toObject();
            if (person.value(QStringLiteral("job")).toString().compare(wantedJob, Qt::CaseInsensitive) != 0)
                continue;
            result.append(person.value(QStringLiteral("name")).toString().trimmed());
            if (maximum > 0 && uniqueNames(result, maximum).size() >= maximum)
                return uniqueNames(result, maximum);
        }
    }
    return uniqueNames(result, maximum);
}

QString TmdbMetadataManager::certification(const QJsonObject &details,
                                           const QString &region,
                                           bool tvSeries) const
{
    const QStringList preferred = region == QStringLiteral("TR")
        ? QStringList{QStringLiteral("TR"), QStringLiteral("US")}
        : QStringList{region, QStringLiteral("US"), QStringLiteral("TR")};

    if (tvSeries) {
        const QJsonArray ratings = details.value(QStringLiteral("content_ratings")).toObject()
                                     .value(QStringLiteral("results")).toArray();
        auto fromCountry = [&ratings](const QString &country) -> QString {
            for (const QJsonValue &value : ratings) {
                const QJsonObject row = value.toObject();
                if (row.value(QStringLiteral("iso_3166_1")).toString() != country)
                    continue;
                const QString rating = row.value(QStringLiteral("rating")).toString().trimmed();
                if (!rating.isEmpty())
                    return rating;
            }
            return {};
        };
        for (const QString &country : preferred) {
            const QString rating = fromCountry(country);
            if (!rating.isEmpty())
                return rating;
        }
        for (const QJsonValue &value : ratings) {
            const QString rating = value.toObject().value(QStringLiteral("rating")).toString().trimmed();
            if (!rating.isEmpty())
                return rating;
        }
        return {};
    }

    const QJsonArray countries = details.value(QStringLiteral("release_dates")).toObject()
                                   .value(QStringLiteral("results")).toArray();
    auto fromCountry = [&countries](const QString &country) -> QString {
        for (const QJsonValue &value : countries) {
            const QJsonObject row = value.toObject();
            if (row.value(QStringLiteral("iso_3166_1")).toString() != country)
                continue;
            const QJsonArray dates = row.value(QStringLiteral("release_dates")).toArray();
            for (int wantedType : {3, 4, 6, 2, 1, 5}) {
                for (const QJsonValue &dateValue : dates) {
                    const QJsonObject date = dateValue.toObject();
                    if (date.value(QStringLiteral("type")).toInt() != wantedType)
                        continue;
                    const QString cert = date.value(QStringLiteral("certification")).toString().trimmed();
                    if (!cert.isEmpty())
                        return cert;
                }
            }
        }
        return {};
    };

    for (const QString &country : preferred) {
        const QString cert = fromCountry(country);
        if (!cert.isEmpty())
            return cert;
    }
    for (const QJsonValue &value : countries) {
        const QJsonArray dates = value.toObject().value(QStringLiteral("release_dates")).toArray();
        for (const QJsonValue &dateValue : dates) {
            const QString cert = dateValue.toObject().value(QStringLiteral("certification")).toString().trimmed();
            if (!cert.isEmpty())
                return cert;
        }
    }
    return {};
}

QString TmdbMetadataManager::chooseImagePath(const QJsonArray &images, const QString &language) const
{
    struct Candidate { int rank; double vote; int width; QString path; };
    QList<Candidate> candidates;
    for (const QJsonValue &value : images) {
        const QJsonObject image = value.toObject();
        const QString path = image.value(QStringLiteral("file_path")).toString();
        if (path.isEmpty())
            continue;
        const QJsonValue languageValue = image.value(QStringLiteral("iso_639_1"));
        const QString imageLanguage = languageValue.isNull() ? QString() : languageValue.toString().toLower();
        int rank = 9;
        if (imageLanguage == language.toLower())
            rank = 0;
        else if (imageLanguage == QStringLiteral("en"))
            rank = 1;
        else if (imageLanguage.isEmpty())
            rank = 2;
        if (rank < 9)
            candidates.append({rank, image.value(QStringLiteral("vote_average")).toDouble(),
                               image.value(QStringLiteral("width")).toInt(), path});
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) {
        if (a.rank != b.rank) return a.rank < b.rank;
        if (!qFuzzyCompare(a.vote + 1.0, b.vote + 1.0)) return a.vote > b.vote;
        return a.width > b.width;
    });
    return candidates.isEmpty() ? QString() : candidates.constFirst().path;
}

QString TmdbMetadataManager::chooseBackdropPath(const QJsonArray &images, const QString &language) const
{
    struct Candidate { int rank; double vote; int width; QString path; };
    QList<Candidate> candidates;
    const QString requestedLanguage = language.toLower();

    for (const QJsonValue &value : images) {
        const QJsonObject image = value.toObject();
        const QString path = image.value(QStringLiteral("file_path")).toString();
        if (path.isEmpty())
            continue;

        const QJsonValue languageValue = image.value(QStringLiteral("iso_639_1"));
        const QString imageLanguage = languageValue.isNull() ? QString() : languageValue.toString().toLower();

        // Backdrops are special: prefer clean artwork without embedded text.
        // If TMDB has none, use the selected metadata language, then English.
        int rank = 9;
        if (imageLanguage.isEmpty())
            rank = 0;
        else if (imageLanguage == requestedLanguage)
            rank = 1;
        else if (imageLanguage == QStringLiteral("en"))
            rank = 2;

        if (rank < 9)
            candidates.append({rank, image.value(QStringLiteral("vote_average")).toDouble(),
                               image.value(QStringLiteral("width")).toInt(), path});
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) {
        if (a.rank != b.rank) return a.rank < b.rank;
        if (!qFuzzyCompare(a.vote + 1.0, b.vote + 1.0)) return a.vote > b.vote;
        return a.width > b.width;
    });
    return candidates.isEmpty() ? QString() : candidates.constFirst().path;
}

void TmdbMetadataManager::finishMetadata(const PendingLookup &pending,
                                         int mediaId,
                                         const QJsonObject &localized,
                                         const QJsonObject &english,
                                         const QJsonObject &images)
{
    const bool tvSeries = pending.parsed.tvSeries;
    const QString titleKey = tvSeries ? QStringLiteral("name") : QStringLiteral("title");
    const QString originalTitleKey = tvSeries ? QStringLiteral("original_name") : QStringLiteral("original_title");
    const QString dateKey = tvSeries ? QStringLiteral("first_air_date") : QStringLiteral("release_date");

    QVariantMap metadata;
    metadata.insert(QStringLiteral("state"), QStringLiteral("ready"));
    metadata.insert(QStringLiteral("tmdbId"), mediaId);
    metadata.insert(QStringLiteral("mediaType"), tvSeries ? QStringLiteral("tv") : QStringLiteral("movie"));
    metadata.insert(QStringLiteral("title"), chooseLocalizedString(localized, english, titleKey));
    metadata.insert(QStringLiteral("originalTitle"), english.value(originalTitleKey).toString());
    metadata.insert(QStringLiteral("overview"), chooseLocalizedString(localized, english, QStringLiteral("overview")));
    metadata.insert(QStringLiteral("tagline"), chooseLocalizedString(localized, english, QStringLiteral("tagline")));
    metadata.insert(QStringLiteral("rating"), localized.value(QStringLiteral("vote_average")).toDouble());
    metadata.insert(QStringLiteral("voteCount"), localized.value(QStringLiteral("vote_count")).toInt());

    int runtime = localized.value(QStringLiteral("runtime")).toInt();
    if (tvSeries) {
        const QJsonArray runtimes = localized.value(QStringLiteral("episode_run_time")).toArray();
        if (!runtimes.isEmpty())
            runtime = runtimes.first().toInt();
        if (runtime <= 0)
            runtime = localized.value(QStringLiteral("last_episode_to_air")).toObject().value(QStringLiteral("runtime")).toInt();
    }
    metadata.insert(QStringLiteral("runtime"), runtime);
    metadata.insert(QStringLiteral("genres"), genreNames(localized, english));
    metadata.insert(QStringLiteral("cast"), castNames(localized));
    metadata.insert(QStringLiteral("directors"), crewNames(localized, {QStringLiteral("Director")}, 3));
    metadata.insert(QStringLiteral("producers"), crewNames(localized,
                    {QStringLiteral("Producer"), QStringLiteral("Executive Producer")}, 4));
    if (tvSeries) {
        QStringList creators;
        QJsonArray createdBy = localized.value(QStringLiteral("created_by")).toArray();
        if (createdBy.isEmpty())
            createdBy = english.value(QStringLiteral("created_by")).toArray();
        for (const QJsonValue &value : createdBy) {
            const QString name = value.toObject().value(QStringLiteral("name")).toString().trimmed();
            if (!name.isEmpty())
                creators.append(name);
        }
        metadata.insert(QStringLiteral("creators"), uniqueNames(creators, 4));
    }
    metadata.insert(QStringLiteral("certification"), certification(localized, pending.region, tvSeries));
    metadata.insert(QStringLiteral("sourcePath"), pending.sourcePath);
    metadata.insert(QStringLiteral("parsedTitle"), pending.parsed.title);
    metadata.insert(QStringLiteral("cached"), false);

    QString releaseDate = localized.value(dateKey).toString();
    if (releaseDate.isEmpty())
        releaseDate = english.value(dateKey).toString();
    metadata.insert(QStringLiteral("releaseDate"), releaseDate);
    metadata.insert(QStringLiteral("year"), releaseDate.left(4).toInt());

    const QString backdropTmdb = chooseBackdropPath(images.value(QStringLiteral("backdrops")).toArray(), pending.language);
    const QString logoTmdb = chooseImagePath(images.value(QStringLiteral("logos")).toArray(), pending.language);
    const QString posterTmdb = chooseImagePath(images.value(QStringLiteral("posters")).toArray(), pending.language);
    const QString sourceKey = sourceKeyFor(pending.sourcePath);
    const QString backdropPath = backdropTmdb.isEmpty() ? QString() : imagePathFor(sourceKey, QStringLiteral("backdrop-v6"), backdropTmdb);
    const QString logoPath = logoTmdb.isEmpty() ? QString() : imagePathFor(sourceKey, QStringLiteral("logo-v6"), logoTmdb);
    const QString posterPath = posterTmdb.isEmpty() ? QString() : imagePathFor(sourceKey, QStringLiteral("poster-v6"), posterTmdb);

    struct ImageJob {
        QString tmdbPath;
        QString size;
        QString targetPath;
        QString field;
    };
    QList<ImageJob> jobs;
    if (!backdropTmdb.isEmpty() && !backdropPath.isEmpty())
        jobs.append({backdropTmdb, QStringLiteral("w1280"), backdropPath, QStringLiteral("backdropUrl")});
    if (!logoTmdb.isEmpty() && !logoPath.isEmpty())
        jobs.append({logoTmdb, QStringLiteral("original"), logoPath, QStringLiteral("logoUrl")});
    if (!posterTmdb.isEmpty() && !posterPath.isEmpty())
        jobs.append({posterTmdb, QStringLiteral("w780"), posterPath, QStringLiteral("posterUrl")});

    struct DownloadState {
        int remaining = 0;
        QVariantMap metadata;
        PendingLookup pending;
    };
    auto state = std::make_shared<DownloadState>();
    state->remaining = jobs.size();
    state->metadata = metadata;
    state->pending = pending;

    auto finalize = [this, state]() {
        const QString requestKey = state->pending.sourcePath + QLatin1Char('\n') + state->pending.language;
        m_pending.remove(requestKey);
        if (!isVirtualMediaSource(state->pending.sourcePath)
                && !QFileInfo::exists(state->pending.sourcePath)) {
            clearForFile(state->pending.sourcePath);
            return;
        }
        writeCache(state->pending, state->metadata);
        emit metadataReady(state->pending.sourcePath, state->metadata);
    };

    if (jobs.isEmpty()) {
        finalize();
        return;
    }

    for (const ImageJob &job : jobs) {
        downloadImage(job.tmdbPath, job.size, job.targetPath,
                      [state, job, finalize](bool ok) {
            if (ok)
                state->metadata.insert(job.field, QUrl::fromLocalFile(job.targetPath).toString());
            --state->remaining;
            if (state->remaining == 0)
                finalize();
        });
    }
}

void TmdbMetadataManager::getJson(const QUrl &url, const JsonCallback &callback)
{
    QNetworkRequest request(url);
    request.setTransferTimeout(15000);
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + m_apiToken.toUtf8());
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Lurviko/1.0 TMDB metadata client"));
    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [reply, callback]() {
        const QByteArray payload = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString error;
        QJsonObject object;
        if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
            const QJsonObject errorObject = QJsonDocument::fromJson(payload).object();
            error = errorObject.value(QStringLiteral("status_message")).toString();
            if (error.isEmpty())
                error = reply->errorString();
            if (status > 0)
                error = QStringLiteral("TMDB %1: %2").arg(status).arg(error);
        } else {
            const QJsonDocument doc = QJsonDocument::fromJson(payload);
            if (doc.isObject())
                object = doc.object();
            else
                error = QStringLiteral("TMDB returned invalid JSON");
        }
        reply->deleteLater();
        callback(object, error);
    });
}

void TmdbMetadataManager::downloadImage(const QString &tmdbPath,
                                        const QString &size,
                                        const QString &targetPath,
                                        const DownloadCallback &callback)
{
    if (QFileInfo::exists(targetPath) && QFileInfo(targetPath).size() > 0) {
        callback(true);
        return;
    }
    QDir().mkpath(QFileInfo(targetPath).absolutePath());
    QUrl url(QStringLiteral("https://image.tmdb.org/t/p/%1%2").arg(size, tmdbPath));
    QNetworkRequest request(url);
    request.setTransferTimeout(20000);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Lurviko/1.0 TMDB metadata client"));
    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [reply, targetPath, callback]() {
        const QByteArray data = reply->readAll();
        const bool networkOk = reply->error() == QNetworkReply::NoError && !data.isEmpty();
        reply->deleteLater();
        if (!networkOk) {
            callback(false);
            return;
        }
        QSaveFile file(targetPath);
        if (!file.open(QIODevice::WriteOnly)) {
            callback(false);
            return;
        }
        file.write(data);
        callback(file.commit());
    });
}
