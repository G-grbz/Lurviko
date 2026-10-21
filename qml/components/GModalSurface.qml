import QtQuick
import Lurviko.App

Item {
    id: surface
    property color accentColor: AppTheme.accent
    property alias sourceItem: nativeSurface.sourceItem

    GOverlaySurface {
        id: nativeSurface
        objectName: "nativeOverlaySurface"
        anchors.fill: parent
        radius: 28
        visible: AppTheme.useQtTheme
    }

    Rectangle {
        anchors.fill: parent
        anchors.topMargin: 10
        radius: 28
        color: AppTheme.shadow
        visible: !AppTheme.useQtTheme
        opacity: AppTheme.dark ? 0.32 : 0.20
    }

    Rectangle {
        anchors.fill: parent
        radius: 28
        visible: !AppTheme.useQtTheme
        border.width: 1
        border.color: AppTheme.dark ? Qt.lighter(AppTheme.borderStrong, 1.08) : AppTheme.borderStrong
        gradient: Gradient {
            GradientStop { position: 0.0; color: AppTheme.useQtTheme ? AppTheme.surface : (AppTheme.dark ? Qt.lighter(AppTheme.surface, 1.08) : "#FFFFFF") }
            GradientStop { position: 0.22; color: AppTheme.surface }
            GradientStop { position: 1.0; color: AppTheme.useQtTheme ? AppTheme.surface : (AppTheme.dark ? Qt.darker(AppTheme.surface, 1.035) : "#FBFCFE") }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: 30
        anchors.rightMargin: 30
        height: 4
        radius: 2
        color: surface.accentColor
        opacity: 0.82
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: 27
        color: "transparent"
        border.width: 1
        border.color: AppTheme.dark ? "#18FFFFFF" : "#70FFFFFF"
        visible: !AppTheme.useQtTheme
    }
}
