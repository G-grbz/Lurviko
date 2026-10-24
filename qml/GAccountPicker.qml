import QtQuick
import QtQuick.Controls
import Lurviko.App

FocusScope {
    id: control
    property var model: []
    property string value: ""
    property bool allowKeep: false
    property string keepText: ""
    readonly property int count: model.length
    readonly property bool acceptableInput: (allowKeep && value.length === 0)
        || model.slice(allowKeep ? 1 : 0).indexOf(value) !== -1
    property alias editText: input.text
    readonly property string ghostSuffix: input.activeFocus && typing && !showAll
        && !input.selectedText.length && input.cursorPosition === input.text.length
        && matches.length && input.text.length
        && measure.advanceWidth < input.width - input.leftPadding - input.rightPadding
        ? matches[Math.min(matches.length - 1, Math.max(0, highlightedIndex))].label.substring(input.text.length) : ""
    property bool typing: false
    property bool showAll: false
    property int highlightedIndex: -1
    readonly property var matches: {
        const prefix = showAll ? "" : input.text.toLocaleLowerCase()
        return model.map(function(name, index) {
            return {label: String(name), value: allowKeep && index === 0 ? "" : String(name)}
        }).filter(function(entry) {
            return !prefix.length || (entry.value.length && entry.label.toLocaleLowerCase().indexOf(prefix) === 0)
        })
    }
    signal edited(string value)

    implicitWidth: 160
    implicitHeight: Math.max(34, AppTheme.typographySize("menu", 12) + 18)
    onValueChanged: if (input.text !== value) input.text = value
    onMatchesChanged: {
        highlightedIndex = -1
        // TextInput emits textEdited before its textChanged notification. Keep
        // the popup in sync with the final filter, not the previous keystroke.
        if (typing && input.activeFocus && enabled) {
            if (matches.length) suggestions.open()
            else suggestions.close()
        } else if (!matches.length && suggestions.visible) suggestions.close()
    }
    onEnabledChanged: if (!enabled) suggestions.close()

    function commit(index) {
        if (index < 0 || index >= matches.length) return
        const chosen = matches[index].value
        input.text = chosen
        input.cursorPosition = chosen.length
        typing = false
        edited(chosen)
        suggestions.close()
        input.forceActiveFocus()
    }
    function moveSelection(direction) {
        if (!matches.length) return
        if (!suggestions.visible) suggestions.open()
        highlightedIndex = highlightedIndex < 0 ? (direction > 0 ? 0 : matches.length - 1)
            : (highlightedIndex + direction + matches.length) % matches.length
        entries.positionViewAtIndex(highlightedIndex, ListView.Contain)
    }

    GTextField {
        id: input
        objectName: "accountEditor"
        anchors.fill: parent
        rightPadding: 32
        basePixelSize: 12
        focus: true
        text: control.value
        placeholderText: control.keepText
        Accessible.name: control.Accessible.name
        onTextEdited: {
            control.typing = true
            control.showAll = false
            control.edited(text)
            if (control.matches.length) suggestions.open()
            else suggestions.close()
        }
        onActiveFocusChanged: if (!activeFocus) {
            control.typing = false
            suggestions.close()
        }
        Keys.priority: Keys.BeforeItem
        Keys.onPressed: function(event) {
            if (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)) return
            if (event.key === Qt.Key_Down || event.key === Qt.Key_Up) {
                if (!control.typing && !suggestions.visible) control.showAll = true
                control.moveSelection(event.key === Qt.Key_Down ? 1 : -1)
                event.accepted = true
            } else if ((event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab)
                       && (control.typing || suggestions.visible) && control.matches.length) {
                if (control.matches.length === 1) control.commit(0)
                else control.moveSelection(event.key === Qt.Key_Backtab || (event.modifiers & Qt.ShiftModifier) ? -1 : 1)
                event.accepted = true
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                if (control.highlightedIndex >= 0) control.commit(control.highlightedIndex)
                else if (control.matches.length === 1) control.commit(0)
                else if (control.acceptableInput) { control.typing = false; suggestions.close() }
                event.accepted = true
            } else if (event.key === Qt.Key_Escape && suggestions.visible) {
                suggestions.close()
                control.typing = false
                event.accepted = true
            }
        }
        background: Rectangle {
            radius: AppTheme.fieldRadius
            color: input.activeFocus ? AppTheme.surfaceRaised : (input.hovered ? AppTheme.surfaceHover : AppTheme.surfaceRaised)
            border.width: input.activeFocus ? 2 : 1
            border.color: !control.acceptableInput && !control.typing ? AppTheme.danger
                : input.activeFocus ? AppTheme.accent : AppTheme.border
        }
    }
    TextMetrics { id: measure; font: input.font; text: input.text }
    Item {
        anchors.fill: input
        anchors.leftMargin: input.leftPadding
        anchors.rightMargin: input.rightPadding
        clip: true
        enabled: false
        GText {
            objectName: "accountCompletionHint"
            x: measure.advanceWidth
            anchors.verticalCenter: parent.verticalCenter
            text: control.ghostSuffix
            font: input.font
            baseColor: AppTheme.textFaint
        }
    }
    GToolButton {
        objectName: "accountDropDown"
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: 30; height: parent.height
        text: "⌄"
        focusPolicy: Qt.NoFocus
        onClicked: {
            input.forceActiveFocus()
            if (suggestions.visible) suggestions.close()
            else { control.showAll = true; suggestions.open() }
        }
    }
    GPopupDismissHandler { popup: suggestions; opener: control }
    Popup {
        id: suggestions
        objectName: "accountSuggestions"
        parent: control
        y: control.height + 4
        width: control.width
        implicitHeight: Math.min(entries.contentHeight + 12, 252)
        padding: 6
        focus: false
        closePolicy: Popup.CloseOnEscape
        contentItem: ListView {
            id: entries
            objectName: "accountSuggestionList"
            clip: true
            model: suggestions.visible ? control.matches : []
            boundsBehavior: Flickable.StopAtBounds
            rightMargin: 8
            spacing: 2
            delegate: ItemDelegate {
                required property var modelData
                required property int index
                objectName: "accountSuggestion_" + index
                width: entries.width - entries.rightMargin
                height: Math.max(32, AppTheme.typographySize("menu", 12) + 14)
                focusPolicy: Qt.NoFocus
                highlighted: index === control.highlightedIndex
                hoverEnabled: true
                onClicked: control.commit(index)
                contentItem: GText {
                    typographyRole: "menu"
                    text: modelData.label
                    baseColor: parent.highlighted ? AppTheme.accent : AppTheme.text
                    basePixelSize: 12
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: AppTheme.radiusSmall
                    color: parent.highlighted ? AppTheme.accentSoft
                        : parent.hovered ? AppTheme.surfaceHover : AppTheme.surfaceHoverTransparent
                }
            }
            ScrollBar.vertical: ScrollBar {
                id: suggestionBar
                width: 6
                policy: ScrollBar.AsNeeded
            }
            GPopupWheelScroll { anchors.fill: parent; view: entries; scrollbar: suggestionBar }
        }
        background: Rectangle {
            radius: AppTheme.fieldRadius
            color: AppTheme.surface
            border.color: AppTheme.borderStrong
        }
    }
}
