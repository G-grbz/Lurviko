import QtQuick
import QtQuick.Controls
import QtTest
import Lurviko.App
import Lurviko.Backend

Item {
    width: 1020; height: 700
    ApplicationWindow {
        id: window
        width: 1020; height: 700
        visible: true
        LanguageManager { id: language; language: "en" }
        FavoritesModel { id: favorites }
        QuickAccessModel { id: shortcuts }
        AdminEditManager { id: admin }
        QtObject { id: auth; property string googleAccessToken: ""; property string oneDriveAccessToken: "" }
        QtObject {
            id: prefs
            property bool showGoogle: false
            property bool showOneDrive: false
            property bool showDropbox: false
            property bool showNextcloud: false
            property bool showOwnCloud: false
            property bool showMega: false
            property bool showPCloud: false
            property bool showWebDav: false
        }
        QtObject { id: cloud; property string accessToken: ""; signal refreshRequested() }
        BrowsePage {
            id: page
            anchors.fill: parent
            initialLocation: String(fileArtworkFixtureUrl)
            lang: language
            contentIndexModel: null
            musicPlayer: null
            favoritesModel: favorites
            quickAccessModel: shortcuts
            adminEditor: admin
            cloudAuth: auth
            cloudIntegrationPreferences: prefs
            googleDrive: cloud
            oneDrive: cloud
        }
        DeferredThumbnail {
            id: preview
            width: 64; height: 64
            visible: false
            fileSuffix: "png"
        }
        TestCase {
            name: "FolderArtwork"
            when: windowShown
            property bool savedSystem
            property bool savedColored
            function initTestCase() {
                savedSystem = AppTheme.useSystemIcons
                savedColored = AppTheme.useColoredFolderIcons
                tryVerify(function() { return page.activePane !== null && page.activePane.itemCount > 40 })
            }
            function cleanupTestCase() {
                AppTheme.setSystemIcons(savedSystem)
                AppTheme.setColoredFolderIcons(savedColored)
            }
            function init() {
                AppTheme.setSystemIcons(false)
                AppTheme.setColoredFolderIcons(true)
                page.activePane.gridMode = true
                page.activePane.showHiddenFiles = true
            }
            function test_multilingual_keywords_data() {
                return [
                    {tag: "Turkish-color", name: "Mor", icon: "purple-folder.svg"},
                    {tag: "English-color", name: "GREEN", icon: "green-folder.svg"},
                    {tag: "Turkish-pictures", name: "Mavi Fotoğraflar", icon: "blue-picture.svg"},
                    {tag: "Turkish-possessive", name: "Pembe Resimlerimiz 2026", icon: "pink-picture.svg"},
                    {tag: "English-camel", name: "PurplePicturesBackup", icon: "purple-picture.svg"},
                    {tag: "screenshots", name: "Turkuaz Ekran Görüntülerim", icon: "turquoise-picture.svg"},
                    {tag: "music", name: "Sarı Şarkılarım", icon: "yellow-music.svg"},
                    {tag: "video", name: "Red TV Shows", icon: "red-video.svg"},
                    {tag: "documents", name: "Yeşil Belgelerim", icon: "green-documents.svg"},
                    {tag: "downloads", name: "Turuncu İndirilenler", icon: "orange-download.svg"},
                    {tag: "specific-media", name: "Blue Download Photos", icon: "blue-picture.svg"},
                    {tag: "joined-Turkish", name: "morresimarsivi", icon: "purple-picture.svg"},
                    {tag: "joined-English", name: "summerphotosbackup", kind: "picture"},
                    {tag: "word-boundary", name: "topic", kind: "folder"},
                    {tag: "prototype-safe", name: "constructor", kind: "folder"}
                ]
            }
            function test_multilingual_keywords(data) {
                const source = String(AppTheme.bundledFolderIcon(data.name, "file:///test/" + data.name))
                if (data.icon) verify(source.endsWith("/" + data.icon), source)
                else verify(new RegExp("/(blue|green|orange|pink|purple|red|turquoise|yellow)-" + data.kind + "\\.svg$").test(source), source)
                const icon = findChild(page.activePane, "gridFileIcon")
                verify(icon !== null)
                verify(source.indexOf("image://bundledicon/") === -1, "Ready-made colors must not be tinted")
            }
            function test_colors_are_stable_and_can_be_disabled() {
                const first = AppTheme.bundledFolderIcon("Projects")
                for (let i = 0; i < 2000; ++i) AppTheme.bundledFolderIcon("Different folder " + i)
                compare(AppTheme.bundledFolderIcon("Projects"), first)
                compare(AppTheme.bundledFolderIcon("Projects", "different parent"), first)
                AppTheme.setColoredFolderIcons(false)
                compare(AppTheme.bundledFolderIcon("Mor"), AppTheme.icon("folder.svg"))
                AppTheme.setColoredFolderIcons(true)
                compare(AppTheme.bundledFolderIcon("Mor"), AppTheme.icon("purple-folder.svg"))
                const external = "file:///tmp/my-folder-art.svg"
                compare(page.activePane.iconForItem(true, "", external, "Mor"), external)
            }
            function test_archive_file_icons_data() {
                return ["pkg.tar.zst", "tar.lz4", "tar.bz2", "tar.xz", "tgz", "tbz2", "tzst", "gzip", "cpio", "cab", "rpm"]
                    .map(function(extension) { return {tag: extension, extension: extension} })
            }
            function test_archive_file_icons(data) {
                const suffix = data.extension.split(".").pop()
                compare(page.activePane.iconForItem(false, suffix, "text-x-generic", "package." + data.extension),
                        AppTheme.icon("archive.svg"))
                compare(page.activePane.iconForItem(false, "dat", "text-x-generic", "unknown.dat", "", "application/zip"),
                        AppTheme.icon("archive.svg"))
            }
            function test_switch_is_shown_only_for_bundled_icons() {
                const row = findChild(page, "coloredFolderIconsSetting")
                const toggle = findChild(page, "coloredFolderIconsSwitch")
                const menu = findChild(page, "toolbarViewMenu")
                verify(row !== null && toggle !== null)
                menu.open()
                tryVerify(function() { return row.visible })
                compare(row.implicitHeight, 42)
                AppTheme.setSystemIcons(true)
                compare(row.implicitHeight, 0)
                verify(!row.visible)
                AppTheme.setSystemIcons(false)
                compare(row.implicitHeight, 42)
                toggle.checked = false
                toggle.toggled()
                compare(AppTheme.useColoredFolderIcons, false)
                menu.close()
            }
            function test_hidden_files_and_folder_opacity_data() {
                return [{tag: "grid", grid: true}, {tag: "list", grid: false}]
            }
            function test_hidden_files_and_folder_opacity(data) {
                const pane = page.activePane
                pane.gridMode = data.grid
                const view = findChild(pane, data.grid ? "fileGridView" : "fileListView")
                tryVerify(function() { return pane.itemCount >= 54 })
                const names = [".hidden-folder", ".hidden-file.txt", "visible-folder", "visible-file.txt"]
                for (const name of names) {
                    let index = -1
                    for (let i = 0; i < view.model.count; ++i)
                        if (view.model.itemAt(i).name === name) { index = i; break }
                    verify(index >= 0, "Missing fixture " + name)
                    view.positionViewAtIndex(index, ListView.Beginning)
                    tryVerify(function() { return view.itemAtIndex(index) !== null })
                    const card = findChild(view.itemAtIndex(index), data.grid ? "gridFileCard" : "listFileCard")
                    verify(card !== null)
                    const expected = name.startsWith(".") ? AppTheme.hiddenItemOpacity : 1
                    tryVerify(function() { return Math.abs(card.opacity - expected) < 0.005 })
                    const icon = findChild(card, data.grid ? "gridFileIcon" : "listFileIcon")
                    verify(icon !== null)
                    compare(icon.retainWhileLoading, false)
                    if (name.endsWith("folder")) {
                        compare(icon.asynchronous, false)
                        compare(icon.status, Image.Ready)
                    }
                }
            }
            function test_preview_does_not_show_the_previous_file_on_delegate_reuse() {
                preview.fileIdentity = "first-file"
                preview.candidateSource = AppTheme.icon("music-cover.png")
                tryCompare(preview, "displayReady", true)
                preview.fileIdentity = "second-file"
                preview.candidateSource = AppTheme.icon("logo.png")
                compare(preview.displayReady, false)
                tryCompare(preview, "displayReady", true)
                compare(preview.trackedIdentity, "second-file")
            }
        }
    }
}
