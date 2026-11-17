import QtQuick
import QtQuick.Controls
import QtTest
import Lurviko.App
import Lurviko.Backend

Item {
    width: 1100; height: 800
    ApplicationWindow {
        id: window
        width: 1100; height: 800; visible: true
        LanguageManager { id: language; language: "tr" }
        FavoritesModel { id: favorites }
        AdminEditManager { id: admin }
        QtObject { id: auth; property string googleAccessToken: ""; property string oneDriveAccessToken: "" }
        QtObject { id: prefs; property bool showGoogle: false; property bool showOneDrive: false; property bool showDropbox: false; property bool showNextcloud: false; property bool showOwnCloud: false; property bool showMega: false; property bool showPCloud: false; property bool showWebDav: false }
        QtObject { id: cloud; property string accessToken: ""; signal refreshRequested() }
        BrowserPane {
            id: pane
            anchors.fill: parent
            initialLocation: String(fileArtworkFixtureUrl)
            lang: language; favoritesModel: favorites; adminEditor: admin
            cloudAuth: auth; cloudIntegrationPreferences: prefs; googleDrive: cloud; oneDrive: cloud
        }
        SftpConnections {
            id: standalone
            visible: false
            lang: language
        }
        SignalSpy { id: opened; target: standalone; signalName: "openLocation" }
        TestCase {
            name: "SftpStartPage"
            when: windowShown

            function test_root_shows_start_page_instead_of_error() {
                const page = findChild(pane, "sftpConnections")
                verify(!page.visible)
                pane.currentLocation = "sftp://"
                tryCompare(page, "visible", true)
                verify(page.active)
                const view = findChild(pane, "fileGridView")
                verify(!view.visible || !view.parent.visible)
                pane.currentLocation = String(fileArtworkFixtureUrl)
                tryCompare(page, "visible", false)
            }

            function test_form_builds_safe_urls_data() {
                return [
                    {tag: "one-line", host: "me@nas.local:2222/srv/data", user: "", port: "", path: "", url: "sftp://me@nas.local:2222/srv/data"},
                    {tag: "fields", host: "nas.local", user: "ayse", port: "2200", path: "/home/ayse/Belgeler", url: "sftp://ayse@nas.local:2200/home/ayse/Belgeler"},
                    {tag: "fields-override", host: "me@nas", user: "you", port: "", path: "", url: "sftp://you@nas"},
                    {tag: "home", host: "10.0.0.5", user: "", port: "", path: "", url: "sftp://10.0.0.5"},
                    {tag: "injection", host: "-oProxyCommand=x", user: "", port: "", path: "", url: ""},
                    {tag: "relative-folder", host: "nas", user: "", port: "", path: "docs", url: ""}
                ]
            }
            function test_form_builds_safe_urls(data) {
                opened.clear()
                findChild(standalone, "sftpHostField").text = data.host
                findChild(standalone, "sftpUserField").text = data.user
                findChild(standalone, "sftpPortField").text = data.port
                findChild(standalone, "sftpPathField").text = data.path
                standalone.connectFromForm()
                if (data.url.length) {
                    compare(opened.count, 1)
                    compare(opened.signalArguments[0][0], data.url)
                    compare(standalone.errorText, "")
                } else {
                    compare(opened.count, 0)
                    verify(standalone.errorText.length > 0)
                }
            }

            function test_saved_servers_round_trip() {
                findChild(standalone, "sftpHostField").text = "backup.example"
                findChild(standalone, "sftpUserField").text = "ops"
                findChild(standalone, "sftpPortField").text = ""
                findChild(standalone, "sftpPathField").text = "/srv"
                findChild(standalone, "sftpSaveName").text = "Yedek"
                standalone.saveCurrent()
                compare(standalone.saved.length, 1)
                compare(standalone.saved[0].name, "Yedek")
                compare(standalone.saved[0].url, "sftp://ops@backup.example/srv")
                standalone.saveCurrent()                    // same server is not duplicated
                compare(standalone.saved.length, 1)
                standalone.removeSaved("sftp://ops@backup.example/srv")
                compare(standalone.saved.length, 0)
            }
        }
    }
}
