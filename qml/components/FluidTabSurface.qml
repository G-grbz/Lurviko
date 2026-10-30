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
    property real tabOverlap: 0
    property bool occludedOnLeft: false
    property bool occludedOnRight: false
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
    readonly property real footWidth: wing - sideInset
    readonly property real overlap: Math.max(0, Math.min(tabOverlap, 2 * footWidth))
    readonly property bool cutLeft: occludedOnLeft && overlap > 0
    readonly property bool cutRight: occludedOnRight && overlap > 0

    function interpolate(a, b, t) {
        return Qt.point(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t)
    }
    // Split the bottom curve where equal adjacent tabs intersect. The hidden
    // fill follows the front tab's curve; its outline stops at the intersection.
    // This is vector geometry, so translucent tabs need no opaque patch or mask.
    readonly property var footSplit: {
        const p0 = Qt.point(footWidth, base - lowerRadius)
        const p1 = Qt.point(footWidth - tangentRun, base - lowerRadius + tangentRise)
        const p2 = Qt.point(footWidth / 2, base)
        const p3 = Qt.point(0, base)
        let low = 0, high = 1
        for (let i = 0; i < 18; ++i) {
            const t = (low + high) / 2
            const x = Math.pow(1 - t, 3) * p0.x
                    + 3 * Math.pow(1 - t, 2) * t * p1.x
                    + 3 * (1 - t) * t * t * p2.x
            if (x > overlap / 2) low = t
            else high = t
        }
        const t = (low + high) / 2
        const a = interpolate(p0, p1, t), b = interpolate(p1, p2, t), c = interpolate(p2, p3, t)
        const d = interpolate(a, b, t), e = interpolate(b, c, t)
        const join = interpolate(d, e, t)
        return {a: a, c: c, d: d, e: e, join: Qt.point(overlap / 2, join.y)}
    }
    // Straight shoulders join small rounded corners with matching tangents.
    // Mirroring the same points keeps both sides smooth at fractional scales.
    component Contour: ShapePath {
        property bool closeBottom: false
        startX: surface.cutLeft ? (closeBottom ? surface.overlap : surface.footSplit.join.x) : 0
        startY: surface.cutLeft && !closeBottom ? surface.footSplit.join.y : surface.base
        PathCubic {
            x: surface.cutLeft ? surface.footSplit.join.x : 0
            y: surface.cutLeft ? surface.footSplit.join.y : surface.base
            control1X: surface.cutLeft ? (closeBottom ? surface.overlap - surface.footSplit.c.x : surface.footSplit.join.x) : 0
            control1Y: surface.cutLeft ? (closeBottom ? surface.footSplit.c.y : surface.footSplit.join.y) : surface.base
            control2X: surface.cutLeft ? (closeBottom ? surface.overlap - surface.footSplit.e.x : surface.footSplit.join.x) : 0
            control2Y: surface.cutLeft ? (closeBottom ? surface.footSplit.e.y : surface.footSplit.join.y) : surface.base
        }
        PathCubic {
            x: surface.wing - surface.sideInset; y: surface.base - surface.lowerRadius
            control1X: surface.cutLeft ? surface.footSplit.d.x : surface.footWidth / 2
            control1Y: surface.cutLeft ? surface.footSplit.d.y : surface.base
            control2X: surface.cutLeft ? surface.footSplit.a.x : surface.footWidth - surface.tangentRun
            control2Y: surface.cutLeft ? surface.footSplit.a.y : surface.base - surface.lowerRadius + surface.tangentRise
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
            x: surface.width - (surface.cutRight ? surface.footSplit.join.x : 0)
            y: surface.cutRight ? surface.footSplit.join.y : surface.base
            control1X: surface.width - (surface.cutRight ? surface.footSplit.a.x : surface.footWidth - surface.tangentRun)
            control1Y: surface.cutRight ? surface.footSplit.a.y : surface.base - surface.lowerRadius + surface.tangentRise
            control2X: surface.width - (surface.cutRight ? surface.footSplit.d.x : surface.footWidth / 2)
            control2Y: surface.cutRight ? surface.footSplit.d.y : surface.base
        }
        PathCubic {
            x: surface.width - (surface.cutRight ? (closeBottom ? surface.overlap : surface.footSplit.join.x) : 0)
            y: surface.cutRight && !closeBottom ? surface.footSplit.join.y : surface.base
            control1X: surface.width - (surface.cutRight ? (closeBottom ? surface.overlap - surface.footSplit.e.x : surface.footSplit.join.x) : 0)
            control1Y: surface.cutRight ? (closeBottom ? surface.footSplit.e.y : surface.footSplit.join.y) : surface.base
            control2X: surface.width - (surface.cutRight ? (closeBottom ? surface.overlap - surface.footSplit.c.x : surface.footSplit.join.x) : 0)
            control2Y: surface.cutRight ? (closeBottom ? surface.footSplit.c.y : surface.footSplit.join.y) : surface.base
        }
        PathLine {
            x: closeBottom ? startX : surface.width - (surface.cutRight ? surface.footSplit.join.x : 0)
            y: closeBottom ? startY : surface.cutRight ? surface.footSplit.join.y : surface.base
        }
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
