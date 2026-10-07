import QtQuick
import QtQuick.Layouts
import QtQuick.Effects
import GFile.App

Rectangle {
    id: preview
    property var itemData: null
    property string thumbnailSource: ""
    property string locationLabel: ""
    readonly property real imageAspect: fullImage.status === Image.Ready && fullImage.implicitHeight > 0
                                       ? fullImage.implicitWidth / fullImage.implicitHeight
                                       : (cachedImage.implicitHeight > 0
                                          ? cachedImage.implicitWidth / cachedImage.implicitHeight : 1)
    readonly property real preferredHeight: (width - 16) / imageAspect + 64
    // This is a visual overlay, so hover and clicks reach the original card.
    enabled: false
    radius: 14
    color: AppTheme.surface
    border.width: 1
    border.color: AppTheme.borderStrong

    Rectangle {
        id: stage
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 8
        height: Math.max(1, preview.height - 64)
        radius: 10
        color: AppTheme.surfaceRaised

        Item {
            id: picture
            anchors.centerIn: parent
            readonly property real aspect: preview.imageAspect
            width: Math.min(stage.width, stage.height * aspect)
            height: Math.min(stage.height, stage.width / aspect)
            layer.enabled: preview.visible
            layer.smooth: true
            layer.effect: MultiEffect { maskEnabled: true; maskSource: pictureMask }
            Image {
                id: cachedImage
                anchors.fill: parent
                source: preview.visible ? preview.thumbnailSource : ""
                sourceSize: Qt.size(512, 512)
                asynchronous: true
                cache: true
                fillMode: Image.PreserveAspectFit
                visible: fullImage.status !== Image.Ready
            }
            Image {
                id: fullImage
                objectName: "hoverPreviewImage"
                anchors.fill: parent
                // Decode just one hovered original at a bounded resolution.
                source: preview.visible && preview.itemData ? preview.itemData.url : ""
                sourceSize: Qt.size(1200, 1200)
                asynchronous: true
                cache: true
                fillMode: Image.PreserveAspectFit
                visible: status === Image.Ready
            }
        }
        Rectangle {
            id: pictureMask
            width: picture.width; height: picture.height
            radius: 8; color: "white"; visible: false
            layer.enabled: preview.visible
            antialiasing: true
        }
    }
    Column {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: stage.bottom
        anchors.margins: 8
        spacing: 4
        Text {
            width: parent.width
            text: preview.itemData ? preview.itemData.name : ""
            color: AppTheme.text
            font.pixelSize: 13
            font.weight: Font.DemiBold
            elide: Text.ElideMiddle
        }
        Text {
            width: parent.width
            text: preview.locationLabel
            color: AppTheme.textMuted
            font.pixelSize: 10
            elide: Text.ElideMiddle
        }
    }
}
