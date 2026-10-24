import QtQuick
import QtQuick.Controls
import Lurviko.App

Switch {
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

    spacing: 10
    hoverEnabled: true
    implicitHeight: Math.max(AppTheme.controlHeightCompact, indicator.implicitHeight, implicitContentHeight + 8)

    indicator: Item {
        implicitWidth: 42
        implicitHeight: 24
        x: control.leftPadding
        y: parent.height / 2 - height / 2

        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: control.checked ? AppTheme.accent
                                   : (control.hovered ? AppTheme.surfacePressed : AppTheme.surfaceHover)
            border.width: control.checked ? 0 : 1
            border.color: AppTheme.borderStrong

            Behavior on color {
                ColorAnimation { duration: AppTheme.motionNormal }
            }
        }

        Rectangle {
            width: 18
            height: 18
            radius: 9
            y: 3
            x: control.checked ? parent.width - width - 3 : 3
            color: control.checked ? AppTheme.highlightedText : AppTheme.textMuted

            Behavior on x {
                NumberAnimation { duration: AppTheme.motionNormal; easing.type: Easing.OutCubic }
            }
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
