.pragma library

function coordinates(metadata, item) {
    metadata = metadata || {}
    const name = String(item ? (item.name || item.path || item.url || "") : "")
    const match = /(?:^|[\s._\-])(?:S(\d{1,2})[\s._\-]*E(\d{1,3})|(\d{1,2})x(\d{1,3}))(?=$|[\s._\-])/i.exec(name)
    const words = /(?:^|[\s._\-])(?:season|sezon)[\s._\-]*(\d{1,2})[\s._\-]*(?:episode|ep|b[oö]l[uü]m)[\s._\-]*(\d{1,3})(?=$|[\s._\-])/i.exec(name)
    const season = match ? Number(match[1] || match[3]) : words ? Number(words[1])
                        : (metadata.seasonNumber === undefined ? -1 : Number(metadata.seasonNumber))
    const episode = match ? Number(match[2] || match[4]) : words ? Number(words[2]) : Number(metadata.episodeNumber || 0)
    return season >= 0 && season <= 99 && episode > 0 && episode <= 999 ? {season: season, episode: episode} : null
}

function episodeLabel(metadata, item) {
    const pair = coordinates(metadata, item)
    if (!pair) return ""
    const label = "S" + String(pair.season).padStart(2, "0") + "E" + String(pair.episode).padStart(2, "0")
    const title = String((metadata || {}).episodeTitle || "").trim()
    return label + (title.length ? " · " + title : "")
}

function displayTitle(metadata, item) {
    metadata = metadata || {}
    const title = String(metadata.title || "").trim()
    const fallback = String(item ? (item.name || item.path || item.url || "") : "")
    if (!title.length) return fallback
    const episode = metadata.mediaType === "movie" ? "" : episodeLabel(metadata, item)
    return title + (episode.length ? " · " + episode : "")
}

function overview(metadata, episodeDetails) {
    metadata = metadata || {}
    return String(episodeDetails ? (metadata.episodeOverview || metadata.overview || "") : (metadata.overview || "")).trim()
}
