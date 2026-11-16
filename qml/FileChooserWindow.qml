import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtCore
import Lurviko.App
import Lurviko.Backend
import "components"

// Open/Save dialog shown for xdg-desktop-portal FileChooser requests
// (src/filechooserportal.cpp). It is Lurviko's own browser pane in picker
// mode, with the same sidebar, so files look exactly as in the main window.
ApplicationWindow {
    id: chooser
    objectName: "fileChooserWindow"
    required property var request

    readonly property string mode: request ? String(request.mode) : "open"
    readonly property bool saving: mode === "save" || mode === "saveFiles"
    readonly property bool turkish: lang.language === "tr"
    readonly property string homeUrl: String(StandardPaths.writableLocation(StandardPaths.HomeLocation))
    readonly property string folderUrl: String(pane.currentLocation || "")
    readonly property bool localFolder: folderUrl.startsWith("file:")
    property var choiceValues: ({})
    property var pendingUris: []
    // The active filter; returned to the caller as current_filter.
    property int filterIndex: -1

    function t(tr, en) { return turkish ? tr : en }

    // Grid / list toggle of the command bar, drawn as four tiles or three rows.
    component ViewModeButton: GButton {
        id: viewModeButton
        required property string mode
        required property string label
        readonly property bool active: pane.viewMode === mode || (mode === "grid" && pane.viewMode === "columns")
        HoverHandler { cursorShape: Qt.PointingHandCursor }
        text: ""
        implicitWidth: 34; implicitHeight: commandRow.controlHeight
        Accessible.name: label
        GToolTip { text: viewModeButton.label }
        onClicked: mode === "grid" ? pane.setGridView() : pane.setListView()
        contentItem: Item {
            implicitWidth: 18; implicitHeight: 18
            Grid {
                anchors.centerIn: parent
                visible: viewModeButton.mode === "grid"
                columns: 2; spacing: 2
                Repeater {
                    model: 4
                    Rectangle { width: 6; height: 6; radius: 1.5; color: AppTheme.text; opacity: viewModeButton.active ? 1 : 0.7 }
                }
            }
            Column {
                anchors.centerIn: parent
                visible: viewModeButton.mode === "list"
                spacing: 2.5
                Repeater {
                    model: 3
                    Rectangle { width: 14; height: 2.5; radius: 1.25; color: AppTheme.text; opacity: viewModeButton.active ? 1 : 0.7 }
                }
            }
        }
        background: Rectangle {
            radius: 11
            color: viewModeButton.hovered || viewModeButton.active ? AppTheme.accentSoft : "transparent"
            border.color: viewModeButton.active ? AppTheme.accentBorder : "transparent"
        }
    }

    width: chooserSettings.width
    height: chooserSettings.height
    minimumWidth: Math.min(720, Screen.width * 0.6)
    minimumHeight: Math.min(480, Screen.height * 0.6)
    visible: false
    flags: Qt.Dialog
    title: request && String(request.title).length ? String(request.title)
         : mode === "save" ? t("Dosyayı Kaydet", "Save File")
         : mode === "saveFiles" ? t("Dosyaları Kaydet", "Save Files")
         : request && request.directory ? t("Klasör Seç", "Select Folder")
         : t("Dosya Aç", "Open File")

    color: AppTheme.useQtTheme ? "transparent" : AppTheme.background
    background: Rectangle { visible: AppTheme.useQtTheme; color: AppTheme.background }
    palette.window: AppTheme.background
    palette.windowText: AppTheme.text
    palette.base: AppTheme.surface
    palette.alternateBase: AppTheme.surfaceRaised
    palette.text: AppTheme.text
    palette.button: AppTheme.surfaceRaised
    palette.buttonText: AppTheme.text
    palette.highlight: AppTheme.accent
    palette.highlightedText: AppTheme.highlightedText
    palette.placeholderText: AppTheme.textFaint

    DesktopWindowEffects { window: chooser; enabled: AppTheme.translucentWindow }

    Settings {
        id: chooserSettings
        category: "FileChooser"
        property int width: 1120
        property int height: 720
    }

    LanguageManager { id: lang }
    StorageModel { id: storageModel }
    FavoritesModel { id: favoritesModel }
    QuickAccessModel { id: quickAccessModel }
    AdminEditManager { id: adminEditor }
    // Cloud folders need the main window's sign-in; a picker lists local
    // files and mounted locations only.
    QtObject { id: noCloudAuth; property string googleAccessToken: ""; property string oneDriveAccessToken: "" }
    QtObject {
        id: noCloudPreferences
        property bool showGoogle: false; property bool showOneDrive: false; property bool showDropbox: false
        property bool showNextcloud: false; property bool showOwnCloud: false; property bool showMega: false
        property bool showPCloud: false; property bool showWebDav: false
    }
    QtObject { id: noCloudDrive; property string accessToken: ""; signal refreshRequested() }

    onClosing: function(close) {
        chooserSettings.width = chooser.width
        chooserSettings.height = chooser.height
        if (request && !request.finished)
            request.cancel()
    }

    Component.onCompleted: {
        const values = {}
        const choices = request ? request.choices : []
        for (let i = 0; i < choices.length; ++i)
            values[choices[i].id] = String(choices[i].value || "")
        choiceValues = values
        applyFilter(request ? request.currentFilter : -1)
        if (saving && request) {
            nameField.text = mode === "save" ? String(request.currentName || "") : ""
            const dot = nameField.text.lastIndexOf(".")
            nameField.select(0, dot > 0 ? dot : nameField.text.length)
            nameField.forceActiveFocus()
        }
    }

    function applyFilter(index) {
        const filters = request ? request.filters : []
        const filter = index >= 0 && index < filters.length ? filters[index] : null
        filterIndex = filter ? index : -1
        pane.pickerNameFilters = filter ? filter.globs : []
        pane.pickerMimeFilters = filter ? filter.mimes : []
    }

    // First "*.ext" glob of a filter: what a saved file of that type ends in.
    function extensionOf(filter) {
        const globs = filter ? filter.globs : []
        for (let i = 0; i < globs.length; ++i) {
            const match = /^\*\.([A-Za-z0-9_+~-]+)$/.exec(String(globs[i]))
            if (match)
                return match[1]
        }
        return ""
    }

    // The user picked another type: a name being saved follows it, as in
    // other file dialogs ("photo.png" -> "photo.jpg").
    function selectFilter(index) {
        applyFilter(index)
        if (mode !== "save")
            return
        const filter = request.filters[index]
        const extension = extensionOf(filter)
        const name = nameField.text.trim()
        if (!extension.length || !name.length)
            return
        const lower = name.toLowerCase()
        if (filter.globs.some(function(glob) {
                const value = String(glob).toLowerCase()
                return value.startsWith("*.") && lower.endsWith(value.substring(1))
            }))
            return
        const dot = name.lastIndexOf(".")
        nameField.text = (dot > 0 ? name.substring(0, dot) : name) + "." + extension
    }

    function finish(uris) {
        if (!request || request.finished)
            return
        chooserSettings.width = chooser.width
        chooserSettings.height = chooser.height
        request.accept(uris, choiceValues, filterIndex, folderUrl)
    }

    function confirmOverwrite(uris) {
        pendingUris = uris
        overwritePopup.open()
    }

    function selectedEntries() {
        return pane.selectedItemsInViewOrder().filter(function(item) { return !!item && !!item.itemUrl })
    }

    function acceptCurrent() {
        if (!request || !localFolder)
            return
        const items = selectedEntries()
        if (mode === "open") {
            if (request.directory) {
                const folders = items.filter(function(item) { return item.isDir })
                                     .map(function(item) { return String(item.itemUrl) })
                finish(folders.length ? (request.multiple ? folders : [folders[0]]) : [folderUrl])
                return
            }
            const files = items.filter(function(item) { return !item.isDir })
                               .map(function(item) { return String(item.itemUrl) })
            if (files.length) {
                finish(request.multiple ? files : [files[0]])
            } else if (items.length === 1) {
                pane.navigateTo(String(items[0].itemUrl))
            } else if (nameField.text.trim().length) {
                const typed = request.childUrl(folderUrl, nameField.text)
                if (typed.length && request.isDirectory(typed)) {
                    pane.navigateTo(typed)
                    nameField.text = ""
                } else if (typed.length && request.exists(typed)) {
                    finish([typed])
                }
            }
            return
        }
        if (mode === "save") {
            const target = request.childUrl(folderUrl, nameField.text)
            if (!target.length)
                return
            if (request.isDirectory(target)) {
                pane.navigateTo(target)
                nameField.text = String(request.currentName || "")
                return
            }
            if (request.exists(target))
                confirmOverwrite([target])
            else
                finish([target])
            return
        }
        // saveFiles: the chosen folder receives every file under its name.
        const folders = items.filter(function(item) { return item.isDir })
        const destination = folders.length === 1 ? String(folders[0].itemUrl) : folderUrl
        const targets = request.files.map(function(name) { return request.childUrl(destination, name) })
                                     .filter(function(url) { return url.length > 0 })
        if (!targets.length)
            return
        if (targets.some(function(url) { return request.exists(url) }))
            confirmOverwrite(targets)
        else
            finish(targets)
    }

    readonly property bool acceptEnabled: {
        if (!request || !localFolder)
            return false
        const selection = pane.selectedUrls.length + pane.selectionRevision * 0
        if (mode === "save")
            return nameField.text.trim().length > 0
        if (mode === "saveFiles" || request.directory)
            return true
        return selection > 0 || nameField.text.trim().length > 0
    }

    Shortcut {
        sequences: [StandardKey.Cancel]
        onActivated: chooser.close()
    }

    // Same shell as the browser window: an inset sidebar and a separate,
    // bordered workspace panel with the command bar, files and the footer.
    RowLayout {
        anchors.fill: parent
        anchors.margins: AppTheme.shellMargin
        spacing: 4

        SidebarShell {
            id: sidebar
            objectName: "fileChooserSidebar"
            Layout.preferredWidth: implicitWidth
            Layout.minimumWidth: implicitWidth
            Layout.maximumWidth: implicitWidth
            Layout.fillHeight: true
            lang: lang
            favoritesModel: favoritesModel
            storageModel: storageModel
            currentLocation: chooser.folderUrl
            onHomeRequested: pane.navigateTo(chooser.homeUrl)
            onLocationRequested: function(location) { pane.navigateTo(location) }
            onLocationNewTabRequested: function(location) { pane.navigateTo(location) }
        }

        Rectangle {
            id: workspace
            objectName: "fileChooserWorkspace"
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: AppTheme.panelRadius
            color: AppTheme.workspace
            border.color: AppTheme.border
            border.width: 1
            clip: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 6

            // Same command bar as the browser window: new folder, hidden files
            // (shared preference) and the folder menu for view and sorting.
            Rectangle {
                id: commandBar
                objectName: "fileChooserCommandBar"
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(38, commandRow.controlHeight + 8)
                radius: 9
                color: AppTheme.commandBar
                border.color: AppTheme.border
                border.width: 1

                RowLayout {
                    id: commandRow
                    readonly property real controlHeight: Math.max(28, AppTheme.typographySize("menu", 11) + 10)
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    anchors.topMargin: 4
                    anchors.bottomMargin: 4
                    spacing: 4

                    GButton {
                        objectName: "fileChooserNewFolder"
                        topPadding: 4; bottomPadding: 4
                        text: lang.t("new_folder")
                        display: AbstractButton.TextBesideIcon
                        implicitWidth: Math.max(90, implicitContentWidth + leftPadding + rightPadding)
                        implicitHeight: commandRow.controlHeight
                        icon.source: AppTheme.icon("plus.svg")
                        icon.width: 14; icon.height: 14
                        enabled: chooser.localFolder
                        onClicked: pane.showNewFolderDialog()
                        background: Rectangle { radius: 7; color: parent.hovered ? AppTheme.surfaceHover : "transparent" }
                    }
                    Item { Layout.fillWidth: true }
                    // Grid / list (columns list every level unfiltered, so a
                    // picker does not offer them).
                    ViewModeButton { objectName: "fileChooserView_grid"; mode: "grid"; label: lang.t("grid") }
                    ViewModeButton { objectName: "fileChooserView_list"; mode: "list"; label: lang.t("list") }
                    Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 22; color: AppTheme.border }
                    GButton {
                        id: hiddenFilesButton
                        objectName: "fileChooserHiddenFiles"
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        text: ""
                        checkable: true
                        checked: pane.showHiddenFiles
                        implicitWidth: 34; implicitHeight: commandRow.controlHeight
                        Accessible.name: lang.t("show_hidden")
                        GToolTip { text: lang.t("show_hidden") }
                        onToggled: pane.showHiddenFiles = checked
                        contentItem: Item {
                            implicitWidth: 18; implicitHeight: 18
                            CrispIcon {
                                anchors.centerIn: parent
                                source: AppTheme.icon("eye.svg")
                                width: 16; height: 16
                                opacity: pane.showHiddenFiles ? 1.0 : 0.68
                            }
                        }
                        background: Rectangle {
                            radius: 11
                            color: parent.hovered || pane.showHiddenFiles ? AppTheme.accentSoft : AppTheme.surface
                            border.color: pane.showHiddenFiles ? AppTheme.accentBorder : AppTheme.border
                        }
                    }
                    GButton {
                        id: folderMenuButton
                        objectName: "fileChooserFolderMenu"
                        property bool menuWasOpenOnPress: false
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        text: "⋯"
                        implicitWidth: 34; implicitHeight: commandRow.controlHeight
                        topPadding: 4; bottomPadding: 4
                        basePixelSize: 14
                        GToolTip { text: lang.t("directory_menu") }
                        onPressed: menuWasOpenOnPress = !!pane.directoryMenuShowing
                        onClicked: {
                            if (menuWasOpenOnPress)
                                pane.closeDirectoryMenu()
                            else
                                pane.openDirectoryMenu(folderMenuButton)
                            menuWasOpenOnPress = false
                        }
                        background: Rectangle { radius: 11; color: parent.hovered ? AppTheme.surfaceHover : "transparent"; border.color: "transparent" }
                    }
                }
            }

            BrowserPane {
                id: pane
                objectName: "fileChooserPane"
                Layout.fillWidth: true
                Layout.fillHeight: true
                pickerMode: true
                pickerDirectoriesOnly: chooser.request ? !!chooser.request.directory : false
                initialLocation: chooser.request && String(chooser.request.currentFolder).length
                                 ? String(chooser.request.currentFolder) : chooser.homeUrl
                lang: lang
                quickAccessModel: quickAccessModel
                favoritesModel: favoritesModel
                adminEditor: adminEditor
                cloudAuth: noCloudAuth
                cloudIntegrationPreferences: noCloudPreferences
                googleDrive: noCloudDrive
                oneDrive: noCloudDrive
                onPickerActivated: function(urls) {
                    if (chooser.mode === "open") {
                        chooser.finish(chooser.request.multiple ? urls : [urls[0]])
                    } else if (chooser.mode === "save") {
                        nameField.text = decodeURIComponent(String(urls[0]).split("/").pop())
                        chooser.acceptCurrent()
                    }
                }
                onSelectedUrlsChanged: {
                    if (chooser.mode !== "save")
                        return
                    const items = chooser.selectedEntries()
                    if (items.length === 1 && !items[0].isDir)
                        nameField.text = String(items[0].name || "")
                }
            }

            Rectangle {
                objectName: "fileChooserFooter"
                Layout.fillWidth: true
                Layout.preferredHeight: footer.implicitHeight + 16
                color: "transparent"
                Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: AppTheme.border }

                ColumnLayout {
                    id: footer
                    anchors.fill: parent
                    anchors.leftMargin: 4
                    anchors.rightMargin: 4
                    anchors.topMargin: 12
                    anchors.bottomMargin: 2
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        GText {
                            text: chooser.mode === "saveFiles" ? chooser.t("Dosyalar:", "Files:") : chooser.t("Ad:", "Name:")
                            baseColor: AppTheme.textMuted
                        }
                        GTextField {
                            id: nameField
                            objectName: "fileChooserName"
                            Layout.fillWidth: true
                            visible: chooser.mode !== "saveFiles"
                            placeholderText: chooser.saving ? "" : chooser.t("Dosya adı veya yol yazın", "Type a file name or path")
                            onAccepted: chooser.acceptCurrent()
                        }
                        GText {
                            objectName: "fileChooserFileList"
                            Layout.fillWidth: true
                            visible: chooser.mode === "saveFiles"
                            text: chooser.request ? chooser.request.files.join(", ") : ""
                            elide: Text.ElideRight
                        }
                        GComboBox {
                            id: filterBox
                            objectName: "fileChooserFilter"
                            visible: chooser.request && chooser.request.filters.length > 0
                            Layout.preferredWidth: 260
                            model: chooser.request ? chooser.request.filters.map(function(filter) { return filter.label }) : []
                            currentIndex: chooser.filterIndex
                            onActivated: function(index) { chooser.selectFilter(index) }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Repeater {
                            model: chooser.request ? chooser.request.choices : []
                            delegate: RowLayout {
                                required property var modelData
                                spacing: 6
                                GText {
                                    visible: modelData.options.length > 0
                                    text: String(modelData.label)
                                    baseColor: AppTheme.textMuted
                                }
                                GComboBox {
                                    visible: modelData.options.length > 0
                                    Layout.preferredWidth: 180
                                    model: modelData.options.map(function(option) { return option.label })
                                    currentIndex: Math.max(0, modelData.options.map(function(option) { return option.id })
                                                                         .indexOf(String(modelData.value)))
                                    onActivated: function(index) {
                                        chooser.choiceValues[modelData.id] = String(modelData.options[index].id)
                                    }
                                }
                                GCheckBox {
                                    visible: modelData.options.length === 0
                                    text: String(modelData.label)
                                    checked: String(modelData.value) === "true"
                                    onToggled: chooser.choiceValues[modelData.id] = checked ? "true" : "false"
                                }
                            }
                        }

                        GText {
                            Layout.fillWidth: true
                            visible: !chooser.localFolder
                            text: chooser.t("Yalnızca bu bilgisayardaki klasörlerden seçim yapılabilir.",
                                            "Only folders on this computer can be chosen.")
                            baseColor: AppTheme.textMuted
                            elide: Text.ElideRight
                        }
                        Item { Layout.fillWidth: true; visible: chooser.localFolder }

                        GModalButton {
                            objectName: "fileChooserCancel"
                            text: chooser.t("İptal", "Cancel")
                            onClicked: chooser.close()
                        }
                        GModalButton {
                            objectName: "fileChooserAccept"
                            primary: true
                            enabled: chooser.acceptEnabled
                            text: chooser.request && String(chooser.request.acceptLabel).length
                                  ? String(chooser.request.acceptLabel).replace("_", "")
                                  : chooser.saving ? chooser.t("Kaydet", "Save")
                                  : chooser.request && chooser.request.directory ? chooser.t("Seç", "Select")
                                  : chooser.t("Aç", "Open")
                            onClicked: chooser.acceptCurrent()
                        }
                    }
                }
            }
        }
        }
    }

    GModalPopup {
        id: overwritePopup
        objectName: "fileChooserOverwrite"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(460, chooser.width - 48)
        height: overwriteContent.implicitHeight + topPadding + bottomPadding
        closePolicy: Popup.CloseOnEscape
        background: GModalSurface { sourceItem: pane }
        contentItem: ColumnLayout {
            id: overwriteContent
            spacing: 14
            GText {
                Layout.fillWidth: true
                text: chooser.pendingUris.length > 1
                      ? chooser.t("Bazı dosyalar zaten var. Üzerine yazılsın mı?", "Some files already exist. Replace them?")
                      : chooser.t("Bu dosya zaten var. Üzerine yazılsın mı?", "This file already exists. Replace it?")
                typographyRole: "subtitle"
                basePixelSize: 16
                baseBold: true
                wrapMode: Text.WordWrap
            }
            GText {
                Layout.fillWidth: true
                text: chooser.pendingUris.map(function(url) { return decodeURIComponent(String(url).split("/").pop()) })
                                         .join(", ")
                baseColor: AppTheme.textMuted
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 8
                GModalButton {
                    objectName: "fileChooserOverwriteNo"
                    text: chooser.t("Vazgeç", "Cancel")
                    onClicked: overwritePopup.close()
                }
                GModalButton {
                    objectName: "fileChooserOverwriteYes"
                    text: chooser.t("Üzerine yaz", "Replace")
                    primary: true
                    onClicked: {
                        overwritePopup.close()
                        chooser.finish(chooser.pendingUris)
                    }
                }
            }
        }
    }
}
