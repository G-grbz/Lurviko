import QtQuick
import Lurviko.App

// Keep contextual sizes and semantic colors (errors, links, media contrast).
// Only normal text colors participate in the user's role color override.
Text {
    readonly property bool symbolicText: ["✓", "−", "+", "⌄", "×", "›", "‹", "…", "▶", "❚❚"].indexOf(text) >= 0
    readonly property TypographyStyle textStyle: AppTheme.styleFor(typographyRole)
    font: textStyle.fontFor(basePixelSize, baseBold ? Font.Bold : baseFontWeight,
        baseItalic, baseUnderline, baseOverline, baseStrikeout, symbolicText,
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
    property real basePixelSize: 12
    property color baseColor: AppTheme.text
    property string typographyRole: basePixelSize >= 16 ? "title"
        : (basePixelSize <= 10 ? "description" : (basePixelSize >= 13 ? "subtitle" : "body"))
    color: baseColor === AppTheme.text || baseColor === AppTheme.textMuted || baseColor === AppTheme.textFaint
        ? (textStyle.customColor || baseColor) : baseColor
}
