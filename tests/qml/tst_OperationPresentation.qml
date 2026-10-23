import QtQuick
import QtTest
import Lurviko.App

Item {
    OperationPresentation { id: presentation; hideDelayMs: 120; autoHide: false }
    TestCase {
        name: "OperationPresentation"
        function running(id) { return {id: id, finished: false, success: false, percent: 0} }
        function completed(id, success) { return {id: id, finished: true, success: success} }
        function init() {
            presentation.operations = []
            presentation.nativeProgress = false
            presentation.autoHide = false
            presentation.hovered = false
            presentation.hideDelayMs = 120
        }
        function cleanup() { presentation.operations = []; presentation.autoHide = false }
        function test_manual_toggle_keeps_the_job_and_restores_progress() {
            presentation.operations = [running(1), running(2)]
            compare(presentation.activeCount, 2)
            compare(presentation.visibleOperations.length, 2)
            presentation.toggle()
            verify(presentation.minimized)
            compare(presentation.visibleOperations.length, 0)
            compare(presentation.activeCount, 2)
            presentation.operations = [Object.assign(running(1), {percent: 50}), running(2)]
            verify(presentation.minimized)
            presentation.toggle()
            compare(presentation.visibleOperations[0].percent, 50)
        }
        function test_completion_clears_the_indicator_without_reopening_cards() {
            presentation.operations = [running(1), running(2)]
            presentation.minimize()
            presentation.operations = [completed(1, true), running(2)]
            compare(presentation.activeCount, 1)
            verify(presentation.minimized)
            presentation.operations = [completed(1, true), completed(2, true)]
            compare(presentation.activeCount, 0)
            compare(presentation.visibleOperations.length, 0)
            presentation.operations = []
            compare(Object.keys(presentation.hiddenCompletions).length, 0)
            presentation.operations = [running(3)]
            verify(!presentation.minimized)
            compare(presentation.visibleOperations.length, 1)
        }
        function test_failure_remains_visible_while_other_jobs_are_minimized() {
            presentation.operations = [running(1), running(2)]
            presentation.minimize()
            presentation.operations = [completed(1, false), running(2)]
            compare(presentation.visibleOperations.length, 1)
            compare(presentation.visibleOperations[0].id, 1)
            verify(presentation.minimized)
        }
        function test_success_keeps_normal_completion_toast_when_expanded() {
            presentation.operations = [running(1)]
            presentation.operations = [completed(1, true)]
            compare(presentation.visibleOperations.length, 1)
            compare(presentation.activeCount, 0)
        }
        function test_progress_updates_do_not_postpone_auto_hide() {
            presentation.autoHide = true
            presentation.operations = [running(1)]
            for (let i = 0; i < 6; ++i) {
                wait(30)
                presentation.operations = [Object.assign(running(1), {percent: i})]
            }
            verify(presentation.minimized)
        }
        function test_auto_hide_waits_until_the_pointer_leaves() {
            presentation.hovered = true
            presentation.autoHide = true
            presentation.operations = [running(1)]
            wait(180)
            verify(!presentation.minimized)
            presentation.hovered = false
            tryCompare(presentation, "minimized", true)
        }
        function test_disabling_auto_hide_cancels_the_timeout() {
            presentation.autoHide = true
            presentation.operations = [running(1)]
            presentation.autoHide = false
            wait(180)
            verify(!presentation.minimized)
        }
        function test_native_progress_disables_minimization_and_auto_hide() {
            presentation.operations = [running(1)]
            presentation.minimize()
            presentation.nativeProgress = true
            verify(!presentation.minimized)
            presentation.autoHide = true
            presentation.minimize()
            wait(180)
            verify(!presentation.minimized)
            compare(presentation.visibleOperations.length, 1)
        }
    }
}
