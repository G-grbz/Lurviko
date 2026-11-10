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

    // Same wheel/trackpad response as the file views, sidebar and Home
    // (BrowserPane.scrollViewFromWheel): notch-based steps with short-burst
    // acceleration, doubled trackpad deltas and a distance-scaled glide.
    NumberAnimation {
        id: movement
        target: control.view
        property: "contentY"
        easing.type: Easing.OutCubic
    }

    function normalizedDelta(event) {
        const angleY = event.angleDelta.y
        const pixelY = event.pixelDelta.y
        const mousePixelOnly = angleY === 0 && pixelY !== 0
                               && (event.phase === Qt.NoScrollPhase
                                   || (event.device && event.device.type === PointerDevice.Mouse))
        if (angleY !== 0 || mousePixelOnly) {
            const now = Date.now()
            const direction = (angleY !== 0 ? angleY : pixelY) > 0 ? 1 : -1
            const gap = lastWheelTime > 0 ? now - lastWheelTime : 9999
            burstCount = direction !== lastDirection || gap > 300 ? 0 : Math.min(5, burstCount + 1)
            lastWheelTime = now
            lastDirection = direction
            const rawFraction = angleY !== 0 ? Math.abs(angleY) / 120.0 : Math.abs(pixelY) / 30.0
            const stepFraction = Math.max(0.125, Math.min(2.0, rawFraction))
            return direction * AppTheme.wheelScrollStep * stepFraction * (1.0 + burstCount * 0.08)
        }
        if (pixelY !== 0) {
            burstCount = 0
            lastDirection = 0
            lastWheelTime = 0
            return pixelY * 2.0
        }
        return 0
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
            event.accepted = true
            const delta = control.normalizedDelta(event)
            if (delta === 0)
                return
            const top = view.originY || 0
            const bottom = top + Math.max(0, view.contentHeight - view.height)
            const continuing = movement.running
            const previousTarget = continuing ? movement.to : view.contentY
            const sameDirection = !continuing || Math.sign(previousTarget - view.contentY) === -Math.sign(delta)
            const base = sameDirection ? previousTarget : view.contentY
            const limit = Math.max(AppTheme.wheelScrollStep * 3, view.height * 1.8, 288)
            const destination = Math.max(top, Math.min(bottom,
                    Math.max(view.contentY - limit, Math.min(view.contentY + limit, base - delta))))
            movement.stop()
            view.cancelFlick()
            if (Math.abs(destination - view.contentY) < 0.5)
                return
            movement.from = view.contentY
            movement.to = destination
            movement.duration = Math.min(400, Math.max(220, 160 + Math.abs(destination - view.contentY) / 4))
            movement.start()
        }
    }
}
