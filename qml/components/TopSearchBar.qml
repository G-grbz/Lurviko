import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import GFile.App

Item {
    id: bar
    required property var lang
    signal searchRequested(string query)
    property alias query: search.text
    implicitHeight: AppTheme.controlHeight

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
                Text { anchors.centerIn: parent; text: "⌕"; font.pixelSize: 19; color: search.activeFocus ? AppTheme.accent : AppTheme.textMuted }
            }
            GTextField {
                id: search
                Layout.fillWidth: true
                placeholderText: lang.language === "tr" ? "Ana dizinde ara…" : "Search your home…"
                background: null
                color: AppTheme.text
                placeholderTextColor: AppTheme.textFaint
                font.pixelSize: 13
                selectByMouse: true
                onTextChanged: {
                    if (text.trim().length)
                        searchDebounce.restart()
                    else
                        searchDebounce.stop()
                }
                onAccepted: { searchDebounce.stop(); bar.submit() }
            }
        }
    }
}
