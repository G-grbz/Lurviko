import QtQuick
import QtTest
import Lurviko.App
import "../../qml/components/VideoMetadata.js" as Titles

Item {
    width: 1000; height: 650
    QtObject { id: language; property string language: "tr" }
    TmdbPauseOverlay {
        id: pause
        anchors.fill: parent
        lang: language
        visible: false
        episodeDetails: true
    }
    TmdbHoverPreview { id: hover; anchors.fill: parent; lang: language; visible: false }
    TestCase {
        name: "VideoMetadata"
        function test_titles_data() {
            return [
                {tag: "actual-file", metadata: {}, name: "Show.S01E03.WEB-DL.mkv", expected: "Show.S01E03.WEB-DL.mkv"},
                {tag: "series", metadata: {title: "Dizi", mediaType: "tv"}, name: "Show.S01E03.mkv", expected: "Dizi · S01E03"},
                {tag: "episode", metadata: {title: "Dizi", mediaType: "tv", episodeTitle: "Yeni bölüm"}, name: "Show.1x03.mkv", expected: "Dizi · S01E03 · Yeni bölüm"},
                {tag: "words", metadata: {title: "Dizi", mediaType: "tv"}, name: "Show Sezon 1 Bölüm 3.mkv", expected: "Dizi · S01E03"},
                {tag: "specials", metadata: {title: "Show", mediaType: "tv"}, name: "Show.S00E01.mkv", expected: "Show · S00E01"},
                {tag: "movie", metadata: {title: "Movie", mediaType: "movie"}, name: "Film.S01E03.mkv", expected: "Movie"},
                {tag: "year-is-not-season", metadata: {title: "Movie"}, name: "Movie.2024.1080p.mkv", expected: "Movie"}
            ]
        }
        function test_titles(data) { compare(Titles.displayTitle(data.metadata, {name: data.name}), data.expected) }
        function test_hover_prefers_episode_details_and_keeps_missing_episode_fallback() {
            hover.itemData = {name: "Show.S01E03.mkv"}
            hover.metadata = {state: "ready", mediaType: "tv", title: "Dizi", overview: "Dizinin konusu",
                runtime: 60, episodeTitle: "Bölüm adı", episodeOverview: "Bölümün konusu", episodeRuntime: 47}
            compare(hover.titleText, "Dizi")
            compare(hover.episodeHeading, "S01E03 · Bölüm adı")
            compare(hover.overviewText, "Bölümün konusu")
            compare(hover.runtimeMinutes, 47)
            hover.metadata = {state: "ready", mediaType: "tv", title: "Dizi", overview: "Dizinin konusu", runtime: 60}
            compare(hover.overviewText, "Dizinin konusu")
            compare(hover.runtimeMinutes, 60)
        }
        function test_pause_prefers_episode_details_and_retains_series_fallback() {
            pause.itemData = {name: "Show.S01E03.mkv"}
            pause.metadata = {state: "ready", mediaType: "tv", title: "Dizi", overview: "Dizinin konusu",
                runtime: 60, episodeRuntime: 47, episodeTitle: "Bölüm adı", episodeOverview: "Bölümün konusu"}
            compare(pause.titleText, "Dizi")
            compare(pause.episodeHeading, "S01E03 · Bölüm adı")
            compare(pause.overviewText, "Bölümün konusu")
            compare(pause.runtimeMinutes, 47)
            pause.metadata = {state: "ready", mediaType: "tv", title: "Dizi", overview: "Dizinin konusu", runtime: 60}
            compare(pause.overviewText, "Dizinin konusu")
            compare(pause.runtimeMinutes, 60)
            pause.episodeDetails = false
            compare(pause.episodeHeading, "")
            pause.episodeDetails = true
        }
    }
}
