import QtQuick
import QtQuick.Effects

Text {
    id: caption
    // 0 off, 1 outline (the existing default), 2 soft, 3 strong, 4 outline + shadow.
    property int shadowMode: 1
    readonly property bool shaderEffectsAvailable: GraphicsInfo.api !== GraphicsInfo.Software
    style: shadowMode === 1 || shadowMode === 4 ? Text.Outline
           : (shadowMode >= 2 && !shaderEffectsAvailable ? Text.Raised : Text.Normal)
    styleColor: "#D9000000"
    layer.enabled: shadowMode >= 2 && shaderEffectsAvailable && visible
    layer.effect: MultiEffect {
        shadowEnabled: true
        shadowColor: "#000000"
        shadowOpacity: caption.shadowMode === 2 ? 0.65 : 0.95
        shadowBlur: caption.shadowMode === 2 ? 0.3 : 0.6
        blurMax: 12
        shadowHorizontalOffset: caption.shadowMode === 2 ? 1 : 2
        shadowVerticalOffset: caption.shadowMode === 2 ? 2 : 3
        autoPaddingEnabled: true
    }
}
