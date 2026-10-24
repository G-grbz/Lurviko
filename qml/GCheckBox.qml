import QtQuick
import QtQuick.Controls
import Lurviko.App

CheckBox {
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
    property real basePixelSize: AppTheme.fontSizeBody

    spacing: 9
    hoverEnabled: true
    implicitHeight: Math.max(AppTheme.controlHeightCompact, indicator.implicitHeight, implicitContentHeight + 8)

    indicator: Rectangle {
        implicitWidth: 20
        implicitHeight: 20
        x: control.leftPadding
        y: parent.height / 2 - height / 2
        radius: 6
        color: control.checked ? AppTheme.accent
                               : (control.hovered ? AppTheme.surfaceHover : AppTheme.surfaceRaised)
        border.width: 1
        border.color: control.checked ? AppTheme.accent
                                      : (control.activeFocus ? AppTheme.accent : AppTheme.borderStrong)

        GText {
            anchors.centerIn: parent
            text: control.checkState === Qt.PartiallyChecked ? "−" : "✓"
            visible: control.checkState !== Qt.Unchecked
            baseColor: AppTheme.highlightedText
            basePixelSize: 12
            baseBold: true
        }

        Behavior on color {
            ColorAnimation { duration: AppTheme.motionFast }
        }
    }

    contentItem: GText {
        typographyRole: "menu"
        leftPadding: control.indicator.width + control.spacing
        text: control.text
        font: control.font
        baseColor: control.enabled ? AppTheme.text : AppTheme.textFaint
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
