import QtQuick
import Lurviko.App

Rectangle {
    objectName: "folderDropBadge"
    width: 24; height: 24
    radius: 8
    color: AppTheme.dropTargetColor
    border.width: 1
    border.color: AppTheme.surface
    z: 100
    CrispIcon {
        objectName: "folderDropBadgeIcon"
        anchors.centerIn: parent
        width: 16; height: 16
        source: AppTheme.icon("plus.svg", "white")
    }
}
