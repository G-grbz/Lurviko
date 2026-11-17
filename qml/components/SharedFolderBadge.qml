import QtQuick
import Lurviko.App
import Lurviko.Backend

// Icon-only badge of a folder shared on the network (Samba usershare).
// The set of shared folders is read once and kept current by SambaShares.
Rectangle {
    id: badge
    objectName: "sharedFolderBadge"
    property string path: ""
    property bool folder: true
    property string language: "en"
    property real size: 18
    readonly property bool shared: {
        const revision = SambaShares.revision
        return folder && path.length > 0 && SambaShares.isShared(path)
    }
    visible: shared
    width: size; height: size; radius: size / 2
    color: AppTheme.surfaceRaised
    border.color: AppTheme.accent
    CrispIcon {
        anchors.centerIn: parent
        width: Math.round(badge.size * 0.66)
        height: width
        source: AppTheme.icon("share.svg", AppTheme.accent)
    }
    HoverHandler { id: hover }
    GToolTip {
        visible: badge.visible && hover.hovered
        text: badge.language === "tr" ? "Ağda paylaşılıyor (Samba)" : "Shared on the network (Samba)"
    }
}
