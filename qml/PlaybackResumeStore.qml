pragma Singleton

import QtQuick
import QtCore

QtObject {
    property Settings settings: Settings {
        category: "VideoResume"
        property string positionsJson: "{}"
    }

    function readMap() {
        try {
            // Read the backing store directly as well as the declared property.
            // This is robust across process restarts and avoids a stale singleton
            // property if settings were flushed by another instance.
            const raw = settings.value("positionsJson", settings.positionsJson || "{}")
            const parsed = JSON.parse(String(raw || "{}"))
            return parsed && typeof parsed === "object" ? parsed : ({})
        } catch (e) {
            return ({})
        }
    }

    function writeMap(map) {
        const json = JSON.stringify(map || ({}))
        settings.positionsJson = json
        settings.setValue("positionsJson", json)
        // Resume is user state, not a cosmetic preference: force it to disk so
        // closing Lurviko immediately after pausing cannot lose the position.
        settings.sync()
    }

    function positionFor(key) {
        const k = String(key || "")
        if (!k.length)
            return -1
        const entry = readMap()[k]
        if (entry === undefined || entry === null)
            return -1
        if (typeof entry === "number")
            return Math.max(0, Number(entry))
        return Math.max(0, Number(entry.position || 0))
    }

    function clear(key) {
        const k = String(key || "")
        if (!k.length)
            return
        const map = readMap()
        if (map[k] === undefined)
            return
        delete map[k]
        writeMap(map)
    }

    function save(key, position, duration) {
        const k = String(key || "")
        const p = Math.max(0, Number(position || 0))
        const d = Math.max(0, Number(duration || 0))
        if (!k.length || d <= 0)
            return

        // Very early stops are not useful resume points. Near the end, treat
        // the title as completed so it starts from the beginning next time.
        if (p < 3000 || p >= d - 60000 || p / d >= 0.95) {
            clear(k)
            return
        }

        const map = readMap()
        map[k] = ({ position: Math.round(p), duration: Math.round(d), updatedAt: Date.now() })

        // Keep the settings payload bounded while retaining plenty of history.
        const keys = Object.keys(map)
        if (keys.length > 300) {
            keys.sort(function(a, b) {
                return Number((map[a] && map[a].updatedAt) || 0)
                     - Number((map[b] && map[b].updatedAt) || 0)
            })
            while (keys.length > 300)
                delete map[keys.shift()]
        }
        writeMap(map)
    }
}
