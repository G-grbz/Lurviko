.pragma library

function key(value) {
    return String(value || "").normalize("NFC").trim().replace(/\s+/g, " ")
        .replace(/İ/g, "I").toLowerCase()
}

function split(value) {
    const values = Array.isArray(value) ? value : [value]
    const result = []
    const seen = ({})
    for (const entry of values) {
        // Explicit collaboration separators only: keep AC/DC and hyphenated
        // names intact. Metadata does not distinguish band names containing &.
        const parts = String(entry || "").split(/\s*(?:[,;&]|\b(?:feat\.?|ft\.?|featuring)\s+|\s+(?:with|and|ve|x|×|\+)\s+)\s*/i)
        for (const part of parts) {
            const name = part.normalize("NFC").trim().replace(/\s+/g, " ")
            const normalized = key(name)
            if (!normalized.length || seen[normalized]) continue
            seen[normalized] = true
            result.push(name)
        }
    }
    return result
}

function contains(value, artist) {
    const wanted = key(artist)
    return wanted.length > 0 && split(value).some(function(name) { return key(name) === wanted })
}

function escapeHtml(value) {
    return String(value || "").replace(/&/g, "&amp;").replace(/</g, "&lt;")
        .replace(/>/g, "&gt;").replace(/"/g, "&quot;")
}
