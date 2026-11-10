import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lurviko.App

// Common modal header: accent icon tile, title, optional subtitle and a
// round close button. Keeps spacing and type sizes identical everywhere.
RowLayout {
    id: header
    property string title
    property string subtitle
    property url iconSource
    property bool closable: true
    property string closeToolTip: qsTr("Close")
    property alias closeButtonName: closeButton.objectName
    signal closeRequested()

    Layout.fillWidth: true
    spacing: 14

    Rectangle {
        visible: String(header.iconSource).length > 0
        Layout.preferredWidth: 46
        Layout.preferredHeight: 46
        Layout.alignment: Qt.AlignTop
        radius: 15
        border.width: 1
        border.color: AppTheme.accentBorder
        gradient: Gradient {
            GradientStop { position: 0.0; color: AppTheme.accentSoft }
            GradientStop { position: 1.0; color: Qt.rgba(AppTheme.accent.r, AppTheme.accent.g, AppTheme.accent.b, AppTheme.dark ? 0.10 : 0.05) }
        }
        CrispIcon {
            anchors.centerIn: parent
            width: 24
            height: 24
            source: header.iconSource
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignVCenter
        spacing: 3
        GText {
            Layout.fillWidth: true
            text: header.title
            baseColor: AppTheme.text
            basePixelSize: 20
            baseFontWeight: Font.DemiBold
            elide: Text.ElideRight
        }
        GText {
            Layout.fillWidth: true
            visible: header.subtitle.length > 0
            text: header.subtitle
            baseColor: AppTheme.textMuted
            basePixelSize: 12
            wrapMode: Text.WordWrap
        }
    }

    AbstractButton {
        id: closeButton
        visible: header.closable
        Layout.preferredWidth: 34
        Layout.preferredHeight: 34
        Layout.alignment: Qt.AlignTop
        hoverEnabled: true
        Accessible.name: header.closeToolTip
        onClicked: header.closeRequested()
        HoverHandler { cursorShape: Qt.PointingHandCursor }
        background: Rectangle {
            radius: width / 2
            color: closeButton.pressed ? AppTheme.surfacePressed
                 : closeButton.hovered ? AppTheme.surfaceHover
                 // Same hue at zero alpha, so the fade never passes through gray.
                 : Qt.rgba(AppTheme.surfaceHover.r, AppTheme.surfaceHover.g, AppTheme.surfaceHover.b, 0)
            border.width: closeButton.hovered ? 1 : 0
            border.color: AppTheme.border
            Behavior on color { ColorAnimation { duration: 120 } }
        }
        contentItem: Item {
            CrispIcon {
                anchors.centerIn: parent
                width: 16
                height: 16
                source: AppTheme.icon("close-ui.svg")
                opacity: closeButton.hovered ? 1 : 0.72
            }
        }
        GToolTip { text: header.closeToolTip }
    }
}
