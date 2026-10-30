import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lurviko.App
import Lurviko.Backend

MenuItem {
    id: control

    hoverEnabled: true
    HoverHandler { cursorShape: Qt.PointingHandCursor }

    property bool favoriteActionVisible: false
    property bool favoriteActionChecked: false
    signal favoriteActionClicked()
    signal middleClicked()
    property string shortcutAction: ""
    readonly property var shortcutSequences: shortcutAction.length ? (KeyboardShortcuts.bindings[shortcutAction] || []) : []
    readonly property string shortcutText: shortcutSequences.map(function(key) {
        return KeyboardShortcuts.displaySequence(key)
    }).join(" / ")
    readonly property bool shortcutHighlighted: enabled && KeyboardShortcuts.heldModifiers !== 0
                                              && KeyboardShortcuts.usesHeldModifiers(shortcutSequences)
    TextMetrics { id: labelMetrics; text: control.text; font.family: AppTheme.typographyFamily("menu"); font.pixelSize: AppTheme.typographySize("menu", 12); font.weight: AppTheme.typographyWeight("menu", Font.DemiBold); font.italic: AppTheme.typographyFlag("menu", "italic", false) }
    TextMetrics { id: shortcutMetrics; text: control.shortcutText; font.family: AppTheme.typographyFamily("menu"); font.pixelSize: AppTheme.typographySize("menu", 11); font.weight: AppTheme.typographyWeight("menu", Font.DemiBold); font.italic: AppTheme.typographyFlag("menu", "italic", false) }

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
    implicitWidth: leftPadding + rightPadding + 22 + 9 + labelMetrics.advanceWidth
                   + (shortcutText.length ? 18 + shortcutMetrics.advanceWidth + 16 : 0)
                   + (favoriteActionVisible ? 37 : 0) + (subMenu !== null ? 27 : 0)
    implicitHeight: visible ? Math.max(30, contentItem.implicitHeight + topPadding + bottomPadding) : 0
    implicitTextPadding: checkable ? 30 : 8
    leftPadding: 10
    rightPadding: 10
    topPadding: 2
    bottomPadding: 2

    indicator: null
    arrow: null

    contentItem: RowLayout {
        spacing: 9

        Item {
            Layout.preferredWidth: 22
            Layout.preferredHeight: 26

            // Keep application artwork in its original colors. Bundled action
            // glyphs receive their palette color through AppTheme.icon().
            CrispIcon {
                objectName: "menuActionIcon"
                anchors.centerIn: parent
                width: 18
                height: 18
                visible: !control.checkable
                         && (String(control.icon.source).length > 0 || control.icon.name.length > 0)
                source: !visible ? "" : String(control.icon.source).length > 0
                        ? control.icon.source : AppTheme.systemIcon(control.icon.name)
                opacity: control.enabled ? 1 : 0.4
                retainWhileLoading: false
            }

            Rectangle {
                objectName: "menuCheckIndicator"
                anchors.centerIn: parent
                width: 20
                height: 20
                radius: 7
                visible: control.checkable
                color: control.checked ? AppTheme.accent : AppTheme.surface
                border.color: control.checked ? AppTheme.accent : AppTheme.borderStrong
                border.width: 1.25

                GText {
                    typographyRole: "menu"
                    anchors.centerIn: parent
                    text: "✓"
                    baseColor: AppTheme.highlightedText
                    visible: control.checked
                    basePixelSize: 10
                    baseBold: true
                }
            }
        }

        GText {
            typographyRole: "menu"
            id: menuLabel
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            text: control.text
            baseColor: AppTheme.useQtTheme
                ? (control.enabled ? AppTheme.typographyColor("menu", AppTheme.qtColors.menuText) : AppTheme.qtColors.menuDisabledText)
                : (control.enabled ? AppTheme.text : AppTheme.textFaint)
            basePixelSize: 12
            baseFontWeight: control.highlighted ? Font.DemiBold : Font.Normal
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }

        Rectangle {
            id: shortcutBadge
            objectName: "menuShortcutBadge"
            visible: control.shortcutText.length > 0
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: shortcutMetrics.advanceWidth + 16
            Layout.maximumWidth: Math.max(80, control.availableWidth * 0.58)
            implicitHeight: shortcutLabel.implicitHeight + 8
            radius: 6
            color: control.shortcutHighlighted ? AppTheme.accentSoft : "transparent"
            border.width: control.shortcutHighlighted ? 1 : 0
            border.color: AppTheme.accentBorder
            GText {
                typographyRole: "menu"
                id: shortcutLabel
                objectName: "menuShortcutText"
                anchors.left: parent.left; anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.margins: 8
                text: control.shortcutText
                baseColor: !control.enabled ? AppTheme.textFaint
                     : control.shortcutHighlighted ? AppTheme.accent : AppTheme.textMuted
                basePixelSize: 11
                baseFontWeight: Font.DemiBold
                horizontalAlignment: Text.AlignRight
                wrapMode: Text.Wrap
                elide: Text.ElideNone
            }
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

            GText {
                typographyRole: "menu"
                anchors.centerIn: parent
                text: control.favoriteActionChecked ? "★" : "☆"
                baseColor: control.favoriteActionChecked ? AppTheme.warning : AppTheme.textMuted
                basePixelSize: 16
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

        GText {
            typographyRole: "menu"
            Layout.preferredWidth: 18
            Layout.alignment: Qt.AlignVCenter
            visible: control.subMenu !== null
            text: "›"
            baseColor: control.highlighted ? AppTheme.accent : AppTheme.textMuted
            basePixelSize: 19
            baseBold: true
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
        radius: 8
        color: control.highlighted ? AppTheme.accentSoft : (control.checked ? AppTheme.surfaceRaised : "transparent")
        border.color: control.highlighted ? AppTheme.accentBorder : "transparent"
        border.width: 1

    }
}
