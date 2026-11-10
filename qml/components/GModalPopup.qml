import QtQuick
import QtQuick.Controls
import Lurviko.App

Popup {
    id: control
    modal: true
    focus: true
    padding: 20
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: GModalSurface { }

    // Blurred, desaturated application behind the modal. Its opacity follows
    // the popup's own enter/exit animation.
    Overlay.modal: GModalBackdrop {
        opacity: control.opacity
    }

    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 170; easing.type: Easing.OutCubic }
            NumberAnimation { property: "scale"; from: 0.94; to: 1; duration: 280; easing.type: Easing.OutBack; easing.overshoot: 0.9 }
        }
    }

    exit: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 120; easing.type: Easing.InCubic }
            NumberAnimation { property: "scale"; from: 1; to: 0.97; duration: 120; easing.type: Easing.InCubic }
        }
    }
}
