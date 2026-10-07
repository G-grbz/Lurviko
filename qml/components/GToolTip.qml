import QtQuick
import QtQuick.Controls

ToolTip {
    id: control
    // Media players can supply a fixed palette; ordinary controls follow AppTheme.
    property var colorTheme: AppTheme

    property int maximumWidth: 360

    visible: parent && parent.hovered === true
    delay: 420
    timeout: 5000
    margins: 8

    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? parent.height + 7 : 0

    topPadding: 8
    bottomPadding: 8
    leftPadding: 16
    rightPadding: 12

    implicitWidth: Math.min(maximumWidth,
                            Math.max(42, contentItem.implicitWidth + leftPadding + rightPadding))
    implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding

    contentItem: Text {
        text: control.text
        color: "#F7F8FC"
        font.pixelSize: 11
        font.weight: Font.Medium
        lineHeight: 1.08
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignLeft
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        radius: 9
        color: control.colorTheme.dark ? "#1A2438" : "#232938"
        border.width: 1
        border.color: control.colorTheme.dark ? "#3A4862" : "#353D50"

        Rectangle {
            anchors.left: parent.left
            anchors.leftMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            width: 3
            height: Math.max(10, parent.height - 14)
            radius: 1.5
            color: control.colorTheme.accent
        }
    }

    enter: Transition {
        NumberAnimation {
            property: "opacity"
            from: 0
            to: 1
            duration: 120
            easing.type: Easing.OutCubic
        }
    }

    exit: Transition {
        NumberAnimation {
            property: "opacity"
            from: 1
            to: 0
            duration: 90
            easing.type: Easing.InCubic
        }
    }
}
