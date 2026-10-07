import QtQuick
import QtQuick.Controls
import GFile.App

Popup {
    id: control
    modal: true
    focus: true
    padding: 20
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: GModalSurface { }

    Overlay.modal: Rectangle {
        color: AppTheme.dark ? "#A0060A12" : "#730F1728"
        Behavior on opacity { NumberAnimation { duration: 140 } }
    }

    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 155; easing.type: Easing.OutCubic }
            NumberAnimation { property: "scale"; from: 0.965; to: 1; duration: 190; easing.type: Easing.OutBack; easing.overshoot: 0.45 }
        }
    }

    exit: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 110; easing.type: Easing.InCubic }
            NumberAnimation { property: "scale"; from: 1; to: 0.985; duration: 120; easing.type: Easing.InCubic }
        }
    }
}
