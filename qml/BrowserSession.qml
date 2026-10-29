pragma Singleton

import QtQuick
import QtCore

QtObject {
    property Settings settings: Settings {
        category: "TabBehavior"
        property bool activateNewTabs: true
        property bool resumeSession: false
        property bool warnOnMultipleTabs: true
        property string sessionJson: ""
    }
    readonly property bool activateNewTabs: settings.activateNewTabs
    readonly property bool resumeSession: settings.resumeSession
    readonly property bool warnOnMultipleTabs: settings.warnOnMultipleTabs

    function setActivateNewTabs(value) {
        settings.activateNewTabs = !!value
        settings.setValue("activateNewTabs", !!value)
    }
    function setResumeSession(value) {
        settings.resumeSession = !!value
        settings.setValue("resumeSession", !!value)
        if (!value) {
            settings.sessionJson = ""
            settings.setValue("sessionJson", "")
        }
        settings.sync()
    }
    function setWarnOnMultipleTabs(value) {
        settings.warnOnMultipleTabs = !!value
        settings.setValue("warnOnMultipleTabs", !!value)
        settings.sync()
    }
    function storableLocation(value) {
        if (typeof value !== "string" || !value.length || value.length > 16384 || /[\x00-\x1f]/.test(value))
            return false
        if (value.startsWith("/")) return true
        if (!/^[a-zA-Z][a-zA-Z0-9+.-]*:\//.test(value) || value.startsWith("private:"))
            return false
        // Never persist URL passwords or token-bearing queries. KIO retains
        // responsibility for authenticating a restored remote/admin location.
        if (/^[^:]+:\/\/[^/?#]*:[^/?#]*@/.test(value)) return false
        return value.startsWith("file:") || !/[?#]/.test(value)
    }
    function normalize(snapshot) {
        if (!snapshot || snapshot.version !== 1 || !Array.isArray(snapshot.locations)) return null
        const locations = []
        let activeIndex = 0
        const selected = Math.max(0, Math.min(snapshot.locations.length - 1, Number(snapshot.activeIndex) || 0))
        // Bound corrupted settings without constructing a model for each tab.
        for (let index = 0; index < Math.min(256, snapshot.locations.length); ++index) {
            const location = snapshot.locations[index]
            if (!storableLocation(location)) continue
            if (index <= selected) activeIndex = locations.length
            locations.push(location)
        }
        return locations.length ? {version: 1, locations: locations, activeIndex: activeIndex} : null
    }
    function save(snapshot) {
        const clean = resumeSession ? normalize(snapshot) : null
        settings.sessionJson = clean ? JSON.stringify(clean) : ""
        // Property autosave is deferred. Flush this exact snapshot before the
        // accepted close schedules Qt.quit(), rather than syncing an old value.
        settings.setValue("sessionJson", settings.sessionJson)
        settings.sync()
    }
    function load() {
        if (!resumeSession || settings.sessionJson.length > 1024 * 1024) return null
        try { return normalize(JSON.parse(settings.sessionJson)) } catch (error) { return null }
    }
}
