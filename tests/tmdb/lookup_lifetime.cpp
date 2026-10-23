#include "tmdbmetadatamanager.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QTimer>
#include <QUrlQuery>
#include <cstdio>
#include <cstdlib>

static void require(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

// Only the HTTP transport is replaced. Parsing, phase transitions, fallback,
// completion and caching run through the application's actual implementation.
class FakeTmdb final : public TmdbMetadataManager {
public:
    enum Mode { Unmatched, NetworkError, RelaxedMatch, TvFallbackMatch,
                FestivalYear, UnverifiedYear, DateError, EpisodeLocalized, EpisodeEnglishFallback,
                EpisodeMissing, EpisodeWrongNumber, EpisodeNoPlot };
    Mode mode = Unmatched;
    QList<QUrl> requests;
    int callbacksAlive = 0;
    int movieSearches = 0;
    int tvSearches = 0;
private:
    void getJson(const QUrl &url,
                 const std::function<void(const QJsonObject &, const QString &)> &callback) override {
        requests.append(url);
        QJsonObject json;
        QString error;
        if (mode == NetworkError) error = QStringLiteral("Fixture network error");
        else if (url.path().contains("/episode/")) {
            if (mode == EpisodeMissing) error = "TMDB 404: Episode unavailable";
            else {
                const bool english = QUrlQuery(url).queryItemValue("language").startsWith("en");
                json = QJsonObject{{"id", 456}, {"season_number", mode == EpisodeWrongNumber ? 2 : 1},
                    {"episode_number", 3}, {"name", english ? "Episode name" : "Bölüm adı"},
                    {"overview", mode == EpisodeNoPlot || (mode == EpisodeEnglishFallback && !english)
                        ? "" : english ? "Episode plot" : "Bölüm konusu"},
                    {"runtime", 47}, {"air_date", "2024-03-04"}};
            }
        }
        else if (url.path().contains("/search/")) {
            const bool tv = url.path().endsWith("/tv");
            const int phase = tv ? ++tvSearches : ++movieSearches;
            const bool match = (mode == RelaxedMatch && !tv && phase == 2)
                || (mode == TvFallbackMatch && tv);
            QJsonArray results;
            if (!tv && mode >= FestivalYear) {
                const bool english = QUrlQuery(url).queryItemValue("language") == "en-US";
                results.append(QJsonObject{{"id", 1291659}, {"title", english ? "The Wizard of the Kremlin" : "Kremlin'in Büyücüsü"},
                    {"original_title", "Le Mage du Kremlin"}, {"release_date", "2026-01-21"}, {"original_language", "fr"}});
            }
            if (match) results.append(QJsonObject{{"id", 123}, {tv ? "name" : "title", "Fixture Movie"},
                {tv ? "original_name" : "original_title", "Fixture Movie"},
                {tv ? "first_air_date" : "release_date", "2024-01-01"}, {"original_language", "en"}});
            json.insert("results", results);
        } else if (url.path().endsWith("/release_dates")) {
            if (mode == DateError) error = QStringLiteral("Fixture date error");
            const QString date = mode == FestivalYear ? "2025-08-31T00:00:00.000Z" : "2026-01-21T00:00:00.000Z";
            json = QJsonObject{{"results", QJsonArray{QJsonObject{{"iso_3166_1", "IT"},
                {"release_dates", QJsonArray{QJsonObject{{"release_date", date}, {"type", 1}}}}}}}};
        } else if (!url.path().endsWith("/images")) {
            json = QJsonObject{{"id", 123}, {"title", "Fixture Movie"}, {"name", "Fixture Movie"},
                {"original_title", "Fixture Movie"}, {"release_date", "2024-01-01"}, {"runtime", 90}};
        }
        ++callbacksAlive;
        QTimer::singleShot(0, this, [this, callback, json, error] {
            callback(json, error);
            --callbacksAlive;
        });
    }
};

static QVariantMap lookup(FakeTmdb &manager, const QString &path, bool typed = false,
                          const QString &name = QStringLiteral("Fixture Movie 2024.mkv")) {
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QVariantMap result;
    bool completed = false;
    const auto ready = QObject::connect(&manager, &TmdbMetadataManager::metadataReady,
        &loop, [&](const QString &source, const QVariantMap &metadata) {
            require(source == path, "completion retains the source path");
            result = metadata; completed = true; loop.quit();
        });
    const auto failed = QObject::connect(&manager, &TmdbMetadataManager::metadataFailed,
        &loop, [&](const QString &source, const QString &error) {
            require(source == path && !error.isEmpty(), "failure retains its source and error");
            result.insert("state", "error"); completed = true; loop.quit();
        });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    const auto initial = typed ? manager.lookupTyped(path, "Fixture Movie", 2024, "movie", "en")
                              : manager.lookup(path, name, manager.mode >= FakeTmdb::FestivalYear ? "tr" : "en");
    require(initial.value("state") == "loading", "uncached lookup starts asynchronously");
    timeout.start(5000); loop.exec();
    QObject::disconnect(ready); QObject::disconnect(failed);
    require(completed, "lookup completes without crashing or hanging");
    require(manager.callbacksAlive == 0, "completed transport callbacks are released");
    return result;
}

static QVariantMap episodeLookup(FakeTmdb &manager, const QString &path,
                                 const QVariantMap &series, const QString &language) {
    QEventLoop loop;
    QTimer timeout; timeout.setSingleShot(true);
    QVariantMap result;
    const auto connection = QObject::connect(&manager, &TmdbMetadataManager::metadataReady, &loop,
        [&](const QString &source, const QVariantMap &value) { require(source == path, "episode retains source"); result = value; loop.quit(); });
    const int requests = manager.requests.size();
    const auto initial = manager.lookupEpisode(path, series, language);
    manager.lookupEpisode(path, series, language);
    require(manager.requests.size() == requests + 1, "in-flight episode queries are deduplicated");
    require(initial.value("seasonNumber").toInt() == 1 && initial.value("episodeNumber").toInt() == 3, "episode identity is available immediately");
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(5000); loop.exec(); QObject::disconnect(connection);
    require(result.value("episodeChecked").toBool() && manager.callbacksAlive == 0, "episode callbacks finish and release captures");
    require(result.value("title") == series.value("title") && result.value("overview") == series.value("overview"), "series fallback metadata is preserved");
    return result;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setOrganizationName("Lurviko-QA"); app.setApplicationName("TmdbLifetime");
    for (int cycle = 0; cycle < 30; ++cycle) {
        FakeTmdb manager;
        manager.setApiToken("fixture-token");
        const QString prefix = QString("dlna://fixture/%1/").arg(cycle);
        auto result = lookup(manager, prefix + "unmatched");
        require(result.value("state") == "unmatched", "exhausted movie and TV searches finish unmatched");
        require(manager.movieSearches == 2 && manager.tvSearches == 2, "both search types exhaust their phases");
        const int beforeCached = manager.requests.size();
        require(manager.lookup(prefix + "unmatched", "Fixture Movie 2024.mkv", "en").value("state") == "unmatched",
                "unmatched cache remains usable");
        require(manager.requests.size() == beforeCached, "cached results do not restart searches");

        manager.movieSearches = manager.tvSearches = 0;
        result = lookup(manager, prefix + "typed", true);
        require(result.value("state") == "unmatched" && manager.tvSearches == 0, "typed movie does not fall back to TV");

        manager.mode = FakeTmdb::NetworkError;
        result = lookup(manager, prefix + "error");
        require(result.value("state") == "error", "network failure completes");
        result = lookup(manager, prefix + "error");
        require(result.value("state") == "error", "network failure clears pending state for retries");

        manager.movieSearches = manager.tvSearches = 0;
        manager.mode = FakeTmdb::RelaxedMatch;
        result = lookup(manager, prefix + "matched");
        require(result.value("state") == "ready" && result.value("tmdbId") == 123,
                "relaxed search still fetches metadata");
        require(manager.movieSearches == 2 && manager.tvSearches == 0, "a confident match stops type fallback");

        manager.movieSearches = manager.tvSearches = 0;
        manager.mode = FakeTmdb::TvFallbackMatch;
        result = lookup(manager, prefix + "tv");
        require(result.value("state") == "ready" && result.value("mediaType") == "tv",
                "movie exhaustion safely transitions to a TV match");

        const QString wizard = QStringLiteral("The Wizard of the Kremlin 2025 1080p WEB-DL DUAL AAC2.0 H.264 TSRG.mkv");
        manager.mode = FakeTmdb::FestivalYear;
        result = lookup(manager, prefix + "festival", false, wizard);
        require(result.value("state") == "ready" && result.value("tmdbId") == 1291659,
                "regional 2026 release matches its officially verified 2025 festival premiere");
        manager.mode = FakeTmdb::UnverifiedYear;
        result = lookup(manager, prefix + "wrong-year", false, wizard);
        require(result.value("state") == "unmatched", "exact title alone does not bypass a conflicting release year");
        manager.mode = FakeTmdb::DateError;
        result = lookup(manager, prefix + "date-error", false, wizard);
        require(result.value("state") == "unmatched", "date verification failure does not accept a conflicting year");
    }
    FakeTmdb episodes;
    episodes.setApiToken("fixture-token");
    const QVariantMap series{{"state", "ready"}, {"mediaType", "tv"}, {"tmdbId", 123},
                            {"title", "Series title"}, {"overview", "Series plot"}};
    episodes.mode = FakeTmdb::EpisodeLocalized;
    const QString source = "dlna://fixture/episodes/Show.S01E03.mkv";
    auto details = episodeLookup(episodes, source, series, "tr");
    require(details.value("episodeTitle") == "Bölüm adı" && details.value("episodeOverview") == "Bölüm konusu"
        && details.value("episodeRuntime").toInt() == 47, "localized episode metadata is returned");
    const int cachedRequests = episodes.requests.size();
    require(episodes.lookupEpisode(source, series, "tr").value("episodeTitle") == "Bölüm adı"
        && episodes.requests.size() == cachedRequests, "episode cache avoids repeated requests");
    episodes.mode = FakeTmdb::EpisodeEnglishFallback;
    details = episodeLookup(episodes, "dlna://fixture/episodes/fallback/Show.1x03.mkv", series, "tr");
    require(details.value("episodeTitle") == "Bölüm adı" && details.value("episodeOverview") == "Episode plot", "English overview fills missing localized plot without replacing localized title");
    episodes.mode = FakeTmdb::EpisodeMissing;
    details = episodeLookup(episodes, "dlna://fixture/episodes/missing/Show.S01E03.mkv", series, "en");
    require(!details.contains("episodeOverview"), "unavailable episode retains series fallback");
    episodes.mode = FakeTmdb::EpisodeWrongNumber;
    details = episodeLookup(episodes, "dlna://fixture/episodes/wrong/Show.S01E03.mkv", series, "en");
    require(!details.contains("episodeTitle"), "mismatched season response is never shown");
    episodes.mode = FakeTmdb::EpisodeLocalized;
    details = episodeLookup(episodes, "dlna://fixture/episodes/words/Show Sezon 1 Bölüm 3.mkv", series, "tr");
    const int beforeIgnored = episodes.requests.size();
    episodes.lookupEpisode("dlna://fixture/episodes/movie.2024.mkv", series, "en");
    auto movie = series; movie.insert("mediaType", "movie");
    episodes.lookupEpisode(source, movie, "en");
    require(episodes.requests.size() == beforeIgnored, "movies and ambiguous filenames never start an episode lookup");
    std::puts("PASS: episode identity, localization/fallback, cache/deduplication, unavailable/wrong episodes and callback lifetimes");
    std::puts("PASS: 30 cycles of unmatched/fallback/error/retry/matched TMDB lookups and callback release");
}
