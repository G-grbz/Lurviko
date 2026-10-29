import QtQuick
import QtTest
import Lurviko.App

TestCase {
    name: "BrowserSession"
    property var saved
    function initTestCase() {
        saved = {activate: BrowserSession.activateNewTabs, resume: BrowserSession.resumeSession,
            warn: BrowserSession.warnOnMultipleTabs, json: BrowserSession.settings.sessionJson}
    }
    function init() {
        BrowserSession.settings.activateNewTabs = true
        BrowserSession.settings.resumeSession = false
        BrowserSession.settings.warnOnMultipleTabs = true
        BrowserSession.settings.sessionJson = ""
    }
    function cleanupTestCase() {
        BrowserSession.settings.activateNewTabs = saved.activate
        BrowserSession.settings.resumeSession = saved.resume
        BrowserSession.settings.warnOnMultipleTabs = saved.warn
        BrowserSession.settings.sessionJson = saved.json
        BrowserSession.settings.sync()
    }
    function test_session_is_opt_in_and_disabling_clears_saved_locations() {
        const snapshot = {version: 1, locations: ["/tmp/first", "category:/images", "/tmp/first"], activeIndex: 1}
        BrowserSession.save(snapshot)
        compare(BrowserSession.settings.sessionJson, "")
        compare(BrowserSession.load(), null)
        BrowserSession.setResumeSession(true)
        BrowserSession.save(snapshot)
        compare(JSON.stringify(BrowserSession.load()), JSON.stringify(snapshot))
        BrowserSession.setResumeSession(false)
        compare(BrowserSession.load(), null)
        compare(BrowserSession.settings.sessionJson, "")
    }
    function test_only_locations_and_active_tab_are_saved() {
        BrowserSession.setResumeSession(true)
        BrowserSession.save({version: 1, locations: ["file:///tmp/one", "sftp://name@host/folder", "admin:///etc"],
            activeIndex: 2, query: "private search", searchSessionId: "expired worker"})
        const restored = BrowserSession.load()
        compare(restored.locations.length, 3)
        compare(restored.activeIndex, 2)
        verify(!BrowserSession.settings.sessionJson.includes("private search"))
        verify(!BrowserSession.settings.sessionJson.includes("expired worker"))
    }
    function test_invalid_and_sensitive_locations_are_skipped_with_stable_selection() {
        const restored = BrowserSession.normalize({version: 1, locations: ["/tmp/one", "private:/",
            "sftp://user:password@host/folder", "sftp://host/folder?token=secret", "relative-folder", "category:/videos"], activeIndex: 5})
        compare(restored.locations.length, 2)
        compare(restored.locations[0], "/tmp/one")
        compare(restored.locations[1], "category:/videos")
        compare(restored.activeIndex, 1)
        compare(BrowserSession.normalize({version: 2, locations: ["/tmp/one"]}), null)
        compare(BrowserSession.normalize({version: 1, locations: []}), null)
        BrowserSession.setResumeSession(true)
        BrowserSession.settings.sessionJson = "broken-json"
        compare(BrowserSession.load(), null)
    }
}
