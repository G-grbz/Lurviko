import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import GFile.App

ColumnLayout {
    id: root
    required property var contentIndexModel
    required property string categoryKey
    required property var lang
    property var draftRoots: []
    property var draftIgnoreRules: []
    property bool draftIndexHidden: false
    property bool draftSingleClickOpen: false
    property bool globalSingleClickOpen: false
    spacing: 10

    function reload() {
        if (!root.contentIndexModel) {
            root.draftRoots = []
            root.draftIgnoreRules = []
            root.draftIndexHidden = false
            root.draftSingleClickOpen = false
            return
        }
        root.draftRoots = Array.from(root.contentIndexModel.mediaRoots(root.categoryKey) || [])
        root.draftIgnoreRules = Array.from(root.contentIndexModel.ignoreRules(root.categoryKey) || [])
        root.draftIndexHidden = !!root.contentIndexModel.categoryIndexHidden(root.categoryKey)
        root.draftSingleClickOpen = !!root.contentIndexModel.categorySingleClickOpen(root.categoryKey)
        ignoreRuleInput.text = ""
    }

    function save() {
        if (!root.contentIndexModel)
            return false
        const saved = root.contentIndexModel.setCategoryIndexSettings(root.categoryKey,
                                                                     root.draftRoots,
                                                                     root.draftIndexHidden,
                                                                     root.draftIgnoreRules)
        root.contentIndexModel.setCategorySingleClickOpen(root.categoryKey, root.draftSingleClickOpen)
        return saved
    }

    function reindex() {
        if (root.contentIndexModel)
            root.contentIndexModel.refreshCategory(root.categoryKey)
    }

    function addPath(path) {
        let clean = String(path || "")
        if (clean.startsWith("file://"))
            clean = decodeURIComponent(clean.substring(7))
        if (!clean.length)
            return
        const next = Array.from(root.draftRoots)
        if (next.indexOf(clean) < 0) {
            next.push(clean)
            next.sort(function(a, b) { return String(a).localeCompare(String(b)) })
            root.draftRoots = next
        }
    }

    function removeRoot(index) {
        if (index < 0 || index >= root.draftRoots.length)
            return
        const next = Array.from(root.draftRoots)
        next.splice(index, 1)
        root.draftRoots = next
    }

    function addIgnoreText(value) {
        const pieces = String(value || "").split(/[\n,;]+/)
        const next = Array.from(root.draftIgnoreRules)
        for (let i = 0; i < pieces.length; ++i) {
            const clean = String(pieces[i] || "").trim().toLowerCase()
            if (clean.length && next.indexOf(clean) < 0)
                next.push(clean)
        }
        next.sort(function(a, b) { return String(a).localeCompare(String(b)) })
        root.draftIgnoreRules = next
        ignoreRuleInput.text = ""
    }

    function removeIgnore(index) {
        if (index < 0 || index >= root.draftIgnoreRules.length)
            return
        const next = Array.from(root.draftIgnoreRules)
        next.splice(index, 1)
        root.draftIgnoreRules = next
    }

    function restoreDefaultIgnores() {
        if (!root.contentIndexModel)
            return
        root.draftIgnoreRules = Array.from(root.contentIndexModel.defaultIgnoreRules(root.categoryKey) || [])
    }

    Text {
        Layout.fillWidth: true
        text: root.lang.language === "tr" ? "İndeksleme" : "Indexing"
        color: AppTheme.text
        font.pixelSize: 12
        font.weight: Font.DemiBold
    }

    GCheckBox {
        Layout.fillWidth: true
        text: root.lang.language === "tr" ? "Gizli dosya ve dizinleri indeksle" : "Index hidden files and folders"
        checked: root.draftIndexHidden
        onToggled: root.draftIndexHidden = checked
    }

    GCheckBox {
        Layout.fillWidth: true
        text: root.lang.language === "tr" ? "Tek tıkla aç" : "Open with single click"
        checked: root.draftSingleClickOpen
        enabled: !root.globalSingleClickOpen
        onToggled: root.draftSingleClickOpen = checked
        ToolTip.visible: hovered
        ToolTip.text: root.globalSingleClickOpen
                      ? (root.lang.language === "tr"
                         ? "Genel Görünüm ayarındaki Tek tıkla aç etkin; kategori ayarı geçici olarak devre dışı."
                         : "Global Open with single click is enabled in View; this category setting is temporarily overridden.")
                      : (root.lang.language === "tr"
                         ? "Bu kategoride öğeleri çift tık yerine tek tıkla açar."
                         : "Open items in this category with one click instead of double-click.")
    }

    Text {
        Layout.fillWidth: true
        text: root.lang.language === "tr" ? "İzlenen dizinler" : "Indexed folders"
        color: AppTheme.text
        font.pixelSize: 12
        font.weight: Font.DemiBold
    }

    Text {
        Layout.fillWidth: true
        text: root.lang.language === "tr"
              ? "Yalnızca aşağıdaki dizinlerin içindeki bu kategori dosyaları indekslenir. USB, CIFS/NFS ve diğer bağlı diskleri ekleyebilirsin."
              : "Only files for this category inside the folders below are indexed. USB, CIFS/NFS and other mounted storage can be added."
        color: AppTheme.textMuted
        font.pixelSize: 9
        wrapMode: Text.Wrap
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: Math.max(58, rootsColumn.implicitHeight + 16)
        radius: 10
        color: AppTheme.surfaceRaised
        border.color: AppTheme.border

        Column {
            id: rootsColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 8
            spacing: 5

            Repeater {
                model: root.draftRoots
                delegate: Rectangle {
                    required property string modelData
                    required property int index
                    width: rootsColumn.width
                    height: 36
                    radius: 8
                    color: AppTheme.surface
                    border.color: AppTheme.border

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 5
                        spacing: 8
                        Text {
                            Layout.fillWidth: true
                            text: modelData
                            color: AppTheme.text
                            font.pixelSize: 9
                            elide: Text.ElideMiddle
                        }
                        GToolButton {
                            Layout.preferredWidth: 30
                            Layout.preferredHeight: 28
                            text: ""
                            display: AbstractButton.IconOnly
                            icon.source: AppTheme.icon("close-ui.svg")
                            icon.width: 16
                            icon.height: 16
                            icon.color: hovered ? AppTheme.danger : AppTheme.textMuted
                            onClicked: root.removeRoot(index)
                            background: Rectangle {
                                radius: 9
                                color: parent.hovered ? AppTheme.surfaceHover : "transparent"
                            }
                        }
                    }
                }
            }

            Text {
                width: parent.width
                visible: root.draftRoots.length === 0
                text: root.lang.language === "tr"
                      ? "Hiç dizin seçilmedi. Bu kategori boş kalacak."
                      : "No folders selected. This category will remain empty."
                color: AppTheme.textFaint
                font.pixelSize: 9
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                topPadding: 10
                bottomPadding: 10
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        GModalButton {
            text: root.lang.language === "tr" ? "Dizin ekle" : "Add folder"
            onClicked: folderDialog.open()
        }
        GModalButton {
            text: root.lang.language === "tr" ? "Ev dizinini ekle" : "Add Home"
            onClicked: {
                if (root.contentIndexModel)
                    root.addPath(root.contentIndexModel.defaultMediaRoot())
            }
        }
        Item { Layout.fillWidth: true }
        Text {
            text: root.draftRoots.length + (root.lang.language === "tr" ? " dizin" : " folders")
            color: AppTheme.textFaint
            font.pixelSize: 9
        }
    }

    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: AppTheme.border }

    RowLayout {
        Layout.fillWidth: true
        Text {
            Layout.fillWidth: true
            text: root.lang.language === "tr" ? "İndekslenmeyecek kelime / uzantılar" : "Ignored words / extensions"
            color: AppTheme.text
            font.pixelSize: 12
            font.weight: Font.DemiBold
        }
        GModalButton {
            text: root.lang.language === "tr" ? "Varsayılanlar" : "Defaults"
            onClicked: root.restoreDefaultIgnores()
        }
    }

    Text {
        Layout.fillWidth: true
        text: root.lang.language === "tr"
              ? "Örn. pdf → PDF dosyaları; index → adında index geçen dosyalar; trailer → trailer içeren tüm video adları. path:cache gibi bir kural yolun tamamında eşleşir."
              : "Example: pdf excludes PDF files; index excludes names containing index; trailer excludes video names containing trailer. A rule such as path:cache matches the full path."
        color: AppTheme.textMuted
        font.pixelSize: 9
        wrapMode: Text.Wrap
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: Math.max(50, ignoreFlow.implicitHeight + 16)
        radius: 10
        color: AppTheme.surfaceRaised
        border.color: AppTheme.border

        Flow {
            id: ignoreFlow
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 8
            spacing: 6

            Repeater {
                model: root.draftIgnoreRules
                delegate: Rectangle {
                    required property string modelData
                    required property int index
                    width: Math.min(ignoreFlow.width, ruleLabel.implicitWidth + 34)
                    height: 30
                    radius: 9
                    color: AppTheme.surface
                    border.color: AppTheme.border

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 9
                        anchors.rightMargin: 3
                        spacing: 4
                        Text {
                            id: ruleLabel
                            Layout.fillWidth: true
                            text: modelData
                            color: AppTheme.text
                            font.family: "monospace"
                            font.pixelSize: 9
                            elide: Text.ElideRight
                        }
                        GToolButton {
                            Layout.preferredWidth: 28
                            Layout.preferredHeight: 28
                            text: ""
                            display: AbstractButton.IconOnly
                            icon.source: AppTheme.icon("close-ui.svg")
                            icon.width: 16
                            icon.height: 16
                            icon.color: hovered ? AppTheme.danger : AppTheme.textMuted
                            onClicked: root.removeIgnore(index)
                            background: Rectangle {
                                radius: 9
                                color: parent.hovered ? AppTheme.surfaceHover : "transparent"
                            }
                        }
                    }
                }
            }

            Text {
                width: ignoreFlow.width
                visible: root.draftIgnoreRules.length === 0
                text: root.lang.language === "tr" ? "Hariç tutma kuralı yok." : "No ignore rules."
                color: AppTheme.textFaint
                font.pixelSize: 9
                horizontalAlignment: Text.AlignHCenter
                topPadding: 8
                bottomPadding: 8
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        GTextField {
            id: ignoreRuleInput
            Layout.fillWidth: true
            placeholderText: root.lang.language === "tr" ? "pdf, index, trailer, path:cache…" : "pdf, index, trailer, path:cache…"
            color: AppTheme.text
            selectByMouse: true
            onAccepted: root.addIgnoreText(text)
            background: Rectangle {
                radius: 9
                color: AppTheme.surfaceRaised
                border.color: ignoreRuleInput.activeFocus ? AppTheme.accent : AppTheme.border
            }
        }
        GModalButton {
            text: root.lang.language === "tr" ? "Ekle" : "Add"
            enabled: ignoreRuleInput.text.trim().length > 0
            onClicked: root.addIgnoreText(ignoreRuleInput.text)
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        GModalButton {
            text: root.lang.language === "tr" ? "Bu kategoriyi yeniden indeksle" : "Reindex this category"
            onClicked: root.reindex()
        }
        Item { Layout.fillWidth: true }
        Text {
            text: root.lang.language === "tr" ? "Diğer kategoriler etkilenmez" : "Other categories are not affected"
            color: AppTheme.textFaint
            font.pixelSize: 9
        }
    }

    FolderDialog {
        id: folderDialog
        title: root.lang.language === "tr" ? "İndekslenecek dizini seç" : "Choose a folder to index"
        onAccepted: root.addPath(selectedFolder)
    }

    Component.onCompleted: reload()
}
