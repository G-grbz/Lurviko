import QtQuick
import QtQuick.Shapes
import Lurviko.App

Item {
    id: surface
    property bool selected: false
    property bool hovered: false
    property bool dropTarget: false
    property bool administrator: false
    // In the main window, the selected tab and page share ONE contour.
    // Keep normal tab outlines for standalone previews and unselected tabs.
    property bool pageOutlineJoined: false
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
    // Raised ("puffy") look: lighter cap fading into the base colour. The
    // bottom keeps fillColor so the selected tab still joins the page seamlessly.
    readonly property color topColor: AppTheme.mix(fillColor, Qt.rgba(1, 1, 1, fillColor.a),
        AppTheme.dark ? (selected ? 0.09 : (hovered ? 0.07 : 0.05))
                      : (selected ? 0.32 : (hovered ? 0.4 : 0.28)))
    readonly property color bottomColor: selected || dropTarget ? fillColor
        : AppTheme.mix(fillColor, Qt.rgba(0, 0, 0, fillColor.a), AppTheme.dark ? 0.10 : 0.025)
    readonly property color outlineColor: dropTarget ? AppTheme.accent
        : administrator ? (AppTheme.useQtTheme ? AppTheme.tint(AppTheme.warning, AppTheme.qtActiveOutlineOpacity) : AppTheme.warning)
        : AppTheme.mix(AppTheme.border, AppTheme.activeTabOutline, selectionProgress)
    Component.onCompleted: animationReady = true
    Behavior on selectionProgress {
        enabled: surface.animationReady && surface.visible
        NumberAnimation { id: selectionTween; duration: 110; easing.type: Easing.OutCubic }
    }
    // Vertical sides with a small concave flare where the tab meets the bar
    // (Chrome / Windows 11), instead of a long slanted S-curve.
    readonly property real wing: Math.min(8, width / 6)
    readonly property real corner: Math.min(8, (height - capTop) / 3)
    // Selection never changes geometry or the text's vertical origin.
    readonly property real capTop: 4
    readonly property real base: height
    readonly property real lowerRadius: Math.min(8, (height - capTop) / 4)
    readonly property real sideInset: 0
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
            fillGradient: LinearGradient {
                x1: 0; y1: surface.capTop
                x2: 0; y2: surface.base
                GradientStop { position: 0; color: surface.topColor }
                GradientStop { position: 0.55; color: AppTheme.mix(surface.topColor, surface.bottomColor, 0.75) }
                GradientStop { position: 1; color: surface.bottomColor }
            }
            strokeColor: "transparent"
            strokeWidth: 0
            closeBottom: true
        }
        Contour {
            fillColor: "transparent"
            // Like Chrome / Windows 11, only the selected tab (and a drop
            // target) is outlined. Inactive tabs are separated by a single
            // divider; their own side outlines would double up at each joint.
            strokeColor: surface.pageOutlineJoined && surface.selected ? "transparent"
                    : surface.selected || surface.dropTarget || !surface.pageOutlineJoined ? surface.outlineColor
                    : "transparent"
            strokeWidth: surface.dropTarget ? 1.5 : surface.administrator ? 0.8 : 1
            // Keep the navigation join open, including during transitions.
            closeBottom: false
        }
    }

    // Upper edge line for inactive tabs (they have no outline). A white
    // gloss vanished on light themes; use a theme-aware edge colour instead.
    readonly property color edgeColor: AppTheme.dark ? Qt.rgba(1, 1, 1, hovered ? 0.24 : 0.16)
                                                     : (hovered ? AppTheme.mix(AppTheme.borderStrong, AppTheme.text, 0.15)
                                                                : AppTheme.borderStrong)
    Rectangle {
        objectName: "fluidTabTopEdge"
        visible: !surface.selected
        x: surface.wing + surface.corner
        y: surface.capTop
        width: Math.max(0, surface.width - 2 * x)
        height: 1
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: "transparent" }
            GradientStop { position: 0.12; color: surface.edgeColor }
            GradientStop { position: 0.88; color: surface.edgeColor }
            GradientStop { position: 1; color: "transparent" }
        }
    }
}
