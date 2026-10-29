import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lurviko.App

Item {
    id: bar
    required property var lang
    signal searchRequested(string query)
    property alias query: search.text
    implicitHeight: AppTheme.controlHeight

    TextEditMenu {
        id: textMenu
        objectName: "homeSearchTextEditMenu"
        parent: bar
        editor: search
        langManager: bar.lang
    }

    function submit() {
        const q = search.text.trim()
        if (q.length)
            bar.searchRequested(q)
    }

    Timer {
        id: searchDebounce
        interval: 260
        repeat: false
        onTriggered: bar.submit()
    }

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: search.activeFocus ? AppTheme.surface : AppTheme.surfaceRaised
        border.width: search.activeFocus ? 2 : 1
        border.color: search.activeFocus ? AppTheme.accent : AppTheme.border
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 6
            anchors.rightMargin: AppTheme.space3
            spacing: AppTheme.space2
            Rectangle {
                Layout.preferredWidth: 30
                Layout.preferredHeight: 30
                radius: 15
                color: search.activeFocus ? AppTheme.accentSoft : "transparent"
                GText { anchors.centerIn: parent; text: "⌕"; basePixelSize: 19; baseColor: search.activeFocus ? AppTheme.accent : AppTheme.textMuted }
            }
            GTextField {
                id: search
                objectName: "homeSearchField"
                persistentSelection: true
                Layout.fillWidth: true
                placeholderText: lang.language === "tr" ? "Ana dizinde ara…" : "Search your home…"
                background: null
                color: AppTheme.text
                placeholderTextColor: AppTheme.textFaint
                basePixelSize: 13
                selectByMouse: true
                onTextChanged: {
                    if (text.trim().length)
                        searchDebounce.restart()
                    else
                        searchDebounce.stop()
                }
                onAccepted: { searchDebounce.stop(); bar.submit() }
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton
                    cursorShape: Qt.IBeamCursor
                    onPressed: function(mouse) {
                        search.forceActiveFocus()
                        const window = bar.ApplicationWindow.window
                        const bounds = window ? window.contentItem : bar
                        textMenu.positioningBounds = bounds
                        const point = search.mapToItem(bounds, mouse.x, mouse.y)
                        const target = bounds.mapToItem(bar,
                            Math.max(8, Math.min(bounds.width - textMenu.implicitWidth - 8, point.x)),
                            Math.max(8, Math.min(bounds.height - textMenu.implicitHeight - 8, point.y)))
                        textMenu.x = target.x
                        textMenu.y = target.y
                        textMenu.open()
                        mouse.accepted = true
                    }
                }
            }
        }
    }
}
