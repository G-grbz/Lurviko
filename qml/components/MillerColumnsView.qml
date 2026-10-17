import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lurviko.App

Item {
    id: view
    objectName: "millerColumnsView"
    required property var browser
    required property var navigation
    readonly property var currentListView: columns.itemAt(navigation.activeColumn)
                                           ? columns.itemAt(navigation.activeColumn).listView : null
    readonly property var selectedItem: {
        const revision = browser.selectionRevision
        return view.visible && browser.selectedCount === 1
                ? browser.directory.itemAt(browser.directory.indexOfUrl(browser.selectedUrl)) : ({})
    }
    readonly property bool previewVisible: view.visible && !!selectedItem.itemUrl && !selectedItem.isDir
    function positionIndex(index) {
        if (currentListView) currentListView.positionViewAtIndex(index, ListView.Contain)
    }
    function revealColumn(columnIndex) {
        const item = columns.itemAt(columnIndex === undefined ? navigation.activeColumn : columnIndex)
        if (!item) return
        horizontal.contentX = Math.max(0, Math.min(Math.max(0, horizontal.contentWidth - horizontal.width),
            item.x + item.width > horizontal.contentX + horizontal.width
                ? item.x + item.width - horizontal.width : Math.min(item.x, horizontal.contentX)))
    }
    function dropOn(drop, destination) {
        let urls = drop.source && drop.source.dragUrls ? drop.source.dragUrls : browser.droppedLocalUrls(drop)
        if (urls.length) {
            browser.dropUrls(urls, destination, (drop.modifiers & Qt.ControlModifier) !== 0)
            drop.acceptProposedAction()
        } else if (browser.extractArkDrop(drop, destination)) drop.acceptProposedAction()
    }
    Connections {
        target: navigation
        function onActiveDirectoryChanged() { Qt.callLater(function() { view.revealColumn() }) }
        function onCountChanged() { Qt.callLater(function() { view.revealColumn(navigation.count - 1) }) }
    }
    Flickable {
        id: horizontal
        anchors.fill: parent
        anchors.bottomMargin: 12
        contentWidth: columnRow.implicitWidth
        contentHeight: height
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        Row {
            id: columnRow
            height: horizontal.height
            spacing: 0
            Repeater {
                id: columns
                model: view.visible ? view.navigation : null
                delegate: Rectangle {
                    id: column
                    required property int index
                    required property var columnDirectory
                    required property string columnLocation
                    required property string columnSelection
                    required property int columnWidth
                    property alias listView: entries
                    width: columnWidth
                    height: columnRow.height
                    color: AppTheme.surface
                    border.color: AppTheme.border
                    Rectangle {
                        height: 2; width: parent.width
                        color: view.navigation.activeColumn === column.index ? AppTheme.accent : "transparent"
                    }
                    Text {
                        id: heading
                        x: 14; y: 10; width: parent.width - 34; height: 30
                        text: decodeURIComponent(column.columnLocation.replace(/\/$/, "").split("/").pop() || "/")
                        color: AppTheme.text
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                        elide: Text.ElideMiddle
                    }
                    MouseArea {
                        anchors.fill: entries
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onPressed: function(mouse) {
                            view.browser.activateMillerColumn(column.index)
                            if (mouse.button === Qt.RightButton) view.browser.openBackgroundContextAt(this, mouse)
                            else view.browser.clearSelection()
                        }
                    }
                    DropArea {
                        anchors.fill: entries
                        onDropped: function(drop) {
                            view.browser.activateMillerColumn(column.index)
                            view.dropOn(drop, column.columnLocation)
                        }
                    }
                    ListView {
                        id: entries
                        objectName: "millerColumnList"
                        anchors { top: heading.bottom; topMargin: 6; bottom: parent.bottom; bottomMargin: 8; left: parent.left; leftMargin: 6; right: parent.right; rightMargin: 14 }
                        model: column.columnDirectory
                        clip: true
                        reuseItems: true
                        cacheBuffer: 160
                        boundsBehavior: Flickable.StopAtBounds
                        onContentYChanged: if (column.index === navigation.activeColumn) browser.scheduleTabScrollSnapshot()
                        delegate: Rectangle {
                            id: entry
                            required property int index
                            required property string name
                            required property string itemUrl
                            required property string localPath
                            required property bool isDir
                            required property string suffix
                            required property string mimeType
                            required property string systemIconName
                            required property bool hidden
                            required property string gitStatus
                            property var dragUrls: []
                            readonly property bool chosen: column.index === navigation.activeColumn && browser.isSelected(itemUrl)
                            width: entries.width
                            height: Math.round(Math.max(36, Math.min(64, 18 + browser.activeIconSize * 0.2)))
                            radius: 7
                            opacity: hidden ? AppTheme.hiddenItemOpacity : 1
                            color: chosen || column.columnSelection === itemUrl ? AppTheme.accentSoft
                                  : pointer.containsMouse ? AppTheme.surfaceHover : "transparent"
                            border.color: chosen ? AppTheme.accentBorder : "transparent"
                            Drag.dragType: Drag.Automatic
                            Drag.source: entry
                            Drag.supportedActions: Qt.MoveAction | Qt.CopyAction
                            Drag.proposedAction: Qt.MoveAction
                            Drag.mimeData: ({ "text/uri-list": dragUrls.join("\r\n") })
                            Drag.imageSource: artwork.source
                            CrispIcon {
                                id: artwork
                                x: 8; anchors.verticalCenter: parent.verticalCenter
                                width: Math.min(36, entry.height - 12); height: width
                                retainWhileLoading: false
                                source: browser.iconForItem(entry.isDir, entry.suffix, entry.systemIconName, entry.name, entry.itemUrl, entry.mimeType)
                            }
                            Text {
                                anchors { left: artwork.right; leftMargin: 9; right: parent.right; rightMargin: 22; verticalCenter: parent.verticalCenter }
                                visible: !editor.visible
                                text: entry.name; color: AppTheme.text
                                font.pixelSize: 13; elide: Text.ElideMiddle
                            }
                            GitStatusBadge {
                                anchors { right: artwork.right; bottom: artwork.bottom }
                                status: entry.gitStatus
                                language: browser.lang.language
                            }
                            Text {
                                anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
                                text: entry.isDir ? "›" : ""; color: AppTheme.textMuted; font.pixelSize: 20
                            }
                            GTextField {
                                id: editor
                                anchors { left: artwork.right; leftMargin: 5; right: parent.right; rightMargin: 6; verticalCenter: parent.verticalCenter }
                                height: entry.height - 4
                                visible: column.index === navigation.activeColumn && browser.inlineRenameUrl === entry.itemUrl
                                onVisibleChanged: if (visible) Qt.callLater(function() { browser.prepareInlineRenameEditor(editor, entry.name, entry.isDir) })
                                onAccepted: browser.commitInlineRename(entry.itemUrl, text)
                                onActiveFocusChanged: if (!activeFocus && visible) Qt.callLater(function() {
                                    if (browser.inlineRenameUrl === entry.itemUrl) browser.commitInlineRename(entry.itemUrl, editor.text)
                                })
                                Keys.onEscapePressed: function(event) { browser.cancelInlineRename(); event.accepted = true }
                                Keys.onTabPressed: function(event) { browser.commitInlineRenameAndAdvance(entry.itemUrl, text, false); event.accepted = true }
                                Keys.onBacktabPressed: function(event) { browser.commitInlineRenameAndAdvance(entry.itemUrl, text, true); event.accepted = true }
                            }
                            MouseArea {
                                id: pointer
                                anchors.fill: parent
                                enabled: !editor.visible
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                                onPressed: function(mouse) {
                                    if (mouse.button === Qt.MiddleButton) {
                                        if (entry.isDir) browser.requestNewTab(entry.itemUrl)
                                        return
                                    }
                                    browser.activateMillerColumn(column.index)
                                    const preserve = browser.isSelected(entry.itemUrl)
                                        && (mouse.button === Qt.RightButton || (browser.selectedCount > 1 && mouse.modifiers === Qt.NoModifier))
                                    if (!preserve) browser.selectIndex(entry.index, mouse.button === Qt.RightButton ? Qt.NoModifier : mouse.modifiers)
                                    else browser.setPrimaryFromItem(column.columnDirectory.itemAt(entry.index), entry.index)
                                    if (mouse.button === Qt.RightButton) browser.openItemContextAt(pointer, mouse)
                                    else browser.forceActiveFocus()
                                }
                                onClicked: function(mouse) {
                                    if (mouse.button === Qt.LeftButton && mouse.modifiers === Qt.NoModifier && browser.selectedCount > 1)
                                        browser.selectIndex(entry.index, Qt.NoModifier)
                                    if (mouse.button === Qt.LeftButton && mouse.modifiers === Qt.NoModifier && !entry.isDir && AppTheme.singleClickOpen)
                                        browser.openItem(entry.itemUrl, false, entry.name, entry.mimeType)
                                }
                                onDoubleClicked: function(mouse) {
                                    if (mouse.button === Qt.LeftButton && (entry.isDir || !AppTheme.singleClickOpen))
                                        browser.openItem(entry.itemUrl, entry.isDir, entry.name, entry.mimeType)
                                }
                            }
                            DragHandler {
                                id: drag
                                target: null
                                enabled: !editor.visible
                                acceptedButtons: Qt.LeftButton
                                onActiveChanged: {
                                    if (active) {
                                        browser.activateMillerColumn(column.index)
                                        if (!browser.isSelected(entry.itemUrl)) browser.selectIndex(entry.index, Qt.NoModifier)
                                        entry.dragUrls = browser.dragUrlsFor(entry.itemUrl)
                                    }
                                    entry.Drag.active = active
                                    if (!active) entry.dragUrls = []
                                }
                            }
                            DropArea {
                                anchors.fill: parent
                                enabled: entry.isDir
                                onDropped: function(drop) { view.dropOn(drop, entry.itemUrl) }
                            }
                        }
                        GPopupWheelScroll { anchors.fill: parent; view: entries }
                        ScrollBar.vertical: ScrollBar {
                            parent: column
                            anchors { right: parent.right; rightMargin: 3; top: heading.bottom; bottom: parent.bottom; bottomMargin: 8 }
                            width: 7
                            policy: ScrollBar.AsNeeded
                            contentItem: Rectangle { implicitWidth: 5; radius: 3; color: AppTheme.textMuted; opacity: parent.active ? 0.7 : 0 }
                            background: Item {}
                        }
                    }
                    GBusyIndicator { anchors.centerIn: entries; running: column.columnDirectory.loading && column.columnDirectory.count === 0; visible: running }
                    Text {
                        anchors.centerIn: entries; width: entries.width - 20
                        visible: !column.columnDirectory.loading && column.columnDirectory.count === 0
                        text: column.columnDirectory.errorString.length ? browser.lang.localizeMessage(column.columnDirectory.errorString)
                              : (browser.lang.language === "tr" ? "Klasör boş" : "Folder is empty")
                        color: column.columnDirectory.errorString.length ? AppTheme.danger : AppTheme.textMuted
                        wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter; font.pixelSize: 12
                    }
                    MouseArea {
                        anchors { top: parent.top; bottom: parent.bottom; right: parent.right }
                        width: 7
                        cursorShape: Qt.SizeHorCursor
                        property real startX: 0
                        property real startWidth: 0
                        onPressed: function(mouse) { startX = mapToGlobal(mouse.x, mouse.y).x; startWidth = column.width }
                        onPositionChanged: function(mouse) {
                            if (pressed) navigation.setColumnWidth(column.index, Math.round(startWidth + mapToGlobal(mouse.x, mouse.y).x - startX))
                        }
                        onReleased: navigation.setColumnWidth(column.index, column.width, true)
                    }
                }
            }
            Rectangle {
                id: preview
                objectName: "millerFilePreview"
                visible: view.previewVisible
                width: visible ? 300 : 0
                height: columnRow.height
                color: AppTheme.surface
                border.color: AppTheme.border
                ColumnLayout {
                    anchors { fill: parent; margins: 20 }
                    spacing: 16
                    Item { Layout.fillHeight: true; Layout.maximumHeight: 50 }
                    Item {
                        Layout.fillWidth: true; Layout.preferredHeight: 230
                        CrispIcon {
                            anchors.centerIn: parent; width: 100; height: 100
                            visible: !thumbnail.displayReady
                            source: view.previewVisible ? browser.iconForItem(false, browser.selectedSuffix, browser.selectedSystemIconName,
                                    browser.selectedName, browser.selectedUrl, browser.selectedMimeType) : ""
                        }
                        DeferredThumbnail {
                            id: thumbnail
                            anchors.fill: parent
                            candidateSource: view.previewVisible ? browser.thumbSource(browser.selectedLocalPath, browser.selectedSuffix,
                                               browser.selectedModified, browser.selectedSize, view.selectedItem.previewRevision) : ""
                            fileIdentity: browser.selectedUrl
                            fileSuffix: browser.selectedSuffix
                            fileModified: browser.selectedModified
                            fileSize: browser.selectedSize
                            sourceSize.width: 512; sourceSize.height: 512
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                            visible: displayReady
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: browser.selectedName; color: AppTheme.text; font.pixelSize: 16; font.weight: Font.DemiBold
                        wrapMode: Text.Wrap; maximumLineCount: 4; elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: browser.directory.formatBytes(browser.selectedSize) + "\n" + browser.selectedMimeType
                        color: AppTheme.textMuted; font.pixelSize: 12; wrapMode: Text.Wrap
                    }
                    GButton { text: browser.lang.language === "tr" ? "Aç" : "Open"; onClicked: browser.openSelected() }
                    Item { Layout.fillHeight: true }
                }
            }
        }
        ScrollBar.horizontal: ScrollBar {
            parent: view
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 9
            policy: ScrollBar.AsNeeded
            contentItem: Rectangle { implicitHeight: 5; radius: 3; color: AppTheme.textMuted; opacity: parent.active ? 0.75 : 0 }
            background: Item {}
        }
    }
}
