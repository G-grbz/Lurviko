import QtQuick
import QtQuick.Controls
import Lurviko.App

MenuSeparator {
    id: control
    topPadding: visible ? 6 : 0
    bottomPadding: visible ? 6 : 0
    leftPadding: 8
    rightPadding: 8
    implicitHeight: visible ? 13 : 0
    height: implicitHeight

    contentItem: Rectangle {
        implicitHeight: control.visible ? 1 : 0
        color: AppTheme.border
        radius: 1
    }
}
