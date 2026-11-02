import QtQuick

// Presentation only: KIO jobs, cancellation and completion remain owned by
// FileOperations. One event-driven timer per pane, never per progress update.
QtObject {
    id: state
    property var operations: []
    property bool nativeProgress: false
    property bool autoHide: true
    // Show the finished card again after it was automatically minimized.
    // This option is only meaningful while autoHide is enabled.
    property bool showOnCompletion: false
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
        // The operation IDs that were active at the last transition identify
        // genuine completions (as opposed to old finished cards in the model).
        const justFinished = operations.filter(function(op) {
            return op.finished && previousActiveIds.indexOf(String(op.id)) >= 0
        }).map(function(op) { return String(op.id) })
        const reveal = minimized && autoHide && showOnCompletion && justFinished.length > 0
        // An operation started while the previous one was minimized must
        // appear immediately, not inherit a stale minimized UI state. The
        // ordinary auto-hide timer will compact it again after the delay.
        const newActiveJob = activeOperations.some(function(op) {
            return previousActiveIds.indexOf(String(op.id)) < 0
        })
        const hidden = ({})
        for (const op of operations) {
            const id = String(op.id)
            const isNewCompletion = justFinished.indexOf(id) >= 0
            // Already-hidden successes remain hidden. Newly completed jobs
            // become visible only when the completion option requests it.
            if (op.finished && op.success && (hiddenCompletions[id]
                    || minimized && isNewCompletion && !reveal))
                hidden[id] = true
        }
        hiddenCompletions = hidden
        previousActiveIds = activeOperations.map(function(op) { return String(op.id) })
        if (!activeCount || reveal || newActiveJob) minimized = false
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
    onAutoHideChanged: {
        if (!autoHide && minimized) restore()
        else scheduleHide()
    }
    onHideDelayMsChanged: scheduleHide()
    onHoveredChanged: if (!hovered && hidePending) scheduleHide()
}
