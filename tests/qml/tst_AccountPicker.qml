import QtQuick
import QtQuick.Controls
import QtTest
import Lurviko.App

Item {
    width: 700; height: 500
    property string draft: "root"
    GAccountPicker {
        id: picker
        x: 40; y: 30; width: 240
        model: ["root", "user"]
        value: draft
        onEdited: function(value) { draft = value }
    }
    GTextField { id: nextField; x: 40; y: 370; width: 240 }
    TestCase {
        name: "AccountPicker"
        when: windowShown
        function init() {
            findChild(picker, "accountSuggestions").close()
            tryCompare(findChild(picker, "accountSuggestions"), "visible", false)
            picker.typing = false
            picker.showAll = false
            picker.allowKeep = false
            picker.model = ["root", "user"]
            draft = "user"
            picker.enabled = true
            wait(30)
        }
        function typeRo() {
            const input = findChild(picker, "accountEditor")
            input.forceActiveFocus()
            keyClick(Qt.Key_A, Qt.ControlModifier)
            keyClick(Qt.Key_R)
            keyClick(Qt.Key_O)
            return input
        }
        function test_unique_completion_is_ghost_until_tab() {
            const input = typeRo()
            compare(input.text.toLowerCase(), "ro")
            compare(picker.ghostSuffix, "ot")
            compare(findChild(picker, "accountCompletionHint").text, "ot")
            verify(!picker.acceptableInput)
            keyClick(Qt.Key_Tab)
            compare(input.text, "root")
            compare(draft, "root")
            verify(picker.acceptableInput)
            verify(input.activeFocus)
            verify(!findChild(picker, "accountSuggestions").visible)
        }
        function test_multiple_matches_tab_arrows_and_enter() {
            picker.model = ["robot", "root", "round", "ronin", "user"]
            const input = typeRo()
            compare(picker.matches.length, 4)
            tryCompare(findChild(picker, "accountSuggestionList"), "count", 4)
            keyClick(Qt.Key_Tab)
            compare(picker.highlightedIndex, 0)
            compare(input.text.toLowerCase(), "ro")
            keyClick(Qt.Key_Tab)
            compare(picker.highlightedIndex, 1)
            keyClick(Qt.Key_Down)
            compare(picker.highlightedIndex, 2)
            keyClick(Qt.Key_Up)
            compare(picker.highlightedIndex, 1)
            keyClick(Qt.Key_Return)
            compare(input.text, "root")
            compare(draft, "root")
        }
        function test_keep_existing_and_mouse_selection() {
            picker.allowKeep = true
            picker.keepText = "Mixed · Keep"
            picker.model = ["Keep existing values", "root", "user"]
            draft = ""
            verify(picker.acceptableInput)
            mouseClick(findChild(picker, "accountDropDown"))
            const list = findChild(picker, "accountSuggestionList")
            tryVerify(function() { return list.itemAtIndex(1) !== null })
            mouseClick(list.itemAtIndex(1))
            compare(draft, "root")
            tryCompare(findChild(picker, "accountSuggestions"), "visible", false)
            mouseClick(findChild(picker, "accountDropDown"))
            tryVerify(function() { return list.itemAtIndex(0) !== null })
            mouseClick(list.itemAtIndex(0))
            compare(draft, "")
            verify(picker.acceptableInput)
        }
        function test_unknown_account_is_not_accepted() {
            const input = findChild(picker, "accountEditor")
            input.forceActiveFocus()
            keyClick(Qt.Key_A, Qt.ControlModifier)
            keyClick(Qt.Key_Z)
            compare(picker.matches.length, 0)
            verify(!picker.acceptableInput)
            tryCompare(findChild(picker, "accountSuggestions"), "visible", false)
            keyClick(Qt.Key_Return)
            verify(!picker.acceptableInput)
            nextField.forceActiveFocus()
            verify(!findChild(picker, "accountSuggestions").visible)
        }
        function test_dropdown_toggle_does_not_reopen_or_change_value() {
            const button = findChild(picker, "accountDropDown")
            const popup = findChild(picker, "accountSuggestions")
            mouseClick(button)
            tryCompare(popup, "visible", true)
            mouseClick(button)
            tryCompare(popup, "visible", false)
            compare(draft, "user")
        }
    }
}
