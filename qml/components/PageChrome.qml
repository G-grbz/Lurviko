import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lurviko.App

ColumnLayout {
    id: chrome
    property string tabTitle: ""
    property url tabIcon: ""
    property color navigationColor: AppTheme.workspace
    property bool pageOutlineJoined: false
    property bool canGoBack: true
    property bool canGoForward: false
    property bool canGoUp: true
    property bool showHistory: true
    property string backTip: ""
    property string forwardTip: ""
    property string upTip: ""
    property string refreshTip: ""
    property string historyTip: ""
    property string newTabTip: ""
    readonly property real tabStripHeight: tabsBar.implicitHeight
    readonly property real headerHeight: tabsBar.implicitHeight + navigationBar.implicitHeight
    readonly property real navigationHeight: navigationBar.implicitHeight
    // Right edge of the navigation buttons, for pages that use the rest of the row.
    readonly property real navigationButtonsExtent: navigationRow.anchors.leftMargin + navigationRow.implicitWidth
    // Main's full-page outline must leave the selected tab's bottom edge open.
    readonly property Item activeTabSurface: chromeTabSurface
    readonly property real activeTabScrollPosition: 0
    readonly property bool activeTabAdministrator: false
    signal backRequested()
    signal forwardRequested()
    signal upRequested()
    signal refreshRequested()
    signal historyRequested()
    signal newTabRequested()
    signal tabSelected()

    spacing: 0
    Rectangle {
        id: tabsBar
        objectName: "sharedPageTabsBar"
        Layout.fillWidth: true
        implicitHeight: Math.max(40, AppTheme.typographySize("menu", 11) + 18)
        color: "transparent"
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 6
            Item {
                Layout.preferredWidth: 220
                Layout.preferredHeight: tabsBar.implicitHeight
                FluidTabSurface {
                    id: chromeTabSurface
                    anchors.fill: parent
                    selected: true
                    pageOutlineJoined: chrome.pageOutlineJoined
                    hovered: tabMouse.containsMouse
                    navigationColor: chrome.navigationColor
                }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 26
                    anchors.rightMargin: 14
                    anchors.topMargin: 4
                    spacing: 8
                    CrispIcon { Layout.preferredWidth: 16; Layout.preferredHeight: 16; source: chrome.tabIcon }
                    GText {
                        Layout.fillWidth: true
                        text: chrome.tabTitle
                        baseColor: AppTheme.text
                        elide: Text.ElideRight
                        basePixelSize: 11
                        baseFontWeight: Font.Medium
                    }
                }
                MouseArea {
                    id: tabMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: chrome.tabSelected()
                }
            }
            GButton {
                id: newTabButton
                objectName: "sharedPageNewTabButton"
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                text: ""
                display: AbstractButton.IconOnly
                icon.source: AppTheme.icon("plus.svg")
                icon.width: 14; icon.height: 14
                implicitWidth: 28; implicitHeight: 28
                Layout.alignment: Qt.AlignVCenter
                Layout.topMargin: 4
                onClicked: chrome.newTabRequested()
                GToolTip { text: chrome.newTabTip }
                background: Rectangle {
                    radius: height / 2
                    color: parent.hovered ? AppTheme.surfaceHover : AppTheme.mix(AppTheme.background, AppTheme.surfaceRaised, 0.35)
                }
            }
            Item { Layout.fillWidth: true }
        }
    }
    Item {
        id: navigationBar
        objectName: "sharedPageNavigationBar"
        Layout.fillWidth: true
        implicitHeight: Math.max(42, AppTheme.typographySize("menu", 11) + 18)
        RowLayout {
            id: navigationRow
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            anchors.topMargin: 4
            anchors.bottomMargin: 4
            spacing: 8
            Repeater {
                // Stable delegates: navigation history changes only their bindings,
                // never destroys a button while its click handler is running.
                model: ["back", "forward", "up", "refresh", "history"]
                delegate: GButton {
                    required property string modelData
                    readonly property string navAction: modelData
                    objectName: "sharedNav_" + navAction
                    HoverHandler { cursorShape: parent.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
                    text: ""
                    display: AbstractButton.IconOnly
                    icon.source: AppTheme.icon(navAction === "back" ? "nav-back.svg"
                            : navAction === "forward" ? "nav-forward.svg"
                            : navAction === "up" ? "nav-up.svg"
                            : navAction === "refresh" ? "refresh.svg" : "history.svg")
                    icon.width: 16; icon.height: 16
                    implicitWidth: 32; implicitHeight: 32
                    enabled: navAction === "back" ? chrome.canGoBack
                           : navAction === "forward" ? chrome.canGoForward
                           : navAction === "up" ? chrome.canGoUp
                           : navAction === "history" ? chrome.showHistory : true
                    opacity: enabled ? 1 : 0.38
                    onClicked: {
                        switch (navAction) {
                        case "back": chrome.backRequested(); break
                        case "forward": chrome.forwardRequested(); break
                        case "up": chrome.upRequested(); break
                        case "refresh": chrome.refreshRequested(); break
                        case "history": chrome.historyRequested(); break
                        }
                    }
                    background: Rectangle {
                        radius: 10
                        color: parent.hovered && parent.enabled ? AppTheme.surfaceHover : "transparent"
                        border.color: parent.enabled ? AppTheme.border : "transparent"
                    }
                    GToolTip {
                        text: navAction === "back" ? chrome.backTip
                            : navAction === "forward" ? chrome.forwardTip
                            : navAction === "up" ? chrome.upTip
                            : navAction === "refresh" ? chrome.refreshTip : chrome.historyTip
                    }
                }
            }
            Item { Layout.fillWidth: true }
        }
    }
}
