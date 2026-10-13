.pragma library

// These are complete, already colored SVGs. No filters or pixel recoloring.
var colors = ["blue", "green", "orange", "pink", "purple", "red", "turquoise", "yellow"]
var colorWords = Object.assign(Object.create(null), {
    blue: "blue", mavi: "blue", lacivert: "blue", navy: "blue",
    green: "green", yesil: "green",
    orange: "orange", turuncu: "orange",
    pink: "pink", pembe: "pink", roze: "pink",
    purple: "purple", mor: "purple", eflatun: "purple", lila: "purple", violet: "purple",
    red: "red", kirmizi: "red", bordo: "red", burgundy: "red",
    turquoise: "turquoise", turkuaz: "turquoise", cyan: "turquoise", siyan: "turquoise", teal: "turquoise",
    yellow: "yellow", sari: "yellow", gold: "yellow", altin: "yellow"
})
var typeWords = Object.create(null)
var endings = ["larimiz", "lerimiz", "larim", "lerim", "lari", "leri", "lar", "ler"]
var priorities = ["picture", "video", "music", "documents", "download"]
var compoundWords = {
    picture: ["fotograf", "resim", "photo", "picture", "images", "screenshot", "wallpaper"],
    video: ["video", "movies", "filmler", "diziler", "belgesel", "documentary"],
    music: ["muzik", "sarki", "music", "songs", "podcast"],
    documents: ["belge", "document", "dokuman", "notlar", "fatura", "rapor", "sunum"],
    download: ["indirilen", "download", "indirmeler"]
}
var vocabulary = {
    picture: ["resim", "resimler", "resimlerim", "resimleri", "fotograf", "fotograflar", "fotograflarim", "fotograflari",
        "foto", "fotolar", "fotolarim", "picture", "pictures", "image", "images", "pic", "pics", "photo", "photos",
        "photograph", "photographs", "photography", "gallery", "galeri", "dcim", "camera", "kamera",
        "screenshot", "screenshots", "wallpaper", "wallpapers", "duvarkagidi", "duvarkagitlari", "fotoalbum"],
    video: ["video", "videos", "videolar", "videolarim", "film", "filmler", "filmlerim", "movie", "movies",
        "sinema", "cinema", "dizi", "diziler", "series", "tvshows", "tv", "anime", "clip", "clips", "klip", "klipler",
        "recording", "recordings", "footage", "belgesel", "belgeseller", "documentaries"],
    music: ["muzik", "muzikler", "muziklerim", "music", "musics", "audio", "ses", "sesler", "song", "songs",
        "sarki", "sarkilar", "sarkilarim", "track", "tracks", "parca", "parcalar", "album", "albums", "albumler",
        "playlist", "playlists", "calmalistesi", "calmalisteleri", "mp3", "flac", "podcast", "podcasts", "seslikitap", "audiobooks"],
    documents: ["belge", "belgeler", "belgelerim", "dokuman", "dokumanlar", "document", "documents", "doc", "docs",
        "office", "ofis", "pdf", "rapor", "raporlar", "report", "reports", "evrak", "evraklar", "fatura", "faturalar",
        "invoice", "invoices", "not", "notlar", "notes", "kitap", "kitaplar", "book", "books", "ebook", "ebooks",
        "presentation", "presentations", "sunum", "sunumlar", "spreadsheet", "spreadsheets", "word", "excel"],
    download: ["download", "downloads", "downloading", "indirilen", "indirilenler", "indirme", "indirmeler",
        "indirdiklerim", "torrent", "torrents", "torrenti", "transfer", "transfers"]
}
for (var type in vocabulary)
    for (var i = 0; i < vocabulary[type].length; ++i) typeWords[vocabulary[type][i]] = type

var cache = new Map()
var cacheKeys = []
var cacheSlot = 0
var cacheLimit = 512

function normalized(name) {
    return String(name || "").replace(/([a-zçğıöşü0-9])([A-ZÇĞİÖŞÜ])/g, "$1 $2").toLowerCase()
        .replace(/\u0307/g, "").replace(/ç/g, "c").replace(/ğ/g, "g").replace(/ı/g, "i")
        .replace(/ö/g, "o").replace(/ş/g, "s").replace(/ü/g, "u")
        .replace(/([a-z])([0-9])/g, "$1 $2").replace(/([0-9])([a-z])/g, "$1 $2")
}

function iconName(name) {
    var cacheKey = String(name || "")
    if (cache.has(cacheKey)) return cache.get(cacheKey)
    var text = normalized(name)
    var words = text.split(/[^a-z0-9]+/).filter(function(word) { return word.length > 0 })
    var compact = words.join("")
    var types = Object.create(null)
    var explicitColor = colorWords[compact] || ""
    for (var i = 0; i < words.length; ++i) {
        var word = words[i]
        if (typeWords[word]) types[typeWords[word]] = true
        // Common Turkish possessive/plural endings, with whole-word matching.
        for (var ending of endings)
            if (word.endsWith(ending) && typeWords[word.slice(0, -ending.length)])
                types[typeWords[word.slice(0, -ending.length)]] = true
    }
    if (compact.indexOf("ekrangoruntu") >= 0) types.picture = true
    if (compact.indexOf("calmaliste") >= 0) types.music = true
    if (!Object.keys(types).length) {
        // Joined names such as "tatilfotograflari" and "summerphotosbackup".
        // Short aliases (pic, doc, tv) remain whole words to avoid e.g. "topic".
        var compound = compact.replace(/appimages?/g, "")
        for (var group of priorities)
            for (var stem of compoundWords[group])
                if (compound.indexOf(stem) >= 0) types[group] = true
    }
    var kind = "folder"
    // A media/document qualifier is more specific than "Downloads".
    for (var candidate of priorities)
        if (types[candidate]) { kind = candidate; break }
    if (!explicitColor && kind !== "folder")
        for (var j = 0; j < words.length; ++j)
            if (colorWords[words[j]]) { explicitColor = colorWords[words[j]]; break }
    if (!explicitColor && kind !== "folder") {
        for (var colorWord in colorWords) {
            if (!compact.startsWith(colorWord)) continue
            var rest = compact.substring(colorWord.length)
            if (typeWords[rest] === kind) { explicitColor = colorWords[colorWord]; break }
            for (var prefix of compoundWords[kind])
                if (rest.startsWith(prefix)) { explicitColor = colorWords[colorWord]; break }
            if (explicitColor) break
        }
    }
    var hash = 0
    var seed = String(compact || name)
    for (var k = 0; k < seed.length; ++k) hash = ((hash << 5) - hash + seed.charCodeAt(k)) | 0
    var color = explicitColor || colors[(hash >>> 0) % colors.length]
    var result = color + "-" + kind + ".svg"
    if (cacheKeys.length < cacheLimit) cacheKeys.push(cacheKey)
    else {
        cache.delete(cacheKeys[cacheSlot])
        cacheKeys[cacheSlot] = cacheKey
        cacheSlot = (cacheSlot + 1) % cacheLimit
    }
    cache.set(cacheKey, result)
    return result
}
