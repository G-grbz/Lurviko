import QtQuick

Item {
    id: root

    property var entries: []
    property int itemCount: 1
    property string primaryName: ""
    property bool turkish: false

    width: 278
    height: 92
    visible: false

    Rectangle {
        visible: root.itemCount > 2
        x: 12
        y: 10
        width: parent.width - 18
        height: parent.height - 16
        radius: 14
        color: AppTheme.dark ? "#172033" : "#E6E8F1"
        border.width: 1
        border.color: AppTheme.borderStrong
        rotation: 2.0
        opacity: 0.74
    }

    Rectangle {
        visible: root.itemCount > 1
        x: 7
        y: 6
        width: parent.width - 14
        height: parent.height - 12
        radius: 14
        color: AppTheme.dark ? "#1B263C" : "#F0F1F7"
        border.width: 1
        border.color: AppTheme.accentBorder
        rotation: -1.2
        opacity: 0.9
    }

    Rectangle {
        x: 2
        y: 2
        width: parent.width - 12
        height: parent.height - 12
        radius: 14
        color: AppTheme.surfaceRaised
        border.width: 1
        border.color: AppTheme.accent

        Rectangle {
            anchors.fill: parent
            anchors.margins: 4
            radius: 11
            color: "transparent"
            border.width: 1
            border.color: AppTheme.dark ? "#20FFFFFF" : "#16000000"
        }

        Item {
            id: iconStack
            x: 14
            anchors.verticalCenter: parent.verticalCenter
            width: 64
            height: 54

            Repeater {
                model: Math.min(3, root.entries ? root.entries.length : 0)

                Rectangle {
                    width: 42
                    height: 42
                    x: index * 8
                    y: (2 - index) * 4
                    radius: 10
                    color: AppTheme.surface
                    border.width: 1
                    border.color: index === 0 ? AppTheme.accent : AppTheme.borderStrong

                    CrispIcon {
                        anchors.fill: parent
                        anchors.margins: 6
                        source: root.entries[index] ? root.entries[index].iconSource : ""
                        fillMode: Image.PreserveAspectFit
                    }
                }
            }
        }

        Column {
            x: 88
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 142
            spacing: 3

            Text {
                width: parent.width
                text: root.primaryName
                color: AppTheme.text
                font.pixelSize: 12
                font.weight: Font.DemiBold
                elide: Text.ElideMiddle
                maximumLineCount: 1
            }

            Text {
                width: parent.width
                text: root.itemCount > 1
                      ? (root.itemCount + (root.turkish ? " öğe seçili" : " items selected"))
                      : (root.turkish ? "Sürükleniyor" : "Dragging")
                color: AppTheme.textMuted
                font.pixelSize: 10
                elide: Text.ElideRight
            }
        }

        Rectangle {
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.top: parent.top
            anchors.topMargin: 10
            visible: root.itemCount > 1
            width: Math.max(28, countText.implicitWidth + 14)
            height: 28
            radius: 14
            color: AppTheme.accent

            Text {
                id: countText
                anchors.centerIn: parent
                text: root.itemCount
                color: "white"
                font.pixelSize: 11
                font.bold: true
            }
        }
    }
}
