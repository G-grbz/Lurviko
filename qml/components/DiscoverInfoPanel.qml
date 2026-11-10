import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lurviko.App
import Lurviko.Backend

// Discover information panel: live, actionable status instead of static
// feature lists. Storage gauges, housekeeping (trash and preview cache),
// content index, connections, running operations and the release state.
Rectangle {
    id: panel
    required property var home
    required property var lang
    required property var storageModel
    required property var contentIndexModel
    property var updateChecker: null
    property var operationPresentation: null
    readonly property bool tr: lang.language === "tr"
    property int homeStorageRevision: 0
    property bool confirmEmptyTrash: false

    objectName: "discoverInfoPanel"
    radius: AppTheme.panelRadius
    color: AppTheme.cardSurface
    border.color: AppTheme.border

    function t(trText, enText) { return tr ? trText : enText }
    function refresh() {
        housekeeping.refresh()
        homeStorageRevision += 1
    }
    onVisibleChanged: if (visible) refresh()
    Component.onCompleted: if (visible) refresh()

    StorageHousekeeping { id: housekeeping }
    FileOperations { id: trashOperations }
    TrashMonitor { id: trashMonitor; onHasItemsChanged: if (panel.visible) housekeeping.refresh() }
    Connections {
        target: trashOperations
        function onOperationFinished() { housekeeping.refresh() }
    }
    Timer { id: confirmReset; interval: 3000; onTriggered: panel.confirmEmptyTrash = false }

    readonly property real homeTotal: { homeStorageRevision; return contentIndexModel.homeStorageTotalBytes() }
    readonly property real homeFree: { homeStorageRevision; return contentIndexModel.homeStorageAvailableBytes() }

    component PanelCard: Rectangle {
        default property alias content: cardColumn.data
        property string title
        property string iconName
        Layout.fillWidth: true
        implicitHeight: cardColumn.implicitHeight + 28
        radius: 16
        color: AppTheme.dark ? Qt.rgba(1, 1, 1, 0.03) : AppTheme.surfaceRaised
        border.color: AppTheme.border
        ColumnLayout {
            id: cardColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 14
            spacing: 10
            RowLayout {
                spacing: 8
                CrispIcon {
                    visible: iconName.length > 0
                    Layout.preferredWidth: 16; Layout.preferredHeight: 16
                    source: iconName.length > 0 ? AppTheme.icon(iconName, AppTheme.textMuted) : ""
                }
                GText {
                    text: title
                    baseColor: AppTheme.textMuted
                    basePixelSize: 10
                    baseFontWeight: Font.DemiBold
                    baseCapitalization: Font.AllUppercase
                    baseLetterSpacing: 0.8
                }
            }
        }
    }

    component PanelAction: AbstractButton {
        id: action
        property bool danger: false
        implicitHeight: 28
        implicitWidth: actionLabel.implicitWidth + 22
        hoverEnabled: true
        HoverHandler { cursorShape: action.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
        opacity: enabled ? 1 : 0.45
        background: Rectangle {
            radius: height / 2
            color: action.pressed || action.hovered ? (action.danger ? Qt.rgba(AppTheme.danger.r, AppTheme.danger.g, AppTheme.danger.b, 0.14) : AppTheme.accentSoft)
                                                    : "transparent"
            border.width: 1
            border.color: action.danger ? AppTheme.danger : AppTheme.accentBorder
        }
        contentItem: GText {
            id: actionLabel
            text: action.text
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            baseColor: action.danger ? AppTheme.danger : AppTheme.accent
            basePixelSize: 10
            baseFontWeight: Font.DemiBold
        }
    }

    Flickable {
        id: panelScroll
        anchors.fill: parent
        anchors.margins: 16
        clip: true
        contentWidth: width
        contentHeight: panelColumn.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        GPopupWheelScroll {
            parent: panelScroll
            anchors.fill: parent
            view: panelScroll
            enabled: panel.visible
        }

        ColumnLayout {
            id: panelColumn
            width: panelScroll.width
            spacing: 12

            // ---- Storage --------------------------------------------------
            PanelCard {
                objectName: "infoStorageCard"
                title: panel.t("Depolama", "Storage")
                iconName: "drive-symbolic.svg"
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 14
                    StorageGauge {
                        objectName: "infoStorageGauge"
                        Layout.preferredWidth: 104; Layout.preferredHeight: 104
                        value: storageModel.totalBytes > 0 ? storageModel.usedBytes / storageModel.totalBytes : 0
                        captionText: panel.t("dolu", "used")
                        centerText: panel.tr ? "%" + Math.round(clamped * 100) : Math.round(clamped * 100) + "%"
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 8
                        rowSpacing: 5
                        GText { text: panel.t("Kullanılan", "Used"); baseColor: AppTheme.textMuted; basePixelSize: 10 }
                        GText { Layout.alignment: Qt.AlignRight; text: storageModel.formatBytes(storageModel.usedBytes); baseColor: AppTheme.text; basePixelSize: 11; baseFontWeight: Font.DemiBold }
                        GText { text: panel.t("Boş", "Free"); baseColor: AppTheme.textMuted; basePixelSize: 10 }
                        GText { Layout.alignment: Qt.AlignRight; text: storageModel.formatBytes(Math.max(0, storageModel.totalBytes - storageModel.usedBytes)); baseColor: AppTheme.text; basePixelSize: 11 }
                        GText { text: panel.t("Toplam", "Total"); baseColor: AppTheme.textMuted; basePixelSize: 10 }
                        GText { Layout.alignment: Qt.AlignRight; text: storageModel.formatBytes(storageModel.totalBytes); baseColor: AppTheme.text; basePixelSize: 11 }
                        GText { text: panel.t("Disk", "Disks"); baseColor: AppTheme.textMuted; basePixelSize: 10 }
                        GText { Layout.alignment: Qt.AlignRight; text: String(storageModel.count); baseColor: AppTheme.text; basePixelSize: 11 }
                    }
                }
                ColumnLayout {
                    objectName: "infoHomeDisk"
                    visible: panel.homeTotal > 0
                    Layout.fillWidth: true
                    spacing: 5
                    RowLayout {
                        Layout.fillWidth: true
                        GText { Layout.fillWidth: true; text: panel.t("Ev dizininin diski", "Home folder disk"); baseColor: AppTheme.text; basePixelSize: 11 }
                        GText {
                            text: storageModel.formatBytes(panel.homeFree) + panel.t(" boş", " free")
                            baseColor: AppTheme.textMuted; basePixelSize: 10
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 6
                        radius: 3
                        color: AppTheme.dark ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(0.05, 0.08, 0.19, 0.08)
                        Rectangle {
                            readonly property real ratio: panel.homeTotal > 0 ? 1 - panel.homeFree / panel.homeTotal : 0
                            width: parent.width * Math.max(0, Math.min(1, ratio))
                            height: parent.height
                            radius: 3
                            color: ratio >= 0.92 ? AppTheme.danger : ratio >= 0.8 ? "#E8A23A" : AppTheme.accent
                        }
                    }
                }
            }

            // ---- Housekeeping --------------------------------------------
            PanelCard {
                objectName: "infoHousekeepingCard"
                title: panel.t("Temizlik", "Cleanup")
                iconName: "delete.svg"
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    ColumnLayout {
                        spacing: 1
                        GText { text: panel.t("Çöp kutusu", "Trash"); baseColor: AppTheme.text; basePixelSize: 12 }
                        GText {
                            objectName: "infoTrashUsage"
                            text: !housekeeping.measured ? panel.t("Hesaplanıyor…", "Measuring…")
                                  : housekeeping.trashItems === 0 ? panel.t("Boş", "Empty")
                                  : housekeeping.trashItems + panel.t(" öğe · ", " items · ") + storageModel.formatBytes(housekeeping.trashBytes)
                            baseColor: AppTheme.textMuted; basePixelSize: 10
                        }
                    }
                    Item { Layout.fillWidth: true }
                    PanelAction {
                        objectName: "infoEmptyTrashButton"
                        danger: true
                        enabled: housekeeping.trashItems > 0
                        text: panel.confirmEmptyTrash ? panel.t("Emin misin?", "Sure?") : panel.t("Boşalt", "Empty")
                        onClicked: {
                            if (!panel.confirmEmptyTrash) {
                                panel.confirmEmptyTrash = true
                                confirmReset.restart()
                                return
                            }
                            panel.confirmEmptyTrash = false
                            confirmReset.stop()
                            trashOperations.emptyTrash()
                        }
                    }
                }
                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: AppTheme.border }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    ColumnLayout {
                        spacing: 1
                        GText { text: panel.t("Önizleme önbelleği", "Preview cache"); baseColor: AppTheme.text; basePixelSize: 12 }
                        GText {
                            objectName: "infoPreviewCacheUsage"
                            text: !housekeeping.measured ? panel.t("Hesaplanıyor…", "Measuring…")
                                  : housekeeping.previewCacheFiles + panel.t(" dosya · ", " files · ") + storageModel.formatBytes(housekeeping.previewCacheBytes)
                            baseColor: AppTheme.textMuted; basePixelSize: 10
                        }
                    }
                    Item { Layout.fillWidth: true }
                    PanelAction {
                        objectName: "infoClearPreviewCacheButton"
                        enabled: housekeeping.previewCacheFiles > 0 && !housekeeping.busy
                        text: panel.t("Temizle", "Clear")
                        onClicked: housekeeping.clearPreviewCache()
                    }
                }
            }

            // ---- Content index -------------------------------------------
            PanelCard {
                objectName: "infoIndexCard"
                title: panel.t("İçerik indeksi", "Content index")
                iconName: "search.svg"
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    ColumnLayout {
                        spacing: 1
                        GText {
                            text: contentIndexModel.totalIndexed.toLocaleString(Qt.locale(), "f", 0) + panel.t(" dosya", " files")
                            baseColor: AppTheme.text; basePixelSize: 12
                        }
                        GText {
                            text: contentIndexModel.scanProgressVisible
                                  ? panel.t("Taranıyor · %", "Scanning · ") + contentIndexModel.scanProgressPercent + panel.t("", "%")
                                  : contentIndexModel.indexing ? panel.t("Güncelleniyor…", "Updating…") : panel.t("Güncel", "Up to date")
                            baseColor: contentIndexModel.indexing ? AppTheme.accent : AppTheme.textMuted
                            basePixelSize: 10
                        }
                    }
                    Item { Layout.fillWidth: true }
                    PanelAction {
                        enabled: !contentIndexModel.indexing
                        text: panel.t("Yeniden tara", "Rescan")
                        onClicked: contentIndexModel.refresh()
                    }
                }
                GProgressBar {
                    visible: contentIndexModel.scanProgressVisible
                    Layout.fillWidth: true
                    from: 0; to: 100
                    value: contentIndexModel.scanProgressPercent
                }
            }

            // ---- Connections ---------------------------------------------
            PanelCard {
                objectName: "infoConnectionsCard"
                visible: home.cloudTabs.length > 0
                title: panel.t("Bağlantılar", "Connections")
                iconName: "network-symbolic.svg"
                Repeater {
                    model: home.cloudTabs
                    delegate: RowLayout {
                        required property var modelData
                        readonly property bool ready: panel.home.cloudReady(modelData.key)
                        Layout.fillWidth: true
                        spacing: 9
                        Rectangle {
                            Layout.preferredWidth: 8; Layout.preferredHeight: 8
                            radius: 4
                            color: parent.ready ? "#34A853" : AppTheme.textFaint
                        }
                        GText { text: modelData.title; baseColor: AppTheme.text; basePixelSize: 11 }
                        GText {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignRight
                            text: panel.home.cloudStatus(modelData.key)
                            baseColor: AppTheme.textMuted
                            basePixelSize: 9
                            elide: Text.ElideRight
                        }
                    }
                }
            }

            // ---- Running operations --------------------------------------
            PanelCard {
                objectName: "infoOperationsCard"
                visible: !!panel.operationPresentation && panel.operationPresentation.activeCount > 0
                title: panel.t("Süren işlemler", "Running operations")
                iconName: "edit-copy.svg"
                Repeater {
                    model: panel.operationPresentation ? panel.operationPresentation.activeOperations : []
                    delegate: ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 4
                        RowLayout {
                            Layout.fillWidth: true
                            GText {
                                Layout.fillWidth: true
                                text: modelData.phaseLabel || modelData.kind || ""
                                baseColor: AppTheme.text; basePixelSize: 11
                                elide: Text.ElideRight
                            }
                            GText {
                                text: Math.round(Number(modelData.percent || 0)) + "%"
                                baseColor: AppTheme.textMuted; basePixelSize: 10
                            }
                        }
                        GProgressBar {
                            Layout.fillWidth: true
                            from: 0; to: 100
                            value: Number(modelData.percent || 0)
                        }
                    }
                }
            }

            // ---- Release -------------------------------------------------
            Rectangle {
                objectName: "updateReleaseCard"
                visible: panel.updateChecker !== null && panel.updateChecker.updateAvailable
                Layout.fillWidth: true
                implicitHeight: updateDescription.implicitHeight + 28
                radius: 16
                color: updateCardMouse.containsMouse ? AppTheme.surfaceHover : AppTheme.accentSoft
                border.color: AppTheme.accent
                ColumnLayout {
                    id: updateDescription
                    anchors.left: parent.left; anchors.right: parent.right
                    anchors.top: parent.top; anchors.margins: 14
                    spacing: 6
                    GText {
                        Layout.fillWidth: true
                        text: "Lurviko v" + (panel.updateChecker ? panel.updateChecker.latestVersion : "")
                        baseColor: AppTheme.accent; baseBold: true; basePixelSize: 12
                    }
                    GText {
                        Layout.fillWidth: true
                        text: panel.t("Yeni sürüm hazır. Yenilikleri ve kaynak kodunu GitHub’da görüntüle →",
                                      "A new release is available. View the changes and source code on GitHub →")
                        baseColor: AppTheme.text; basePixelSize: 11; wrapMode: Text.WordWrap
                    }
                }
                MouseArea {
                    id: updateCardMouse
                    anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: panel.updateChecker.openReleasePage()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 2
                spacing: 8
                CrispIcon {
                    source: AppTheme.icon("logo.png")
                    Layout.preferredWidth: 22; Layout.preferredHeight: 22
                }
                GText {
                    Layout.fillWidth: true
                    readonly property string version: (panel.updateChecker && panel.updateChecker.currentVersion)
                                                      || Qt.application.version || ""
                    text: "Lurviko" + (version.length ? " v" + version : "")
                          + (panel.updateChecker && panel.updateChecker.updateAvailable ? "" : panel.t(" · güncel", " · up to date"))
                    baseColor: AppTheme.textMuted
                    basePixelSize: 10
                    elide: Text.ElideRight
                }
            }
        }
    }
}
