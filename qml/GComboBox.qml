import QtQuick
import QtQuick.Controls
import GFile.App

ComboBox {
    id: control
    // Media players can supply a fixed palette; ordinary controls follow AppTheme.
    property var colorTheme: AppTheme

    implicitWidth: 160
    implicitHeight: AppTheme.controlHeight
    leftPadding: 12
    rightPadding: 38
    topPadding: 0
    bottomPadding: 0
    spacing: 8
    hoverEnabled: true
    font.pixelSize: AppTheme.fontSizeControl

    // Keep Qt ComboBox's editable semantics intact: its template wires an
    // editable TextField content item to editText, completion and validators.
    contentItem: TextField {
        leftPadding: 0
        rightPadding: 0
        topPadding: 0
        bottomPadding: 0
        text: control.editable ? control.editText : control.displayText
        enabled: control.editable
        autoScroll: control.editable
        readOnly: control.down
        inputMethodHints: control.inputMethodHints
        validator: control.validator
        selectByMouse: control.selectTextByMouse
        font: control.font
        color: control.enabled ? control.colorTheme.text : control.colorTheme.textFaint
        selectionColor: control.colorTheme.selection
        selectedTextColor: "white"
        verticalAlignment: Text.AlignVCenter
        background: null
    }

    indicator: Item {
        width: 28
        height: control.height
        anchors.right: parent.right
        anchors.rightMargin: 5

        Text {
            anchors.centerIn: parent
            text: "⌄"
            color: control.enabled ? control.colorTheme.textMuted : control.colorTheme.textFaint
            font.pixelSize: 17
            rotation: control.popup.visible ? 180 : 0

            Behavior on rotation {
                NumberAnimation { duration: AppTheme.motionNormal; easing.type: Easing.OutCubic }
            }
        }
    }

    background: Rectangle {
        radius: AppTheme.fieldRadius
        color: control.down ? control.colorTheme.surfacePressed
                            : (control.hovered ? control.colorTheme.surfaceHover : control.colorTheme.surfaceRaised)
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? control.colorTheme.accent
                                          : (control.hovered ? control.colorTheme.borderStrong : control.colorTheme.border)

        Behavior on color {
            ColorAnimation { duration: AppTheme.motionFast }
        }
        Behavior on border.color {
            ColorAnimation { duration: AppTheme.motionFast }
        }
    }

    delegate: ItemDelegate {
        required property int index
        width: control.popup ? control.popup.width - 12 : control.width
        height: 36
        hoverEnabled: true
        leftPadding: 10
        rightPadding: 10
        text: control.textAt(index)
        highlighted: control.highlightedIndex === index

        contentItem: Text {
            text: parent.text
            color: parent.enabled ? control.colorTheme.text : control.colorTheme.textFaint
            font.pixelSize: AppTheme.fontSizeBody
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        background: Rectangle {
            radius: AppTheme.radiusSmall
            color: parent.highlighted ? control.colorTheme.accentSoft : "transparent"
            border.width: parent.highlighted ? 1 : 0
            border.color: control.colorTheme.accentBorder
        }
    }

    popup: Popup {
        y: control.height + 6
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + topPadding + bottomPadding, 320)
        padding: 6

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            spacing: 2
            boundsBehavior: Flickable.StopAtBounds
            ScrollIndicator.vertical: ScrollIndicator { }
        }

        background: Rectangle {
            radius: AppTheme.buttonRadius
            color: control.colorTheme.surface
            border.width: 1
            border.color: control.colorTheme.borderStrong
        }

        enter: Transition {
            ParallelAnimation {
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: AppTheme.motionFast }
                NumberAnimation { property: "scale"; from: 0.98; to: 1; duration: AppTheme.motionNormal; easing.type: Easing.OutCubic }
            }
        }
        exit: Transition {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: AppTheme.motionFast }
        }
    }
}
