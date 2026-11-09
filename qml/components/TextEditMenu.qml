import QtQuick
import QtQuick.Controls
import Lurviko.App

GMenu {
    id: control

    property var editor: null
    property var langManager: null

    function label(key, fallback) {
        if (!langManager)
            return fallback
        // t() is a C++ call without change notification; reading `language`
        // makes every label binding re-evaluate when the language switches.
        const language = langManager.language
        return language !== undefined ? langManager.t(key) : fallback
    }

    // Password fields never offer their contents to the clipboard.
    readonly property bool concealed: !!editor && editor.echoMode !== undefined
                                      && editor.echoMode !== TextInput.Normal

    function hasSelection() {
        return editor && editor.selectedText !== undefined && editor.selectedText.length > 0
    }

    GMenuItem {
        text: control.label("undo", "Undo") + "    Ctrl+Z"
        enabled: control.editor && !control.editor.readOnly && control.editor.canUndo
        onTriggered: if (control.editor) control.editor.undo()
    }

    GMenuItem {
        text: control.label("redo", "Redo") + "    Ctrl+Shift+Z"
        enabled: control.editor && !control.editor.readOnly && control.editor.canRedo
        onTriggered: if (control.editor) control.editor.redo()
    }

    GMenuSeparator {}

    GMenuItem {
        text: control.label("cut", "Cut") + "    Ctrl+X"
        enabled: control.editor && !control.editor.readOnly && !control.concealed && control.hasSelection()
        onTriggered: if (control.editor && !control.concealed) control.editor.cut()
    }

    GMenuItem {
        text: control.label("copy", "Copy") + "    Ctrl+C"
        enabled: !control.concealed && control.hasSelection()
        onTriggered: if (control.editor && !control.concealed) control.editor.copy()
    }

    GMenuItem {
        text: control.label("paste", "Paste") + "    Ctrl+V"
        enabled: control.editor && !control.editor.readOnly && control.editor.canPaste
        onTriggered: if (control.editor) control.editor.paste()
    }

    GMenuItem {
        text: control.label("delete", "Delete")
        enabled: control.editor && !control.editor.readOnly && control.hasSelection()
        onTriggered: {
            if (!control.editor || !control.hasSelection())
                return
            control.editor.remove(control.editor.selectionStart, control.editor.selectionEnd)
        }
    }

    GMenuSeparator {}

    GMenuItem {
        text: control.label("select_all", "Select all") + "    Ctrl+A"
        enabled: control.editor && control.editor.text !== undefined && control.editor.text.length > 0
        onTriggered: if (control.editor) control.editor.selectAll()
    }
}
