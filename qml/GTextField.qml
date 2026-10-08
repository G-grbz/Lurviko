import QtQuick
import QtQuick.Controls
import Lurviko.App

TextField {
    id: control
    // Media players can supply a fixed palette; ordinary controls follow AppTheme.
    property var colorTheme: AppTheme

    implicitHeight: AppTheme.controlHeight
    leftPadding: 12
    rightPadding: 12
    topPadding: 0
    bottomPadding: 0

    color: enabled ? control.colorTheme.text : control.colorTheme.textFaint
    placeholderTextColor: control.colorTheme.textFaint
    selectionColor: control.colorTheme.selection
    selectedTextColor: "white"
    font.pixelSize: AppTheme.fontSizeControl
    selectByMouse: true
    verticalAlignment: TextInput.AlignVCenter

    HoverHandler {
        cursorShape: control.enabled ? Qt.IBeamCursor : Qt.ArrowCursor
    }

    background: Rectangle {
        radius: AppTheme.fieldRadius
        color: {
            if (!control.enabled)
                return control.colorTheme.surfaceSunken
            if (control.activeFocus)
                return control.colorTheme.surfaceRaised
            return control.hovered ? control.colorTheme.surfaceHover : control.colorTheme.surfaceRaised
        }
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
}
