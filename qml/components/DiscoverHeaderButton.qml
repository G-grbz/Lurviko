import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lurviko.App

// Section header action: icon + label in a pill matching ViewModeSwitcher.
AbstractButton {
    id: control
    property string iconName: ""
    property bool accented: false

    implicitHeight: 32
    implicitWidth: buttonRow.implicitWidth + 26
    hoverEnabled: true
    Accessible.name: text
    HoverHandler { cursorShape: Qt.PointingHandCursor }

    background: Rectangle {
        radius: height / 2
        color: control.pressed ? AppTheme.accentSoft
             : control.hovered ? (control.accented ? AppTheme.accentSoft : AppTheme.surfaceHover)
             : (control.accented ? "transparent" : AppTheme.surfaceRaised)
        border.width: 1
        border.color: control.accented || control.hovered ? AppTheme.accentBorder : AppTheme.border
    }
    contentItem: Item {
        RowLayout {
            id: buttonRow
            anchors.centerIn: parent
            spacing: 7
            CrispIcon {
                visible: control.iconName.length > 0
                Layout.preferredWidth: 15; Layout.preferredHeight: 15
                source: control.iconName.length > 0
                        ? AppTheme.icon(control.iconName, control.accented ? AppTheme.accent : AppTheme.text) : ""
            }
            GText {
                typographyRole: "menu"
                text: control.text
                baseColor: control.accented ? AppTheme.accent : AppTheme.text
                basePixelSize: 11
                baseFontWeight: Font.DemiBold
            }
        }
    }
}
