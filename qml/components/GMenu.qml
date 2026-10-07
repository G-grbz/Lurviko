import QtQuick
import QtQuick.Controls
import GFile.App

Menu {
    id: control

    popupType: Popup.Item
    cascade: true
    overlap: 4
    // visible stays true throughout the exit animation; button toggles need
    // the requested state rather than treating a closing menu as still open.
    property bool interactionOpen: false
    property bool reopenAfterExit: false
    function requestOpen() {
        if (visible && !interactionOpen) {
            reopenAfterExit = true
            interactionOpen = true
        } else {
            open()
        }
    }
    function requestClose() {
        reopenAfterExit = false
        interactionOpen = false
        close()
    }
    delegate: GMenuItem {}

    topPadding: 10
    bottomPadding: 10
    leftPadding: 10
    rightPadding: 10
    margins: 8
    implicitWidth: Math.max(228, contentItem ? contentItem.implicitWidth + leftPadding + rightPadding : 228)
    // Keep long context menus inside the visible window. The ListView below
    // becomes scrollable when content exceeds this height.
    property real maximumPopupHeight: Math.max(180, Math.min(620,
        control.parent && control.parent.height > 0 ? control.parent.height - 24 : 620))
    implicitHeight: Math.min(contentItem ? contentItem.contentHeight + topPadding + bottomPadding : 0,
                             maximumPopupHeight)
    modal: false
    dim: false
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    onAboutToShow: { interactionOpen = true; currentIndex = -1 }
    onAboutToHide: interactionOpen = false
    Connections {
        target: control
        function onClosed() {
            if (control.reopenAfterExit) {
                Qt.callLater(function() {
                    if (!control.reopenAfterExit)
                        return
                    control.reopenAfterExit = false
                    control.open()
                })
            }
        }
    }

    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0.0; to: 1.0; duration: 140; easing.type: Easing.OutCubic }
            NumberAnimation { property: "scale"; from: 0.96; to: 1.0; duration: 170; easing.type: Easing.OutBack }
            NumberAnimation { property: "y"; from: control.y - 8; to: control.y; duration: 150; easing.type: Easing.OutCubic }
        }
    }

    exit: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 1.0; to: 0.0; duration: 55; easing.type: Easing.InOutCubic }
            NumberAnimation { property: "scale"; from: 1.0; to: 0.98; duration: 55; easing.type: Easing.InOutCubic }
            NumberAnimation { property: "y"; from: control.y; to: control.y - 4; duration: 55; easing.type: Easing.InOutCubic }
        }
    }

    background: Rectangle {
        implicitWidth: 244
        radius: 20
        color: AppTheme.commandBar
        border.color: AppTheme.borderStrong
        border.width: 1

        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: 19
            color: "transparent"
            border.width: 1
            border.color: AppTheme.dark ? "#12FFFFFF" : "#8AFFFFFF"
        }
    }

    contentItem: Item {
        id: menuViewport
        implicitHeight: menuList.contentHeight
        implicitWidth: 220
        property real contentHeight: menuList.contentHeight
        function itemAtIndex(index) { return menuList.itemAtIndex(index) }

        ListView {
            id: menuList
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: menuScroll.visible ? menuGutter.left : parent.right
            model: control.contentModel
            interactive: contentHeight > height
            currentIndex: control.currentIndex
            spacing: 2
            boundsBehavior: Flickable.StopAtBounds
            clip: true

            // Keyboard navigation can move the Menu currentIndex to an item that
            // is currently outside the viewport. Keep that item visible too.
            onCurrentIndexChanged: {
                if (currentIndex >= 0)
                    positionViewAtIndex(currentIndex, ListView.Contain)
            }
        }

        Item {
            id: menuGutter
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.right: parent.right
            width: menuScroll.visible ? 12 : 0
        }

        ScrollBar {
            id: menuScroll
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.right: parent.right
            orientation: Qt.Vertical
            policy: ScrollBar.AsNeeded
            visible: menuList.contentHeight > menuList.height + 0.5
            active: visible && (pressed || hovered || menuList.moving || menuList.flicking)
            interactive: true
            width: 8
            size: menuList.visibleArea.heightRatio
            position: menuList.visibleArea.yPosition
            onPositionChanged: {
                if (pressed)
                    menuList.contentY = menuList.originY
                            + position * Math.max(0, menuList.contentHeight - menuList.height)
            }
            contentItem: Rectangle {
                implicitWidth: 5
                radius: 3
                color: menuScroll.pressed || menuScroll.hovered ? AppTheme.textMuted : AppTheme.borderStrong
                opacity: menuScroll.active ? 0.9 : 0.58
            }
            background: Item {}
        }
    }
}
