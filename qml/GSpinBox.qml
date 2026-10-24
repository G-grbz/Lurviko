import QtQuick
import QtQuick.Controls
import Lurviko.App

SpinBox {
    id: control
    font: AppTheme.menuTypography.fontFor(basePixelSize, baseBold ? Font.Bold : baseFontWeight,
        baseItalic, baseUnderline, baseOverline, baseStrikeout, false,
        baseFontFamily, baseLetterSpacing, baseCapitalization)
    property string baseFontFamily: ""
    property real baseLetterSpacing: 0
    property int baseCapitalization: Font.MixedCase
    property bool baseBold: false
    property int baseFontWeight: Font.Normal
    property bool baseItalic: false
    property bool baseUnderline: false
    property bool baseOverline: false
    property bool baseStrikeout: false
    property real basePixelSize: AppTheme.fontSizeControl

    implicitWidth: 116
    implicitHeight: Math.max(AppTheme.controlHeight, font.pixelSize + 18)
    leftPadding: 36
    rightPadding: 36

    contentItem: TextInput {
        z: 2
        text: control.displayText
        font: control.font
        color: control.enabled ? AppTheme.typographyColor("menu", AppTheme.text) : AppTheme.textFaint
        selectionColor: AppTheme.selection
        selectedTextColor: AppTheme.highlightedText
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

        GText {
            anchors.centerIn: parent
            text: "+"
            baseColor: control.enabled ? AppTheme.typographyColor("menu", AppTheme.text) : AppTheme.textFaint
            basePixelSize: 16
            baseFontWeight: Font.Medium
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

        GText {
            anchors.centerIn: parent
            text: "−"
            baseColor: control.enabled ? AppTheme.typographyColor("menu", AppTheme.text) : AppTheme.textFaint
            basePixelSize: 16
            baseFontWeight: Font.Medium
        }
    }

    background: Rectangle {
        radius: AppTheme.fieldRadius
        color: AppTheme.surfaceRaised
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? AppTheme.accent : AppTheme.border
    }
}
