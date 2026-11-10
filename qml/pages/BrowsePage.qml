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
    property var initialSession: null
    // Only the visual chrome is hosted above the sidebar. Tab history, models
    // and file actions still belong to this page and its existing panes.
    property Item headerHost: null
    readonly property real tabStripHeight: tabsBar.implicitHeight
    readonly property real headerHeight: tabsBar.implicitHeight + browserHeader.spacing + navigationSlot.implicitHeight
    readonly property real toolbarPopupHeight: Math.max(180, Math.min(620,
        page.height - (page.headerHost ? 6 : page.headerHeight + 6) - commandBar.height - 10))

    readonly property color navigationColor: AppTheme.useQtTheme
            ? (AppTheme.translucentWindow ? AppTheme.tint(AppTheme.surfaceRaised, 0.30) : AppTheme.workspace)
            : AppTheme.mix(AppTheme.workspace, AppTheme.surface, 0.55)

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
    property var privateVault: null
    property var operationPresentation: null
    readonly property var notificationSources: splitEnabled && rightPaneLoader.item
        ? [leftPane.fileOperations, rightPaneLoader.item.fileOperations]
        : [leftPane.fileOperations]
    signal backRequested(string lastLocation)
    signal sidebarLocationChanged(string location)
    signal watermarkEditorRequested()

    property var tabs: []
    property var closedTabs: []
    property int currentTab: 0
    // Repeater delegates can appear a frame after tabs/currentTab change.
    // The revision forces the frame to rebind as soon as the new tab exists.
    property int activeSurfaceGeneration: 0
    // Expose the painted selected tab to the shared outer outline, including
    // scrolling and tab reordering. No hard-coded tab position is needed.
    readonly property Item activeTabSurface: {
        const generation = activeSurfaceGeneration
        const count = tabRepeater.count + generation * 0
        const delegate = currentTab >= 0 && currentTab < count
                ? tabRepeater.itemAt(currentTab) : null
        return delegate ? delegate.surfaceItem : null
    }
    readonly property real activeTabScrollPosition: tabScrollView.contentItem
        ? tabScrollView.contentItem.contentX : 0
    readonly property bool activeTabAdministrator: currentTab >= 0 && currentTab < tabs.length
            && isAdminLocation(tabs[currentTab].location)
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
    // Test diagnostics for clicks swallowed by the Qt popup overlay.
    property int toolbarRecoverySeen: 0
    property int toolbarRecoveryApplied: 0

    onCurrentTabChanged: Qt.callLater(page.ensureActiveTabVisible)
    readonly property string visibleLocation: activePane && activePane.currentLocation !== undefined
                                              ? activePane.currentLocation : initialLocation
    onActivePaneChanged: {
        if (activePane && activePane.currentLocation !== undefined)
            sidebarLocationChanged(activePane.currentLocation)
    }

    function isAdminLocation(location) {
        return String(location || "").startsWith("admin:")
    }

    readonly property string tabHomePath: {
        const value = String(StandardPaths.writableLocation(StandardPaths.HomeLocation))
        return value.startsWith("file://") ? decodeURIComponent(value.substring(7)) : value
    }

    function tabLocalPath(location) {
        let value = String(location || "")
        if (value.startsWith("file://")) value = decodeURIComponent(value.substring(7))
        else if (value.startsWith("admin:")) value = value.replace(/^admin:\/*/, "/")
        else if (!value.startsWith("/")) return ""
        return value.length > 1 ? value.replace(/\/+$/, "") : value
    }

    // Small location icon for a tab: the same artwork the sidebar and folder
    // grid use (bundled colored folders, or the desktop icon theme).
    function tabIconSource(location) {
        const value = String(location || "")
        const pick = function(bundled, system) {
            return AppTheme.useSystemIcons ? AppTheme.systemIconAtSize(AppTheme.systemIcon(system), 16)
                                           : AppTheme.icon(bundled)
        }
        if (value.startsWith("category:/")) {
            const icon = AppTheme.categoryDefaultIcon(value.substring("category:/".length))
            return AppTheme.useSystemIcons ? AppTheme.systemIconAtSize(AppTheme.systemIcon(AppTheme.systemIconName(icon)), 16)
                                           : AppTheme.icon(icon)
        }
        if (value.startsWith("trash:")) return pick("trash.svg", "user-trash")
        if (value.startsWith("gdrive://")) return pick("cloud-google.svg", "folder-cloud")
        if (value.startsWith("onedrive://")) return pick("cloud-onedrive.svg", "folder-cloud")
        if (value.startsWith("admin:") && tabLocalPath(value) === "/") return pick("root.svg", "folder-root")
        const path = tabLocalPath(value)
        if (!path.length) return pick("network.svg", "folder-network")
        if (path === "/") return pick("drive.svg", "drive-harddisk")
        if (path === tabHomePath) return pick("home-folder.svg", "user-home")
        const name = path.substring(path.lastIndexOf("/") + 1)
        return AppTheme.useSystemIcons
                ? AppTheme.systemIconAtSize(AppTheme.systemIcon(AppTheme.folderSystemIcon(name, "folder")), 16)
                : AppTheme.bundledFolderIcon(name, path)
    }

    // A running copy/move into this tab's folder (from either pane).
    function tabTransfer(location) {
        const path = tabLocalPath(location)
        if (!path.length) return null
        const lists = [leftPane.fileOperations ? leftPane.fileOperations.transferTargets : []]
        if (rightPaneLoader.item && rightPaneLoader.item.fileOperations)
            lists.push(rightPaneLoader.item.fileOperations.transferTargets)
        for (const list of lists)
            for (const transfer of (list || []))
                if (transfer.path === path) return transfer
        return null
    }

    function titleFor(location) {
        location = String(location || "")
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

    function sessionSnapshot() {
        return {version: 1, locations: tabs.map(function(tab) { return tab.location }), activeIndex: currentTab}
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
        location = String(location || "")
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
            if (index === currentTab) {
                Qt.callLater(page.ensureActiveTabVisible)
                return
            }
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
                    // Restore the cached category snapshot, not a disk reindex.
                    leftPane.directory.refresh()
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
        activeTabScrollAnimation.stop()
        const maxX = Math.max(0, flick.contentWidth - flick.width)
        flick.contentX = Math.max(0, Math.min(maxX, flick.contentX + delta))
    }

    function ensureActiveTabVisible() {
        if (!tabScrollView || !tabScrollView.contentItem || currentTab < 0)
            return
        const flick = tabScrollView.contentItem
        const item = tabRepeater.itemAt(currentTab)
        if (!item || flick.width <= 0)
            return
        // Keep context around the selected tab. Reveal up to two neighbours
        // on either side when space allows, without moving tabs that are
        // already comfortably inside the viewport.
        const padding = 6
        const step = item.width + tabRow.spacing
        const neighbours = Math.min(2, Math.max(0,
            Math.floor((flick.width - item.width - padding * 2) / (step * 2))))
        let before = neighbours
        let after = neighbours
        if (neighbours === 0 && flick.width >= item.width + step + padding * 2) {
            // A narrow strip can fit the active tab plus only one neighbour.
            // Reveal it in the direction of the clicked viewport edge.
            if (item.x + item.width / 2 >= flick.contentX + flick.width / 2)
                after = 1
            else
                before = 1
        }
        const first = tabRepeater.itemAt(Math.max(0, currentTab - before))
        const last = tabRepeater.itemAt(Math.min(tabs.length - 1, currentTab + after))
        if (!first || !last)
            return
        const maxX = Math.max(0, flick.contentWidth - flick.width)
        const left = Math.max(0, first.x - padding)
        const right = Math.min(flick.contentWidth, last.x + last.width + padding)
        let target = flick.contentX
        if (left < target)
            target = left
        else if (right > target + flick.width)
            target = right - flick.width
        target = Math.max(0, Math.min(maxX, target))
        activeTabScrollAnimation.stop()
        if (Math.abs(target - flick.contentX) > 0.5) {
            flick.cancelFlick()
            activeTabScrollAnimation.from = flick.contentX
            activeTabScrollAnimation.to = target
            activeTabScrollAnimation.start()
        }
    }

    NumberAnimation {
        id: activeTabScrollAnimation
        target: tabScrollView.contentItem
        property: "contentX"
        duration: 160
        easing.type: Easing.OutCubic
    }

    function addTab(location, activateOverride) {
        const target = location && location.length ? location : leftPane.currentLocation
        const activate = activateOverride !== undefined ? !!activateOverride : BrowserSession.activateNewTabs
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
        const activate = BrowserSession.activateNewTabs
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

        page.positionToolbarMenu(menu, button)
        if (typeof menu.requestOpen === "function") menu.requestOpen()
        else menu.open()
    }

    function recoverToolbarClick(globalPosition, closingPopup) {
        // This only handles a left-button click on a known toolbar opener;
        // clicks on files, blank space, or menus remain entirely native.
        const active = page.activePane
        const directoryPopup = active ? active.directoryMenuPopup : null
        const recentPopup = active ? active.recentLocationsPopup : null
        const targets = [
            { button: hiddenFilesButton, menu: hiddenFilesMenu },
            { button: viewButton, menu: toolbarViewMenu },
            { button: wheelSpeedButton, menu: wheelSpeedPopup },
            { button: directoryMenuButton, menu: directoryPopup },
            { button: active ? active.recentLocationsOpener : null, menu: recentPopup }
        ]
        for (let i = 0; i < targets.length; ++i) {
            const target = targets[i]
            const button = target.button
            if (!button || !button.visible || !button.enabled || !target.menu
                    || target.menu === closingPopup)
                continue
            const corner = button.mapToGlobal(0, 0)
            if (globalPosition.x < corner.x || globalPosition.y < corner.y
                    || globalPosition.x >= corner.x + button.width
                    || globalPosition.y >= corner.y + button.height)
                continue
            page.toolbarRecoverySeen += 1
            Qt.callLater(function() {
                if (!page.visible || !button.visible || !button.enabled)
                    return
                // Real onClicked succeeded: do not toggle it back off.
                if (target.menu.interactionOpen)
                    return
                page.toolbarRecoveryApplied += 1
                if (target.menu === directoryPopup) {
                    page.activePane.openDirectoryMenu(button)
                } else if (target.menu === recentPopup) {
                    // A recent-locations opener lives inside BrowserPane, not
                    // the page toolbar. Close any old page menus first, then
                    // use its existing API so history and positioning stay in
                    // one place. Never synthesize a second mouse click.
                    if (wheelSpeedPopup.visible) wheelSpeedPopup.close()
                    if (hiddenFilesMenu.visible) hiddenFilesMenu.requestClose()
                    if (toolbarViewMenu.visible) toolbarViewMenu.requestClose()
                    active.openRecentLocationsMenu()
                } else {
                    page.prepareToolbarMenu(target.menu)
                    page.openToolbarMenu(target.menu, button)
                }
            })
            return
        }
    }

    function positionToolbarMenu(menu, button) {
        const p = button.parent.mapToItem(page, button.x, button.y + button.height + 6)
        const target = page.mapToItem(menu.parent,
            Math.max(8, Math.min(page.width - menu.implicitWidth - 8, p.x)),
            Math.max(8, Math.min(page.height - menu.implicitHeight - 8, p.y)))
        menu.x = target.x
        menu.y = target.y
    }
    // Collapsing the sidebar moves/resizes this page while the View settings
    // stay open. Follow the opener after layout instead of retaining old x/y.
    onWidthChanged: Qt.callLater(function() {
        if (toolbarViewMenu.visible) page.positionToolbarMenu(toolbarViewMenu, viewButton)
    })
    Connections {
        target: viewButton
        function onXChanged() {
            if (toolbarViewMenu.visible) Qt.callLater(function() { page.positionToolbarMenu(toolbarViewMenu, viewButton) })
        }
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
        const restored = BrowserSession.normalize(initialSession)
        tabs = restored ? restored.locations.map(function(location) { return page.makeTab(location) }) : [makeTab(start)]
        if (restored) currentTab = restored.activeIndex
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
        case "columns": page.activePane.viewMode = "columns"; break
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
        radius: page.headerHost ? 0 : Math.max(0, AppTheme.panelRadius - 1)
        color: page.headerHost ? "transparent" : AppTheme.workspace
        antialiasing: true
    }

    // A nested Popup.Item overlay can intercept the click used to switch from
    // a menu to a toolbar action. The native dismiss filter reports a finished
    // outside click; defer a fallback until after Qt processes the real click.
    GPopupDismissHandler {
        popup: hiddenFilesMenu; opener: hiddenFilesButton
        onOutsideClickReleased: function(p) { page.recoverToolbarClick(p, hiddenFilesMenu) }
    }
    GPopupDismissHandler {
        popup: toolbarViewMenu; opener: viewButton
        onOutsideClickReleased: function(p) { page.recoverToolbarClick(p, toolbarViewMenu) }
    }
    GPopupDismissHandler {
        popup: wheelSpeedPopup; opener: wheelSpeedButton
        onOutsideClickReleased: function(p) { page.recoverToolbarClick(p, wheelSpeedPopup) }
    }
    Connections {
        target: page.activePane
        function onMenuOutsideClickReleased(position, sourcePopup) {
            page.recoverToolbarClick(position, sourcePopup)
        }
    }

    GMenu {
        id: hiddenFilesMenu
        positioningBounds: page
        parent: page
        maximumPopupHeight: page.toolbarPopupHeight
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

    SettingsDialog {
        id: settingsDialog
        lang: page.lang
        onWatermarkEditorRequested: page.watermarkEditorRequested()
    }

    GMenu {
        id: toolbarViewMenu
        positioningBounds: page
        objectName: "toolbarViewMenu"
        parent: page
        maximumPopupHeight: page.toolbarPopupHeight
        closePolicy: Popup.CloseOnEscape
        GMenuItem {
            text: lang.t("grid")
            checkable: true
            checked: page.activePane.viewMode === "grid"
            onTriggered: page.activePane.setGridView()
        }
        GMenuItem {
            text: lang.t("list")
            checkable: true
            checked: page.activePane.viewMode === "list"
            onTriggered: page.activePane.setListView()
        }
        GMenuItem {
            text: lang.language === "tr" ? "Sütunlar" : "Columns"
            checkable: true
            checked: page.activePane.viewMode === "columns"
            onTriggered: page.activePane.viewMode = "columns"
        }
        GMenuSeparator {}
        Item {
            implicitWidth: 300
            implicitHeight: 68
            width: parent ? parent.width : implicitWidth
            height: implicitHeight
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 2
                RowLayout {
                    Layout.fillWidth: true
                    GText {
                        Layout.fillWidth: true
                        text: lang.language === "tr" ? "Başlık satırları" : "Title lines"
                        baseColor: AppTheme.text
                        basePixelSize: 12
                    }
                    GText {
                        text: AppTheme.fileTitleLines
                        baseColor: AppTheme.accent
                        basePixelSize: 12
                        baseBold: true
                    }
                }
                GSlider {
                    objectName: "fileTitleLinesSlider"
                    Layout.fillWidth: true
                    from: 1; to: 4; stepSize: 1
                    snapMode: Slider.SnapAlways
                    value: AppTheme.fileTitleLines
                    Accessible.name: lang.language === "tr" ? "Başlık satırları" : "Title lines"
                    onMoved: AppTheme.setFileTitleLines(value)
                }
            }
        }
        Item {
            implicitWidth: 300
            implicitHeight: 68
            width: parent ? parent.width : implicitWidth
            height: implicitHeight
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 2
                RowLayout {
                    Layout.fillWidth: true
                    GText {
                        Layout.fillWidth: true
                        text: lang.language === "tr" ? "Alt başlık satırları" : "Subtitle lines"
                        baseColor: AppTheme.text
                        basePixelSize: 12
                    }
                    GText {
                        text: AppTheme.fileSubtitleLines
                        baseColor: AppTheme.accent
                        basePixelSize: 12
                        baseBold: true
                    }
                }
                GSlider {
                    objectName: "fileSubtitleLinesSlider"
                    Layout.fillWidth: true
                    from: 1; to: 4; stepSize: 1
                    snapMode: Slider.SnapAlways
                    value: AppTheme.fileSubtitleLines
                    Accessible.name: lang.language === "tr" ? "Alt başlık satırları" : "Subtitle lines"
                    onMoved: AppTheme.setFileSubtitleLines(value)
                }
            }
        }
        GMenuSeparator {}
        GMenuItem {
            objectName: "filePreviewsMenuItem"
            text: lang.language === "tr" ? "Dosya önizlemeleri" : "File previews"
            checkable: true
            checked: PreviewSettings.enabled
            onTriggered: PreviewSettings.enabled = !PreviewSettings.enabled
        }
        GMenuItem {
            text: lang.language === "tr" ? "Klasör önizlemeleri" : "Folder previews"
            checkable: true
            checked: page.activePane.folderPreviewsEnabled
            onTriggered: page.activePane.folderPreviewsEnabled = !page.activePane.folderPreviewsEnabled
        }
        GMenuSeparator {}
        GMenuItem {
            objectName: "openSettingsMenuItem"
            text: lang.language === "tr" ? "Tüm ayarlar…" : "All settings…"
            icon.source: AppTheme.icon("settings.svg")
            onTriggered: settingsDialog.open()
        }
    }

    Popup {
        id: wheelSpeedPopup
        objectName: "wheelSpeedPopup"
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

        background: GPopupSurface { popup: wheelSpeedPopup }

        contentItem: ColumnLayout {
            spacing: 7

            RowLayout {
                Layout.fillWidth: true
                GText {
                    Layout.fillWidth: true
                    text: lang.t("wheel_speed")
                    baseColor: AppTheme.text
                    basePixelSize: 12
                    baseBold: true
                }
                GTextField {
                    id: wheelSpeedField
                    Layout.preferredWidth: 62
                    Layout.preferredHeight: 30
                    horizontalAlignment: TextInput.AlignHCenter
                    color: AppTheme.text
                    basePixelSize: 12
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
                GText { text: "10"; baseColor: AppTheme.textMuted; basePixelSize: 10 }
                Item { Layout.fillWidth: true }
                GText { text: "1000"; baseColor: AppTheme.textMuted; basePixelSize: 10 }
            }
        }
    }

    ColumnLayout {
        id: browserHeader
        objectName: "browserHeader"
        parent: page.headerHost || page
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: page.headerHeight
        visible: page.visible && (!page.StackView.view || page.StackView.view.currentItem === page)
        spacing: 0

        Rectangle {
            id: tabsBar
            objectName: "browserTabsBar"
            Layout.fillWidth: true
            implicitHeight: Math.max(40, AppTheme.typographySize("menu", 11) + 18)
            color: "transparent"
            RowLayout {
                id: tabStrip
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                anchors.topMargin: 0
                spacing: 2
                GButton {
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    text: ""
                    display: AbstractButton.IconOnly
                    icon.source: AppTheme.icon("nav-back.svg")
                    icon.width: 16; icon.height: 16
                    implicitWidth: 32; implicitHeight: 34
                    visible: tabRow.width > tabScrollView.width
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
                    visible: tabRow.width > tabScrollView.width
                    enabled: tabScrollView.contentItem
                             && tabScrollView.contentItem.contentX < Math.max(0, tabScrollView.contentItem.contentWidth - tabScrollView.contentItem.width) - 0.5
                    onClicked: { page.dismissActivePaneTransientUi(); page.scrollTabsBy(220) }
                    GToolTip { text: lang.language === "tr" ? "Sekmeleri sağa kaydır" : "Scroll tabs right" }
                    background: Rectangle { radius: 9; color: parent.hovered ? AppTheme.surfaceHover : "transparent"; border.color: "transparent"; opacity: parent.enabled ? 1 : 0.45 }
                }
    
                ScrollView {
                    id: tabScrollView
                    objectName: "browserTabScrollView"
                    Layout.preferredWidth: Math.min(tabRow.width, Math.max(0, tabStrip.width - 112))
                    Layout.fillWidth: false
                    Layout.preferredHeight: tabsBar.implicitHeight
                    contentHeight: tabsBar.implicitHeight
                    Layout.alignment: Qt.AlignBottom
                    onWidthChanged: Qt.callLater(page.ensureActiveTabVisible)
                    ScrollBar.vertical.policy: ScrollBar.AlwaysOff
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    TapHandler {
                        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
                        gesturePolicy: TapHandler.WithinBounds
                        onTapped: page.dismissActivePaneTransientUi()
                    }
                    Row {
                        id: tabRow
                        objectName: "browserTabRow"
                        onWidthChanged: Qt.callLater(page.ensureActiveTabVisible)
                        // Neighbouring flares overlap completely (2 x 8 px feet).
                        spacing: -16
                        Repeater {
                            id: tabRepeater
                            onItemAdded: function(index, item) {
                                page.activeSurfaceGeneration++
                            }
                            onItemRemoved: function(index, item) {
                                page.activeSurfaceGeneration++
                            }
                            // Saved scroll/search state changes must not recreate every
                            // tab, interrupting selection animations and pointer targets.
                            model: page.tabs.length
                            delegate: Item {
                                id: tabDelegate
                                objectName: "browserTab" + index
                                required property int index
                                readonly property var tabData: page.tabs[index]
                                readonly property Item surfaceItem: tabSurface
                                property bool suppressClickAfterDrag: false
                                property int dragStartIndex: -1
                                readonly property bool dragging: tabDrag.active
                                readonly property bool dragTarget: page.tabDragTarget === index && page.tabDragFrom !== index
                                width: 208
                                height: tabsBar.implicitHeight
                                readonly property bool administratorTab: page.isAdminLocation(tabData.location)
                                readonly property bool selectedTab: index === page.currentTab
                                readonly property bool hoveredTab: tabMouse.containsMouse || tabClose.hovered
                                // Only the visible tab lists a folder; background tabs are data.
                                readonly property bool loadingTab: selectedTab && !!leftPane.directory && leftPane.directory.loading
                                // Every tab switch relists the shared pane for a moment. Only a
                                // slow listing replaces the (already cached) icon with a spinner.
                                property bool slowLoading: false
                                onLoadingTabChanged: {
                                    if (loadingTab) slowLoadingDelay.restart()
                                    else { slowLoadingDelay.stop(); slowLoading = false }
                                }
                                Timer {
                                    id: slowLoadingDelay
                                    interval: 350
                                    onTriggered: tabDelegate.slowLoading = tabDelegate.loadingTab
                                }
                                readonly property var transfer: page.tabTransfer(tabData.location)
                                FluidTabSurface {
                                    id: tabSurface
                                    objectName: "fluidTabSurface"
                                    anchors.fill: parent
                                    selected: index === page.currentTab
                                    pageOutlineJoined: page.headerHost !== null
                                    hovered: tabMouse.containsMouse
                                    dropTarget: tabDelegate.dragTarget
                                    administrator: tabDelegate.administratorTab
                                    navigationColor: page.navigationColor
                                    tabOverlap: -tabRow.spacing
                                    // A translucent front tab cannot hide another
                                    // tab's paint. Cut only the covered lower wing.
                                    occludedOnLeft: AppTheme.translucentWindow && page.tabDragFrom < 0
                                            && index > 0 && page.currentTab === index - 1
                                    occludedOnRight: AppTheme.translucentWindow && page.tabDragFrom < 0
                                            && index < page.tabs.length - 1 && !selected
                                }
                                scale: dragging ? 0.96 : 1.0
                                opacity: dragging ? 0.78 : 1.0
                                z: dragging ? 20 : (index === page.currentTab ? 10 : 1)
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
                                    anchors.leftMargin: 26
                                    anchors.rightMargin: 26
                                    anchors.topMargin: 4
                                    anchors.bottomMargin: 0
                                    spacing: 6
                                    Item {
                                        objectName: "browserTabIcon"
                                        Layout.preferredWidth: 16
                                        Layout.preferredHeight: 16
                                        CrispIcon {
                                            anchors.fill: parent
                                            visible: !tabDelegate.slowLoading
                                            source: page.tabIconSource(tabData.location)
                                            opacity: tabDelegate.selectedTab || tabDelegate.hoveredTab ? 1 : 0.82
                                        }
                                        GBusyIndicator {
                                            objectName: "browserTabLoading"
                                            anchors.centerIn: parent
                                            width: 16; height: 16
                                            visible: tabDelegate.slowLoading
                                            running: visible
                                        }
                                    }
                                    Rectangle {
                                        visible: parent.parent.administratorTab
                                        Layout.preferredWidth: 30
                                        Layout.preferredHeight: 18
                                        radius: 6
                                        color: AppTheme.warning
                                        GText {
                                            anchors.centerIn: parent
                                            text: "ADM"
                                            baseColor: AppTheme.dark ? "#21180A" : "#FFFFFF"
                                            basePixelSize: 8
                                            baseBold: true
                                        }
                                    }
                                    GText {
                                        objectName: "browserTabTitle"
                                        Layout.fillWidth: true
                                        text: page.titleFor(tabData.location)
                                        baseColor: tabDelegate.selectedTab || tabDelegate.hoveredTab ? AppTheme.text : AppTheme.textMuted
                                        elide: Text.ElideRight
                                        basePixelSize: 11
                                        // Weight stays constant: selection must never reflow the title.
                                        baseFontWeight: Font.Medium
                                    }
                                    GToolButton {
                                        id: tabClose
                                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                                        objectName: "browserTabClose"
                                        visible: page.tabs.length > 1
                                        // Keeps its slot (titles never jump); shown on the
                                        // active or hovered tab only.
                                        opacity: tabDelegate.selectedTab || tabDelegate.hoveredTab ? 1 : 0
                                        enabled: opacity > 0
                                        Behavior on opacity { NumberAnimation { duration: 90 } }
                                        text: ""
                                        display: AbstractButton.IconOnly
                                        icon.source: AppTheme.icon("close-ui.svg")
                                        icon.width: 14; icon.height: 14
                                        implicitWidth: 22; implicitHeight: 22
                                        onPressed: page.dismissActivePaneTransientUi()
                                        onClicked: page.closeTab(index)
                                        background: Rectangle {
                                            radius: width / 2
                                            color: parent.pressed ? AppTheme.mix(AppTheme.border, AppTheme.accent, 0.25)
                                                 : parent.hovered ? AppTheme.border : "transparent"
                                        }
                                    }
                                }
                                // Active tab: accent stripe along the upper edge.
                                Rectangle {
                                    objectName: "browserTabAccent"
                                    visible: tabDelegate.selectedTab && !tabDelegate.administratorTab
                                    x: tabSurface.wing + tabSurface.corner
                                    y: tabSurface.capTop
                                    width: Math.max(0, parent.width - 2 * x)
                                    height: 2.5
                                    radius: 1.25
                                    color: AppTheme.accent
                                    opacity: tabSurface.selectionProgress
                                }
                                // Divider between two inactive, unhovered neighbours. Drawn by the
                                // right-hand tab: later delegates paint over earlier ones.
                                Rectangle {
                                    objectName: "browserTabDivider"
                                    readonly property int previous: index - 1
                                    readonly property Item previousTab: previous >= 0 && tabRepeater.count > previous
                                                                        ? tabRepeater.itemAt(previous) : null
                                    visible: index > 0 && !tabDelegate.selectedTab
                                             && previous !== page.currentTab && !tabDelegate.hoveredTab
                                             && !(previousTab && previousTab.hoveredTab) && page.tabDragFrom < 0
                                    // Exactly where both vertical sides meet.
                                    x: tabSurface.wing - 0.5
                                    y: tabSurface.capTop + 9
                                    width: 1
                                    height: Math.max(0, parent.height - y - 9)
                                    // border is nearly the window colour in light themes.
                                    color: AppTheme.dark ? Qt.rgba(1, 1, 1, 0.16) : AppTheme.borderStrong
                                    z: 30
                                }
                                // A copy/move into this tab's folder: thin progress line.
                                Item {
                                    id: tabTransferBar
                                    objectName: "browserTabTransfer"
                                    visible: tabDelegate.transfer !== null
                                    x: tabSurface.wing + 4
                                    width: Math.max(0, parent.width - 2 * x)
                                    y: parent.height - 4
                                    height: 2
                                    clip: true
                                    Rectangle { anchors.fill: parent; radius: 1; color: AppTheme.accentSoft }
                                    readonly property int percent: tabDelegate.transfer ? tabDelegate.transfer.percent : -1
                                    readonly property color barColor: tabDelegate.transfer && tabDelegate.transfer.paused
                                                                      ? AppTheme.warning : AppTheme.accent
                                    Rectangle {
                                        visible: parent.percent >= 0
                                        height: parent.height
                                        radius: 1
                                        color: parent.barColor
                                        width: parent.width * Math.max(0, parent.percent) / 100
                                        Behavior on width { NumberAnimation { duration: 180 } }
                                    }
                                    Rectangle {
                                        // Unknown size: a sliding segment.
                                        id: tabTransferSlide
                                        visible: tabTransferBar.visible && tabTransferBar.percent < 0
                                        height: tabTransferBar.height
                                        radius: 1
                                        width: tabTransferBar.width * 0.3
                                        color: tabTransferBar.barColor
                                        NumberAnimation on x {
                                            running: tabTransferSlide.visible
                                            loops: Animation.Infinite
                                            from: -tabTransferBar.width * 0.3; to: tabTransferBar.width; duration: 1100
                                        }
                                    }
                                }
                                MouseArea {
                                    id: tabMouse
                                    anchors.left: parent.left
                                    anchors.top: parent.top
                                    anchors.bottom: parent.bottom
                                    anchors.right: parent.right
                                    // Keep the selection hit area outside the close button
                                    // (26 px inset + 22 px button) in the curved body.
                                    anchors.rightMargin: page.tabs.length > 1 ? 48 : 0
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
                    id: newTabButton
                    objectName: "browserNewTabButton"
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    text: ""
                    display: AbstractButton.IconOnly
                    icon.source: AppTheme.icon("plus.svg")
                    icon.width: 14; icon.height: 14
                    implicitWidth: 28; implicitHeight: 28
                    Layout.alignment: Qt.AlignVCenter
                    Layout.topMargin: 4
                    GToolTip {
                        text: lang.t("new_tab")
                    }
                    onClicked: { page.dismissActivePaneTransientUi(); page.addTab(page.activePane.currentLocation, true) }
                    background: Rectangle {
                        radius: height / 2
                        color: parent.hovered ? AppTheme.surfaceHover : AppTheme.mix(AppTheme.background, AppTheme.surfaceRaised, 0.35)
                    }
                }
                Item { Layout.fillWidth: true }
            }
        }
        Item {
            id: navigationSlot
            objectName: "browserNavigationHost"
            Layout.fillWidth: true
            // Split view moves navigation into each pane.
            visible: !page.splitEnabled
            implicitHeight: page.splitEnabled ? 0 : Math.max(42, AppTheme.typographySize("menu", 11) + 18)
            Rectangle {
                objectName: "browserNavigationSurface"
                anchors.fill: parent
                topLeftRadius: 12
                topRightRadius: 12
                bottomLeftRadius: 0
                bottomRightRadius: 0
                color: page.navigationColor
                visible: page.headerHost === null
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        anchors.topMargin: page.headerHost ? 6 : page.headerHeight + 6
        anchors.bottomMargin: 8
        spacing: 6

        Rectangle {
            id: commandBar
            objectName: "browserCommandBar"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(38, commandControls.controlHeight + 8)
            radius: 9
            color: AppTheme.commandBar
            border.color: AppTheme.border
            border.width: 1

            RowLayout {
                id: commandControls
                objectName: "browserCommandControls"
                readonly property real controlHeight: Math.max(28, AppTheme.typographySize("menu", 11) + 10)
                // Narrow windows collapse the bar in steps instead of letting the
                // RowLayout squeeze buttons on top of each other. Hidden actions
                // stay available from the context menu and keyboard shortcuts.
                readonly property int compactLevel: commandBar.width < 340 ? 4
                                                  : commandBar.width < 420 ? 3
                                                  : commandBar.width < 500 ? 2
                                                  : commandBar.width < 640 ? 1 : 0
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                anchors.topMargin: 4
                anchors.bottomMargin: 4
                spacing: 4
                GButton {
                    objectName: "commandNewFolder"
                    topPadding: 4; bottomPadding: 4
                    // Narrow bars show only the icon. An empty text makes GButton use
                    // its icon-button size and padding; text padding (16 px a side)
                    // would squeeze the icon in a 34 px button down to a dot.
                    text: commandControls.compactLevel > 0 ? "" : lang.t("new_folder")
                    display: commandControls.compactLevel > 0 ? AbstractButton.IconOnly : AbstractButton.TextBesideIcon
                    implicitWidth: commandControls.compactLevel > 0 ? 34
                                   : Math.max(90, implicitContentWidth + leftPadding + rightPadding)
                    Accessible.name: lang.t("new_folder")
                    Layout.minimumWidth: implicitWidth
                    GToolTip { visible: commandControls.compactLevel > 0 && parent.hovered; text: lang.t("new_folder") }
                    icon.source: AppTheme.icon("plus.svg")
                    icon.width: 14; icon.height: 14
                    implicitHeight: commandControls.controlHeight
                    visible: !page.activePane.isCategoryLocation && !page.activePane.isTrashLocation
                    onClicked: page.activePane.showNewFolderDialog()
                    background: Rectangle { radius: 7; color: parent.hovered ? AppTheme.surfaceHover : "transparent" }
                }
                Rectangle {
                    visible: !page.activePane.isCategoryLocation && !page.activePane.isTrashLocation
                    Layout.preferredWidth: 1; Layout.preferredHeight: 22; color: AppTheme.border
                }
                Repeater {
                    model: [
                        {key: "cut", icon: "edit-cut.svg", label: lang.t("cut")},
                        {key: "copy", icon: "edit-copy.svg", label: lang.t("copy")},
                        {key: "paste", icon: "edit-paste.svg", label: lang.t("paste")},
                        {key: "rename", icon: "edit-rename.svg", label: lang.t("rename")},
                        {key: "trash", icon: "delete.svg", label: lang.t("trash")},
                        {key: "properties", icon: "properties.svg", label: lang.t("properties")}
                    ]
                    delegate: GToolButton {
                        required property var modelData
                        objectName: "command_" + modelData.key
                        visible: !((commandControls.compactLevel >= 2 && (modelData.key === "rename" || modelData.key === "properties"))
                                   || (commandControls.compactLevel >= 3 && (modelData.key === "cut" || modelData.key === "copy")))
                        implicitWidth: 34; implicitHeight: commandControls.controlHeight
                        Layout.minimumWidth: implicitWidth
                        icon.source: AppTheme.icon(modelData.icon)
                        icon.width: 16; icon.height: 16
                        enabled: modelData.key === "paste" ? page.activePane.canPasteHere
                                 : page.activePane.selectedCount > 0
                        Accessible.name: modelData.label
                        GToolTip { text: parent.modelData.label + page.activePane.shortcutSuffix(parent.modelData.key) }
                        onClicked: page.runShortcutAction(modelData.key)
                    }
                }
                Item {
                    id: categoryToolsSlot
                    objectName: "categoryToolsHost"
                    visible: page.activePane.isCategoryLocation
                    Layout.preferredWidth: visible ? page.activePane.categoryToolsWidth : 0
                    Layout.preferredHeight: commandControls.controlHeight
                }
                Item { Layout.fillWidth: true }
                GButton {
                    id: wheelSpeedButton
                    visible: commandControls.compactLevel < 4
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    text: commandControls.compactLevel > 0 ? "↕" : "↕ " + AppTheme.wheelScrollStep
                    implicitWidth: commandControls.compactLevel > 0 ? 34 : 72; implicitHeight: commandControls.controlHeight
                    Layout.minimumWidth: implicitWidth
                    topPadding: 4; bottomPadding: 4
                    basePixelSize: 12
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
                    id: splitButton
                    visible: commandControls.compactLevel < 4
                    Layout.minimumWidth: 34
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    text: ""
                    implicitWidth: 34; implicitHeight: commandControls.controlHeight
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
                            objectName: "splitViewIcon"
                            source: AppTheme.icon(page.splitEnabled ? "split-view.svg" : "split-view-off.svg")
                            opacity: page.splitEnabled ? 1.0 : 0.88
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
                    Layout.minimumWidth: 34
                    objectName: "hiddenFilesButton"
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    text: ""
                    implicitWidth: 34; implicitHeight: commandControls.controlHeight
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
                    Layout.minimumWidth: 34
                    objectName: "viewButton"
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    text: ""
                    implicitWidth: 34; implicitHeight: commandControls.controlHeight
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
                    }
                    background: Rectangle {
                        radius: 11
                        color: parent.hovered ? AppTheme.surfaceHover : "transparent"
                        border.color: "transparent"
                    }
                }
                GButton {
                    id: directoryMenuButton
                    Layout.minimumWidth: 34
                    property bool menuWasOpenOnPress: false
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    text: "⋯"
                    implicitWidth: 34; implicitHeight: commandControls.controlHeight
                    topPadding: 4; bottomPadding: 4
                    basePixelSize: 14
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

            // A clear divider in every theme: a hairline with a centred grip
            // that turns to the accent colour while hovered or dragged.
            handle: Item {
                id: splitHandle
                objectName: "browserSplitHandle"
                implicitWidth: 14
                readonly property bool hovered: SplitHandle.hovered
                readonly property bool pressed: SplitHandle.pressed
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.topMargin: 8
                    anchors.bottomMargin: 8
                    width: 1
                    color: splitHandle.hovered || splitHandle.pressed ? AppTheme.accentBorder : AppTheme.borderStrong
                }
                Rectangle {
                    objectName: "browserSplitGrip"
                    anchors.centerIn: parent
                    width: splitHandle.pressed || splitHandle.hovered ? 6 : 4
                    height: splitHandle.pressed ? 56 : 44
                    radius: width / 2
                    color: splitHandle.pressed ? AppTheme.accent
                         : splitHandle.hovered ? AppTheme.accentBorder
                         : Qt.rgba(AppTheme.textMuted.r, AppTheme.textMuted.g, AppTheme.textMuted.b, 0.55)
                    Behavior on height { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
                }
                HoverHandler { cursorShape: Qt.SplitHCursor }
            }

            BrowserPane {
                id: leftPane
                objectName: "browserLeftPane"
                SplitView.fillWidth: true
                SplitView.minimumWidth: 340
                unifiedSurface: page.headerHost !== null
                categoryToolsHost: categoryToolsSlot
                categoryToolsVisible: page.activePane === leftPane
                navigationHost: page.splitEnabled ? null : navigationSlot
                splitNavigation: page.splitEnabled
                navigationVisible: page.activePane === leftPane
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
                privateVault: page.privateVault
                operationPresentation: page.operationPresentation
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
                objectName: "browserRightPane"
                unifiedSurface: page.headerHost !== null
                categoryToolsHost: categoryToolsSlot
                categoryToolsVisible: page.activePane === rightPane
                navigationHost: page.splitEnabled ? null : navigationSlot
                splitNavigation: page.splitEnabled
                navigationVisible: page.activePane === rightPane
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
                privateVault: page.privateVault
                operationPresentation: page.operationPresentation
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
        objectName: "browserToast"
        x: parent.width - width - 26
        y: parent.height - height - 26
        width: Math.min(390, Math.max(210, toastLabel.implicitWidth + 72))
        height: 58
        padding: 0
        closePolicy: Popup.NoAutoClose
        background: GPopupSurface {
            popup: toast
            radius: 20
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
                GText {
                    anchors.centerIn: parent
                    text: "i"
                    baseColor: AppTheme.accent
                    basePixelSize: 15
                    baseBold: true
                }
            }
            GText {
                id: toastLabel
                Layout.fillWidth: true
                Layout.rightMargin: 16
                baseColor: AppTheme.text
                basePixelSize: 12
                verticalAlignment: Text.AlignVCenter
                wrapMode: Text.WordWrap
            }
        }
        onOpened: toastTimer.restart()
    }
    Timer { id: toastTimer; interval: 2400; onTriggered: toast.close() }
}
