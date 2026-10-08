import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import Lurviko.App
import Lurviko.Backend
import "../components"

Item {
    id: page
    property string initialLocation: ""
    required property var lang
    required property var contentIndexModel
    required property var musicPlayer
    required property var favoritesModel
    required property var quickAccessModel
    required property var adminEditor
    required property var cloudAuth
    required property var cloudIntegrationPreferences
    required property var googleDrive
    required property var oneDrive
    signal backRequested(string lastLocation)
    signal sidebarLocationChanged(string location)

    property var tabs: []
    property var closedTabs: []
    property int currentTab: 0
    property int tabDragFrom: -1
    property int tabDragTarget: -1
    property bool splitEnabled: false
    property bool suppressTabHistory: false
    property bool switchingTabLocation: false
    property var activePane: leftPane
    // Capture the requested state before the button click; fading-out menus
    // remain visible briefly but must be eligible to open again immediately.
    property var toolbarPressedMenu: null
    property bool toolbarPressedMenuWasOpen: false

    Settings {
        id: tabBehaviorSettings
        category: "TabBehavior"
        property bool activateNewTabs: true
    }
    onCurrentTabChanged: Qt.callLater(page.ensureActiveTabVisible)
    readonly property string visibleLocation: activePane && activePane.currentLocation !== undefined
                                              ? activePane.currentLocation : initialLocation
    onActivePaneChanged: {
        if (activePane && activePane.currentLocation !== undefined)
            sidebarLocationChanged(activePane.currentLocation)
    }

    function isAdminLocation(location) {
        return !!location && location.startsWith("admin:")
    }

    function titleFor(location) {
        if (!location) return "/"
        if (location.startsWith("category:/"))
            return contentIndexModel.titleForCategory(location.substring("category:/".length), lang.language)
        var clean = location
        if (clean.startsWith("file://")) clean = decodeURIComponent(clean.substring(7))
        if (clean === "/") return "/"
        if (clean.startsWith("admin:")) {
            // Keep the actual folder name in administrator tabs. The ADM badge
            // and warning-colored tab already communicate the privileged mode.
            var adminPath = clean.replace(/^admin:\/*/, "/")
            if (adminPath === "/") return lang.t("root")
            var adminPieces = adminPath.split('/').filter(Boolean)
            return adminPieces.length ? decodeURIComponent(adminPieces[adminPieces.length - 1]) : lang.t("root")
        }
        if (clean.startsWith("smb://")) return "SMB"
        if (clean.startsWith("sftp://")) return "SFTP"
        if (clean.startsWith("gdrive://")) return "Google Drive"
        if (clean.startsWith("onedrive://")) return "OneDrive"
        var pieces = clean.split('/').filter(Boolean)
        return pieces.length ? pieces[pieces.length - 1] : clean
    }

    function makeTab(location, pendingRevealUrl) {
        return {
            "location": location,
            "back": [],
            "forward": [],
            "searchVisible": false,
            "searchQuery": "",
            "searchEverywhere": false,
            "searchSessionId": "",
            "searchScrollY": -1,
            "viewScrollY": 0,
            "directoryViews": ({}),
            "pendingRevealUrl": pendingRevealUrl ? String(pendingRevealUrl) : "",
            "revealAnchorUrl": pendingRevealUrl ? String(pendingRevealUrl) : ""
        }
    }

    function withSearchState(tab, state) {
        return {
            "location": tab.location,
            "back": tab.back ? tab.back.slice() : [],
            "forward": tab.forward ? tab.forward.slice() : [],
            "searchVisible": !!(state && state.searchVisible),
            "searchQuery": state && state.searchQuery !== undefined ? String(state.searchQuery) : "",
            "searchEverywhere": !!(state && state.searchEverywhere),
            "searchSessionId": state && state.searchSessionId !== undefined ? String(state.searchSessionId) : (tab.searchSessionId || ""),
            "searchScrollY": state && state.searchScrollY !== undefined ? Number(state.searchScrollY) : -1,
            "viewScrollY": state && state.viewScrollY !== undefined && Number(state.viewScrollY) >= 0
                           ? Number(state.viewScrollY)
                           : (tab.viewScrollY !== undefined ? Number(tab.viewScrollY) : 0),
            "directoryViews": tab.directoryViews || ({}),
            "pendingRevealUrl": tab.pendingRevealUrl !== undefined ? String(tab.pendingRevealUrl) : "",
            "revealAnchorUrl": tab.revealAnchorUrl !== undefined ? String(tab.revealAnchorUrl) : ""
        }
    }

    function captureCurrentTabSearchState(detachForSwitch) {
        if (currentTab < 0 || currentTab >= tabs.length || !leftPane)
            return
        var next = tabs.slice()
        const state = detachForSwitch ? leftPane.detachSearchStateForTabSwitch() : leftPane.searchState()
        if (!(state.searchVisible && String(state.searchSessionId || "").length > 0)) {
            const normalOffset = leftPane.tabScrollOffset()
            if (normalOffset >= 0)
                state.viewScrollY = normalOffset
        }
        next[currentTab] = withSearchState(next[currentTab], state)
        tabs = next
    }

    function updateCurrentTabViewport(location, offset) {
        if (page.switchingTabLocation || currentTab < 0 || currentTab >= tabs.length)
            return
        const tab = tabs[currentTab]
        if (!tab || String(tab.location || "") !== String(location || ""))
            return
        const numeric = Number(offset)
        if (!Number.isFinite(numeric) || numeric < 0)
            return
        var updated = {}
        for (var key in tab)
            updated[key] = tab[key]
        updated.viewScrollY = numeric
        var next = tabs.slice()
        next[currentTab] = updated
        tabs = next
    }

    function rememberDirectoryView(location, state) {
        if (switchingTabLocation || currentTab < 0 || currentTab >= tabs.length
                || !state || state.viewScrollY < 0)
            return
        const tab = tabs[currentTab]
        if (!tab || !leftPane.locationsEquivalent(tab.location, location))
            return
        var views = Object.assign({}, tab.directoryViews || {})
        views[leftPane.comparableLocation(location)] = state
        var next = tabs.slice()
        next[currentTab] = Object.assign({}, tab, { directoryViews: views })
        tabs = next
    }

    function restoreHistoryView(location, tab) {
        const views = tab.directoryViews || {}
        const state = views[leftPane.comparableLocation(location)]
        if (!location.startsWith("category:/"))
            leftPane.prepareNavigationViewRestore(location, state)
        setPaneLocationWithoutHistory(location)
        Qt.callLater(leftPane.restorePendingTabScroll)
    }

    function updateCurrentTabRevealState(location, itemUrl, offset) {
        if (currentTab < 0 || currentTab >= tabs.length)
            return
        const tab = tabs[currentTab]
        if (!tab || String(tab.location || "") !== String(location || ""))
            return
        var updated = {}
        for (var key in tab)
            updated[key] = tab[key]
        updated.revealAnchorUrl = String(itemUrl || "")
        updated.pendingRevealUrl = ""
        const numeric = Number(offset)
        if (Number.isFinite(numeric) && numeric >= 0)
            updated.viewScrollY = numeric
        var next = tabs.slice()
        next[currentTab] = updated
        tabs = next
    }

    function clearPendingReveal(index) {
        if (index < 0 || index >= tabs.length)
            return
        const tab = tabs[index]
        if (!tab || !String(tab.pendingRevealUrl || "").length)
            return
        var updated = {}
        for (var key in tab)
            updated[key] = tab[key]
        updated.pendingRevealUrl = ""
        var next = tabs.slice()
        next[index] = updated
        tabs = next
    }

    function dismissActivePaneTransientUi() {
        if (page.activePane && typeof page.activePane.dismissLocationCompletion === "function")
            page.activePane.dismissLocationCompletion()
    }

    function tabAt(index) {
        return index >= 0 && index < tabs.length ? tabs[index] : null
    }

    function currentTabLocation() {
        const tab = tabAt(currentTab)
        return tab ? tab.location : leftPane.currentLocation
    }

    function canGoBack() {
        const tab = tabAt(currentTab)
        return !!tab && tab.back && tab.back.length > 0
    }

    function canGoForward() {
        const tab = tabAt(currentTab)
        return !!tab && tab.forward && tab.forward.length > 0
    }

    function setPaneLocationWithoutHistory(location) {
        suppressTabHistory = true
        leftPane.currentLocation = location
        suppressTabHistory = false
    }

    function switchToTab(index, skipOutgoingCapture) {
        if (index < 0 || index >= tabs.length)
            return

        // BrowserPane is shared by all tabs, so persist the outgoing tab's
        // search before DirectoryModel clears it while changing location.
        if (!skipOutgoingCapture) {
            if (index === currentTab)
                return
            captureCurrentTabSearchState(true)
        }

        currentTab = index
        const tab = tabs[index]
        const sameLocation = tab.location === leftPane.currentLocation
        const pendingRevealUrl = String(tab.pendingRevealUrl || "")
        const revealAnchorUrl = String(tab.revealAnchorUrl || "")
        if (pendingRevealUrl.length > 0)
            page.clearPendingReveal(index)
        Qt.callLater(function() {
            // Do not let transient locationChanged signals overwrite the
            // target tab's saved state while switching.
            page.switchingTabLocation = true

            // A live search tab already owns a C++ result cache. Restore that
            // session first so we never launch an unnecessary normal-directory
            // worker merely to replace it one event-loop later.
            const hasSavedSearch = !!tab.searchVisible
                    && String(tab.searchQuery || "").trim().length > 0
                    && String(tab.searchSessionId || "").length > 0
            if (hasSavedSearch && leftPane.restoreSearchState(tab, true)) {
                page.switchingTabLocation = false
                return
            }

            if (!pendingRevealUrl.length)
                leftPane.prepareTabScrollRestore(tab.location, tab.viewScrollY !== undefined ? tab.viewScrollY : 0)
            page.setPaneLocationWithoutHistory(tab.location)
            if (pendingRevealUrl.length)
                Qt.callLater(function() { leftPane.revealExternalItem(tab.location, pendingRevealUrl, true) })
            else if (revealAnchorUrl.length)
                // A previously revealed target is part of this tab's state.
                // Restore only its selection; the saved viewport is restored
                // independently so returning to the tab never jumps to center.
                Qt.callLater(function() { leftPane.restoreTabRevealSelection(tab.location, revealAnchorUrl) })
            Qt.callLater(function() {
                // If two tabs point at the same folder, assigning the location
                // is a no-op. A target tab without search still needs its normal
                // directory model back after the outgoing search was detached.
                if (sameLocation && !(tab.searchVisible && String(tab.searchQuery || "").trim().length))
                    leftPane.refreshDirectory()
                const restoredSearch = leftPane.restoreSearchState(tab, false)
                if (!restoredSearch && !pendingRevealUrl.length && !revealAnchorUrl.length)
                    leftPane.restorePendingTabScroll()
                page.switchingTabLocation = false
            })
        })
    }

    function scrollTabsBy(delta) {
        if (!tabScrollView || !tabScrollView.contentItem)
            return
        const flick = tabScrollView.contentItem
        const maxX = Math.max(0, flick.contentWidth - flick.width)
        flick.contentX = Math.max(0, Math.min(maxX, flick.contentX + delta))
    }

    function ensureActiveTabVisible() {
        if (!tabScrollView || !tabScrollView.contentItem || currentTab < 0)
            return
        const flick = tabScrollView.contentItem
        const itemX = currentTab * (160 + tabRow.spacing)
        const itemRight = itemX + 160
        if (itemX < flick.contentX)
            flick.contentX = Math.max(0, itemX - 6)
        else if (itemRight > flick.contentX + flick.width)
            flick.contentX = Math.min(Math.max(0, flick.contentWidth - flick.width), itemRight - flick.width + 6)
    }

    function addTab(location, activateOverride) {
        const target = location && location.length ? location : leftPane.currentLocation
        const activate = activateOverride !== undefined ? !!activateOverride : tabBehaviorSettings.activateNewTabs
        var next = tabs.slice()
        next.push(makeTab(target))
        tabs = next
        if (activate)
            switchToTab(tabs.length - 1)
    }

    function moveTab(from, to) {
        if (from < 0 || to < 0 || from >= tabs.length || to >= tabs.length || from === to)
            return

        captureCurrentTabSearchState()
        var next = tabs.slice()
        const moved = next.splice(from, 1)[0]
        next.splice(to, 0, moved)

        var activeIndex = currentTab
        if (currentTab === from) {
            activeIndex = to
        } else if (from < currentTab && to >= currentTab) {
            activeIndex = currentTab - 1
        } else if (from > currentTab && to <= currentTab) {
            activeIndex = currentTab + 1
        }

        tabs = next
        currentTab = activeIndex
    }

    function navigateCurrentTab(location) {
        if (!location || !location.length)
            return
        leftPane.navigateTo(location)
    }

    function revealInNewTab(location, itemUrl) {
        const activate = tabBehaviorSettings.activateNewTabs
        var next = tabs.slice()
        next.push(makeTab(location, itemUrl))
        tabs = next
        if (activate)
            switchToTab(tabs.length - 1)
    }

    function updateTabFromPane(location) {
        if (page.switchingTabLocation)
            return
        if (currentTab < 0 || currentTab >= tabs.length)
            return
        const oldTab = tabs[currentTab]
        if (!oldTab || oldTab.location === location)
            return

        var next = tabs.slice()
        const searchState = leftPane.searchState()
        // A real navigation inside the tab is a fresh directory view. Only
        // switching away from and back to the same tab restores its scroll.
        searchState.viewScrollY = 0
        if (suppressTabHistory) {
            next[currentTab] = withSearchState({
                "location": location,
                "back": oldTab.back ? oldTab.back.slice() : [],
                "forward": oldTab.forward ? oldTab.forward.slice() : [],
                "directoryViews": oldTab.directoryViews || ({}),
                "pendingRevealUrl": "",
                "revealAnchorUrl": ""
            }, searchState)
        } else {
            var back = oldTab.back ? oldTab.back.slice() : []
            if (oldTab.location && (!back.length || back[back.length - 1] !== oldTab.location))
                back.push(oldTab.location)
            next[currentTab] = withSearchState({
                "location": location,
                "back": back,
                "forward": [],
                "directoryViews": oldTab.directoryViews || ({}),
                "pendingRevealUrl": "",
                "revealAnchorUrl": ""
            }, searchState)
        }
        tabs = next
    }

    function goBack(originPane) {
        const pane = originPane || activePane
        if (pane && pane.dismissSearchForNavigation())
            return
        if (pane && pane.closeMusicGroup()) return
        rememberDirectoryView(leftPane.currentLocation, leftPane.navigationViewState())
        const tab = tabAt(currentTab)
        if (!tab || !tab.back || tab.back.length === 0) {
            // Tabs live inside this BrowsePage. Falling back to the outer
            // StackView while more than one tab is open destroys the whole
            // page and therefore closes every tab at once. With no history in
            // the active tab, simply stay put and preserve the tab session.
            if (tabs.length > 1)
                return
            page.backRequested(tab ? tab.location : leftPane.currentLocation)
            return
        }
        var back = tab.back.slice()
        const target = back.pop()
        var forward = tab.forward ? tab.forward.slice() : []
        if (tab.location)
            forward.push(tab.location)
        var next = tabs.slice()
        next[currentTab] = { "location": target, "back": back, "forward": forward,
                             "directoryViews": tab.directoryViews || ({}) }
        tabs = next
        restoreHistoryView(target, next[currentTab])
    }

    function goForward() {
        rememberDirectoryView(leftPane.currentLocation, leftPane.navigationViewState())
        const tab = tabAt(currentTab)
        if (!tab || !tab.forward || tab.forward.length === 0)
            return
        var forward = tab.forward.slice()
        const target = forward.pop()
        var back = tab.back ? tab.back.slice() : []
        if (tab.location)
            back.push(tab.location)
        var next = tabs.slice()
        next[currentTab] = { "location": target, "back": back, "forward": forward,
                             "directoryViews": tab.directoryViews || ({}) }
        tabs = next
        restoreHistoryView(target, next[currentTab])
    }

    function closeTab(index) {
        if (tabs.length <= 1 || index < 0 || index >= tabs.length)
            return
        // Snapshot the active pane even when a different tab is being closed;
        // otherwise restoring the active tab from the model could revive an
        // older search query.
        captureCurrentTabSearchState(true)
        var closed = closedTabs.slice()
        closed.push(tabs[index])
        if (closed.length > 20) {
            const dropped = closed.shift()
            if (dropped && dropped.searchSessionId)
                leftPane.releaseSearchSession(dropped.searchSessionId)
        }
        closedTabs = closed

        var next = tabs.slice()
        next.splice(index, 1)
        var newIndex = currentTab
        if (index < currentTab)
            newIndex = currentTab - 1
        else if (index === currentTab)
            newIndex = Math.min(index, next.length - 1)
        tabs = next
        switchToTab(Math.max(0, newIndex), true)
    }

    function reopenClosedTab() {
        if (!closedTabs.length)
            return
        var closed = closedTabs.slice()
        const tab = closed.pop()
        closedTabs = closed
        var next = tabs.slice()
        next.push({
            "location": tab.location,
            "back": tab.back ? tab.back.slice() : [],
            "forward": tab.forward ? tab.forward.slice() : [],
            "searchVisible": !!tab.searchVisible,
            "searchQuery": tab.searchQuery !== undefined ? String(tab.searchQuery) : "",
            "searchEverywhere": !!tab.searchEverywhere,
            "searchSessionId": tab.searchSessionId !== undefined ? String(tab.searchSessionId) : "",
            "searchScrollY": tab.searchScrollY !== undefined ? Number(tab.searchScrollY) : -1,
            "viewScrollY": tab.viewScrollY !== undefined ? Number(tab.viewScrollY) : 0,
            "directoryViews": tab.directoryViews || ({}),
            "pendingRevealUrl": tab.pendingRevealUrl !== undefined ? String(tab.pendingRevealUrl) : ""
        })
        tabs = next
        switchToTab(tabs.length - 1)
    }

    function showToast(message) {
        toastLabel.text = page.lang.localizeMessage(message)
        toast.open()
    }

    function prepareToolbarMenu(menu) {
        page.dismissActivePaneTransientUi()
        toolbarPressedMenu = menu
        toolbarPressedMenuWasOpen = !!menu.interactionOpen
    }

    function openToolbarMenu(menu, button) {
        const sameMenuWasOpen = toolbarPressedMenu === menu && toolbarPressedMenuWasOpen
        toolbarPressedMenu = null
        toolbarPressedMenuWasOpen = false
        if (sameMenuWasOpen) {
            if (typeof menu.requestClose === "function") menu.requestClose()
            else menu.close()
            return
        }

        // Explicitly close the other top-bar popups before opening the target.
        // This avoids overlapping enter/exit transitions after rapid switching.
        if (wheelSpeedPopup !== menu && wheelSpeedPopup.visible)
            wheelSpeedPopup.close()
        if (hiddenFilesMenu !== menu && hiddenFilesMenu.visible)
            hiddenFilesMenu.requestClose()
        if (toolbarViewMenu !== menu && toolbarViewMenu.visible)
            toolbarViewMenu.requestClose()
        if (page.activePane && page.activePane.directoryMenuVisible
                && typeof page.activePane.closeDirectoryMenu === "function")
            page.activePane.closeDirectoryMenu()

        const p = button.parent.mapToItem(page, button.x, button.y + button.height + 6)
        const target = page.mapToItem(menu.parent,
            Math.max(8, Math.min(page.width - menu.implicitWidth - 8, p.x)),
            Math.max(8, Math.min(page.height - menu.implicitHeight - 8, p.y)))
        menu.x = target.x
        menu.y = target.y
        if (typeof menu.requestOpen === "function") menu.requestOpen()
        else menu.open()
    }

    function applyWheelSpeedText() {
        const entered = Number(wheelSpeedField.text)
        if (wheelSpeedField.text.trim().length > 0 && Number.isFinite(entered))
            AppTheme.setWheelScrollStep(entered)
        wheelSpeedField.text = String(AppTheme.wheelScrollStep)
    }

    function setSplitEnabled(enabled) {
        if (!enabled) {
            page.activePane = leftPane
            page.splitEnabled = false
            return
        }
        page.splitEnabled = true
        Qt.callLater(function() {
            const rightPane = rightPaneLoader.item
            if (!rightPane)
                return
            rightPane.currentLocation = leftPane.currentLocation
            const half = Math.max(340, splitView.width / 2)
            leftPane.SplitView.preferredWidth = half
            rightPaneLoader.SplitView.preferredWidth = half
        })
    }

    Component.onCompleted: {
        const start = initialLocation.length ? initialLocation : StandardPaths.writableLocation(StandardPaths.HomeLocation)
        tabs = [makeTab(start)]
        sidebarLocationChanged(start)
    }

    function shortcutActionAllowed(action) {
        const pane = page.activePane
        if (!page.visible || !pane || KeyboardShortcuts.editorOpen
            || pane.renameConfirmationOpen
            || (pane.hostWindow && pane.hostWindow.mediaViewerConsumesF11)) return false
        if (action === "symlink") return pane.fileShortcutsAllowed && pane.selectedCanCreateLink
        if (action === "hardlink") return pane.fileShortcutsAllowed && pane.selectedCanCreateLink
                                            && !pane.selectedIsDir && pane.selectedLinkType !== "symlink"
        if (action === "show_target") return pane.fileShortcutsAllowed && pane.selectedCount === 1 && pane.selectedLinkType.length > 0
        if (action === "containing_folder") return pane.fileShortcutsAllowed && pane.selectedCount === 1
        if (action === "rename") return pane.renameShortcutAllowed
                                       && (!pane.inlineRenameUrl.length || pane.inlineRenameUrl !== pane.selectedUrl)
        if (action === "cancel" || action === "find" || action === "location") return true
        return pane.fileShortcutsAllowed
    }

    function runShortcutAction(action) {
        switch (action) {
        case "terminal": page.activePane.openTerminalHere(); break
        case "select_all": page.activePane.selectAll(); break
        case "clear_selection": page.activePane.clearSelection(); break
        case "copy": page.activePane.copySelection(); break
        case "cut": page.activePane.cutSelection(); break
        case "paste": page.activePane.pasteHere(); break
        case "undo": page.activePane.undoLastOperation(); break
        case "duplicate": page.activePane.duplicateSelection(); break
        case "rename": page.activePane.renameSelection(); break
        case "trash": page.activePane.trashSelection(); break
        case "delete": page.activePane.deleteSelection(); break
        case "open": page.activePane.openSelected(); break
        case "toggle_selection": page.activePane.togglePrimarySelection(); break
        case "properties": page.activePane.showPropertiesForSelection(); break
        case "refresh": page.activePane.refreshDirectory(); break
        case "find": page.activePane.showSearch(); break
        case "kfind": page.activePane.openKFindHere(); break
        case "hidden": page.activePane.toggleHiddenFiles(); break
        case "location": page.activePane.focusLocationField(); break
        case "up": page.activePane.navigateUp(); break
        case "home": page.activePane.navigateTo(StandardPaths.writableLocation(StandardPaths.HomeLocation)); break
        case "back": page.activePane.handleBackNavigation(); break
        case "forward": page.activePane.handleForwardNavigation(); break
        case "new_folder": page.activePane.showNewFolderDialog(); break
        case "new_file": page.activePane.showNewFileDialog(); break
        case "new_tab": page.addTab(page.activePane.currentLocation); break
        case "close_tab": page.closeTab(page.currentTab); break
        case "reopen_tab": page.reopenClosedTab(); break
        case "next_tab": page.switchToTab((page.currentTab + 1) % page.tabs.length); break
        case "previous_tab": page.switchToTab((page.currentTab - 1 + page.tabs.length) % page.tabs.length); break
        case "split": page.setSplitEnabled(!page.splitEnabled); break
        case "grid": page.activePane.setGridView(); break
        case "list": page.activePane.setListView(); break
        case "zoom_in": page.activePane.adjustIconSize(12); break
        case "zoom_out": page.activePane.adjustIconSize(-12); break
        case "zoom_reset": page.activePane.resetIconSize(); break
        case "move_left": page.activePane.moveSelection("left", false); break
        case "move_right": page.activePane.moveSelection("right", false); break
        case "move_up": page.activePane.moveSelection("up", false); break
        case "move_down": page.activePane.moveSelection("down", false); break
        case "extend_left": page.activePane.moveSelection("left", true); break
        case "extend_right": page.activePane.moveSelection("right", true); break
        case "extend_up": page.activePane.moveSelection("up", true); break
        case "extend_down": page.activePane.moveSelection("down", true); break
        case "first": page.activePane.selectEdge(false, false); break
        case "last": page.activePane.selectEdge(true, false); break
        case "extend_first": page.activePane.selectEdge(false, true); break
        case "extend_last": page.activePane.selectEdge(true, true); break
        case "cancel": page.activePane.cancelCurrentAction(); break
        case "paste_into": page.activePane.pasteIntoSelection(); break
        case "symlink": page.activePane.showNewSymlinkDialog(true); break
        case "hardlink": page.activePane.showNewHardlinkDialog(true); break
        case "show_target": page.activePane.showSelectedLinkTarget(false); break
        case "containing_folder": page.activePane.openSelectedContainingFolder(false); break
        }
    }

    Instantiator {
        model: KeyboardShortcuts.catalog.filter(function(action) { return action.scope === "browser" })
        delegate: Shortcut {
            required property var modelData
            sequences: KeyboardShortcuts.bindings[modelData.id] || []
            context: Qt.WindowShortcut
            enabled: page.shortcutActionAllowed(modelData.id)
            onActivated: page.runShortcutAction(modelData.id)
        }
    }


    Rectangle {
        anchors.fill: parent
        radius: Math.max(0, AppTheme.panelRadius - 1)
        color: AppTheme.workspace
        antialiasing: true
    }

    GPopupDismissHandler { popup: hiddenFilesMenu; opener: hiddenFilesButton }
    GPopupDismissHandler { popup: toolbarViewMenu; opener: viewButton }
    GPopupDismissHandler { popup: wheelSpeedPopup; opener: wheelSpeedButton }

    GMenu {
        id: hiddenFilesMenu
        parent: page
        maximumPopupHeight: Math.max(180, Math.min(620, page.height - 24))
        closePolicy: Popup.CloseOnEscape
        GMenuItem {
            text: lang.t("show_hidden")
            checkable: true
            checked: page.activePane.showHiddenFiles
            onTriggered: page.activePane.showHiddenFiles = !page.activePane.showHiddenFiles
        }
        GMenuSeparator {}
        GMenuItem {
            text: lang.t("hidden_top")
            checkable: true
            checked: page.activePane.hiddenPlacement === "top"
            enabled: page.activePane.showHiddenFiles
            onTriggered: page.activePane.hiddenPlacement = "top"
        }
        GMenuItem {
            text: lang.t("hidden_normal")
            checkable: true
            checked: page.activePane.hiddenPlacement === "normal"
            enabled: page.activePane.showHiddenFiles
            onTriggered: page.activePane.hiddenPlacement = "normal"
        }
        GMenuItem {
            text: lang.t("hidden_bottom")
            checkable: true
            checked: page.activePane.hiddenPlacement === "bottom"
            enabled: page.activePane.showHiddenFiles
            onTriggered: page.activePane.hiddenPlacement = "bottom"
        }
    }

    GMenu {
        id: toolbarViewMenu
        parent: page
        maximumPopupHeight: Math.max(180, Math.min(620, page.height - 24))
        closePolicy: Popup.CloseOnEscape
        GMenuItem {
            text: lang.t("grid")
            checkable: true
            checked: page.activePane.gridMode
            onTriggered: page.activePane.gridMode = true
        }
        GMenuItem {
            text: lang.t("list")
            checkable: true
            checked: !page.activePane.gridMode
            onTriggered: page.activePane.gridMode = false
        }
        GMenuSeparator {}
        GMenuItem {
            text: lang.t("default_icons")
            checkable: true
            checked: !AppTheme.useSystemIcons
            onTriggered: AppTheme.setSystemIcons(false)
        }
        GMenuItem {
            text: lang.t("system_icons")
            checkable: true
            checked: AppTheme.useSystemIcons
            onTriggered: AppTheme.setSystemIcons(true)
        }
        GMenuSeparator {}
        GMenuItem {
            text: lang.language === "tr" ? "Klasör önizlemeleri" : "Folder previews"
            checkable: true
            checked: page.activePane.folderPreviewsEnabled
            onTriggered: page.activePane.folderPreviewsEnabled = !page.activePane.folderPreviewsEnabled
        }
        GMenuSeparator {}
        GMenuItem {
            text: lang.language === "tr" ? "Yeni sekmeye otomatik geç" : "Switch to newly opened tab"
            checkable: true
            checked: tabBehaviorSettings.activateNewTabs
            onTriggered: tabBehaviorSettings.activateNewTabs = !tabBehaviorSettings.activateNewTabs
        }
        GMenuSeparator {}
        GMenuItem {
            text: lang.language === "tr" ? "Tek tıkla aç" : "Open with single click"
            checkable: true
            checked: AppTheme.singleClickOpen
            onTriggered: AppTheme.setSingleClickOpen(!AppTheme.singleClickOpen)
        }
        GMenuSeparator {}
        GMenuItem {
            text: lang.t("kio_progress")
            checkable: true
            checked: AppTheme.kioProgressEnabled
            onTriggered: AppTheme.setKioProgressEnabled(!AppTheme.kioProgressEnabled)
        }
        GMenuSeparator {}
        GMenuItem {
            objectName: "skipHiddenNameConfirmationItem"
            text: lang.language === "tr" ? "Gizli ad onayını atla" : "Skip hidden name confirmation"
            checkable: true
            checked: AppTheme.skipHiddenNameConfirmation
            onTriggered: AppTheme.setSkipHiddenNameConfirmation(!AppTheme.skipHiddenNameConfirmation)
        }
        GMenuItem {
            objectName: "skipPermanentDeleteConfirmationItem"
            text: lang.language === "tr" ? "Kalıcı silme onayını atla" : "Skip permanent delete confirmation"
            checkable: true
            checked: AppTheme.skipPermanentDeleteConfirmation
            onTriggered: AppTheme.setSkipPermanentDeleteConfirmation(!AppTheme.skipPermanentDeleteConfirmation)
        }
    }

    Popup {
        id: wheelSpeedPopup
        parent: page
        property bool interactionOpen: false
        onAboutToShow: interactionOpen = true
        onAboutToHide: interactionOpen = false
        implicitWidth: 300
        implicitHeight: 126
        padding: 14
        focus: true
        closePolicy: Popup.CloseOnEscape
        onOpened: wheelSpeedField.text = String(AppTheme.wheelScrollStep)
        onClosed: page.applyWheelSpeedText()

        background: Rectangle {
            radius: 20
            color: AppTheme.commandBar
            border.color: AppTheme.borderStrong
            border.width: 1
        }

        contentItem: ColumnLayout {
            spacing: 7

            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: lang.t("wheel_speed")
                    color: AppTheme.text
                    font.pixelSize: 12
                    font.bold: true
                }
                GTextField {
                    id: wheelSpeedField
                    Layout.preferredWidth: 62
                    Layout.preferredHeight: 30
                    horizontalAlignment: TextInput.AlignHCenter
                    color: AppTheme.text
                    font.pixelSize: 12
                    selectByMouse: true
                    inputMethodHints: Qt.ImhDigitsOnly
                    validator: IntValidator { bottom: 10; top: 1000 }
                    onEditingFinished: page.applyWheelSpeedText()
                    background: Rectangle {
                        radius: 8
                        color: AppTheme.surfaceRaised
                        border.color: wheelSpeedField.activeFocus ? AppTheme.accent : AppTheme.border
                    }
                }
            }

            GSlider {
                id: wheelSpeedSlider
                Layout.fillWidth: true
                Layout.preferredHeight: 26
                from: 10
                to: 1000
                stepSize: 1
                value: AppTheme.wheelScrollStep
                onMoved: {
                    AppTheme.setWheelScrollStep(value)
                    wheelSpeedField.text = String(AppTheme.wheelScrollStep)
                }
                background: Rectangle {
                    x: wheelSpeedSlider.leftPadding
                    y: wheelSpeedSlider.topPadding + wheelSpeedSlider.availableHeight / 2 - height / 2
                    width: wheelSpeedSlider.availableWidth
                    height: 5
                    radius: 3
                    color: AppTheme.border
                    Rectangle {
                        width: wheelSpeedSlider.visualPosition * parent.width
                        height: parent.height
                        radius: parent.radius
                        color: AppTheme.accent
                    }
                }
                handle: Rectangle {
                    x: wheelSpeedSlider.leftPadding + wheelSpeedSlider.visualPosition * (wheelSpeedSlider.availableWidth - width)
                    y: wheelSpeedSlider.topPadding + wheelSpeedSlider.availableHeight / 2 - height / 2
                    width: 17
                    height: 17
                    radius: 9
                    color: wheelSpeedSlider.pressed ? AppTheme.accent : AppTheme.surface
                    border.color: AppTheme.accent
                    border.width: 2
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Text { text: "10"; color: AppTheme.textMuted; font.pixelSize: 10 }
                Item { Layout.fillWidth: true }
                Text { text: "1000"; color: AppTheme.textMuted; font.pixelSize: 10 }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 12

        Rectangle {
            id: commandBar
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            radius: AppTheme.commandRadius
            color: AppTheme.commandBar
            border.color: AppTheme.border
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6
                GButton {
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    text: ""
                    display: AbstractButton.IconOnly
                    icon.source: AppTheme.icon("nav-back.svg")
                    icon.width: 16; icon.height: 16
                    implicitWidth: 32; implicitHeight: 34
                    enabled: tabScrollView.contentItem && tabScrollView.contentItem.contentX > 0.5
                    onClicked: { page.dismissActivePaneTransientUi(); page.scrollTabsBy(-220) }
                    GToolTip { text: lang.language === "tr" ? "Sekmeleri sola kaydır" : "Scroll tabs left" }
                    background: Rectangle { radius: 9; color: parent.hovered ? AppTheme.surfaceHover : "transparent"; border.color: "transparent"; opacity: parent.enabled ? 1 : 0.45 }
                }
    
                GButton {
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    text: ""
                    display: AbstractButton.IconOnly
                    icon.source: AppTheme.icon("nav-forward.svg")
                    icon.width: 16; icon.height: 16
                    implicitWidth: 32; implicitHeight: 34
                    enabled: tabScrollView.contentItem
                             && tabScrollView.contentItem.contentX < Math.max(0, tabScrollView.contentItem.contentWidth - tabScrollView.contentItem.width) - 0.5
                    onClicked: { page.dismissActivePaneTransientUi(); page.scrollTabsBy(220) }
                    GToolTip { text: lang.language === "tr" ? "Sekmeleri sağa kaydır" : "Scroll tabs right" }
                    background: Rectangle { radius: 9; color: parent.hovered ? AppTheme.surfaceHover : "transparent"; border.color: "transparent"; opacity: parent.enabled ? 1 : 0.45 }
                }
    
                ScrollView {
                    id: tabScrollView
                    Layout.fillWidth: true
                    Layout.preferredHeight: 36
                    contentHeight: 36
                    ScrollBar.vertical.policy: ScrollBar.AlwaysOff
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    TapHandler {
                        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
                        gesturePolicy: TapHandler.WithinBounds
                        onTapped: page.dismissActivePaneTransientUi()
                    }
                    Row {
                        id: tabRow
                        spacing: 6
                        Repeater {
                            model: page.tabs
                            delegate: Rectangle {
                                id: tabDelegate
                                required property int index
                                required property var modelData
                                property bool suppressClickAfterDrag: false
                                property int dragStartIndex: -1
                                readonly property bool dragging: tabDrag.active
                                readonly property bool dragTarget: page.tabDragTarget === index && page.tabDragFrom !== index
                                width: 160
                                height: 34
                                radius: 11
                                readonly property bool administratorTab: page.isAdminLocation(modelData.location)
                                color: dragTarget
                                       ? AppTheme.accentSoft
                                       : (administratorTab
                                          ? (index === page.currentTab
                                             ? (AppTheme.dark ? "#3A2A18" : "#FFF0D8")
                                             : (AppTheme.dark ? "#2A2118" : "#FFF8ED"))
                                          : (index === page.currentTab ? AppTheme.surface : AppTheme.surfaceRaised))
                                border.width: dragTarget ? 2 : (administratorTab ? 1.5 : 1)
                                border.color: dragTarget
                                              ? AppTheme.accent
                                              : (administratorTab
                                                 ? AppTheme.warning
                                                 : (index === page.currentTab ? AppTheme.borderStrong : "transparent"))
                                scale: dragging ? 0.96 : 1.0
                                opacity: dragging ? 0.78 : 1.0
                                z: dragging ? 20 : 0
                                transform: Translate {
                                    x: tabDelegate.dragging ? tabDrag.translation.x : 0
                                }
    
                                Timer {
                                    id: tabDragClickReset
                                    interval: 90
                                    onTriggered: tabDelegate.suppressClickAfterDrag = false
                                }
    
                                // Reordering is calculated from the pointer translation instead of
                                // QML DropArea. The tab delegate itself stays in place during the drag,
                                // so relying on Drag attached coordinates would never enter sibling tabs.
    
                                DragHandler {
                                    id: tabDrag
                                    target: null
                                    acceptedButtons: Qt.LeftButton
                                    xAxis.enabled: true
                                    yAxis.enabled: false
    
                                    function updateDestination() {
                                        if (!active || tabDelegate.dragStartIndex < 0 || page.tabs.length < 2)
                                            return
                                        const step = tabDelegate.width + tabRow.spacing
                                        const raw = tabDelegate.dragStartIndex + Math.round(translation.x / step)
                                        page.tabDragTarget = Math.max(0, Math.min(page.tabs.length - 1, raw))
                                    }
    
                                    onTranslationChanged: updateDestination()
    
                                    onActiveChanged: {
                                        if (active) {
                                            tabDelegate.dragStartIndex = tabDelegate.index
                                            page.tabDragFrom = tabDelegate.index
                                            page.tabDragTarget = tabDelegate.index
                                            updateDestination()
                                            return
                                        }
    
                                        const from = tabDelegate.dragStartIndex
                                        const destination = page.tabDragTarget
                                        tabDelegate.dragStartIndex = -1
                                        page.tabDragFrom = -1
                                        page.tabDragTarget = -1
                                        tabDelegate.suppressClickAfterDrag = true
                                        tabDragClickReset.restart()
    
                                        // Mutate the Repeater model only after the pointer grab has been
                                        // released. This avoids destroying the source delegate mid-drag.
                                        if (from >= 0 && destination >= 0 && from !== destination) {
                                            Qt.callLater(function() {
                                                page.moveTab(from, destination)
                                            })
                                        }
                                    }
                                }
    
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 12
                                    anchors.rightMargin: 7
                                    spacing: 6
                                    Rectangle {
                                        visible: parent.parent.administratorTab
                                        Layout.preferredWidth: 30
                                        Layout.preferredHeight: 18
                                        radius: 6
                                        color: AppTheme.warning
                                        Text {
                                            anchors.centerIn: parent
                                            text: "ADM"
                                            color: AppTheme.dark ? "#21180A" : "#FFFFFF"
                                            font.pixelSize: 8
                                            font.bold: true
                                        }
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: page.titleFor(modelData.location)
                                        color: AppTheme.text
                                        elide: Text.ElideRight
                                        font.pixelSize: 11
                                        font.bold: index === page.currentTab
                                    }
                                    GToolButton {
                                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                                        visible: page.tabs.length > 1
                                        text: ""
                                        display: AbstractButton.IconOnly
                                        icon.source: AppTheme.icon("close-ui.svg")
                                        icon.width: 16; icon.height: 16
                                        implicitWidth: 28; implicitHeight: 28
                                        onPressed: page.dismissActivePaneTransientUi()
                                        onClicked: page.closeTab(index)
                                        background: Rectangle { radius: 8; color: parent.hovered ? AppTheme.border : "transparent" }
                                    }
                                }
                                MouseArea {
                                    anchors.left: parent.left
                                    anchors.top: parent.top
                                    anchors.bottom: parent.bottom
                                    anchors.right: parent.right
                                    anchors.rightMargin: 30
                                    acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onPressed: page.dismissActivePaneTransientUi()
                                    onClicked: function(mouse) {
                                        if (tabDelegate.suppressClickAfterDrag) {
                                            mouse.accepted = true
                                            return
                                        }
                                        if (mouse.button === Qt.MiddleButton) {
                                            if (page.tabs.length > 1)
                                                page.closeTab(index)
                                        } else {
                                            page.switchToTab(index)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
    
                GButton {
                    id: wheelSpeedButton
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    text: "↕ " + AppTheme.wheelScrollStep
                    implicitWidth: 72; implicitHeight: 34
                    topPadding: 4; bottomPadding: 4
                    font.pixelSize: 12
                    GToolTip { text: lang.t("wheel_speed") }
                    onPressed: page.prepareToolbarMenu(wheelSpeedPopup)
                    onClicked: page.openToolbarMenu(wheelSpeedPopup, wheelSpeedButton)
                    background: Rectangle {
                        radius: 11
                        color: parent.hovered || wheelSpeedPopup.opened ? AppTheme.accentSoft : AppTheme.surface
                        border.color: wheelSpeedPopup.opened ? AppTheme.accentBorder : AppTheme.border
                    }
                }
    
                GButton {
                    id: newTabButton
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    text: ""
                    display: AbstractButton.IconOnly
                    icon.source: AppTheme.icon("plus.svg")
                    icon.width: 16; icon.height: 16
                    implicitWidth: 34; implicitHeight: 34
                    GToolTip {
                        text: lang.t("new_tab")
                    }
                    onClicked: { page.dismissActivePaneTransientUi(); page.addTab(page.activePane.currentLocation, true) }
                    background: Rectangle { radius: 11; color: parent.hovered ? AppTheme.surfaceHover : "transparent"; border.color: "transparent" }
                }
                GButton {
                    id: splitButton
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    text: ""
                    implicitWidth: 34; implicitHeight: 34
                    GToolTip {
                        text: page.splitEnabled ? lang.t("close_split") : lang.t("split_view")
                    }
                    onClicked: { page.dismissActivePaneTransientUi(); page.setSplitEnabled(!page.splitEnabled) }
                    contentItem: Item {
                        anchors.centerIn: parent
                        implicitWidth: 18
                        implicitHeight: 18
                        CrispIcon {
                            anchors.centerIn: parent
                            width: 16; height: 16
                            source: AppTheme.icon("split-view.svg")
                            opacity: page.splitEnabled ? 1.0 : 0.88
                        }
                        Rectangle {
                            visible: page.splitEnabled
                            width: 10
                            height: 2
                            radius: 1
                            color: AppTheme.accent
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.rightMargin: -1
                            anchors.topMargin: -1
                            rotation: -45
                        }
                    }
                    background: Rectangle {
                        radius: 11
                        color: parent.hovered || page.splitEnabled ? AppTheme.accentSoft : AppTheme.surface
                        border.color: page.splitEnabled ? AppTheme.accentBorder : AppTheme.border
                    }
                }
                GButton {
                    id: hiddenFilesButton
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    text: ""
                    implicitWidth: 34; implicitHeight: 34
                    GToolTip {
                        text: lang.t("show_hidden")
                    }
                    onPressed: page.prepareToolbarMenu(hiddenFilesMenu)
                    onClicked: page.openToolbarMenu(hiddenFilesMenu, hiddenFilesButton)
                    contentItem: Item {
                        implicitWidth: 18; implicitHeight: 18
                        CrispIcon {
                            anchors.centerIn: parent
                            source: AppTheme.icon("eye.svg")
                            width: 16; height: 16
                            opacity: page.activePane.showHiddenFiles ? 1.0 : 0.68
                        }
                    }
                    background: Rectangle {
                        radius: 11
                        color: parent.hovered || page.activePane.showHiddenFiles ? AppTheme.accentSoft : AppTheme.surface
                        border.color: page.activePane.showHiddenFiles ? AppTheme.accentBorder : AppTheme.border
                    }
                }
                GButton {
                    id: viewButton
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    text: ""
                    implicitWidth: 34; implicitHeight: 34
                    GToolTip {
                        text: lang.t("view")
                    }
                    onPressed: page.prepareToolbarMenu(toolbarViewMenu)
                    onClicked: page.openToolbarMenu(toolbarViewMenu, viewButton)
                    contentItem: Item {
                        anchors.centerIn: parent
                        implicitWidth: 18
                        implicitHeight: 18
                        CrispIcon {
                            anchors.centerIn: parent
                            width: 16; height: 16
                            source: AppTheme.icon("view-layout.svg")
                            opacity: 0.92
                        }
                        Rectangle {
                            width: 4
                            height: 4
                            radius: 3
                            color: page.activePane.gridMode ? AppTheme.accent : AppTheme.textMuted
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.rightMargin: -1
                            anchors.bottomMargin: -1
                        }
                    }
                    background: Rectangle {
                        radius: 11
                        color: parent.hovered ? AppTheme.surfaceHover : "transparent"
                        border.color: "transparent"
                    }
                }
                GButton {
                    id: directoryMenuButton
                    property bool menuWasOpenOnPress: false
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    text: "⋯"
                    implicitWidth: 34; implicitHeight: 34
                    topPadding: 4; bottomPadding: 4
                    font.pixelSize: 14
                    GToolTip {
                        text: lang.t("directory_menu")
                    }
                    onPressed: {
                        page.dismissActivePaneTransientUi()
                        menuWasOpenOnPress = !!page.activePane.directoryMenuShowing
                    }
                    onClicked: {
                        if (menuWasOpenOnPress) {
                            menuWasOpenOnPress = false
                            page.activePane.closeDirectoryMenu()
                            return
                        }
                        menuWasOpenOnPress = false
                        if (wheelSpeedPopup.visible) wheelSpeedPopup.close()
                        if (hiddenFilesMenu.visible) hiddenFilesMenu.requestClose()
                        if (toolbarViewMenu.visible) toolbarViewMenu.requestClose()
                        page.activePane.openDirectoryMenu(directoryMenuButton)
                    }
                    background: Rectangle { radius: 11; color: parent.hovered ? AppTheme.surfaceHover : "transparent"; border.color: "transparent" }
                }
            }
        }

        SplitView {
            id: splitView
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal

            BrowserPane {
                id: leftPane
                SplitView.fillWidth: true
                SplitView.minimumWidth: 340
                initialLocation: page.initialLocation
                lang: page.lang
                contentIndexModel: page.contentIndexModel
                musicPlayer: page.musicPlayer
                quickAccessModel: page.quickAccessModel
                favoritesModel: page.favoritesModel
                adminEditor: page.adminEditor
                cloudAuth: page.cloudAuth
                cloudIntegrationPreferences: page.cloudIntegrationPreferences
                googleDrive: page.googleDrive
                oneDrive: page.oneDrive
                backNavigationEnabled: page.canGoBack()
                forwardNavigationEnabled: page.canGoForward()
                onLocationChangedByUser: function(location) {
                    page.updateTabFromPane(location)
                    page.quickAccessModel.setLastLocation(location)
                    if (page.activePane === leftPane)
                        page.sidebarLocationChanged(location)
                }
                onRequestToast: function(message) { page.showToast(message) }
                onRequestNewTab: function(location) { page.addTab(location) }
                onRequestRevealInNewTab: function(location, itemUrl) { page.revealInNewTab(location, itemUrl) }
                onViewportScrollChanged: function(location, offset) { page.updateCurrentTabViewport(location, offset) }
                onNavigationAboutToStart: function(location, viewState) { page.rememberDirectoryView(location, viewState) }
                onExternalRevealResolved: function(location, itemUrl, offset) { page.updateCurrentTabRevealState(location, itemUrl, offset) }
                onBackNavigationRequested: page.goBack(leftPane)
                onForwardNavigationRequested: page.goForward()
                HoverHandler { onHoveredChanged: if (hovered) page.activePane = leftPane }
            }

            Loader {
                id: rightPaneLoader
                visible: page.splitEnabled
                active: page.splitEnabled
                SplitView.fillWidth: true
                SplitView.minimumWidth: 340
                sourceComponent: rightPaneComponent
                onLoaded: {
                    item.currentLocation = leftPane.currentLocation
                    const half = Math.max(340, splitView.width / 2)
                    leftPane.SplitView.preferredWidth = half
                    rightPaneLoader.SplitView.preferredWidth = half
                }
            }
        }
    }

    Component {
        id: rightPaneComponent
        BrowserPane {
                id: rightPane
                initialLocation: page.initialLocation
                lang: page.lang
                contentIndexModel: page.contentIndexModel
                musicPlayer: page.musicPlayer
                quickAccessModel: page.quickAccessModel
                favoritesModel: page.favoritesModel
                adminEditor: page.adminEditor
                cloudAuth: page.cloudAuth
                cloudIntegrationPreferences: page.cloudIntegrationPreferences
                googleDrive: page.googleDrive
                oneDrive: page.oneDrive
                backNavigationEnabled: page.canGoBack()
                forwardNavigationEnabled: page.canGoForward()
                onLocationChangedByUser: function(location) {
                    page.quickAccessModel.setLastLocation(location)
                    if (page.activePane === rightPane)
                        page.sidebarLocationChanged(location)
                }
                onRequestToast: function(message) { page.showToast(message) }
                HoverHandler { onHoveredChanged: if (hovered) page.activePane = rightPane }
                onRequestNewTab: function(location) { page.addTab(location) }
                onRequestRevealInNewTab: function(location, itemUrl) { page.revealInNewTab(location, itemUrl) }
                onBackNavigationRequested: page.goBack(rightPane)
                onForwardNavigationRequested: page.goForward()
        }
    }

    Connections {
        target: page.adminEditor
        function onOpened() { page.showToast(page.lang.t("admin_file_opened")) }
        function onSaved() { page.showToast(page.lang.t("admin_file_saved")) }
        function onError(message) { page.showToast(message) }
    }


    Popup {
        id: toast
        x: parent.width - width - 26
        y: parent.height - height - 26
        width: Math.min(390, Math.max(210, toastLabel.implicitWidth + 72))
        height: 58
        padding: 0
        closePolicy: Popup.NoAutoClose
        background: Rectangle {
            radius: 20
            color: AppTheme.commandBar
            border.width: 1
            border.color: AppTheme.accentBorder
        }
        contentItem: RowLayout {
            spacing: 12
            Rectangle {
                Layout.leftMargin: 15
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28
                radius: 9
                color: AppTheme.accentSoft
                Text {
                    anchors.centerIn: parent
                    text: "i"
                    color: AppTheme.accent
                    font.pixelSize: 15
                    font.bold: true
                }
            }
            Text {
                id: toastLabel
                Layout.fillWidth: true
                Layout.rightMargin: 16
                color: AppTheme.text
                font.pixelSize: 12
                verticalAlignment: Text.AlignVCenter
                wrapMode: Text.WordWrap
            }
        }
        onOpened: toastTimer.restart()
    }
    Timer { id: toastTimer; interval: 2400; onTriggered: toast.close() }
}
