import QtQuick

// Presentation only: KIO jobs, cancellation and completion remain owned by
// FileOperations. One event-driven timer per pane, never per progress update.
QtObject {
    id: state
    property var operations: []
    property bool nativeProgress: false
    property bool autoHide: true
    property int hideDelayMs: 8000
    property bool hovered: false
    property bool minimized: false
    property bool hidePending: false
    property var hiddenCompletions: ({})
    property var previousActiveIds: []
    readonly property var activeOperations: operations.filter(function(op) { return !op.finished })
    readonly property int activeCount: activeOperations.length
    readonly property string activeKey: activeOperations.map(function(op) { return String(op.id) }).join(",")
    readonly property var visibleOperations: operations.filter(function(op) {
        return op.finished && !op.success || (!state.minimized && !state.hiddenCompletions[String(op.id)])
    })
    property Timer hideTimer: Timer {
        interval: Math.max(1, state.hideDelayMs)
        onTriggered: {
            if (state.hovered) state.hidePending = true
            else state.minimize()
        }
    }
    function scheduleHide() {
        hideTimer.stop()
        hidePending = false
        if (!nativeProgress && autoHide && activeCount > 0 && !minimized)
            hideTimer.restart()
    }
    function minimize() {
        if (nativeProgress || activeCount === 0) return
        const hidden = Object.assign({}, hiddenCompletions)
        for (const op of operations)
            if (op.finished && op.success) hidden[String(op.id)] = true
        hiddenCompletions = hidden
        minimized = true
        hideTimer.stop()
        hidePending = false
    }
    function restore() { minimized = false; scheduleHide() }
    function toggle() { if (minimized) restore(); else minimize() }
    function syncOperations() {
        const hidden = ({})
        for (const op of operations) {
            const id = String(op.id)
            if (op.finished && op.success && (hiddenCompletions[id]
                    || minimized && previousActiveIds.indexOf(id) >= 0)) hidden[id] = true
        }
        hiddenCompletions = hidden
        previousActiveIds = activeOperations.map(function(op) { return String(op.id) })
        if (!activeCount) minimized = false
        scheduleHide()
    }
    onActiveKeyChanged: syncOperations()
    // Progress updates only prune expired completion IDs; they don't restart
    // the timeout. Finished jobs can disappear without changing activeKey.
    onOperationsChanged: {
        const retained = ({})
        for (const op of operations)
            if (hiddenCompletions[String(op.id)]) retained[String(op.id)] = true
        hiddenCompletions = retained
    }
    onNativeProgressChanged: {
        if (nativeProgress) { minimized = false; hiddenCompletions = ({}) }
        scheduleHide()
    }
    onAutoHideChanged: scheduleHide()
    onHideDelayMsChanged: scheduleHide()
    onHoveredChanged: if (!hovered && hidePending) scheduleHide()
}
