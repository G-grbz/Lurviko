import QtQuick
import QtQuick.Controls

// Shows the application's text-edit menu for its parent editor on right click,
// on every supported Qt version.
//
// Qt 6.9+ has the `ContextMenu` attached type, and its Basic style gives every
// TextField/TextArea a default menu through it. There the attached property is
// set at runtime, which replaces that default exactly like a static
// `ContextMenu.menu:` binding (and reparents the shared menu to the editor).
// Qt 6.8 has no such type: the lookup throws, and this right-click handler
// opens the menu instead. A static binding would not load on Qt 6.8.
TapHandler {
    id: handler

    property var menu: null
    // True when Qt's own ContextMenu attached type shows `menu` (Qt 6.9+).
    property bool attachedMenu: false

    acceptedButtons: Qt.RightButton
    enabled: !attachedMenu && menu !== null && menu !== undefined

    function attach() {
        const editor = handler.parent
        if (!editor)
            return
        try {
            editor.ContextMenu.menu = handler.menu
            handler.attachedMenu = true
        } catch (error) {
            handler.attachedMenu = false
        }
    }

    onMenuChanged: attach()
    onParentChanged: attach()
    Component.onCompleted: attach()

    onTapped: function(eventPoint, button) {
        const editor = handler.parent
        if (!handler.menu || !editor)
            return
        if (editor.forceActiveFocus)
            editor.forceActiveFocus()
        handler.menu.popup(editor, eventPoint.position.x, eventPoint.position.y)
    }
}
