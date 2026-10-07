import QtQuick
import QtQuick.Controls
import GFile.App

SpinBox {
    id: control

    implicitWidth: 116
    implicitHeight: AppTheme.controlHeight
    leftPadding: 36
    rightPadding: 36
    font.pixelSize: AppTheme.fontSizeControl

    contentItem: TextInput {
        z: 2
        text: control.displayText
        font: control.font
        color: control.enabled ? AppTheme.text : AppTheme.textFaint
        selectionColor: AppTheme.selection
        selectedTextColor: "white"
        horizontalAlignment: Qt.AlignHCenter
        verticalAlignment: Qt.AlignVCenter
        readOnly: !control.editable
        validator: control.validator
        inputMethodHints: control.inputMethodHints
    }

    up.indicator: Rectangle {
        x: control.width - width - 4
        y: 4
        width: 30
        height: control.height - 8
        radius: AppTheme.radiusSmall
        color: control.up.pressed ? AppTheme.surfacePressed
                                  : (control.up.hovered ? AppTheme.surfaceHover : "transparent")

        Text {
            anchors.centerIn: parent
            text: "+"
            color: control.enabled ? AppTheme.text : AppTheme.textFaint
            font.pixelSize: 16
            font.weight: Font.Medium
        }
    }

    down.indicator: Rectangle {
        x: 4
        y: 4
        width: 30
        height: control.height - 8
        radius: AppTheme.radiusSmall
        color: control.down.pressed ? AppTheme.surfacePressed
                                    : (control.down.hovered ? AppTheme.surfaceHover : "transparent")

        Text {
            anchors.centerIn: parent
            text: "−"
            color: control.enabled ? AppTheme.text : AppTheme.textFaint
            font.pixelSize: 16
            font.weight: Font.Medium
        }
    }

    background: Rectangle {
        radius: AppTheme.fieldRadius
        color: AppTheme.surfaceRaised
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? AppTheme.accent : AppTheme.border
    }
}
