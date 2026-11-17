import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import Lurviko.Backend
import Lurviko.App

// Start page of sftp:// without a server: connect form, saved servers,
// servers from the SSH configuration, recent connections, SSH servers found
// on the local network and hosts already trusted in known_hosts.
Item {
    id: page
    objectName: "sftpConnections"
    property var lang
    property bool active: false
    // QuickAccessModel.recentLocations: [{ title, location }]
    property var recentLocations: []
    property var textEditMenu: null
    signal openLocation(string url)
    readonly property bool tr: lang && lang.language === "tr"
    function t(trText, enText) { return tr ? trText : enText }

    property string errorText: ""
    readonly property var saved: {
        try { return JSON.parse(store.saved) || [] } catch (error) { return [] }
    }
    readonly property var recent: {
        const seen = ({})
        const result = []
        for (const entry of recentLocations || []) {
            const location = String(entry.location || "")
            if (!/^sftp:\/\/[^\/]/i.test(location) || seen[location]) continue
            seen[location] = true
            result.push({ title: location.replace(/^sftp:\/\//i, ""), url: location })
            if (result.length >= 8) break
        }
        return result
    }

    onActiveChanged: if (active) { directory.refresh(); hostField.forceActiveFocus() }

    Settings {
        id: store
        category: "SftpConnections"
        property string saved: "[]"
    }
    SftpDirectory { id: directory; objectName: "sftpDirectory" }

    function connectFromForm() {
        errorText = ""
        // The server field also takes a whole "user@host:port/path".
        const parsed = directory.parseTarget(hostField.text)
        if (!parsed.valid) {
            errorText = parsed.error || t("Sunucu adresini kontrol et.", "Check the server address.")
            return
        }
        const user = userField.text.trim().length ? userField.text.trim() : parsed.user
        const port = portField.text.trim().length ? Number(portField.text) : parsed.port
        const path = pathField.text.trim().length ? pathField.text.trim() : parsed.path
        if (path.length && path[0] !== "/") {
            errorText = t("Klasör / ile başlayan tam bir yol olmalı.", "The folder must be a full path starting with /.")
            return
        }
        const url = directory.buildUrl(parsed.host, user, port, path)
        if (!url.length) {
            errorText = t("Kullanıcı adını, portu ve klasörü kontrol et.", "Check the user name, port and folder.")
            return
        }
        page.openLocation(url)
    }
    function formUrl() {
        const parsed = directory.parseTarget(hostField.text)
        if (!parsed.valid) return ""
        const user = userField.text.trim().length ? userField.text.trim() : parsed.user
        const port = portField.text.trim().length ? Number(portField.text) : parsed.port
        const path = pathField.text.trim().length ? pathField.text.trim() : parsed.path
        return path.length && path[0] !== "/" ? "" : directory.buildUrl(parsed.host, user, port, path)
    }
    function saveCurrent() {
        const url = formUrl()
        if (!url.length) {
            errorText = t("Kaydetmek için geçerli bir sunucu gir.", "Enter a valid server to save it.")
            return
        }
        const list = saved.filter(function(entry) { return entry.url !== url })
        list.unshift({ name: nameField.text.trim().length ? nameField.text.trim() : url.replace(/^sftp:\/\//, ""), url: url })
        store.saved = JSON.stringify(list.slice(0, 50))
        nameField.text = ""
        errorText = ""
    }
    function removeSaved(url) {
        store.saved = JSON.stringify(saved.filter(function(entry) { return entry.url !== url }))
    }
    function fillFrom(url) {
        const parsed = directory.parseTarget(url)
        if (!parsed.valid) return
        hostField.text = parsed.host
        userField.text = parsed.user
        portField.text = parsed.port === 22 ? "" : String(parsed.port)
        pathField.text = parsed.path
    }

    component ServerTile: GButton {
        id: tile
        property string title: ""
        property string subtitle: ""
        property string url: ""
        property string iconName: "network.svg"
        property bool removable: false
        signal removeRequested()
        implicitWidth: 236
        implicitHeight: 58
        onClicked: page.openLocation(url)
        GToolTip { text: tile.url }
        background: Rectangle {
            radius: 12
            color: tile.hovered ? AppTheme.surfaceHover : AppTheme.surface
            border.color: tile.hovered ? AppTheme.accentBorder : AppTheme.border
        }
        contentItem: RowLayout {
            spacing: 10
            CrispIcon { Layout.preferredWidth: 26; Layout.preferredHeight: 26; source: AppTheme.icon(tile.iconName) }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                GText { Layout.fillWidth: true; text: tile.title; textFormat: Text.PlainText; elide: Text.ElideRight
                        basePixelSize: 12; baseFontWeight: Font.DemiBold; baseColor: AppTheme.text }
                GText { Layout.fillWidth: true; text: tile.subtitle; textFormat: Text.PlainText; elide: Text.ElideMiddle
                        basePixelSize: 10; baseColor: AppTheme.textMuted }
            }
            GToolButton {
                visible: tile.removable
                implicitWidth: 26; implicitHeight: 26
                icon.source: AppTheme.icon("close-ui.svg")
                icon.width: 12; icon.height: 12
                onClicked: tile.removeRequested()
                Accessible.name: page.t("Kayıtlı sunucuyu sil", "Remove saved server")
            }
        }
    }

    component Section: ColumnLayout {
        id: section
        property string title: ""
        property string note: ""
        default property alias tiles: flow.data
        Layout.fillWidth: true
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            GText { text: section.title; basePixelSize: 13; baseFontWeight: Font.DemiBold; baseColor: AppTheme.text }
            GText { Layout.fillWidth: true; text: section.note; basePixelSize: 10; baseColor: AppTheme.textMuted; elide: Text.ElideRight }
        }
        Flow { id: flow; Layout.fillWidth: true; spacing: 8 }
    }

    ScrollView {
        id: scroller
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth

        ColumnLayout {
            width: scroller.availableWidth
            spacing: 18

            // --- Connect form ---------------------------------------------
            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: 6
                Layout.preferredHeight: form.implicitHeight + 32
                radius: 16
                color: AppTheme.surfaceRaised
                border.color: AppTheme.border

                ColumnLayout {
                    id: form
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 16
                    spacing: 12

                    RowLayout {
                        spacing: 12
                        Rectangle {
                            Layout.preferredWidth: 42; Layout.preferredHeight: 42; radius: 12
                            color: AppTheme.accentSoft; border.color: AppTheme.accentBorder
                            CrispIcon { anchors.centerIn: parent; width: 24; height: 24; source: AppTheme.icon("network.svg") }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            GText { text: page.t("SFTP sunucusuna bağlan", "Connect to an SFTP server")
                                    basePixelSize: 15; baseFontWeight: Font.DemiBold; baseColor: AppTheme.text }
                            GText {
                                Layout.fillWidth: true
                                text: page.t("SSH üzerinden şifreli dosya erişimi. Parola veya anahtar parolası bağlanırken sorulur; SSH anahtarların ve ~/.ssh/config ayarların kullanılır.",
                                             "Encrypted file access over SSH. The password or key passphrase is asked when connecting; your SSH keys and ~/.ssh/config are used.")
                                wrapMode: Text.Wrap; basePixelSize: 11; baseColor: AppTheme.textMuted
                            }
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: width > 620 ? 4 : 2
                        columnSpacing: 10
                        rowSpacing: 8
                        GText { text: page.t("Sunucu", "Server"); basePixelSize: 11; baseColor: AppTheme.textMuted }
                        GTextField {
                            id: hostField
                            objectName: "sftpHostField"
                            Layout.fillWidth: true
                            Layout.columnSpan: parent.columns === 4 ? 3 : 1
                            placeholderText: page.t("sunucu.ornek.com · 192.168.1.10 · kullanici@sunucu:2222/yol",
                                                    "server.example.com · 192.168.1.10 · user@server:2222/path")
                            selectByMouse: true
                            onAccepted: page.connectFromForm()
                            onTextChanged: page.errorText = ""
                            EditorContextMenu { menu: page.textEditMenu }
                        }
                        GText { text: page.t("Kullanıcı", "User"); basePixelSize: 11; baseColor: AppTheme.textMuted }
                        GTextField {
                            id: userField
                            objectName: "sftpUserField"
                            Layout.fillWidth: true
                            placeholderText: directory.localUser + page.t(" (varsayılan)", " (default)")
                            selectByMouse: true
                            onAccepted: page.connectFromForm()
                            EditorContextMenu { menu: page.textEditMenu }
                        }
                        GText { text: page.t("Port", "Port"); basePixelSize: 11; baseColor: AppTheme.textMuted }
                        GTextField {
                            id: portField
                            objectName: "sftpPortField"
                            Layout.preferredWidth: 110
                            placeholderText: "22"
                            validator: IntValidator { bottom: 1; top: 65535 }
                            selectByMouse: true
                            onAccepted: page.connectFromForm()
                        }
                        GText { text: page.t("Klasör", "Folder"); basePixelSize: 11; baseColor: AppTheme.textMuted }
                        GTextField {
                            id: pathField
                            objectName: "sftpPathField"
                            Layout.fillWidth: true
                            Layout.columnSpan: parent.columns === 4 ? 3 : 1
                            placeholderText: page.t("İsteğe bağlı, ör. /srv/paylasim · boşsa ev klasörü açılır",
                                                    "Optional, e.g. /srv/share · empty opens the home folder")
                            selectByMouse: true
                            onAccepted: page.connectFromForm()
                            EditorContextMenu { menu: page.textEditMenu }
                        }
                    }

                    GText {
                        Layout.fillWidth: true
                        visible: page.errorText.length > 0
                        text: page.errorText
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        baseColor: AppTheme.danger
                        basePixelSize: 11
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        GTextField {
                            id: nameField
                            objectName: "sftpSaveName"
                            Layout.preferredWidth: 220
                            placeholderText: page.t("Kayıt adı (isteğe bağlı)", "Name to save (optional)")
                            selectByMouse: true
                        }
                        GModalButton {
                            objectName: "sftpSave"
                            text: page.t("Kaydet", "Save")
                            enabled: hostField.text.trim().length > 0
                            onClicked: page.saveCurrent()
                        }
                        Item { Layout.fillWidth: true }
                        GText {
                            text: page.formUrl()
                            textFormat: Text.PlainText
                            baseColor: AppTheme.textFaint
                            basePixelSize: 10
                            elide: Text.ElideMiddle
                            Layout.maximumWidth: 280
                        }
                        GModalButton {
                            objectName: "sftpConnect"
                            primary: true
                            text: page.t("Bağlan", "Connect")
                            enabled: hostField.text.trim().length > 0
                            onClicked: page.connectFromForm()
                        }
                    }
                }
            }

            Section {
                visible: page.saved.length > 0
                title: page.t("Kayıtlı sunucular", "Saved servers")
                Repeater {
                    model: page.saved
                    delegate: ServerTile {
                        required property var modelData
                        objectName: "sftpSavedTile"
                        title: modelData.name
                        subtitle: modelData.url.replace(/^sftp:\/\//, "")
                        url: modelData.url
                        iconName: "favorite-symbolic.svg"
                        removable: true
                        onRemoveRequested: page.removeSaved(modelData.url)
                    }
                }
            }

            Section {
                visible: directory.configHosts.length > 0
                title: page.t("SSH yapılandırması", "SSH configuration")
                note: "~/.ssh/config"
                Repeater {
                    model: directory.configHosts
                    delegate: ServerTile {
                        required property var modelData
                        objectName: "sftpConfigTile"
                        title: modelData.alias
                        subtitle: (modelData.user ? modelData.user + "@" : "") + modelData.hostName
                                  + (modelData.port !== 22 ? ":" + modelData.port : "")
                        url: modelData.url
                        iconName: "settings.svg"
                    }
                }
            }

            Section {
                visible: page.recent.length > 0
                title: page.t("Son bağlantılar", "Recent connections")
                Repeater {
                    model: page.recent
                    delegate: ServerTile {
                        required property var modelData
                        title: modelData.title.split("/")[0]
                        subtitle: modelData.title
                        url: modelData.url
                        iconName: "history.svg"
                    }
                }
            }

            Section {
                title: page.t("Yerel ağdaki SSH sunucuları", "SSH servers on the local network")
                note: directory.discovering ? page.t("Aranıyor…", "Searching…")
                    : directory.discoveryError.length ? directory.discoveryError
                    : directory.discovered.length === 0 ? page.t("Duyuru yapan sunucu bulunamadı.", "No server announced itself.")
                    : ""
                Repeater {
                    model: directory.discovered
                    delegate: ServerTile {
                        required property var modelData
                        objectName: "sftpDiscoveredTile"
                        title: modelData.name
                        subtitle: modelData.host + (modelData.port !== 22 ? ":" + modelData.port : "")
                                  + (modelData.address ? " · " + modelData.address : "")
                        url: modelData.url
                    }
                }
                GModalButton {
                    text: page.t("Yeniden tara", "Search again")
                    enabled: !directory.discovering
                    onClicked: directory.discover()
                }
            }

            Section {
                visible: directory.knownHosts.length > 0
                title: page.t("Daha önce bağlanılan sunucular", "Previously trusted servers")
                note: "~/.ssh/known_hosts"
                Repeater {
                    model: directory.knownHosts
                    delegate: ServerTile {
                        required property var modelData
                        title: modelData.host
                        subtitle: modelData.port !== 22 ? page.t("port ", "port ") + modelData.port : "SSH"
                        url: modelData.url
                        iconName: "lock.svg"
                    }
                }
            }

            Item { Layout.preferredHeight: 8 }
        }
    }
}
