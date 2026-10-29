import QtQuick
import QtQuick.Shapes
import Lurviko.App

Item {
    id: surface
    property bool selected: false
    property bool hovered: false
    property bool dropTarget: false
    property bool administrator: false
    property color navigationColor: AppTheme.surface
    // Only color interpolation runs during selection; no textures, blur or
    // render-backend-specific effects are allocated for tabs.
    property bool animationReady: false
    property real selectionProgress: selected ? 1 : 0
    readonly property bool selectionAnimating: selectionTween.running
    // Native Window/Base brushes can be almost identical. Use the desktop's
    // own highlight for selection instead of relying on a 4%/14% alpha change.
    readonly property color activeColor: administrator
        ? (AppTheme.dark ? "#3A2A18" : "#FFF0D8")
        : AppTheme.useQtTheme
        ? AppTheme.tint(AppTheme.mix(AppTheme.surfaceRaised, AppTheme.accent, 0.14),
                        AppTheme.translucentWindow ? 0.62 : 1)
        : navigationColor
    readonly property color passiveColor: administrator
        ? (AppTheme.dark ? "#2A2118" : "#FFF8ED")
        : AppTheme.useQtTheme && AppTheme.translucentWindow
            ? AppTheme.tint(AppTheme.surfaceRaised, hovered ? 0.12 : 0.04)
            : AppTheme.mix(AppTheme.background, AppTheme.workspace, hovered ? 0.48 : 0.22)
    readonly property color fillColor: dropTarget ? AppTheme.accentSoft
        : AppTheme.mix(passiveColor, activeColor, selectionProgress)
    readonly property color outlineColor: dropTarget ? AppTheme.accent
        : administrator ? AppTheme.warning
        : AppTheme.useQtTheme
            ? AppTheme.mix(AppTheme.border, AppTheme.tint(AppTheme.accent, 0.70), selectionProgress)
            : AppTheme.border
    Component.onCompleted: animationReady = true
    Behavior on selectionProgress {
        enabled: surface.animationReady && surface.visible
        NumberAnimation { id: selectionTween; duration: 110; easing.type: Easing.OutCubic }
    }
    readonly property real wing: Math.min(18, width / 6)
    readonly property real corner: Math.min(8, (height - capTop) / 3)
    // Selection never changes geometry or the text's vertical origin.
    readonly property real capTop: 4
    readonly property real base: height
    readonly property real lowerRadius: Math.min(6, (height - capTop) / 4)
    readonly property real sideInset: Math.min(6, wing / 3)
    readonly property real sideHeight: base - lowerRadius - capTop - corner
    readonly property real tangentRise: Math.min(corner, lowerRadius, sideHeight / 2) * 0.55
    readonly property real tangentRun: sideHeight > 0 ? sideInset * tangentRise / sideHeight : 0
    // Straight shoulders join small rounded corners with matching tangents.
    // Mirroring the same points keeps both sides smooth at fractional scales.
    component Contour: ShapePath {
        property bool closeBottom: false
        startX: 0
        startY: surface.base
        PathCubic {
            x: surface.wing - surface.sideInset; y: surface.base - surface.lowerRadius
            control1X: (surface.wing - surface.sideInset) / 2; control1Y: surface.base
            control2X: surface.wing - surface.sideInset - surface.tangentRun
            control2Y: surface.base - surface.lowerRadius + surface.tangentRise
        }
        PathLine { x: surface.wing; y: surface.capTop + surface.corner }
        PathCubic {
            x: surface.wing + surface.corner; y: surface.capTop
            control1X: surface.wing + surface.tangentRun
            control1Y: surface.capTop + surface.corner - surface.tangentRise
            control2X: surface.wing + surface.corner / 2; control2Y: surface.capTop
        }
        PathLine { x: surface.width - surface.wing - surface.corner; y: surface.capTop }
        PathCubic {
            x: surface.width - surface.wing; y: surface.capTop + surface.corner
            control1X: surface.width - surface.wing - surface.corner / 2; control1Y: surface.capTop
            control2X: surface.width - surface.wing - surface.tangentRun
            control2Y: surface.capTop + surface.corner - surface.tangentRise
        }
        PathLine { x: surface.width - surface.wing + surface.sideInset; y: surface.base - surface.lowerRadius }
        PathCubic {
            x: surface.width; y: surface.base
            control1X: surface.width - surface.wing + surface.sideInset + surface.tangentRun
            control1Y: surface.base - surface.lowerRadius + surface.tangentRise
            control2X: surface.width - (surface.wing - surface.sideInset) / 2; control2Y: surface.base
        }
        PathLine { x: closeBottom ? 0 : surface.width; y: surface.base }
    }

    Shape {
        anchors.fill: parent
        // Native curve antialiasing needs no layer, blur texture or MSAA.
        // Qt's software backend automatically uses its raster fallback.
        preferredRendererType: Shape.CurveRenderer
        antialiasing: true
        Contour {
            fillColor: surface.fillColor
            strokeColor: "transparent"
            strokeWidth: 0
            closeBottom: true
        }
        Contour {
            fillColor: "transparent"
            strokeColor: surface.outlineColor
            strokeWidth: surface.dropTarget ? 1.5 : surface.administrator ? 0.8 : 1
            // Keep the navigation join open, including during transitions.
            closeBottom: false
        }
    }
}
