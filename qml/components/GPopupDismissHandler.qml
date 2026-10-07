import QtQuick
import GFile.Backend

PopupDismissFilter {
    required property var popup
    property var opener: null
    property var relatedPopups: []
    window: popup && popup.parent ? popup.parent.Window.window : null
    targetPopup: popup
    openerItem: opener
    relatedPopupObjects: relatedPopups
}
