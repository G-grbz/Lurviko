import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lurviko.App

Rectangle {
    id: control

    property string title: ""
    property int value: 20
    property int from: 14
    property int to: 32
    property var allowedValues: []
    readonly property bool discreteValues: allowedValues && allowedValues.length > 0
    signal valueEdited(int value)
    signal editingStarted()
    signal editingFinished()

    function nearestAllowedIndex(currentValue) {
        if (!discreteValues)
            return currentValue
        var bestIndex = 0
        var bestDistance = Math.abs(Number(allowedValues[0]) - currentValue)
        for (var i = 1; i < allowedValues.length; ++i) {
            var distance = Math.abs(Number(allowedValues[i]) - currentValue)
            if (distance < bestDistance) {
                bestIndex = i
                bestDistance = distance
            }
        }
        return bestIndex
    }

    implicitHeight: 78
    radius: 16
    color: AppTheme.surfaceSunken
    border.color: AppTheme.border

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        anchors.topMargin: 10
        anchors.bottomMargin: 10
        spacing: 5

        RowLayout {
            Layout.fillWidth: true
            GText {
                Layout.fillWidth: true
                text: control.title
                baseColor: AppTheme.text
                basePixelSize: 11
                baseFontWeight: Font.Medium
            }
            Rectangle {
                implicitWidth: valueText.implicitWidth + 14
                implicitHeight: 23
                radius: 10
                color: AppTheme.accentSoft
                border.width: 1
                border.color: AppTheme.accentBorder
                GText {
                    id: valueText
                    anchors.centerIn: parent
                    text: control.value + " px"
                    baseColor: AppTheme.accent
                    basePixelSize: 10
                    baseBold: true
                }
            }
        }

        GSlider {
            id: slider
            Layout.fillWidth: true
            Layout.preferredHeight: 22
            from: control.discreteValues ? 0 : control.from
            to: control.discreteValues ? Math.max(0, control.allowedValues.length - 1) : control.to
            stepSize: 1
            value: control.discreteValues ? control.nearestAllowedIndex(control.value) : control.value
            onMoved: {
                if (control.discreteValues)
                    control.valueEdited(Number(control.allowedValues[Math.round(value)]))
                else
                    control.valueEdited(Math.round(value))
            }
            onPressedChanged: {
                if (pressed)
                    control.editingStarted()
                else
                    control.editingFinished()
            }

            background: Rectangle {
                x: slider.leftPadding
                y: slider.topPadding + slider.availableHeight / 2 - height / 2
                width: slider.availableWidth
                height: 5
                radius: 3
                color: AppTheme.border

                Rectangle {
                    width: slider.visualPosition * parent.width
                    height: parent.height
                    radius: parent.radius
                    color: AppTheme.accent
                }
            }

            handle: Rectangle {
                x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
                y: slider.topPadding + slider.availableHeight / 2 - height / 2
                width: 17
                height: 17
                radius: 9
                color: slider.pressed ? AppTheme.accent : AppTheme.surface
                border.color: AppTheme.accent
                border.width: 2
            }
        }
    }
}
