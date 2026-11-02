import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lurviko.App

// One renderer shared by file jobs and private-vault transfers. The card UI
// and completion animation originate from BrowserPane's existing operationCards.
Item {
    id: toastRoot
    required property var lang
    required property var presentation
    property var backdropSource: null
    property var operationSources: []
    property var vault: null
    property int bottomMargin: 42
    // QQuickItem already has a `layer` property: never use that name as
    // an id. In a Repeater delegate it resolves to the native item layer,
    // not to this component, and breaks every operation-card binding.
    readonly property var visibleJobs: presentation && presentation.visibleOperations
        ? presentation.visibleOperations : []
    readonly property bool cardsVisible: visibleJobs.length > 0
    readonly property bool cardsHovered: operationCardsHover.hovered

    function sourceFor(op) {
        const index = op.sourceIndex === undefined ? 0 : Number(op.sourceIndex)
        return op.kind !== "vault" && index >= 0 && index < operationSources.length
            ? operationSources[index] : null
    }
    function dismissOperation(op) {
        if (op.kind === "vault") {
            vaultDismissed()
            return
        }
        const source = sourceFor(op)
        if (source) source.dismissOperation(op.sourceId === undefined ? op.id : op.sourceId)
    }
    function setDismissPaused(op, paused) {
        const source = sourceFor(op)
        if (source) source.setOperationDismissPaused(op.sourceId === undefined ? op.id : op.sourceId, paused)
    }
    function toggleOperationPause(op) {
        const source = sourceFor(op)
        if (source) source.toggleOperationPause(op.sourceId === undefined ? op.id : op.sourceId)
    }
    function cancelOperation(op) {
        const source = sourceFor(op)
        if (source) source.cancelOperation(op.sourceId === undefined ? op.id : op.sourceId)
    }
    signal vaultDismissed()
    function formatTransferBytes(value) {
        const bytes = Math.max(0, Number(value || 0))
        if (bytes < 1024) return Math.round(bytes) + " B"
        if (bytes < 1048576) return (bytes / 1024).toFixed(1) + " KiB"
        if (bytes < 1073741824) return (bytes / 1048576).toFixed(1) + " MiB"
        return (bytes / 1073741824).toFixed(2) + " GiB"
    }
    function formatRemaining(seconds) {
        const value = Math.max(0, Math.ceil(Number(seconds || 0)))
        if (value < 60) return value + (lang.language === "tr" ? " sn" : " sec")
        if (value < 3600) return Math.floor(value / 60) + (lang.language === "tr" ? " dk" : " min")
            + " " + (value % 60) + (lang.language === "tr" ? " sn" : " sec")
        return Math.floor(value / 3600) + (lang.language === "tr" ? " sa" : " hr")
            + " " + Math.floor((value % 3600) / 60) + (lang.language === "tr" ? " dk" : " min")
    }
    function transferPercent(op) {
        const reported = Number(op.percent === undefined ? -1 : op.percent)
        if (reported >= 0) return Math.max(0, Math.min(100, reported))
        const total = Number(op.totalBytes || 0)
        return total > 0 ? Math.max(0, Math.min(100, Number(op.processedBytes || 0) * 100 / total)) : -1
    }
    function transferMetrics(op) {
        const parts = []
        const count = Number(op.processedItems || 0)
        const total = Number(op.totalItems || 0)
        if (total > 0 && count <= total)
            parts.push(count + " / " + total + (lang.language === "tr" ? " öğe" : " items"))
        else if (count > 0)
            parts.push(count + (lang.language === "tr" ? " öğe işlendi" : " items processed"))
        const bytes = Number(op.processedBytes || 0)
        const totalBytes = Number(op.totalBytes || 0)
        if (totalBytes > 0) parts.push(formatTransferBytes(bytes) + " / " + formatTransferBytes(totalBytes))
        else if (bytes > 0) parts.push(formatTransferBytes(bytes))
        if (parts.length) return parts.join("  ·  ")
        if (op.kind === "vault") return lang.language === "tr" ? "Şifreleme hazırlanıyor…" : "Preparing encryption…"
        if (op.kind === "extract") return lang.language === "tr" ? "Arşiv içeriği çıkarılıyor…" : "Extracting archive contents…"
        if (op.kind === "clipboard") return lang.language === "tr" ? "Dosya listesi panoya hazırlanıyor…" : "Preparing clipboard…"
        return lang.language === "tr" ? "İşlem hazırlanıyor…" : "Preparing operation…"
    }
    function transferRemaining(op) {
        if (op.kind === "vault") return op.phaseLabel || (lang.language === "tr" ? "Dosyalar şifreleniyor" : "Encrypting files")
        if (op.kind === "cloud") return op.phaseLabel || (lang.language === "tr" ? "Bulut aktarımı sürüyor" : "Cloud transfer in progress")
        if (op.paused) return lang.language === "tr" ? "Duraklatıldı" : "Paused"
        if (op.kind === "clipboard") return lang.language === "tr" ? "Dosya listesi hazırlanıyor" : "Preparing file list"
        if (op.kind === "trash" && Number(op.speed || 0) <= 0)
            return lang.language === "tr" ? "Dosyalar çöpe hazırlanıyor…" : "Preparing files for Trash…"
        const speed = Number(op.speed || 0)
        const remaining = Number(op.totalBytes || 0) - Number(op.processedBytes || 0)
        if (speed <= 0 || remaining <= 0)
            return lang.language === "tr" ? "Kalan süre hesaplanıyor" : "Calculating remaining time"
        return (lang.language === "tr" ? "Kalan " : "Remaining ") + formatRemaining(remaining / speed)
    }
    Column {
        id: operationCards
        objectName: "operationCards"
        visible: toastRoot.cardsVisible
        z: 30000
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 14
        anchors.bottomMargin: toastRoot.bottomMargin
        width: Math.min(350, toastRoot.width - 28)
        spacing: 8

        HoverHandler { id: operationCardsHover }
        Repeater {
            model: Math.min(3, toastRoot.visibleJobs.length)
            delegate: GOverlaySurface {
                id: operationCard
                sourceItem: toastRoot.backdropSource
                required property int index
                readonly property var operation: toastRoot.visibleJobs[Math.max(0, toastRoot.visibleJobs.length - 3) + index] || ({id: -1, finished: false, detail: ""})
                readonly property real shownPercent: toastRoot.transferPercent(operation)
                property real completionCountdown: 1.0
                property string trackedOperationId: ""
                property bool completionInitialized: false
                width: operationCards.width
                height: operation.finished ? 108 : (operation.controllable === false ? 140 : 174)

                function syncCompletionState() {
                    const currentId = String(operation && operation.id !== undefined ? operation.id : "")
                    if (currentId !== trackedOperationId) {
                        completionCountdownAnimation.stop()
                        completionCountdown = 1.0
                        completionInitialized = false
                        trackedOperationId = currentId
                    }
                    if (!operation || !operation.finished) {
                        completionCountdownAnimation.stop()
                        completionCountdown = 1.0
                        completionInitialized = false
                        return
                    }
                    if (completionInitialized)
                        return
                    completionInitialized = true
                    completionCountdown = 1.0
                    if (cardHover.hovered) {
                        toastRoot.setDismissPaused(operation, true)
                    } else {
                        completionCountdownAnimation.restart()
                    }
                }

                onOperationChanged: syncCompletionState()
                Component.onCompleted: syncCompletionState()

                NumberAnimation {
                    id: completionCountdownAnimation
                    target: operationCard
                    property: "completionCountdown"
                    from: 1.0
                    to: 0.0
                    duration: 5000
                    easing.type: Easing.Linear
                    onFinished: {
                        if (operationCard.operation.kind === "vault" && operationCard.operation.finished)
                            toastRoot.dismissOperation(operationCard.operation)
                    }
                }

                HoverHandler {
                    id: cardHover
                    onHoveredChanged: {
                        if (!operation || !operation.finished)
                            return
                        completionCountdownAnimation.stop()
                        completionCountdown = 1.0
                        toastRoot.setDismissPaused(operation, hovered)
                        if (!hovered)
                            completionCountdownAnimation.restart()
                    }
                }

                Behavior on height { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                radius: 14
                customColor: cardHover.hovered && operation.finished ? AppTheme.surfaceHover : AppTheme.surface
                border.color: cardHover.hovered && operation.finished ? AppTheme.accentBorder : AppTheme.borderStrong
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        GText {
                            Layout.fillWidth: true
                            baseColor: AppTheme.text
                            basePixelSize: 12
                            baseBold: true
                            text: operation.kind === "cloud" ? operation.title
                                  : operation.kind === "vault"
                                  ? (operation.finished
                                     ? (operation.success ? (toastRoot.lang.language === "tr" ? "Kasaya gönderildi" : "Sent to vault")
                                          : (toastRoot.lang.language === "tr" ? "Kasaya gönderilemedi" : "Vault transfer failed"))
                                     : (toastRoot.lang.language === "tr" ? "Kasaya gönderiliyor" : "Sending to vault"))
                                  : operation.finished
                                  ? (operation.success
                                     ? (operation.kind === "copy" ? (lang.language === "tr" ? "Kopyalama tamamlandı" : "Copy complete")
                                        : operation.kind === "clipboard" ? (lang.language === "tr" ? "Panoya hazırlandı" : "Clipboard ready")
                                        : operation.kind === "move" ? (lang.language === "tr" ? "Taşıma tamamlandı" : "Move complete")
                                        : operation.kind === "trash" ? (lang.language === "tr" ? "Çöpe taşıma tamamlandı" : "Moved to Trash")
                                        : operation.kind === "duplicate" ? (lang.language === "tr" ? "Klonlama tamamlandı" : "Duplication complete")
                                        : operation.kind === "delete" ? (lang.language === "tr" ? "Silme tamamlandı" : "Deletion complete")
                                        : operation.kind === "restore" ? (lang.language === "tr" ? "Geri yükleme tamamlandı" : "Restore complete")
                                        : operation.kind === "rename" ? (lang.language === "tr" ? "Yeniden adlandırıldı" : "Renamed")
                                        : operation.kind === "inspect" ? (lang.language === "tr" ? "Arşiv incelendi" : "Archive inspected")
                                        : operation.kind === "compress" ? (lang.language === "tr" ? "Arşiv oluşturuldu" : "Archive created")
                                        : (lang.language === "tr" ? "Arşiv çıkarıldı" : "Extraction complete"))
                                     : operation.cancelled ? (lang.language === "tr" ? "İptal edildi" : "Cancelled")
                                     : (lang.language === "tr" ? "İşlem tamamlanamadı" : "Operation failed"))
                                  : operation.kind === "copy" ? (lang.language === "tr" ? "Kopyalanıyor" : "Copying")
                                  : operation.kind === "clipboard" ? (lang.language === "tr" ? "Pano hazırlanıyor" : "Preparing clipboard")
                                  : operation.kind === "move" ? (lang.language === "tr" ? "Taşınıyor" : "Moving")
                                  : operation.kind === "trash" ? (lang.language === "tr" ? "Çöpe taşınıyor" : "Moving to Trash")
                                  : operation.kind === "duplicate" ? (lang.language === "tr" ? "Klonlanıyor" : "Duplicating")
                                  : operation.kind === "delete" ? (lang.language === "tr" ? "Siliniyor" : "Deleting")
                                  : operation.kind === "restore" ? (lang.language === "tr" ? "Geri yükleniyor" : "Restoring")
                                  : operation.kind === "rename" ? (lang.language === "tr" ? "Yeniden adlandırılıyor" : "Renaming")
                                  : operation.kind === "inspect" ? (lang.language === "tr" ? "Arşiv inceleniyor" : "Inspecting archive")
                                  : operation.kind === "compress" ? (lang.language === "tr" ? "Arşiv oluşturuluyor" : "Creating archive")
                                  : (lang.language === "tr" ? "Arşiv çıkarılıyor" : "Extracting archive")
                        }
                        GText {
                            text: operation.finished ? (operation.success ? "✓" : "!")
                                                     : (shownPercent >= 0 ? Math.round(shownPercent) + "%" : "…")
                            baseColor: operation.finished ? (operation.success ? AppTheme.success : AppTheme.danger) : AppTheme.accent
                            basePixelSize: 11
                            baseBold: true
                        }
                        GToolButton {
                            objectName: "operationCardMinimize"
                            visible: !operation.finished && (!AppTheme.kioProgressEnabled || operation.kind === "vault" || operation.kind === "cloud")
                            implicitWidth: 24; implicitHeight: 24
                            text: "−"
                            onClicked: toastRoot.presentation.minimize()
                            GToolTip { visible: parent.hovered; text: lang.t("operation_minimize") }
                        }
                        Rectangle {
                            id: dismissButton
                            visible: operation.finished
                            Layout.preferredWidth: 24
                            Layout.preferredHeight: 24
                            radius: 8
                            color: dismissHover.hovered ? AppTheme.surfaceHover : "transparent"
                            border.width: dismissHover.hovered ? 1 : 0
                            border.color: AppTheme.accentBorder

                            CrispIcon {
                                anchors.centerIn: parent
                                width: 15
                                height: 15
                                source: AppTheme.icon("close-ui.svg")
                                opacity: dismissHover.hovered ? 1.0 : 0.82
                            }
                            HoverHandler { id: dismissHover }
                            TapHandler {
                                acceptedButtons: Qt.LeftButton
                                onTapped: toastRoot.dismissOperation(operation)
                            }
                        }
                    }
                    GText {
                        Layout.fillWidth: true
                        text: operation.detail
                        baseColor: AppTheme.textMuted
                        basePixelSize: 11
                        elide: Text.ElideMiddle
                    }
                    GText {
                        Layout.fillWidth: true
                        text: toastRoot.transferMetrics(operation)
                        baseColor: AppTheme.text
                        basePixelSize: 10
                        elide: Text.ElideRight
                    }
                    Rectangle {
                        id: progressTrack
                        Layout.fillWidth: true
                        Layout.preferredHeight: 7
                        radius: 4
                        color: AppTheme.surfaceHover
                        clip: true
                        Rectangle {
                            property real indeterminateX: 0
                            x: !operation.finished && shownPercent < 0 ? indeterminateX : 0
                            width: operation.finished ? progressTrack.width * completionCountdown
                                  : shownPercent < 0 ? progressTrack.width * 0.28
                                  : progressTrack.width * shownPercent / 100
                            height: parent.height
                            radius: parent.radius
                            color: operation.finished ? (operation.success ? AppTheme.success : AppTheme.danger) : AppTheme.accent
                            SequentialAnimation on indeterminateX {
                                running: !operation.finished && shownPercent < 0 && !operation.paused
                                loops: Animation.Infinite
                                NumberAnimation { from: 0; to: progressTrack.width * 0.72; duration: 1050; easing.type: Easing.InOutSine }
                                NumberAnimation { from: progressTrack.width * 0.72; to: 0; duration: 1050; easing.type: Easing.InOutSine }
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: !operation.finished
                        GText {
                            Layout.fillWidth: true
                            text: toastRoot.transferRemaining(operation)
                            baseColor: AppTheme.textMuted
                            basePixelSize: 10
                            elide: Text.ElideRight
                        }
                        GText {
                            visible: !operation.paused && Number(operation.speed || 0) > 0
                            text: toastRoot.formatTransferBytes(operation.speed) + (lang.language === "tr" ? "/sn" : "/s")
                            baseColor: AppTheme.text
                            basePixelSize: 10
                            baseBold: true
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: !operation.finished && operation.controllable !== false
                        Item { Layout.fillWidth: true }
                        GButton {
                            visible: operation.canPause !== false
                            text: operation.paused ? (lang.language === "tr" ? "Sürdür" : "Resume")
                                                   : (lang.language === "tr" ? "Duraklat" : "Pause")
                            onClicked: toastRoot.toggleOperationPause(operation)
                        }
                        GButton {
                            visible: operation.canCancel !== false
                            text: lang.language === "tr" ? "İptal" : "Cancel"
                            onClicked: toastRoot.cancelOperation(operation)
                        }
                    }
                }
            }
        }
    }
}
