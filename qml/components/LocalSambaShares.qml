import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lurviko.Backend
import Lurviko.App

Rectangle {
    id: panel
    property var lang
    property bool active: false
    property bool ready: false
    signal openShare(string url)
    readonly property bool tr: lang && lang.language === "tr"
    objectName: "localSambaShares"
    implicitHeight: content.implicitHeight + 20
    color: AppTheme.surfaceRaised; border.color: AppTheme.border; radius: 12
    function refresh() { if (active) sharing.listShares(); else sharing.clear() }
    onActiveChanged: if (ready) refresh()
    Component.onCompleted: { ready = true; refresh() }
    SambaShareManager { id: sharing; objectName: "localSambaShareManager" }
    ColumnLayout {
        id: content
        anchors.fill: parent; anchors.margins: 10; spacing: 7
        RowLayout {
            Layout.fillWidth: true
            GText { text: panel.tr ? "Bu bilgisayardaki paylaşımlar" : "Shares on this computer"; basePixelSize: 12; baseColor: AppTheme.text; Layout.fillWidth: true }
            GBusyIndicator { running: sharing.busy; visible: running; implicitWidth: 20; implicitHeight: 20 }
            GModalButton { text: panel.tr ? "Yenile" : "Refresh"; enabled: !sharing.busy; onClicked: panel.refresh() }
        }
        GText {
            Layout.fillWidth: true
            visible: !sharing.busy && sharing.localShares.length === 0
            text: sharing.error || (panel.tr ? "Henüz yerel paylaşım yok. Bir klasörün Özellikler penceresinden paylaşım oluşturabilirsin." : "No local shares yet. Create one in a folder's Properties.")
            textFormat: Text.PlainText; wrapMode: Text.Wrap; basePixelSize: 11; baseColor: AppTheme.textMuted
        }
        ListView {
            id: shares
            objectName: "localSambaShareList"
            Layout.fillWidth: true; Layout.preferredHeight: count > 0 ? 62 : 0
            orientation: ListView.Horizontal; clip: true; spacing: 8
            model: panel.active ? sharing.localShares : []
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.horizontal: ScrollBar {}
            delegate: GButton {
                required property var modelData
                objectName: "localSambaShare_" + modelData.name
                width: 200; height: 54
                onClicked: panel.openShare(modelData.url)
                contentItem: RowLayout {
                    spacing: 8
                    CrispIcon { Layout.preferredWidth: 28; Layout.preferredHeight: 28; source: AppTheme.icon("network.svg") }
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 2
                        GText { Layout.fillWidth: true; text: modelData.name; textFormat: Text.PlainText; elide: Text.ElideRight; basePixelSize: 12; baseColor: AppTheme.text }
                        GText { Layout.fillWidth: true; text: modelData.url; textFormat: Text.PlainText; elide: Text.ElideMiddle; basePixelSize: 10; baseColor: AppTheme.textMuted }
                    }
                }
            }
        }
    }
}
