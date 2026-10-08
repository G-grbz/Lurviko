import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lurviko.App
import Lurviko.Backend

GModalPopup {
    id: dialog
    required property var lang
    readonly property bool turkish: lang.language === "tr"
    property var selectedAction: null
    property var draft: []
    property var validation: ({ok: true})
    property string feedback: ""
    property bool confirmReset: false
    readonly property var groups: ["all", "files", "navigation", "tabs", "view", "selection", "app", "video", "photos"]
    readonly property var groupLabels: turkish
        ? ["Tüm eylemler", "Dosya işlemleri", "Gezinme ve arama", "Sekmeler", "Görünüm", "Seçim", "Uygulama", "Video oynatıcı", "Görsel görüntüleyici"]
        : ["All actions", "File operations", "Navigation & search", "Tabs", "View", "Selection", "Application", "Video player", "Photo viewer"]
    readonly property var filteredActions: {
        const query = searchField.text.trim().toLocaleLowerCase()
        const bindings = KeyboardShortcuts.bindings
        const group = groups[groupFilter.currentIndex] || "all"
        return KeyboardShortcuts.catalog.filter(function(action) {
            const keys = bindings[action.id] || []
            if (group !== "all" && action.group !== group) return false
            if (statusFilter.currentIndex === 1 && keys.length) return false
            if (statusFilter.currentIndex === 2 && JSON.stringify(keys) === JSON.stringify(action.defaults)) return false
            return !query.length || (action.titleTr + " " + action.titleEn + " " + keys.join(" ")).toLocaleLowerCase().indexOf(query) >= 0
        })
    }
    readonly property bool dirty: selectedAction
        && JSON.stringify(draft) !== JSON.stringify(KeyboardShortcuts.bindings[selectedAction.id] || [])
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(1040, parent ? parent.width - 48 : 1040)
    height: Math.min(700, parent ? parent.height - 48 : 700)
    padding: 24
    closePolicy: KeyboardShortcuts.recording ? Popup.NoAutoClose : Popup.CloseOnEscape
    objectName: "keyboardShortcutsDialog"

    function title(action) { return action ? (turkish ? action.titleTr : action.titleEn) : "" }
    function groupTitle(group) { return groupLabels[Math.max(0, groups.indexOf(group))] }
    function describe(keys) { return keys.map(function(key) { return KeyboardShortcuts.displaySequence(key) }).join("   ·   ") }
    function selectAction(action) {
        KeyboardShortcuts.stopRecording()
        selectedAction = action
        draft = (KeyboardShortcuts.bindings[action.id] || []).slice()
        validation = KeyboardShortcuts.validateBindings(action.id, draft)
        feedback = ""
    }
    function updateDraft(keys) {
        draft = keys.slice()
        feedback = ""
        validation = KeyboardShortcuts.validateBindings(selectedAction.id, draft)
    }
    function save(replaceConflicts) {
        const result = KeyboardShortcuts.assign(selectedAction.id, draft, replaceConflicts)
        validation = result
        if (result.ok) {
            draft = (KeyboardShortcuts.bindings[selectedAction.id] || []).slice()
            feedback = turkish ? "Kısayollar kaydedildi." : "Shortcuts saved."
        }
    }
    onAboutToShow: {
        KeyboardShortcuts.editorOpen = true
        searchField.text = ""
        groupFilter.currentIndex = 0
        statusFilter.currentIndex = 0
        confirmReset = false
        selectAction(KeyboardShortcuts.catalog[0])
    }
    onClosed: KeyboardShortcuts.editorOpen = false
    Component.onDestruction: if (visible) KeyboardShortcuts.editorOpen = false
    Connections {
        target: KeyboardShortcuts
        function onSequenceRecorded(sequence) {
            if (!dialog.visible || !dialog.selectedAction) return
            const keys = dialog.draft.slice()
            if (keys.indexOf(sequence) < 0 && keys.length < 8) keys.push(sequence)
            dialog.updateDraft(keys)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 18
        RowLayout {
            Layout.fillWidth: true
            spacing: 14
            Rectangle {
                Layout.preferredWidth: 48; Layout.preferredHeight: 48
                radius: 15; color: AppTheme.accentSoft
                CrispIcon { anchors.centerIn: parent; width: 28; height: 28; source: AppTheme.icon("keyboard.svg", AppTheme.accent) }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 5
                Text { text: dialog.turkish ? "Klavye kısayolları" : "Keyboard shortcuts"; color: AppTheme.text; font.pixelSize: 22; font.weight: Font.DemiBold }
                Text {
                    Layout.fillWidth: true
                    text: dialog.turkish ? "Eylemleri bul, tuşlarını kaydet ve kendine göre düzenle." : "Find actions, record key combinations and make them your own."
                    color: AppTheme.textMuted; font.pixelSize: 12; wrapMode: Text.WordWrap
                }
            }
            GButton {
                objectName: "closeShortcutsButton"
                Layout.preferredWidth: 36; Layout.preferredHeight: 36
                icon.source: AppTheme.icon("close-ui.svg")
                onClicked: dialog.close()
                GToolTip { text: dialog.turkish ? "Kapat" : "Close" }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            GTextField {
                id: searchField; objectName: "shortcutSearchField"
                Layout.fillWidth: true
                placeholderText: dialog.turkish ? "Eylem veya kısayol ara…" : "Search actions or shortcuts…"
            }
            GComboBox { id: groupFilter; Layout.preferredWidth: 192; model: dialog.groupLabels }
            GComboBox {
                id: statusFilter; Layout.preferredWidth: 156
                model: dialog.turkish ? ["Tüm atamalar", "Atanmamış", "Değiştirilmiş"] : ["All bindings", "Unassigned", "Modified"]
            }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true
            spacing: 20
            ColumnLayout {
                Layout.fillWidth: true; Layout.fillHeight: true
                Layout.minimumWidth: 360
                spacing: 9
                Text {
                    text: dialog.filteredActions.length + (dialog.turkish ? " eylem" : " actions")
                    color: AppTheme.textMuted; font.pixelSize: 11
                }
                Item {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    ListView {
                        id: actionList; objectName: "shortcutActionsList"
                        anchors.fill: parent; anchors.rightMargin: 14
                        clip: true; spacing: 6; boundsBehavior: Flickable.StopAtBounds
                        model: dialog.filteredActions
                        delegate: Rectangle {
                            id: actionRow
                            required property var modelData
                            width: ListView.view.width; height: 78; radius: 12
                            readonly property bool selected: dialog.selectedAction && dialog.selectedAction.id === modelData.id
                            readonly property var keys: KeyboardShortcuts.bindings[modelData.id] || []
                            color: selected ? AppTheme.accentSoft : (rowMouse.containsMouse ? AppTheme.surfaceHover : AppTheme.surfaceRaised)
                            border.width: 1; border.color: selected ? AppTheme.accentBorder : AppTheme.border
                            ColumnLayout {
                                anchors.fill: parent; anchors.margins: 13; spacing: 6
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { Layout.fillWidth: true; text: dialog.title(actionRow.modelData); color: AppTheme.text; font.pixelSize: 13; font.weight: Font.DemiBold; elide: Text.ElideRight }
                                    Text {
                                        visible: JSON.stringify(actionRow.keys) !== JSON.stringify(actionRow.modelData.defaults)
                                        text: dialog.turkish ? "Özel" : "Custom"
                                        color: AppTheme.accent; font.pixelSize: 10
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: dialog.groupTitle(actionRow.modelData.group); color: AppTheme.textFaint; font.pixelSize: 10 }
                                    Item { Layout.fillWidth: true }
                                    Text {
                                        Layout.maximumWidth: actionRow.width * 0.65
                                        text: actionRow.keys.length ? dialog.describe(actionRow.keys) : (dialog.turkish ? "Atanmamış" : "Unassigned")
                                        color: actionRow.keys.length ? AppTheme.textMuted : AppTheme.textFaint
                                        font.pixelSize: 11; font.weight: Font.Medium; elide: Text.ElideRight
                                    }
                                }
                            }
                            MouseArea { id: rowMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: dialog.selectAction(actionRow.modelData) }
                        }
                    }
                    ScrollBar {
                        anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
                        width: 7; orientation: Qt.Vertical
                        size: actionList.visibleArea.heightRatio; position: actionList.visibleArea.yPosition
                        visible: actionList.contentHeight > actionList.height
                        active: hovered || pressed || actionList.moving
                        onPositionChanged: if (pressed) actionList.contentY = actionList.originY + position * actionList.contentHeight
                        contentItem: Rectangle { radius: 3; color: AppTheme.textMuted; opacity: parent.active ? 0.75 : 0.35 }
                    }
                    Text {
                        anchors.centerIn: parent; visible: dialog.filteredActions.length === 0
                        text: dialog.turkish ? "Eşleşen eylem bulunamadı." : "No matching actions."
                        color: AppTheme.textMuted; font.pixelSize: 13
                    }
                }
            }
            Rectangle { Layout.fillHeight: true; Layout.preferredWidth: 1; color: AppTheme.border }
            ColumnLayout {
                Layout.preferredWidth: 320; Layout.fillHeight: true
                Layout.minimumWidth: 300; Layout.maximumWidth: 320
                spacing: 14
                Item {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    Flickable {
                        id: editorScroll
                        anchors.fill: parent; anchors.rightMargin: 12
                        contentHeight: editorColumn.implicitHeight + 8
                        clip: true; boundsBehavior: Flickable.StopAtBounds
                        ColumnLayout {
                            id: editorColumn
                            width: editorScroll.width
                            spacing: 12
                            Text {
                                Layout.fillWidth: true
                                text: dialog.title(dialog.selectedAction)
                                color: AppTheme.text; font.pixelSize: 17; font.weight: Font.DemiBold; wrapMode: Text.WordWrap
                            }
                            Text {
                                Layout.fillWidth: true
                                text: dialog.turkish ? "Bir eyleme birden fazla kombinasyon atayabilirsin. Değişiklikler Kaydet ile uygulanır." : "Assign multiple combinations to an action. Changes take effect when you save."
                                color: AppTheme.textMuted; font.pixelSize: 11; wrapMode: Text.WordWrap
                            }
                            Flow {
                                Layout.fillWidth: true
                                Layout.preferredHeight: implicitHeight
                                spacing: 7; clip: true
                                Repeater {
                                    model: dialog.draft
                                    delegate: GButton {
                                        required property string modelData
                                        required property int index
                                        width: Math.min(implicitWidth, parent.width)
                                        implicitHeight: 32; font.pixelSize: 11
                                        text: KeyboardShortcuts.displaySequence(modelData) + "   ×"
                                        enabled: !KeyboardShortcuts.recording
                                        onClicked: { const keys = dialog.draft.slice(); keys.splice(index, 1); dialog.updateDraft(keys) }
                                        GToolTip { text: dialog.turkish ? "Bu atamayı kaldır" : "Remove this binding" }
                                    }
                                }
                            }
                            GButton {
                                objectName: "recordShortcutButton"
                                Layout.fillWidth: true
                                enabled: dialog.draft.length < 8 || KeyboardShortcuts.recording
                                primary: KeyboardShortcuts.recording
                                text: KeyboardShortcuts.recording
                                    ? (dialog.turkish ? "Tuşlara bas… / Kaydı durdur" : "Press your keys… / Stop recording")
                                    : (dialog.turkish ? "+ Kısayol kaydet" : "+ Record shortcut")
                                onClicked: KeyboardShortcuts.recording ? KeyboardShortcuts.stopRecording() : KeyboardShortcuts.startRecording()
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                GButton {
                                    Layout.fillWidth: true
                                    text: dialog.turkish ? "Temizle" : "Clear"
                                    enabled: dialog.draft.length > 0 && !KeyboardShortcuts.recording
                                    onClicked: dialog.updateDraft([])
                                }
                                GButton {
                                    Layout.fillWidth: true
                                    text: dialog.turkish ? "Varsayılan" : "Default"
                                    enabled: !KeyboardShortcuts.recording
                                    onClicked: dialog.updateDraft(dialog.selectedAction.defaults)
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: dialog.selectedAction && dialog.selectedAction.defaults.length
                                    ? (dialog.turkish ? "Varsayılan: " : "Default: ") + dialog.describe(dialog.selectedAction.defaults)
                                    : (dialog.turkish ? "Varsayılan bir kısayolu yok." : "No default shortcut.")
                                color: AppTheme.textFaint; font.pixelSize: 11; wrapMode: Text.WordWrap
                            }
                            Text {
                                Layout.fillWidth: true
                                text: !dialog.selectedAction ? "" : dialog.selectedAction.scope === "global"
                                    ? (dialog.turkish ? "Uygulama genelinde çalışır." : "Works throughout the application.")
                                    : dialog.selectedAction.scope === "video"
                                    ? (dialog.turkish ? "Video oynatıcı açıkken çalışır." : "Works while the video player is open.")
                                    : dialog.selectedAction.scope === "photo"
                                    ? (dialog.turkish ? "Görsel görüntüleyici açıkken çalışır." : "Works while the photo viewer is open.")
                                    : (dialog.turkish ? "Dosya listesinde çalışır. Metin girişlerine müdahale etmez." : "Works in the file browser without interrupting text entry.")
                                color: AppTheme.textMuted; font.pixelSize: 11; wrapMode: Text.WordWrap
                            }
                            Text {
                                Layout.fillWidth: true
                                visible: dialog.validation.error === "conflict"
                                text: (dialog.turkish ? "Bu tuşlar şu eylemlerde kullanılıyor:\n" : "These keys are already used by:\n")
                                    + (dialog.validation.conflicts || []).map(function(action) { return dialog.title(action) }).join("\n")
                                color: AppTheme.warning; font.pixelSize: 11; wrapMode: Text.WordWrap
                            }
                            GButton {
                                Layout.fillWidth: true
                                visible: dialog.validation.error === "conflict"
                                text: dialog.turkish ? "Çakışmaları kaldır ve ata" : "Reassign conflicting shortcuts"
                                onClicked: dialog.save(true)
                            }
                            Text {
                                Layout.fillWidth: true
                                visible: dialog.validation.error === "invalid"
                                text: dialog.turkish ? "Geçerli bir tuş kombinasyonu kaydet." : "Record a valid key combination."
                                color: AppTheme.danger; font.pixelSize: 11; wrapMode: Text.WordWrap
                            }
                        }
                    }
                    ScrollBar {
                        anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
                        width: 7; orientation: Qt.Vertical
                        visible: editorScroll.contentHeight > editorScroll.height
                        size: editorScroll.visibleArea.heightRatio; position: editorScroll.visibleArea.yPosition
                        active: hovered || pressed || editorScroll.moving
                        onPositionChanged: if (pressed) editorScroll.contentY = position * editorScroll.contentHeight
                        contentItem: Rectangle { radius: 3; color: AppTheme.textMuted; opacity: parent.active ? 0.75 : 0.35 }
                    }
                }
                Text { text: dialog.feedback; color: AppTheme.success; font.pixelSize: 11; visible: text.length > 0 }
                GButton {
                    objectName: "saveShortcutButton"
                    Layout.fillWidth: true; primary: true
                    text: dialog.turkish ? "Kaydet" : "Save"
                    enabled: dialog.dirty && dialog.validation.ok && !KeyboardShortcuts.recording
                    onClicked: dialog.save(false)
                }
            }
        }
        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: AppTheme.border }
        RowLayout {
            Layout.fillWidth: true; spacing: 10
            Text {
                Layout.fillWidth: true
                text: dialog.confirmReset
                    ? (dialog.turkish ? "Tüm özel atamalar varsayılana döndürülsün mü?" : "Restore all custom bindings to their defaults?")
                    : (dialog.turkish ? "Atamalar otomatik yüklenir ve uygulama yeniden açıldığında korunur." : "Saved bindings are restored automatically when Lurviko starts.")
                color: dialog.confirmReset ? AppTheme.warning : AppTheme.textMuted
                font.pixelSize: 11; wrapMode: Text.WordWrap
            }
            GButton {
                visible: dialog.confirmReset
                text: dialog.turkish ? "Vazgeç" : "Cancel"
                onClicked: dialog.confirmReset = false
            }
            GButton {
                text: dialog.confirmReset
                    ? (dialog.turkish ? "Sıfırlamayı onayla" : "Confirm reset")
                    : (dialog.turkish ? "Tümünü varsayılana döndür" : "Restore all defaults")
                enabled: !KeyboardShortcuts.recording
                onClicked: {
                    if (!dialog.confirmReset) { dialog.confirmReset = true; return }
                    KeyboardShortcuts.resetAll()
                    dialog.selectAction(dialog.selectedAction)
                    dialog.confirmReset = false
                }
            }
        }
    }
}
