import QtQuick
import Lurviko.App

Item {
    id: control
    property var view: null
    property var scrollbar: null
    property real lastWheelTime: 0
    property int burstCount: 0
    property int lastDirection: 0

    function reset() {
        movement.stop()
        lastWheelTime = 0
        burstCount = 0
        lastDirection = 0
    }

    onViewChanged: reset()
    onEnabledChanged: if (!enabled) reset()

    Connections {
        target: control.scrollbar
        function onPressedChanged() { if (control.scrollbar.pressed) control.reset() }
    }
    Connections {
        target: control.view
        function onDraggingChanged() { if (control.view.dragging) control.reset() }
    }

    NumberAnimation {
        id: movement
        target: control.view
        property: "contentY"
        duration: 140
        easing.type: Easing.OutCubic
    }

    WheelHandler {
        target: null
        blocking: true
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        acceptedModifiers: Qt.NoModifier
        onWheel: function(event) {
            const view = control.view
            if (!view || view.contentHeight <= view.height)
                return
            const pixels = event.pixelDelta.y
            const angle = event.angleDelta.y
            const touchpad = pixels !== 0 && (event.phase !== Qt.NoScrollPhase
                    || (event.device && event.device.type === PointerDevice.TouchPad))
            let delta = pixels
            if (!touchpad) {
                const value = angle !== 0 ? angle : pixels
                if (!value) return
                const now = Date.now()
                const direction = Math.sign(value)
                control.burstCount = direction === control.lastDirection && now - control.lastWheelTime < 300
                        ? Math.min(5, control.burstCount + 1) : 0
                control.lastDirection = direction
                control.lastWheelTime = now
                delta = direction * AppTheme.wheelScrollStep
                        * Math.min(2, Math.max(0.125, Math.abs(value) / (angle !== 0 ? 120 : 30)))
                        * (1 + control.burstCount * 0.08)
            }
            const top = view.originY || 0
            const bottom = top + Math.max(0, view.contentHeight - view.height)
            const continuing = movement.running && Math.sign(movement.to - view.contentY) === -Math.sign(delta)
            const destination = Math.max(top, Math.min(bottom,
                    (continuing ? movement.to : view.contentY) - delta))
            movement.stop()
            view.cancelFlick()
            if (touchpad) {
                view.contentY = destination
            } else {
                movement.from = view.contentY
                movement.to = destination
                movement.start()
            }
            event.accepted = true
        }
    }
}
