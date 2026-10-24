import QtQuick

// One stable object per role. Parse/validate settings once, and share immutable
// QFont values across delegates instead of resolving each field on every Text.
QtObject {
    id: style
    property string specification: "{}"
    property string defaultFamily: Qt.application.font.family
    property var families: []
    readonly property var options: {
        try { return JSON.parse(specification) || ({}) } catch (error) { return ({}) }
    }
    readonly property string fontSpecification: JSON.stringify({
        family: options.family || "", pixels: options.pixels || 0,
        bold: options.bold, italic: options.italic, underline: options.underline,
        overline: options.overline, strikeout: options.strikeout
    })
    readonly property var fontOptions: JSON.parse(fontSpecification)
    readonly property string family: fontOptions.family && families.indexOf(fontOptions.family) >= 0
        ? fontOptions.family : defaultFamily
    readonly property int pixels: fontOptions.pixels >= 8 && fontOptions.pixels <= 48
        ? Math.round(fontOptions.pixels) : 0
    readonly property string customColor: /^#[0-9a-fA-F]{6}$/.test(options.color || "") ? options.color : ""
    property var fonts: ({})
    onFontSpecificationChanged: fonts = ({})
    onFamilyChanged: fonts = ({})

    function flag(field, automaticValue) {
        const value = fontOptions[field]
        return value === true
    }
    function weight(automaticValue) {
        return fontOptions.bold === true ? Font.Bold : Font.Normal
    }
    function fontFor(basePixels, baseWeight, italic, underline, overline, strikeout, symbol, familyOverride, letterSpacing, capitalization) {
        const fontFamily = familyOverride || family
        const spacing = letterSpacing || 0
        const capitals = capitalization || Font.MixedCase
        const size = Math.max(1, Math.round(symbol || !pixels ? basePixels : pixels))
        const effectiveWeight = symbol ? baseWeight : weight(baseWeight)
        const effectiveItalic = symbol ? italic : flag("italic", italic)
        const effectiveUnderline = symbol ? underline : flag("underline", underline)
        const effectiveOverline = symbol ? overline : flag("overline", overline)
        const effectiveStrikeout = symbol ? strikeout : flag("strikeout", strikeout)
        const key = [fontFamily, size, effectiveWeight, effectiveItalic, effectiveUnderline,
            effectiveOverline, effectiveStrikeout, spacing, capitals].join("|")
        if (fonts[key] !== undefined) return fonts[key]
        const result = Qt.font({family: fontFamily, pixelSize: size, weight: effectiveWeight,
            italic: effectiveItalic, underline: effectiveUnderline, overline: effectiveOverline,
            strikeout: effectiveStrikeout, hintingPreference: Font.PreferVerticalHinting})
        // Qt.font's map conversion does not retain every decoration on all
        // supported Qt builds. Set these on the QFont value before caching.
        result.hintingPreference = Font.PreferVerticalHinting
        result.letterSpacing = spacing
        result.capitalization = capitals
        result.overline = effectiveOverline
        result.strikeout = effectiveStrikeout
        // Bound the variants used by font pickers; normal labels reuse a
        // small set of sizes and weights. Eviction keeps existing Text fonts.
        const keys = Object.keys(fonts)
        if (keys.length >= 64) delete fonts[keys[0]]
        fonts[key] = result
        return result
    }
}
