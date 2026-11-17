import QtQuick
import QtTest
import Lurviko.App

Item {
    width: 200
    height: 200
    QtObject {
        id: fakeRequest
        property string mode: "open"
        property string title: ""
        property var filters: []
        property var choices: []
        property string currentFolder: "file:///usr"
        property bool directory: false
        property bool multiple: false
        property string currentName: ""
        property bool finished: false
        property string acceptLabel: ""
        property var files: []
        property int currentFilter: -1
        function cancel() {}
        function accept() {}
        function childUrl(folder, name) { return folder + "/" + name }
        function isDirectory(url) { return false }
        function exists(url) { return false }
    }
    FileChooserWindow { id: chooser; request: fakeRequest }

    TestCase {
        name: "FileChooserNavigation"
        when: windowShown

        function pane() { return findChild(chooser.contentItem, "fileChooserPane") }

        function initTestCase() {
            chooser.width = 1000
            chooser.height = 640
            chooser.visible = true
            tryCompare(chooser, "historyLocation", "file:///usr")
            tryVerify(function() { return pane().userLocationSignalsReady })
        }

        function test_back_forward_buttons_mouse_and_keys() {
            const browser = pane()
            browser.navigateTo("file:///usr/share")
            tryCompare(chooser, "historyLocation", "file:///usr/share")
            browser.navigateTo("file:///usr/lib")
            tryCompare(chooser, "historyLocation", "file:///usr/lib")
            compare(chooser.backHistory, ["file:///usr", "file:///usr/share"])
            verify(browser.effectiveBackNavigationEnabled)
            verify(!browser.effectiveForwardNavigationEnabled)

            // Toolbar / mouse side button path.
            browser.handleBackNavigation()
            tryCompare(browser, "currentLocation", "file:///usr/share")
            browser.handleBackNavigation()
            tryCompare(browser, "currentLocation", "file:///usr")
            verify(!browser.effectiveBackNavigationEnabled)
            compare(chooser.forwardHistory.length, 2)

            // Keyboard: Alt+Right / Alt+Left.
            keyClick(Qt.Key_Right, Qt.AltModifier)
            tryCompare(browser, "currentLocation", "file:///usr/share")
            keyClick(Qt.Key_Right, Qt.AltModifier)
            tryCompare(browser, "currentLocation", "file:///usr/lib")
            keyClick(Qt.Key_Left, Qt.AltModifier)
            tryCompare(browser, "currentLocation", "file:///usr/share")

            // A new folder starts a new branch: Forward is cleared.
            browser.navigateTo("file:///usr")
            tryCompare(chooser, "historyLocation", "file:///usr")
            compare(chooser.forwardHistory.length, 0)
        }

        function test_narrow_window_keeps_navigation_and_address() {
            chooser.width = 560
            const bar = findChild(pane(), "paneNavigationBar")
            const address = findChild(pane(), "locationBar")
            tryVerify(function() { return bar.width < 400 })
            wait(50)
            verify(address.width >= 120)
            // Everything in the row fits inside the pane.
            const right = address.mapToItem(pane(), address.width, 0).x
            verify(right <= pane().width)
            verify(!findChild(pane(), "iconSizeControl").visible)
            chooser.width = 1000
            tryVerify(function() { return findChild(pane(), "iconSizeControl").visible })
        }
    }
}
