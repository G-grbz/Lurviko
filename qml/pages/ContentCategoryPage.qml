import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GFile.App
import GFile.Backend
import "../components"

Item {
    id: page
    required property var contentIndexModel
    required property var lang
    required property string categoryKey
    required property string categoryTitle
    required property string categoryIcon
    signal backRequested()
    signal browseRequested(string location)

    property var files: []
    property double categoryBytes: 0
    property double homeStorageBytes: 0
    readonly property bool mediaGallery: categoryKey === "images" || categoryKey === "videos"
    readonly property real storageShare: homeStorageBytes > 0 ? Math.min(1.0, categoryBytes / homeStorageBytes) : 0

    function reloadFiles() {
        files = contentIndexModel.filesForCategory(categoryKey)
        categoryBytes = Number(contentIndexModel.bytesForCategory(categoryKey) || 0)
        homeStorageBytes = Number(contentIndexModel.homeStorageTotalBytes() || 0)
    }

    function previewable(suffix) {
        const s = (suffix || "").toLowerCase()
        return ["png", "jpg", "jpeg", "webp", "bmp", "gif", "svg", "avif",
                "mp4", "mkv", "avi", "mov", "webm", "m4v", "pdf", "mp3",
                "srt", "lrc", "lrclib", "vtt", "ass", "ssa", "txt", "md",
                "csv", "tsv"].indexOf(s) >= 0
    }

    function thumbSource(item) {
        if (!item || !item.path || !previewable(item.suffix))
            return ""
        return "image://gfilethumb/" + encodeURIComponent(item.path)
                + "|index-" + String(item.modifiedMs || 0) + "-" + String(item.size || 0)
    }

    function formatSize(bytes) {
        const n = Number(bytes || 0)
        if (n < 1024) return n + " B"
        if (n < 1024 * 1024) return (n / 1024).toFixed(1) + " KiB"
        if (n < 1024 * 1024 * 1024) return (n / (1024 * 1024)).toFixed(1) + " MiB"
        if (n < 1024 * 1024 * 1024 * 1024) return (n / (1024 * 1024 * 1024)).toFixed(1) + " GiB"
        return (n / (1024 * 1024 * 1024 * 1024)).toFixed(2) + " TiB"
    }

    Component.onCompleted: reloadFiles()
    Connections {
        target: contentIndexModel
        function onIndexChanged() { page.reloadFiles() }
    }

    FileOperations {
        id: fileOps
        kioProgressEnabled: AppTheme.kioProgressEnabled
    }

    Rectangle {
        anchors.fill: parent
        radius: Math.max(0, AppTheme.panelRadius - 1)
        color: AppTheme.workspace
        antialiasing: true
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            GToolButton {
                id: backButton
                Layout.preferredWidth: 40
                Layout.preferredHeight: 40
                onClicked: page.backRequested()
                background: Rectangle {
                    radius: 11
                    color: backButton.hovered ? AppTheme.surfaceHover : AppTheme.surface
                    border.color: AppTheme.border
                }
                text: ""
                display: AbstractButton.IconOnly
                icon.source: AppTheme.icon("nav-back.svg")
                icon.width: 16
                icon.height: 16
            }

            Rectangle {
                Layout.preferredWidth: 42
                Layout.preferredHeight: 42
                radius: 12
                color: AppTheme.accentSoft
                border.color: AppTheme.accentBorder
                CrispIcon {
                    anchors.centerIn: parent
                    width: 23; height: 23
                    source: AppTheme.customIcon(page.categoryIcon)
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                Text {
                    text: page.categoryTitle
                    color: AppTheme.text
                    font.pixelSize: 19
                    font.weight: Font.DemiBold
                }
                Text {
                    text: files.length + " " + (lang.language === "tr" ? "öğe · Ev dizini indeksi" : "items · Home index")
                    color: AppTheme.textMuted
                    font.pixelSize: 10
                }
            }

            Rectangle {
                visible: page.mediaGallery
                Layout.preferredWidth: 240
                Layout.preferredHeight: 52
                radius: 12
                color: AppTheme.surface
                border.color: AppTheme.border

                Column {
                    anchors.fill: parent
                    anchors.margins: 9
                    spacing: 5
                    Row {
                        width: parent.width
                        Text {
                            width: parent.width * 0.62
                            text: lang.language === "tr" ? "Kullanılan alan" : "Storage used"
                            color: AppTheme.textMuted
                            font.pixelSize: 9
                        }
                        Text {
                            width: parent.width * 0.38
                            text: page.formatSize(page.categoryBytes)
                            color: AppTheme.text
                            font.pixelSize: 10
                            font.weight: Font.DemiBold
                            horizontalAlignment: Text.AlignRight
                        }
                    }
                    Rectangle {
                        width: parent.width
                        height: 7
                        radius: 4
                        color: AppTheme.surfaceHover
                        clip: true
                        Rectangle {
                            width: Math.max(page.categoryBytes > 0 ? 5 : 0, parent.width * page.storageShare)
                            height: parent.height
                            radius: parent.radius
                            color: AppTheme.accent
                        }
                    }
                    Text {
                        width: parent.width
                        text: page.homeStorageBytes > 0
                              ? (lang.language === "tr"
                                 ? "Ev diskinin %" + (page.storageShare * 100).toFixed(page.storageShare < 0.01 ? 2 : 1)
                                 : (page.storageShare * 100).toFixed(page.storageShare < 0.01 ? 2 : 1) + "% of home volume")
                              : ""
                        color: AppTheme.textFaint
                        font.pixelSize: 8
                        horizontalAlignment: Text.AlignRight
                    }
                }
            }

            Text {
                visible: contentIndexModel.indexing
                text: lang.language === "tr" ? "İndeks güncelleniyor…" : "Updating index…"
                color: AppTheme.accent
                font.pixelSize: 10
            }

            GButton {
                id: refreshButton
                text: lang.language === "tr" ? "Yenile" : "Refresh"
                onClicked: contentIndexModel.refresh()
                background: Rectangle {
                    radius: 10
                    color: refreshButton.hovered ? AppTheme.surfaceHover : AppTheme.surface
                    border.color: AppTheme.border
                }
                contentItem: Text {
                    text: refreshButton.text
                    color: AppTheme.text
                    font.pixelSize: 10
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            MediaGalleryView {
                anchors.fill: parent
                visible: page.mediaGallery
                files: page.files
                lang: page.lang
                categoryKey: page.categoryKey
                categoryIcon: page.categoryIcon
                gallerySize: 168
                onBrowseRequested: function(location) { page.browseRequested(location) }
            }

            GridView {
                id: grid
                anchors.fill: parent
                visible: !page.mediaGallery
                clip: true
                model: page.files
                readonly property int columnCount: Math.max(2, Math.floor(width / 176))
                cellWidth: width / columnCount
                cellHeight: 184
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Item {
                    id: delegateRoot
                    required property var modelData
                    width: grid.cellWidth
                    height: grid.cellHeight

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 5
                        radius: 14
                        color: cardMouse.containsMouse ? AppTheme.surfaceHover : AppTheme.surface
                        border.color: cardMouse.containsMouse ? AppTheme.accentBorder : AppTheme.border

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 6
                            Item {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 104
                                Image {
                                    id: preview
                                    anchors.centerIn: parent
                                    width: Math.min(parent.width, 104)
                                    height: 100
                                    source: page.thumbSource(delegateRoot.modelData)
                                    sourceSize.width: 240
                                    sourceSize.height: 240
                                    fillMode: Image.PreserveAspectFit
                                    asynchronous: true
                                    cache: true
                                    visible: source.toString().length > 0 && status !== Image.Error
                                }
                                CrispIcon {
                                    anchors.centerIn: parent
                                    width: 54; height: 54
                                    source: AppTheme.customIcon(page.categoryIcon)
                                    visible: !preview.visible
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: delegateRoot.modelData.name
                                color: AppTheme.text
                                font.pixelSize: 11
                                font.weight: Font.Medium
                                horizontalAlignment: Text.AlignHCenter
                                elide: Text.ElideMiddle
                            }
                            Text {
                                Layout.fillWidth: true
                                text: page.formatSize(delegateRoot.modelData.size)
                                color: AppTheme.textMuted
                                font.pixelSize: 9
                                horizontalAlignment: Text.AlignHCenter
                                elide: Text.ElideRight
                            }
                        }

                        MouseArea {
                            id: cardMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            cursorShape: Qt.PointingHandCursor
                            onDoubleClicked: function(mouse) {
                                if (mouse.button !== Qt.LeftButton) return
                                if (page.categoryKey === "appimage")
                                    fileOps.runExecutable(delegateRoot.modelData.url)
                                else
                                    Qt.openUrlExternally(delegateRoot.modelData.url)
                            }
                            onClicked: function(mouse) {
                                if (mouse.button === Qt.RightButton)
                                    containingMenu.popup()
                            }
                        }

                        GMenu {
                            id: containingMenu
                            GMenuItem {
                                text: lang.language === "tr" ? "İçeren klasörü aç" : "Open containing folder"
                                onTriggered: page.browseRequested(delegateRoot.modelData.parentPath)
                            }
                            GMenuItem {
                                text: lang.language === "tr" ? "Dosyayı aç" : "Open file"
                                onTriggered: {
                                    if (page.categoryKey === "appimage")
                                        fileOps.runExecutable(delegateRoot.modelData.url)
                                    else
                                        Qt.openUrlExternally(delegateRoot.modelData.url)
                                }
                            }
                        }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: page.files.length === 0 && !contentIndexModel.indexing
                    text: lang.language === "tr" ? "Bu kategoride dosya bulunamadı" : "No files in this category"
                    color: AppTheme.textMuted
                    font.pixelSize: 12
                }
            }

            Text {
                anchors.centerIn: parent
                visible: page.mediaGallery && page.files.length === 0 && !contentIndexModel.indexing
                text: lang.language === "tr" ? "Bu galeride medya bulunamadı" : "No media in this gallery"
                color: AppTheme.textMuted
                font.pixelSize: 12
            }
        }
    }
}
