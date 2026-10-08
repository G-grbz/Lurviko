import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lurviko.App

MenuItem {
    id: control

    hoverEnabled: true
    HoverHandler { cursorShape: Qt.PointingHandCursor }

    property bool favoriteActionVisible: false
    property bool favoriteActionChecked: false
    signal favoriteActionClicked()
    signal middleClicked()

    // MenuItem normally closes its owning native popup after activation, but
    // GMenu uses a custom content ListView and Popup.Item. Explicit items can
    // therefore remain visible after their action has run. Keep this in a
    // Connections object so page-level onTriggered handlers are not replaced,
    // and dismiss on the next event turn so the action gets to open dialogs or
    // navigate before the complete parent/submenu chain is closed.
    Connections {
        target: control
        function onTriggered() {
            if (control.subMenu !== null || control.menu === null)
                return
            const owningMenu = control.menu
            Qt.callLater(function() {
                if (owningMenu)
                    owningMenu.dismiss()
            })
        }
    }

    // Keep each row inside the actual scroll viewport. Without this, an item
    // can retain its implicit width and the highlighted background gets clipped
    // on the right edge when the menu scrollbar/gutter is visible.
    width: ListView.view ? ListView.view.width : implicitWidth
    implicitHeight: visible ? 42 : 0
    implicitTextPadding: checkable ? 30 : 8
    leftPadding: 10
    rightPadding: 10
    topPadding: 3
    bottomPadding: 3

    indicator: null
    arrow: null

    contentItem: RowLayout {
        spacing: 9

        Item {
            Layout.preferredWidth: 22
            Layout.preferredHeight: 26

            Rectangle {
                anchors.centerIn: parent
                width: 20
                height: 20
                radius: 7
                visible: control.checkable
                color: control.checked ? AppTheme.accent : AppTheme.surface
                border.color: control.checked ? AppTheme.accent : AppTheme.borderStrong
                border.width: 1.25

                Text {
                    anchors.centerIn: parent
                    text: "✓"
                    color: "white"
                    visible: control.checked
                    font.pixelSize: 10
                    font.bold: true
                }
            }
        }

        Text {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            text: control.text
            color: control.enabled ? AppTheme.text : AppTheme.textFaint
            font.pixelSize: 12
            font.weight: control.highlighted ? Font.DemiBold : Font.Normal
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }

        Item {
            visible: control.favoriteActionVisible
            Layout.preferredWidth: visible ? 28 : 0
            Layout.preferredHeight: 28

            Rectangle {
                anchors.fill: parent
                radius: 8
                color: favoriteMouse.containsMouse ? AppTheme.accentSoft : "transparent"
            }

            Text {
                anchors.centerIn: parent
                text: control.favoriteActionChecked ? "★" : "☆"
                color: control.favoriteActionChecked ? AppTheme.warning : AppTheme.textMuted
                font.pixelSize: 16
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: Text.AlignHCenter
            }

            MouseArea {
                id: favoriteMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                acceptedButtons: Qt.LeftButton
                onPressed: function(mouse) { mouse.accepted = true }
                onReleased: function(mouse) { mouse.accepted = true }
                onClicked: function(mouse) {
                    mouse.accepted = true
                    control.favoriteActionClicked()
                }
            }
        }

        Text {
            Layout.preferredWidth: 18
            Layout.alignment: Qt.AlignVCenter
            visible: control.subMenu !== null
            text: "›"
            color: control.highlighted ? AppTheme.accent : AppTheme.textMuted
            font.pixelSize: 19
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    // Qt Quick Controls' MenuItem consumes pointer events internally.
    // Handle the middle button at the shared menu-item level so callers can
    // reliably provide "open in new tab" without interfering with the normal
    // left-button trigger path.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.MiddleButton
        hoverEnabled: false
        z: 1000
        onClicked: function(mouse) {
            mouse.accepted = true
            control.middleClicked()
        }
    }

    background: Rectangle {
        radius: 12
        color: control.highlighted ? AppTheme.accentSoft : (control.checked ? AppTheme.surfaceRaised : "transparent")
        border.color: control.highlighted ? AppTheme.accentBorder : "transparent"
        border.width: 1

    }
}
