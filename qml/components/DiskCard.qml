import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import Lurviko.App

Rectangle {
    id: card
    property string language: "en"
    property int diskIndex: -1
    property bool diskReorder: true
    property bool dragging: reorderDrag.active
    property bool suppressClick: false
    property int pendingReorderTo: -1
    property string title: "Disk"
    property string stableId: ""
    property string iconName: "drive.svg"
    property string subtitle: ""
    property string fileSystem: ""
    property string rootPath: "/"
    property string device: ""
    property string usedText: ""
    property string totalText: ""
    property real ratio: 0
    property bool mounted: false
    property bool favorite: false
    // Mounted disk images reuse this card: they are not reorderable, cannot be
    // pinned as favorites (their mount point changes) or restyled, and offer
    // "Unmount" in the context menu.
    property bool favoritesEnabled: true
    property bool editable: true
    property bool unmountInMenu: false
    readonly property bool favoriteAvailable: card.favoritesEnabled && card.rootPath.length > 0 && !card.rootPath.startsWith("/dev/")
    property bool canMountToggle: false
    property bool mountBusy: false
    property string mountActionText: ""
    property string mountedStatusText: ""
    property int systemIconPixels: AppTheme.systemHomeIconSize
    // Keep user-selected icons responsive, but constrain artwork to the card's
    // compact header. A large global icon preference must never overpaint its
    // siblings or escape the rounded card at high DPI / narrow widths.
    // Unlike the old 48/62 px clamp, retain the user's real icon preference.
    // The card and its content grow with the image instead of clipping it.
    // Compact disk tile: small icon, one usage line and the bar. Unmounting
    // moves to the context menu.
    property bool compact: false
    readonly property int effectiveIconPixels: compact ? 28 : Math.max(28, Math.min(
            Math.round(AppTheme.effectiveHomeIconSize), Math.max(28, Math.floor(card.width * 0.34))))
    // Folder silhouette: a tab on the upper left, the body below it.
    readonly property real folderTabHeight: compact ? 10 : 14
    readonly property real folderTabWidth: Math.min(card.width * 0.42, 150)
    readonly property real folderRadius: AppTheme.cardRadius
    // The folder tab's slope is the cubic P0..P3 used in the folder path; the
    // sheet starts where that slope crosses the sheet's top line.
    readonly property var sheetGeometry: {
        const w = card.width - 1, o = 0.5
        const t = card.folderTabHeight, tw = card.folderTabWidth
        const tr = Math.min(10, card.folderRadius), slope = 14
        const sy = Math.max(o + 1, t * 0.25), right = w - card.folderRadius, rr = Math.min(6, t - sy)
        const p = [[tw - tr, o], [tw - tr / 3, o], [tw, t], [tw + slope, t]]
        const at = function(u) {
            const v = 1 - u
            return [v*v*v*p[0][0] + 3*v*v*u*p[1][0] + 3*v*u*u*p[2][0] + u*u*u*p[3][0],
                    v*v*v*p[0][1] + 3*v*v*u*p[1][1] + 3*v*u*u*p[2][1] + u*u*u*p[3][1]]
        }
        let lo = 0, hi = 1
        for (let i = 0; i < 30; ++i) { const mid = (lo + hi) / 2; if (at(mid)[1] < sy) lo = mid; else hi = mid }
        const u = (lo + hi) / 2
        // de Casteljau: the part of the slope from u to the body edge.
        const lerp = function(a, b) { return [a[0] + (b[0] - a[0]) * u, a[1] + (b[1] - a[1]) * u] }
        const a1 = lerp(p[0], p[1]), b1 = lerp(p[1], p[2]), c1 = lerp(p[2], p[3])
        const a2 = lerp(a1, b1), b2 = lerp(b1, c1), start = lerp(a2, b2)
        if (right - start[0] < 2 * rr)
            return { fill: "", outline: "" }
        const n = function(v) { return v.toFixed(2) }
        const top = "M " + n(start[0]) + " " + n(start[1])
                  + " L " + n(right - rr) + " " + n(sy)
                  + " Q " + n(right) + " " + n(sy) + " " + n(right) + " " + n(sy + rr)
                  + " L " + n(right) + " " + n(t)
        return {
            outline: top,
            fill: top + " L " + n(p[3][0]) + " " + n(t)
                + " C " + n(c1[0]) + " " + n(c1[1]) + " " + n(b2[0]) + " " + n(b2[1]) + " " + n(start[0]) + " " + n(start[1]) + " Z"
        }
    }
    readonly property int responsiveHeight: compact ? 112 : Math.max(158, effectiveIconPixels + 116)
    readonly property color usageColor: card.ratio >= 0.9 ? AppTheme.danger : AppTheme.accent
    readonly property string percentText: "%" + Math.round(Math.max(0, Math.min(1, card.ratio)) * 100)
    readonly property string mountPillText: card.mountActionText.length ? card.mountActionText : card.mountedStatusText
    readonly property string usageSummaryText: {
        if (card.usedText.length && card.totalText.length)
            return card.usedText + " / " + card.totalText
        if (card.usedText.length)
            return card.usedText
        if (card.totalText.length)
            return card.totalText
        return card.language === "tr" ? "Bilgi yok" : "Unavailable"
    }
    signal openRequested(string path)
    signal openNewTabRequested(string path)
    signal favoriteToggleRequested(string path, string title)
    signal editRequested(string stableId, string title, string iconName)
    signal propertiesRequested(string path, string title, string iconName)
    signal refreshRequested()
    signal mountToggleRequested(string device)
    signal reorderRequested(int from, int to)
    signal reorderDragStateChanged(bool active)

    radius: AppTheme.cardRadius
    // The visible surface is the folder shape below; the item itself is clear.
    color: "transparent"
    // Always opaque: with a translucent window the theme's card surface is
    // only 6% Qt base colour. Disk cards keep the Qt colours, at full opacity.
    readonly property bool glassSurface: AppTheme.useQtTheme && AppTheme.translucentWindow
    readonly property color folderFill: dropTarget.containsDrag
                                  ? (card.glassSurface ? AppTheme.mix(AppTheme.surface, AppTheme.accent, 0.14) : AppTheme.accentSoft)
                                  : (mouse.containsMouse || dragging
                                     ? (card.glassSurface ? AppTheme.mix(AppTheme.surface, AppTheme.accent, 0.05) : AppTheme.cardHover)
                                     : (card.glassSurface ? AppTheme.surface : AppTheme.cardSurface))
    readonly property color folderStroke: dropTarget.containsDrag ? AppTheme.accent
                                          : (mouse.containsMouse ? AppTheme.accentBorder : AppTheme.border)

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        antialiasing: true
        // A sheet tucked into the folder, peeking out beside the tab. It is
        // cut exactly along the tab's slope and the body's top edge, so it
        // never lies behind the (possibly translucent, Qt theme) folder.
        ShapePath {
            strokeColor: "transparent"
            fillColor: AppTheme.dark ? AppTheme.mix(card.folderFill, Qt.rgba(1, 1, 1, card.folderFill.a), 0.07)
                                     : AppTheme.mix(card.folderFill, Qt.rgba(AppTheme.text.r, AppTheme.text.g, AppTheme.text.b,
                                                                             card.folderFill.a), 0.06)
            PathSvg { path: card.sheetGeometry.fill }
        }
        ShapePath {
            fillColor: "transparent"
            strokeWidth: 1
            // Scale the stroke's own alpha: Qt theme borders are a translucent text colour.
            strokeColor: Qt.rgba(card.folderStroke.r, card.folderStroke.g, card.folderStroke.b,
                                 card.folderStroke.a * (AppTheme.dark ? 0.6 : 0.8))
            PathSvg { path: card.sheetGeometry.outline }
        }
        ShapePath {
            fillColor: card.folderFill
            strokeColor: card.folderStroke
            strokeWidth: dropTarget.containsDrag ? 2 : 1
            PathSvg {
                path: {
                    const w = card.width - 1, h = card.height - 1, o = 0.5
                    const t = card.folderTabHeight, tw = card.folderTabWidth
                    const r = card.folderRadius, tr = Math.min(10, r), slope = 14
                    return "M " + o + " " + (h - r)
                        + " L " + o + " " + (tr + o)
                        + " Q " + o + " " + o + " " + (tr + o) + " " + o
                        + " L " + (tw - tr) + " " + o
                        + " C " + (tw - tr / 3) + " " + o + " " + tw + " " + t + " " + (tw + slope) + " " + t
                        + " L " + (w - r) + " " + t
                        + " Q " + w + " " + t + " " + w + " " + (t + r)
                        + " L " + w + " " + (h - r)
                        + " Q " + w + " " + h + " " + (w - r) + " " + h
                        + " L " + (r + o) + " " + h
                        + " Q " + o + " " + h + " " + o + " " + (h - r) + " Z"
                }
            }
        }
    }
    scale: dragging ? 0.97 : 1.0
    opacity: dragging ? 0.72 : 1.0
    clip: true

    Drag.dragType: Drag.Automatic
    Drag.active: reorderDrag.active
    Drag.source: card
    Drag.supportedActions: Qt.MoveAction
    Drag.proposedAction: Qt.MoveAction

    Timer { id: dragClickReset; interval: 90; onTriggered: card.suppressClick = false }

    DropArea {
        id: dropTarget
        anchors.fill: parent
        onDropped: function(drop) {
            if (card.diskReorder && drop.source && drop.source.diskReorder && drop.source.diskIndex !== card.diskIndex) {
                drop.source.pendingReorderTo = card.diskIndex
                drop.acceptProposedAction()
            }
        }
    }

    MouseArea {
        id: mouse
        z: 1
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
        onClicked: function(event) {
            if (card.suppressClick) {
                event.accepted = true
                return
            }
            if (event.button === Qt.RightButton) {
                diskContextMenu.popup(event.x, event.y)
                return
            }
            if (!card.mounted && card.canMountToggle) {
                card.mountToggleRequested(card.device)
                return
            }
            if (event.button === Qt.MiddleButton)
                card.openNewTabRequested(card.rootPath)
            else
                card.openRequested(card.rootPath)
        }
    }

    GMenu {
        id: diskContextMenu
        GMenuItem {
            text: card.language === "tr" ? "Yeni sekmede aç" : "Open in new tab"
            enabled: card.mounted && card.rootPath.length > 0
            onTriggered: card.openNewTabRequested(card.rootPath)
        }
        GMenuItem {
            text: card.favorite
                ? (card.language === "tr" ? "Favorilerden çıkar" : "Remove from favorites")
                : (card.language === "tr" ? "Favorilere ekle" : "Add to favorites")
            enabled: card.favoriteAvailable && card.mounted
            onTriggered: card.favoriteToggleRequested(card.rootPath, card.title)
        }
        GMenuItem {
            text: card.language === "tr" ? "Yenile" : "Refresh"
            onTriggered: card.refreshRequested()
        }
        MenuSeparator { }
        GMenuItem {
            visible: card.unmountInMenu
            text: card.language === "tr" ? "Bağlantıyı kes" : "Unmount"
            enabled: card.canMountToggle && card.mounted && !card.mountBusy
            onTriggered: card.mountToggleRequested(card.device)
        }
        GMenuItem {
            text: card.language === "tr" ? "Özellikler" : "Properties"
            enabled: card.mounted && !card.rootPath.startsWith("/dev/")
            onTriggered: card.propertiesRequested(card.rootPath, card.title, card.iconName)
        }
        GMenuItem {
            visible: card.editable
            text: card.language === "tr" ? "Düzenle" : "Edit"
            onTriggered: card.editRequested(card.stableId, card.title, card.iconName)
        }
    }

    DragHandler {
        id: reorderDrag
        target: null
        enabled: card.diskReorder
        acceptedButtons: Qt.LeftButton
        onActiveChanged: {
            if (active) {
                card.pendingReorderTo = -1
                card.reorderDragStateChanged(true)
                return
            }
            card.suppressClick = true
            dragClickReset.restart()
            if (card.pendingReorderTo >= 0 && card.pendingReorderTo !== card.diskIndex) {
                const destination = card.pendingReorderTo
                card.pendingReorderTo = -1
                card.reorderRequested(card.diskIndex, destination)
            } else {
                card.pendingReorderTo = -1
            }
            card.reorderDragStateChanged(false)
        }
    }

    ColumnLayout {
        // Keep actionable children above the card-wide navigation hit area.
        z: 2
        anchors.fill: parent
        anchors.margins: 12
        anchors.topMargin: card.folderTabHeight + 12
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            spacing: 9

            Item {
                clip: true
                Layout.preferredWidth: card.effectiveIconPixels
                Layout.preferredHeight: card.effectiveIconPixels
                Layout.alignment: Qt.AlignTop
                CrispIcon {
                    anchors.fill: parent
                    source: AppTheme.useSystemIcons
                            ? AppTheme.systemIconAtSize(AppTheme.discoveryIcon(card.iconName, "drive.svg"), card.effectiveIconPixels)
                            : AppTheme.discoveryIcon(card.iconName, "drive.svg")
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                spacing: 3
                GText {
                    Layout.fillWidth: true
                    text: card.title
                    baseColor: AppTheme.text
                    basePixelSize: 14
                    baseFontWeight: Font.DemiBold
                    elide: Text.ElideRight
                }
                GText {
                    Layout.fillWidth: true
                    text: card.subtitle
                    baseColor: AppTheme.textMuted
                    basePixelSize: 10
                    elide: Text.ElideMiddle
                }
            }

            // Both metadata and favorite are centered within the SAME row;
            // previously the filesystem badge lived in the title's nested
            // layout while the star aligned to the top of the whole card.
            RowLayout {
                Layout.alignment: Qt.AlignVCenter
                spacing: 8
                DiskInfoChip {
                    visible: card.fileSystem.length > 0 && !card.compact
                    text: card.fileSystem
                    emphasis: false
                    Layout.alignment: Qt.AlignVCenter
                }
                DiskIconActionButton {
                    visible: card.favoriteAvailable
                    iconSource: AppTheme.icon("favorite.svg")
                    iconOpacity: card.favorite ? 1.0 : 0.35
                    Layout.alignment: Qt.AlignVCenter
                    onClicked: card.favoriteToggleRequested(card.rootPath, card.title)
                }
            }
        }

        // Push storage usage to the lower edge at any font or icon scale.
        Item { Layout.fillHeight: true }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            GText {
                Layout.fillWidth: true
                text: card.compact ? card.usageSummaryText
                      : (card.usedText.length && card.totalText.length ? card.usedText : card.usageSummaryText)
                baseColor: AppTheme.text
                basePixelSize: card.compact ? 11 : 13
                baseFontWeight: Font.DemiBold
                elide: Text.ElideRight
            }
            GText {
                visible: card.compact && card.mounted && card.totalText.length > 0
                text: card.percentText
                baseColor: card.usageColor
                basePixelSize: 11
                baseFontWeight: Font.DemiBold
            }
            GText {
                visible: !card.compact && card.usedText.length > 0 && card.totalText.length > 0
                text: card.totalText
                baseColor: AppTheme.textMuted
                basePixelSize: 12
                baseFontWeight: Font.Medium
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 6
                radius: height / 2
                color: AppTheme.surfaceHover
                clip: true
                Rectangle {
                    width: parent.width * Math.max(0, Math.min(1, card.ratio))
                    height: parent.height
                    radius: height / 2
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: Qt.lighter(card.usageColor, 1.38) }
                        GradientStop { position: 1.0; color: Qt.darker(card.usageColor, 1.08) }
                    }
                }
            }
        }

        RowLayout {
            visible: !card.compact
            Layout.fillWidth: true
            spacing: 8
            DiskMountBadge {
                id: mountBadge
                visible: card.mountPillText.length > 0
                Layout.alignment: Qt.AlignVCenter | Qt.AlignLeft
            }
            Item { Layout.fillWidth: true }
            DiskInfoChip {
                text: card.percentText
                emphasis: true
                emphasisColor: card.usageColor
            }
        }

    }

    component DiskIconActionButton: Item {
        id: actionButton
        property url iconSource: ""
        property real iconOpacity: 1.0
        signal clicked()
        Layout.preferredWidth: visible ? 24 : 0
        Layout.preferredHeight: visible ? 24 : 0
        Layout.alignment: Qt.AlignVCenter
        z: 5

        Rectangle {
            anchors.fill: parent
            radius: 9
            color: actionMouse.containsMouse ? AppTheme.surfaceRaised : "transparent"
        }
        CrispIcon {
            anchors.centerIn: parent
            source: actionButton.iconSource
            width: 17
            height: 17
            opacity: actionButton.iconOpacity
        }
        MouseArea {
            id: actionMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: actionButton.clicked()
        }
    }

    component DiskInfoChip: Rectangle {
        id: chip
        property string text: ""
        property bool emphasis: false
        property color emphasisColor: AppTheme.accent
        radius: 11
        implicitHeight: 22
        implicitWidth: chipText.implicitWidth + 16
        color: emphasis
               ? AppTheme.tint(emphasisColor, AppTheme.dark ? 0.20 : 0.12)
               : AppTheme.surfaceRaised
        border.width: 1
        border.color: emphasis
                     ? AppTheme.tint(emphasisColor, AppTheme.dark ? 0.55 : 0.40)
                     : AppTheme.border
        GText {
            id: chipText
            anchors.centerIn: parent
            text: chip.text
            baseColor: emphasis ? chip.emphasisColor : AppTheme.textMuted
            basePixelSize: 10
            baseFontWeight: Font.DemiBold
        }
    }

    component DiskMountBadge: Rectangle {
        id: mountChip
        // The card's full-surface navigation MouseArea sits at z:1;
        // keep the mount action above it so clicks never open the folder.
        z: 6
        radius: 11
        implicitHeight: 24
        implicitWidth: Math.max(86, mountBadgeRow.implicitWidth + 14)
        color: {
            if (card.mountBusy)
                return AppTheme.surfaceRaised
            if (!card.canMountToggle)
                return card.mounted ? AppTheme.tint(AppTheme.success, AppTheme.dark ? 0.20 : 0.12) : AppTheme.surfaceRaised
            if (mountBadgeMouse.containsMouse)
                return card.mounted ? AppTheme.tint(AppTheme.danger, AppTheme.dark ? 0.16 : 0.10)
                                    : AppTheme.tint(AppTheme.accent, AppTheme.dark ? 0.18 : 0.10)
            return AppTheme.surfaceRaised
        }
        border.width: 1
        border.color: {
            if (card.mountBusy)
                return AppTheme.accentBorder
            if (!card.canMountToggle)
                return card.mounted ? AppTheme.success : AppTheme.border
            return card.mounted ? (mountBadgeMouse.containsMouse ? AppTheme.danger : AppTheme.border)
                                : (mountBadgeMouse.containsMouse ? AppTheme.accent : AppTheme.border)
        }

        RowLayout {
            id: mountBadgeRow
            anchors.centerIn: parent
            spacing: 6
            GBusyIndicator {
                visible: card.mountBusy
                running: visible
                Layout.preferredWidth: 12
                Layout.preferredHeight: 12
            }
            GText {
                text: card.mountPillText
                baseColor: card.mountBusy ? AppTheme.textMuted
                          : (!card.canMountToggle && card.mounted ? AppTheme.success
                             : (card.mounted && card.canMountToggle && mountBadgeMouse.containsMouse ? AppTheme.danger
                                : (mountBadgeMouse.containsMouse ? AppTheme.accent : AppTheme.textMuted)))
                basePixelSize: 10
                baseFontWeight: Font.DemiBold
            }
        }

        MouseArea {
            id: mountBadgeMouse
            anchors.fill: parent
            enabled: card.canMountToggle && !card.mountBusy
            hoverEnabled: true
            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: card.mountToggleRequested(card.device)
        }
    }
}
