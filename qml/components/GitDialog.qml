import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lurviko.App

GModalPopup {
    id: dialog
    objectName: "gitDialog"
    property var lang
    property var controller: null
    property string mode: "info"
    property var selected: ({})
    property string resultMessage: ""
    property bool resultSuccess: true
    readonly property bool tr: lang && lang.language === "tr"
    readonly property var info: controller ? controller.info : ({})
    readonly property var tagInfo: controller ? controller.tagInfo : ({})
    readonly property var operationSteps: controller ? controller.operationSteps : []
    readonly property bool tagReady: !!tagInfo.remoteVerified
    function tagSuggestion() {
        const options = [tagInfo.patch || "", tagInfo.minor || "", tagInfo.major || ""]
        return options[versionBump.currentIndex] || ""
    }
    function applyTagSuggestion() {
        if (mode === "commit") commitTagName.text = tagSuggestion()
        else independentTagName.text = tagSuggestion()
    }
    function validTag(name) {
        return /^(v?)(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?$/.test(name)
    }
    function historySubjectColor(subject) {
        const name = String(subject || "")
        if (/^feat(?:\([^)]+\))?!?:/.test(name)) return AppTheme.success
        if (/^fix(?:\([^)]+\))?!?:/.test(name)) return AppTheme.accent
        if (/^revert(?:\([^)]+\))?!?:/.test(name)) return AppTheme.warning
        return AppTheme.text
    }
    function resultColor(kind) {
        return kind === "success" ? AppTheme.success : kind === "error" ? AppTheme.danger
                : kind === "warning" ? AppTheme.warning : AppTheme.textMuted
    }
    function resultIcon(kind) {
        return kind === "success" ? "✓" : kind === "error" ? "✕" : kind === "warning" ? "!" : "ⓘ"
    }
    readonly property int selectedCount: Object.keys(selected).length
    readonly property bool working: controller !== null && controller.busy
    readonly property var visibleChanges: {
        const changes = controller ? controller.changes : []
        const filter = statusFilter.currentIndex
        return changes.filter(function(row) {
            return filter === 0 || (filter === 1 && row.staged) || (filter === 2 && row.unstaged)
                    || (filter === 3 && row.untracked) || (filter === 4 && row.deleted)
        })
    }
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(850, parent ? parent.width - 32 : 850)
    height: Math.min(680, parent ? parent.height - 32 : 680)
    padding: 20
    closePolicy: working ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside
    function show(source, action) {
        controller = source
        if (controller) controller.clearResult()
        selected = ({})
        resultMessage = ""
        messageInput.text = ""
        pushAfter.checked = false
        tagAfter.checked = false
        standalonePush.checked = false
        statusFilter.currentIndex = 0
        changeMode(action === "commit" || action === "history" || action === "diff" ? action : "info")
        open()
        if (action === "sync") source.synchronize()
        else if (action === "push") source.push()
        else if (action === "pull") source.pull()
        else if (action === "fetch") source.fetch()
    }
    function changeMode(value) {
        mode = value
        if (!controller) return
        if (value === "commit" || value === "info") controller.loadChanges()
        else if (value === "history") controller.loadHistory()
        else if (value === "diff" || value === "compare") controller.loadDiff(value === "compare")
        else if (value === "upstream") controller.loadUpstreams()
        else if (value === "tags") controller.loadTags()
    }
    function choose(id, checked) {
        const next = Object.assign({}, selected)
        if (checked) next[id] = true
        else delete next[id]
        selected = next
    }
    function stateLabel(row) {
        let labels = []
        if (row.staged) labels.push(tr ? "Staged" : "Staged")
        if (row.unstaged) labels.push(tr ? "Stage edilmemiş" : "Unstaged")
        const names = tr ? { untracked: "İzlenmeyen", deleted: "Silinmiş", renamed: "Yeniden adlandırılmış", added: "Eklenmiş", modified: "Değiştirilmiş", conflict: "Çakışma", ignored: "Yok sayılan" }
                         : { untracked: "Untracked", deleted: "Deleted", renamed: "Renamed", added: "Added", modified: "Modified", conflict: "Conflict", ignored: "Ignored" }
        labels.push(names[row.status] || row.status)
        return labels.join(" · ")
    }
    Connections {
        target: dialog.controller
        function onDetailsChanged() {
            if (!dialog.controller || !dialog.tagInfo) return
            if (dialog.mode === "commit" && tagAfter.checked && !commitTagName.activeFocus
                    && !commitTagName.text.length) {
                const choices = [dialog.tagInfo.patch || "", dialog.tagInfo.minor || "", dialog.tagInfo.major || ""]
                commitTagName.text = choices[commitBump.currentIndex]
            }
            if (dialog.mode === "tags" && !independentTagName.activeFocus
                    && !independentTagName.text.length) dialog.applyTagSuggestion()
        }
        function onOperationFinished(operation, success, message) {
            dialog.resultMessage = message
            dialog.resultSuccess = success
            // A rejected Push must never offer to create the successful commit again.
            if (operation === "commit" && dialog.controller.lastCommit.length > 0) {
                dialog.mode = "info"
                dialog.selected = ({})
            }
            if (operation === "upstream" && success) dialog.mode = "info"
            if (operation === "create-tag" || operation === "push-tag" || operation === "commit" && success && tagAfter.checked)
                dialog.controller.loadTags()
            if (dialog.visible && dialog.mode === "info") dialog.controller.loadChanges()
        }
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            GText {
                Layout.fillWidth: true
                text: dialog.mode === "commit" ? (dialog.tr ? "Commit oluştur" : "Create commit")
                    : dialog.mode === "history" ? (dialog.tr ? "Commit geçmişi" : "Commit history")
                    : dialog.mode === "diff" ? (dialog.tr ? "Değişiklikler" : "Changes")
                    : dialog.mode === "compare" ? (dialog.tr ? "Dal karşılaştırması" : "Branch comparison")
                    : dialog.mode === "upstream" ? (dialog.tr ? "Upstream seç" : "Choose upstream")
                    : dialog.mode === "tags" ? (dialog.tr ? "Sürüm etiketleri" : "Version tags")
                    : (dialog.tr ? "Git deposu" : "Git repository")
                basePixelSize: 18; baseBold: true; baseColor: AppTheme.text
            }
            GBusyIndicator { running: dialog.controller !== null && (dialog.controller.loading || dialog.working); visible: running; implicitWidth: 24; implicitHeight: 24 }
            GModalButton { text: dialog.tr ? "Depo bilgileri" : "Repository info"; visible: dialog.mode !== "info"; enabled: !dialog.working; onClicked: dialog.changeMode("info") }
        }
        GText {
            Layout.fillWidth: true
            text: String(dialog.info.branch || "") + (dialog.info.detached ? " · Detached HEAD" : "")
            basePixelSize: 13; baseColor: AppTheme.text; textFormat: Text.PlainText; elide: Text.ElideMiddle
        }
        GText {
            Layout.fillWidth: true
            text: String(dialog.info.root || "")
            basePixelSize: 11; baseColor: AppTheme.textMuted; textFormat: Text.PlainText; elide: Text.ElideMiddle
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: notice.implicitHeight + 16
            visible: notice.text.length > 0 && (dialog.working || dialog.operationSteps.length === 0)
            radius: 9
            color: AppTheme.surfaceRaised
            GText {
                id: notice
                anchors.fill: parent; anchors.margins: 8
                text: dialog.working ? (dialog.controller.progress || (dialog.tr ? "Git işlemi sürüyor…" : "Git operation in progress…"))
                     : dialog.resultMessage || (dialog.controller ? dialog.controller.error : "")
                textFormat: Text.PlainText; wrapMode: Text.Wrap
                basePixelSize: 11
                baseColor: !dialog.working && ((!dialog.resultMessage.length && dialog.controller !== null && dialog.controller.error.length) || !dialog.resultSuccess) ? AppTheme.danger
                    : !dialog.working && dialog.resultMessage.length > 0 ? AppTheme.success : AppTheme.textMuted
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            visible: !dialog.working && dialog.operationSteps.length > 0
            spacing: 3
            Repeater {
                model: dialog.operationSteps
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: stageMessage.implicitHeight + 12
                    radius: 8
                    color: AppTheme.tint(dialog.resultColor(modelData.kind), AppTheme.dark ? 0.15 : 0.08)
                    RowLayout {
                        anchors.fill: parent; anchors.margins: 6; spacing: 7
                        GText { text: dialog.resultIcon(modelData.kind); baseBold: true; basePixelSize: 12; baseColor: dialog.resultColor(modelData.kind) }
                        GText { id: stageMessage; Layout.fillWidth: true; text: modelData.message; textFormat: Text.PlainText; wrapMode: Text.Wrap; basePixelSize: 11; baseColor: dialog.resultColor(modelData.kind) }
                    }
                }
            }
        }
        ColumnLayout {
            visible: dialog.mode === "info"
            Layout.fillWidth: true
            spacing: 8
            GText {
                Layout.fillWidth: true
                text: dialog.info.upstream ? "Upstream: " + String(dialog.info.upstream).replace(/^refs\/remotes\//, "")
                     : (dialog.tr ? "Upstream tanımlı değil" : "No upstream configured")
                textFormat: Text.PlainText; wrapMode: Text.Wrap; basePixelSize: 12; baseColor: AppTheme.text
            }
            GText {
                Layout.fillWidth: true
                text: (dialog.info.countsKnown ? (dialog.info.ahead === 0 && dialog.info.behind === 0
                    ? (dialog.tr ? "✓ Yerel ve bilinen uzak geçmiş eşit" : "✓ Local and known remote history match")
                    : "↑" + dialog.info.ahead + "  ↓" + dialog.info.behind)
                    : (dialog.tr ? "Commit farkı bilinmiyor" : "Commit difference unavailable"))
                    + " · " + (dialog.info.dirtyKnown ? (dialog.info.dirty ? (dialog.tr ? "Commit edilmemiş değişiklikler" : "Uncommitted changes") : (dialog.tr ? "Çalışma ağacı temiz" : "Clean working tree")) : (dialog.tr ? "Durum okunuyor" : "Reading status"))
                wrapMode: Text.Wrap; basePixelSize: 12
                baseColor: dialog.info.countsKnown && dialog.info.ahead === 0 && dialog.info.behind === 0 && !dialog.info.dirty
                    ? AppTheme.success : dialog.info.countsKnown && (dialog.info.ahead > 0 || dialog.info.behind > 0)
                    ? AppTheme.warning : AppTheme.text
            }
            GText {
                Layout.fillWidth: true
                text: dialog.tr ? "Uzak bilgiler yalnızca Fetch ile yenilenir. Ayrışmış geçmiş otomatik birleştirilmez." : "Remote information updates only after Fetch. Diverged histories are never merged automatically."
                wrapMode: Text.Wrap; basePixelSize: 11; baseColor: AppTheme.textMuted
            }
            Flow {
                Layout.fillWidth: true
                spacing: 6
                GModalButton { text: dialog.tr ? "Depoyu eşitle" : "Synchronize repository"; primary: true; enabled: !dialog.working && dialog.controller !== null && dialog.controller.available; onClicked: dialog.controller.synchronize() }
                GModalButton { text: dialog.tr ? "Commit oluştur…" : "Create commit…"; enabled: !dialog.working; onClicked: dialog.changeMode("commit") }
                GModalButton { text: "Fetch"; enabled: !dialog.working; onClicked: dialog.controller.fetch() }
                GModalButton { text: "Pull"; enabled: !dialog.working; onClicked: dialog.controller.pull() }
                GModalButton { text: "Push"; enabled: !dialog.working; onClicked: dialog.controller.push() }
                GModalButton { text: dialog.tr ? "Karşılaştır" : "Compare"; enabled: !dialog.working && !!dialog.info.upstream; onClicked: dialog.changeMode("compare") }
                GModalButton { text: dialog.tr ? "Upstream seç…" : "Choose upstream…"; enabled: !dialog.working && !dialog.info.detached && !dialog.info.unborn; onClicked: dialog.changeMode("upstream") }
                GModalButton { text: dialog.tr ? "Değişiklikler" : "Changes"; enabled: !dialog.working; onClicked: dialog.changeMode("diff") }
                GModalButton { text: dialog.tr ? "Geçmiş" : "History"; enabled: !dialog.working; onClicked: dialog.changeMode("history") }
                GModalButton { objectName: "gitTagsAction"; text: dialog.tr ? "Tag oluştur…" : "Create tag…"; enabled: !dialog.working && !dialog.info.unborn; onClicked: { independentTagName.text = ""; dialog.changeMode("tags") } }
                GModalButton { text: dialog.tr ? "Uzak depoda aç" : "Open remote"; enabled: !!dialog.info.webUrl; onClicked: dialog.controller.openRemote() }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: dialog.mode === "commit" || dialog.mode === "info"
            GComboBox {
                id: statusFilter
                objectName: "gitChangeFilter"
                Layout.fillWidth: true
                model: dialog.tr ? ["Tüm değişiklikler", "Staged", "Stage edilmemiş", "İzlenmeyen", "Silinmiş"] : ["All changes", "Staged", "Unstaged", "Untracked", "Deleted"]
            }
            GModalButton {
                visible: dialog.mode === "commit"
                text: dialog.tr ? "Görünenleri seç" : "Select shown"
                enabled: !dialog.working
                onClicked: { const next = Object.assign({}, dialog.selected); dialog.visibleChanges.forEach(function(row) { if (row.selectable) next[row.id] = true }); dialog.selected = next }
            }
            GModalButton { visible: dialog.mode === "commit"; text: dialog.tr ? "Seçimi temizle" : "Clear selection"; enabled: !dialog.working; onClicked: dialog.selected = ({}) }
        }
        GText {
            visible: (dialog.mode === "commit" || dialog.mode === "info") && !!dialog.info.changesTruncated
            Layout.fillWidth: true
            text: dialog.tr ? "İlk 10.000 değişiklik gösteriliyor. Yalnızca seçtiğin dosyalar commit edilir." : "Showing the first 10,000 changes. Only selected files are committed."
            wrapMode: Text.Wrap; basePixelSize: 11; baseColor: AppTheme.warning
        }
        ListView {
            id: changeList
            objectName: "gitChangeList"
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 60
            visible: dialog.mode === "commit" || dialog.mode === "info"
            clip: true; spacing: 3; model: dialog.visibleChanges
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                required property var modelData
                width: changeList.width; height: 50; radius: 8
                color: dialog.selected[modelData.id] ? AppTheme.accentSoft : AppTheme.surfaceRaised
                RowLayout {
                    anchors.fill: parent; anchors.margins: 6; spacing: 8
                    GCheckBox {
                        objectName: "gitFileCheck"
                        visible: dialog.mode === "commit"
                        checked: !!dialog.selected[modelData.id]
                        enabled: modelData.selectable && !dialog.working && dialog.controller !== null && !dialog.controller.loading
                        onClicked: dialog.choose(modelData.id, checked)
                    }
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 2
                        GText { Layout.fillWidth: true; text: (modelData.oldPath ? modelData.oldPath + " → " : "") + modelData.path; textFormat: Text.PlainText; elide: Text.ElideMiddle; basePixelSize: 12; baseColor: AppTheme.text }
                        GText { Layout.fillWidth: true; text: dialog.stateLabel(modelData); elide: Text.ElideRight; basePixelSize: 10; baseColor: modelData.conflict ? AppTheme.danger : AppTheme.textMuted }
                    }
                }
            }
            GText { anchors.centerIn: parent; visible: changeList.count === 0 && dialog.controller !== null && !dialog.controller.loading; text: dialog.tr ? "Gösterilecek değişiklik yok" : "No changes to show"; baseColor: AppTheme.textMuted; basePixelSize: 12 }
        }
        ListView {
            id: historyList
            Layout.fillWidth: true; Layout.fillHeight: true
            visible: dialog.mode === "history"
            clip: true; spacing: 5; model: dialog.controller ? dialog.controller.history : []
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                required property var modelData
                width: historyList.width; height: historyRow.implicitHeight + 18; radius: 8; color: AppTheme.surfaceRaised
                ColumnLayout {
                    id: historyRow
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 9; spacing: 4
                    GText { Layout.fillWidth: true; text: modelData.shortHash + "  " + modelData.subject; textFormat: Text.PlainText; wrapMode: Text.Wrap; baseColor: dialog.historySubjectColor(modelData.subject); basePixelSize: 12 }
                    GText { Layout.fillWidth: true; text: modelData.author + " · " + modelData.date; textFormat: Text.PlainText; wrapMode: Text.Wrap; baseColor: AppTheme.textMuted; basePixelSize: 10 }
                }
            }
        }
        ScrollView {
            Layout.fillWidth: true; Layout.fillHeight: true
            visible: dialog.mode === "diff" || dialog.mode === "compare"
            clip: true
            TextArea {
                objectName: "gitDiffText"
                text: dialog.controller ? dialog.controller.detailText : ""
                textFormat: TextEdit.PlainText; readOnly: true; selectByMouse: true; wrapMode: TextEdit.NoWrap
                color: AppTheme.text; selectionColor: AppTheme.accent; selectedTextColor: AppTheme.highlightedText
                font.family: "monospace"; font.pixelSize: 12
                background: Rectangle { color: AppTheme.surfaceRaised; radius: 9 }
            }
        }
        ColumnLayout {
            visible: dialog.mode === "upstream"
            Layout.fillWidth: true; Layout.fillHeight: true
            GText { Layout.fillWidth: true; text: dialog.tr ? "Seçtiğin mevcut uzak dal, bu dalın takip ettiği upstream olarak kaydedilecek. Fetch, Push veya Pull otomatik başlatılmaz." : "The selected existing remote branch will become this branch's upstream. This does not start Fetch, Push or Pull."; wrapMode: Text.Wrap; basePixelSize: 12; baseColor: AppTheme.textMuted }
            GComboBox { id: upstreamChoice; objectName: "gitUpstreamChoice"; Layout.fillWidth: true; model: dialog.controller ? dialog.controller.upstreamTargets : []; textRole: "label" }
            GModalButton { text: dialog.tr ? "Upstream olarak ayarla" : "Set as upstream"; primary: true; enabled: !dialog.working && dialog.controller !== null && !dialog.controller.loading && upstreamChoice.currentIndex >= 0; onClicked: dialog.controller.setUpstream(dialog.controller.upstreamTargets[upstreamChoice.currentIndex].ref) }
            Item { Layout.fillHeight: true }
        }
        ColumnLayout {
            Layout.fillWidth: true; Layout.fillHeight: true
            visible: dialog.mode === "tags"
            spacing: 8
            GText {
                Layout.fillWidth: true
                text: dialog.tagInfo.latest
                    ? (dialog.tr ? "Son kararlı sürüm: " : "Latest stable release: ") + dialog.tagInfo.latest
                    : (dialog.tr ? "Sürüm etiketi bulunamadı; başlangıç önerisi varsayılandır." : "No version tag found; the initial suggestion is a default.")
                baseColor: AppTheme.text; basePixelSize: 12
            }
            GText {
                Layout.fillWidth: true
                text: dialog.tagReady
                    ? (dialog.tr ? "Yerel ve uzak tag'ler doğrulandı." : "Local and remote tags verified.")
                    : (dialog.tr ? "Uzak tag'ler doğrulanamadı: " : "Remote tags not verified: ") + (dialog.tagInfo.remoteError || "")
                wrapMode: Text.Wrap; basePixelSize: 11; baseColor: dialog.tagReady ? AppTheme.success : AppTheme.warning
            }
            ListView {
                id: tagList
                Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 70
                clip: true; spacing: 3; model: dialog.controller ? dialog.controller.tags : []
                ScrollBar.vertical: ScrollBar {}
                delegate: Rectangle {
                    required property var modelData
                    width: tagList.width; height: 43; radius: 8; color: AppTheme.surfaceRaised
                    RowLayout {
                        anchors.fill: parent; anchors.margins: 8; spacing: 9
                        GText { Layout.preferredWidth: 125; text: modelData.name; baseBold: true; baseColor: AppTheme.text }
                        GText {
                            Layout.fillWidth: true
                            text: (modelData.local ? (dialog.tr ? "Yerel" : "Local") : "")
                                + (modelData.local && modelData.remote ? " · " : "")
                                + (modelData.remote ? (dialog.tr ? "Uzak" : "Remote") : "")
                                + " · " + (modelData.commit || "").slice(0, 10)
                            baseColor: modelData.mismatch ? AppTheme.warning : AppTheme.textMuted; basePixelSize: 11
                        }
                        GModalButton {
                            text: dialog.tr ? "Gönder" : "Push"
                            visible: modelData.local && !modelData.remote && dialog.tagReady
                            enabled: !dialog.working
                            onClicked: dialog.controller.pushTag(modelData.name)
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                GComboBox {
                    id: versionBump
                    objectName: "gitVersionBump"
                    Layout.preferredWidth: 155
                    model: ["Patch", "Minor", "Major"]
                    onActivated: dialog.applyTagSuggestion()
                }
                GTextField {
                    id: independentTagName
                    objectName: "gitIndependentTagName"
                    Layout.fillWidth: true
                    placeholderText: "v1.6.0"
                    enabled: !dialog.working
                }
            }
            GCheckBox {
                id: standalonePush
                objectName: "gitStandaloneTagPush"
                text: dialog.tr ? "Tag'i uzak depoya gönder" : "Push tag to remote"
                checked: false; enabled: !dialog.working && dialog.tagReady
            }
            GModalButton {
                objectName: "gitIndependentTagSubmit"
                text: dialog.tr ? "Açıklamalı tag oluştur" : "Create annotated tag"
                primary: true
                enabled: !dialog.working && dialog.controller !== null && !dialog.controller.loading
                    && dialog.validTag(independentTagName.text) && (!standalonePush.checked || dialog.tagReady)
                    && !(dialog.controller.tags || []).some(function(row) { return row.name === independentTagName.text })
                onClicked: dialog.controller.createTag(independentTagName.text, standalonePush.checked)
            }
        }
        ColumnLayout {
            Layout.fillWidth: true; visible: dialog.mode === "commit"
            GText { Layout.fillWidth: true; text: dialog.tr ? "Seçilen dosyaların çalışma kopyası commit edilir; diğer staged dosyalar korunur. Commit hook'ları çalıştırılmaz; yapılandırılmış imzalama kullanılır." : "Selected working copies are committed; other staged files are preserved. Commit hooks are not run; configured signing is used."; wrapMode: Text.Wrap; basePixelSize: 10; baseColor: AppTheme.textMuted }
            ScrollView {
                Layout.fillWidth: true; Layout.preferredHeight: 82; clip: true
                TextArea {
                    id: messageInput
                    objectName: "gitCommitMessage"
                    enabled: !dialog.working
                    textFormat: TextEdit.PlainText; selectByMouse: true; wrapMode: TextEdit.Wrap
                    placeholderText: dialog.tr ? "Commit mesajı…" : "Commit message…"
                    color: AppTheme.text; placeholderTextColor: AppTheme.textFaint
                    selectionColor: AppTheme.accent; selectedTextColor: AppTheme.highlightedText
                    font.family: AppTheme.typographyFamily("body"); font.pixelSize: AppTheme.typographySize("body", 12)
                    background: Rectangle { color: AppTheme.surfaceRaised; radius: 9; border.color: AppTheme.border }
                }
            }
            GCheckBox {
                id: pushAfter; objectName: "gitPushAfterCommit"
                text: dialog.tr ? "Commit sonrası gönder" : "Push after commit"
                checked: false; enabled: !dialog.working
                onCheckedChanged: if (!checked) tagAfter.checked = false
            }
            GCheckBox {
                id: tagAfter; objectName: "gitTagAfterCommit"
                text: dialog.tr ? "Yeni sürüm tag'i oluştur ve gönder" : "Create and push a version tag"
                checked: false; enabled: !dialog.working
                onCheckedChanged: if (checked) { pushAfter.checked = true; dialog.controller.loadTags(); commitTagName.text = "" }
            }
            RowLayout {
                visible: tagAfter.checked
                Layout.fillWidth: true
                GComboBox {
                    id: commitBump; objectName: "gitCommitVersionBump"
                    Layout.preferredWidth: 155
                    model: ["Patch", "Minor", "Major"]
                    onActivated: {
                        const values = [dialog.tagInfo.patch || "", dialog.tagInfo.minor || "", dialog.tagInfo.major || ""]
                        commitTagName.text = values[currentIndex]
                    }
                }
                GTextField {
                    id: commitTagName; objectName: "gitCommitTagName"
                    Layout.fillWidth: true
                    enabled: !dialog.working
                    placeholderText: "v1.6.0"
                }
            }
            GText {
                Layout.fillWidth: true; visible: tagAfter.checked
                text: dialog.tagReady
                    ? (dialog.tr ? "Uzak tag'ler doğrulandı. Commit, ardından dal ve yalnızca yeni tag gönderilecek." : "Remote tags verified. Commit, branch, then only the new tag will be pushed.")
                    : (dialog.tr ? "Uzak tag'ler henüz doğrulanmadı. Gönderim için doğrulama gerekli." : "Remote tags are not verified yet. Verification is required before publishing.")
                basePixelSize: 10; baseColor: dialog.tagReady ? AppTheme.success : AppTheme.warning; wrapMode: Text.Wrap
            }
        }
        RowLayout {
            Layout.fillWidth: true
            GText { visible: dialog.mode === "commit"; text: dialog.selectedCount + (dialog.tr ? " dosya seçili" : " files selected"); baseColor: AppTheme.textMuted; basePixelSize: 11 }
            Item { Layout.fillWidth: true }
            GModalButton { text: dialog.tr ? "İptal et" : "Cancel operation"; visible: dialog.working; onClicked: dialog.controller.cancel() }
            GModalButton { text: dialog.tr ? "Yenile" : "Refresh"; enabled: !dialog.working && dialog.controller !== null && !dialog.controller.loading; onClicked: { dialog.resultMessage = ""; dialog.controller.refresh(); dialog.changeMode(dialog.mode) } }
            GModalButton { text: dialog.tr ? "Kapat" : "Close"; enabled: !dialog.working; onClicked: dialog.close() }
            GModalButton {
                objectName: "gitCommitSubmit"
                visible: dialog.mode === "commit"; primary: true
                text: dialog.tr ? "Commit oluştur" : "Create commit"
                enabled: !dialog.working && dialog.controller !== null && !dialog.controller.loading && dialog.selectedCount > 0 && messageInput.text.trim().length > 0
                    && (!tagAfter.checked || (dialog.tagReady && dialog.validTag(commitTagName.text)
                        && !(dialog.controller.tags || []).some(function(row) { return row.name === commitTagName.text })))
                onClicked: dialog.controller.commit(Object.keys(dialog.selected), messageInput.text, pushAfter.checked, tagAfter.checked ? commitTagName.text : "")
            }
        }
    }
}
