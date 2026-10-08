import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Effects
import QtQml.Models
import QtCore
import Lurviko.Backend
import Lurviko.App

Rectangle {
    id: root
    property string initialLocation: ""
    property alias currentLocation: directory.location
    readonly property int itemCount: directory.count
    property bool gridMode: true
    property int iconSize: 168
    // Category views have their own zoom level. Changing a category must not
    // silently resize ordinary directories (and vice versa).
    readonly property int categoryIconSize: AppTheme.categoryIconSize
    readonly property int musicIconSize: AppTheme.musicIconSize
    readonly property var hostWindow: ApplicationWindow.window
    readonly property var systemDirectoryIconSizes: iconPicker.availableSystemIconSizes("folder", 256)
    property alias showHiddenFiles: directory.showHidden
    property alias hiddenPlacement: directory.hiddenPlacement
    property alias folderPreviewsEnabled: paneSettings.folderPreviewsEnabled
    required property var lang
    property var contentIndexModel: null
    property var musicPlayer: null
    readonly property var musicDockHost: root.categoryKey === "music"
                                         ? musicDockHostItem : directoryMusicDockHost
    property alias musicPlayerArea: viewStack
    property var quickAccessModel: null
    required property var favoritesModel
    required property var adminEditor
    required property var cloudAuth
    required property var cloudIntegrationPreferences
    required property var googleDrive
    required property var oneDrive
    signal locationChangedByUser(string location)
    signal requestToast(string message)
    signal requestNewTab(string location)
    signal requestRevealInNewTab(string location, string itemUrl)
    signal viewportScrollChanged(string location, real offset)
    signal navigationAboutToStart(string location, var viewState)
    signal externalRevealResolved(string location, string itemUrl, real offset)
    signal backNavigationRequested()
    signal forwardNavigationRequested()
    property bool backNavigationEnabled: true
    property bool forwardNavigationEnabled: true

    property string selectedUrl: ""
    property string selectedLocalPath: ""
    property string selectedName: ""
    property bool selectedIsDir: false
    property var selectedSize: 0
    property var selectedModified: null
    property string selectedMimeType: ""
    property string selectedSuffix: ""
    property string selectedSystemIconName: ""
    property string selectedLinkType: ""
    property string selectedLinkTarget: ""
    property string pendingHardLinkRevealSource: ""
    property bool pendingHardLinkRevealNewTab: false
    property var selectedUrls: []
    property var selectedLookup: ({})
    property int selectionRevision: 0
    property int primaryIndex: -1
    property int selectionAnchorIndex: -1
    property bool rubberSelecting: false
    property string rubberView: ""
    property real rubberStartX: 0
    property real rubberStartY: 0
    property real rubberCurrentX: 0
    property real rubberCurrentY: 0
    property int rubberModifiers: Qt.NoModifier
    property var rubberBaseSelection: []
    property var rubberPreviewSelection: []
    property real rubberPointerViewportY: 0
    property bool scrollRestorePending: false
    property real scrollRestoreY: 0
    property bool scrollRestoreGridMode: true
    // History and tab restoration are separate from directory refreshes.
    property real pendingTabScrollOffset: -1
    property string pendingTabScrollLocation: ""
    property int pendingTabScrollAttempts: 0
    property var pendingNavigationSelection: null
    property string pendingNavigationPrimaryUrl: ""
    // Fresh navigation starts at the top; Back/Forward restores its saved view.
    property string pendingNavigationTopLocation: ""
    property bool pendingResultSelection: false
    property bool pendingResultAwaitRefresh: false
    property string pendingResultMode: ""
    property string pendingResultLocation: ""
    property var pendingResultNames: []
    property var pendingRenameSourceUrls: []
    property var pendingRenameResultUrls: []
    property var pendingResultBeforeUrls: []
    property bool pendingResultRevealSingle: false
    property var pendingResultBeforeSignatures: ({})
    property bool pendingResultKeepUntilMatch: false
    property string pendingExternalRevealUrl: ""
    property string pendingExternalRevealLocation: ""
    property int pendingExternalRevealAttempts: 0
    property bool pendingExternalRevealRefreshRequested: false
    property bool pendingExternalRevealCenter: true
    property bool searchVisible: false
    property bool searchEverywhere: false
    property bool restoringSearchState: false
    property var locationCompletions: []
    property int locationCompletionIndex: 0
    property bool locationCompletionExplicit: false
    property bool locationCompletionCycling: false
    property var locationCompletionCycleItems: []
    property string locationCompletionBaseText: ""
    property bool locationEditMode: false
    readonly property bool directoryMenuVisible: backgroundContextMenu.visible
    readonly property bool directoryMenuShowing: backgroundContextMenu.interactionOpen
    property var directoryMenuOpener: null
    GPopupDismissHandler { popup: recentLocationsMenu; opener: recentLocationsButton }
    GPopupDismissHandler {
        popup: backgroundContextMenu
        opener: root.directoryMenuOpener
        relatedPopups: [newItemMenu, sortMenu]
    }
    readonly property var breadcrumbItems: root.buildBreadcrumbItems(directory.displayLocation)
    readonly property int selectedCount: selectedUrls.length
    property string actionMode: "copy"
    property string drivePickerAction: "copy"
    property string pendingArchiveUrl: ""
    property string pendingArchiveConflictSource: ""
    property string pendingArchiveConflictDestination: ""
    property bool pendingArchiveConflictSubfolder: false
    property var pendingArchiveConflicts: []
    property var pendingContextMenu: null
    property real pendingContextX: 0
    property real pendingContextY: 0
    property bool submenuActivationReady: false
    property bool contextShiftDelete: false
    property int thumbnailRevision: 0
    property bool loadingOverlayReady: false
    property string inlineRenameUrl: ""
    property string inlineRenameOriginalName: ""
    property var inlineRenameEditor: null
    readonly property bool renameConfirmationOpen: hiddenRenameDialog.visible
    onInlineRenameUrlChanged: {
        if (hiddenRenameDialog.actionType === "rename" && hiddenRenameDialog.targetUrl.length
            && inlineRenameUrl !== hiddenRenameDialog.targetUrl)
            hiddenRenameDialog.close()
    }
    property string typeAheadBuffer: ""
    // Consecutive wheel events share a destination so a fast spin builds
    // speed instead of restarting from the current visible position.
    property double wheelBurstLastMs: 0
    property int wheelBurstCount: 0
    property int wheelBurstDirection: 0
    property bool userLocationSignalsReady: false
    property var textEditTarget: null
    property string breadcrumbContextLocation: ""
    onGridModeChanged: wheelScrollAnimation.stop()
    readonly property bool selectedIsGoogleDrive: selectedUrl.startsWith("gdrive://")
    readonly property bool selectedIsOneDrive: selectedUrl.startsWith("onedrive://")
    readonly property bool selectedIsCloud: selectedIsGoogleDrive || selectedIsOneDrive
    readonly property bool isCategoryLocation: directory.location.startsWith("category:/")
    readonly property string categoryKey: root.isCategoryLocation
                                          ? directory.location.substring("category:/".length) : ""
    readonly property bool mediaGalleryCategory: root.categoryKey === "images" || root.categoryKey === "videos"
    readonly property bool groupedCategory: root.isCategoryLocation && !root.mediaGalleryCategory
    readonly property bool categorySingleClickOpen: {
        if (AppTheme.singleClickOpen)
            return true
        const revision = root.contentIndexModel ? root.contentIndexModel.categoryInteractionRevision : 0
        return revision >= 0 && root.contentIndexModel && root.categoryKey.length > 0
                ? !!root.contentIndexModel.categorySingleClickOpen(root.categoryKey) : false
    }
    property int categoryViewRevision: 0
    property var groupedCategoryFiles: []
    property int musicLibraryTab: 0
    property string musicLibraryFilter: ""
    property string musicGenreFilter: ""
    property string musicYearFilter: ""
    onMusicGenreFilterChanged: musicGenreCombo.syncSelection()
    onMusicYearFilterChanged: musicYearCombo.syncSelection()
    readonly property var musicLibraryTracks: root.visible && root.categoryKey === "music"
                                              && root.musicPlayer && root.musicPlayer.libraryManager
                                              ? root.musicPlayer.libraryManager.tracks : []
    readonly property var musicTabLabels: lang.language === "tr"
                                          ? ["Parçalar", "Albümler", "Sanatçılar", "Favoriler", "Çalma listeleri", "Son dinlenenler", "En çok dinlenenler", "İstatistikler"]
                                          : ["Tracks", "Albums", "Artists", "Favorites", "Playlists", "Recently played", "Most played", "Statistics"]
    readonly property var musicGenreOptions: musicLibraryView.genreOptions
    function selectMusicGenre(value) {
        root.musicGenreFilter = String(value || "")
        // Genre browsing lists the whole collection, rather than only the
        // recently played/favorite tracks from a previously selected tab.
        if (root.musicGenreFilter.length) root.musicLibraryTab = 0
    }
    readonly property var musicYearOptions: {
        const values = ({})
        const tracks = root.musicLibraryTracks || []
        for (let i = 0; i < tracks.length; ++i) {
            const value = String(tracks[i].year || "").trim()
            if (value.length) values[value] = true
        }
        const years = Object.keys(values).sort(function(a, b) { return Number(b) - Number(a) })
        const result = [lang.language === "tr" ? "Tüm yıllar" : "All years"]
        return result.concat(years)
    }
    readonly property int activeIconSize: root.categoryKey === "music" ? root.musicIconSize
                                         : (root.isCategoryLocation ? root.categoryIconSize : root.iconSize)
    readonly property real categoryTextScale: Math.max(0.94, Math.min(1.18,
                                                     1.0 + ((root.categoryIconSize / 168.0) - 1.0) * 0.12))
    property var mediaGalleryFiles: []
    property bool dlnaViewActive: false
    // DlnaMediaView has navigation below category:/videos that DirectoryModel
    // cannot see. Keep a live navigator reference so toolbar/mouse Back,
    // Forward and Up operate on that virtual hierarchy first.
    property var activeDlnaNavigator: null
    property bool dlnaSourceForwardAvailable: false
    readonly property bool effectiveBackNavigationEnabled:
        (root.categoryKey === "videos" && root.dlnaViewActive)
        || (root.categoryKey === "music" && !!musicLibraryView.groupSelection) || root.backNavigationEnabled
    readonly property bool effectiveForwardNavigationEnabled:
        root.categoryKey === "videos" && root.dlnaViewActive
        ? (!!root.activeDlnaNavigator && root.activeDlnaNavigator.canNavigateForward)
        : ((root.categoryKey === "videos" && root.dlnaSourceForwardAvailable)
           || root.forwardNavigationEnabled)
    readonly property bool localSearchMode: root.isCategoryLocation || (root.categoryKey === "videos" && root.dlnaViewActive)
    property double mediaGalleryBytes: 0
    property double mediaGalleryHomeBytes: 0
    readonly property real mediaGalleryStorageShare: mediaGalleryHomeBytes > 0
                                                    ? Math.min(1.0, mediaGalleryBytes / mediaGalleryHomeBytes) : 0
    readonly property bool currentLocationIsLocal: directory.location.startsWith("/") || directory.location.startsWith("file://")
    readonly property bool selectedCanCreateLink: selectedCount === 1
                                                 && selectedUrl.startsWith("file://")
                                                 && selectedLocalPath.startsWith("/")
    readonly property bool isTrashLocation: directory.location.startsWith("trash:")
    readonly property string homeLocation: StandardPaths.writableLocation(StandardPaths.HomeLocation)
    readonly property bool currentLocationIsHome: root.normalizedLocalPath(directory.location) === root.normalizedLocalPath(root.homeLocation)
    readonly property bool selectedIsArchive: selectedCount === 1 && !selectedIsDir && selectedLocalPath.length > 0
                                              && isArchive(selectedName, selectedMimeType)
    readonly property bool renameShortcutAllowed: !locationEditMode && !locationField.activeFocus
                                                  && !searchInput.activeFocus
                                                  && !renameInput.activeFocus
                                                  && !newFolderInput.activeFocus
                                                  && !newFileInput.activeFocus
                                                  && !renameDialog.opened
                                                  && !hiddenRenameDialog.visible
    readonly property bool fileShortcutsAllowed: root.renameShortcutAllowed
                                                && inlineRenameUrl.length === 0
                                                && (!root.hostWindow
                                                    || !root.hostWindow.activeVideoViewer
                                                    || !root.hostWindow.activeVideoViewer.visible)

    focus: true
    color: AppTheme.surfaceRaised
    radius: 16
    border.color: AppTheme.border

    function normalizedLocalPath(value) {
        let path = String(value || "")
        if (path.startsWith("file://")) {
            path = path.substring(7)
            try { path = decodeURIComponent(path) } catch (e) {}
        }
        while (path.length > 1 && path.endsWith("/"))
            path = path.substring(0, path.length - 1)
        return path
    }

    function openFilelightPopover(sourceItem) {
        if (filelightPopup.visible) {
            filelightPopup.close()
            return
        }
        if (!sourceItem || !root.currentLocationIsLocal)
            return
        const point = sourceItem.mapToItem(root, 0, 0)
        filelightPopup.x = Math.max(8, Math.min(root.width - filelightPopup.width - 8,
                                               point.x + sourceItem.width - filelightPopup.width))
        filelightPopup.y = Math.max(8, point.y - filelightPopup.height - 8)
        filelightPopup.open()
    }

    function dismissLocationCompletion() {
        locationCompletions = []
        locationCompletionIndex = 0
    }

    function scheduleTabScrollSnapshot() {
        // While a tab viewport is being restored, GridView/ListView can emit
        // transient contentY=0 changes as delegates are rebuilt. Never persist
        // those temporary values over the tab's real saved offset.
        if (!root.visible || directory.loading || directory.searchActive || root.isCategoryLocation
                || root.pendingTabScrollOffset >= 0)
            return
        tabScrollSnapshotTimer.restart()
    }

    Timer {
        id: tabScrollSnapshotTimer
        interval: 90
        repeat: false
        onTriggered: {
            if (!root.visible || directory.loading || root.pendingTabScrollOffset >= 0
                    || root.pendingNavigationTopLocation.length || root.scrollRestorePending)
                return
            const offset = root.tabScrollOffset()
            if (offset >= 0)
                root.viewportScrollChanged(directory.location, offset)
        }
    }

    Keys.onPressed: function(event) {
        if (!root.fileShortcutsAllowed || directory.loading)
            return
        const blockedModifiers = event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)
        if (blockedModifiers !== 0)
            return
        if (event.text && event.text.length > 0 && event.text !== " ") {
            if (root.typeAheadSelect(event.text))
                event.accepted = true
        }
    }

    Timer {
        id: typeAheadReset
        interval: 950
        repeat: false
        onTriggered: root.typeAheadBuffer = ""
    }

    Timer {
        id: loadingOverlayDelay
        interval: 160
        repeat: false
        onTriggered: root.loadingOverlayReady = directory.loading && directory.count === 0
    }

    Settings {
        id: paneSettings
        category: "BrowserView"
        property int iconSize: 168
        property int systemIconSize: 128
        property bool folderPreviewsEnabled: true
    }

    function normalizedShareDavLocation(value) {
        var raw = String(value || "").trim()
        if (!raw.length)
            return ""
        if (raw.startsWith("https://"))
            return "webdavs://" + raw.substring(8)
        if (raw.startsWith("http://"))
            return "webdav://" + raw.substring(7)
        if (raw.indexOf("://") < 0)
            return "webdavs://" + raw
        return raw
    }

    function cloudShareLocation(provider) {
        if (provider === "dropbox") return String(cloudIntegrationPreferences.dropboxLocation || "").trim()
        if (provider === "nextcloud") return root.normalizedShareDavLocation(cloudIntegrationPreferences.nextcloudLocation)
        if (provider === "owncloud") return root.normalizedShareDavLocation(cloudIntegrationPreferences.ownCloudLocation)
        if (provider === "mega") return String(cloudIntegrationPreferences.megaLocation || "").trim()
        if (provider === "pcloud") return String(cloudIntegrationPreferences.pCloudLocation || "").trim()
        if (provider === "webdav") return root.normalizedShareDavLocation(cloudIntegrationPreferences.webDavLocation)
        return ""
    }

    function cloudShareTargetAvailable(provider) {
        if (provider === "google")
            return cloudIntegrationPreferences.showGoogle && root.cloudAuth.googleAccessToken.length > 0
        if (provider === "onedrive")
            return cloudIntegrationPreferences.showOneDrive && root.cloudAuth.oneDriveAccessToken.length > 0
        if (provider === "dropbox")
            return cloudIntegrationPreferences.showDropbox && root.cloudShareLocation(provider).length > 0
        if (provider === "nextcloud")
            return cloudIntegrationPreferences.showNextcloud && root.cloudShareLocation(provider).length > 0
        if (provider === "owncloud")
            return cloudIntegrationPreferences.showOwnCloud && root.cloudShareLocation(provider).length > 0
        if (provider === "mega")
            return cloudIntegrationPreferences.showMega && root.cloudShareLocation(provider).length > 0
        if (provider === "pcloud")
            return cloudIntegrationPreferences.showPCloud && root.cloudShareLocation(provider).length > 0
        if (provider === "webdav")
            return cloudIntegrationPreferences.showWebDav && root.cloudShareLocation(provider).length > 0
        return false
    }

    function sendSelectionToConfiguredCloud(provider, urls) {
        var destination = root.cloudShareLocation(provider)
        if (!destination.length || !urls || urls.length === 0)
            return
        fileOps.copyMany(urls, destination)
    }

    Connections {
        target: directory
        function onModelReset() { root.thumbnailRevision += 1 }
    }

    // QML Settings persists immediately. Writing it for every slider pixel
    // stalls the render loop, so keep visual updates live and persist once the
    // drag has briefly settled.
    onIconSizeChanged: directoryIconSizeSaveTimer.restart()

    Timer {
        id: directoryIconSizeSaveTimer
        interval: 180
        repeat: false
        onTriggered: {
            if (AppTheme.useSystemIcons)
                paneSettings.systemIconSize = root.iconSize
            else
                paneSettings.iconSize = root.iconSize
        }
    }


    function directorySystemIconMinimum() {
        const values = systemDirectoryIconSizes
        return values && values.length > 0 ? Number(values[0]) : 16
    }

    function directorySystemIconMaximum() {
        const values = systemDirectoryIconSizes
        return values && values.length > 0 ? Number(values[values.length - 1]) : 256
    }

    function applyDirectoryIconModeSize() {
        if (AppTheme.useSystemIcons)
            root.iconSize = Math.max(directorySystemIconMinimum(), Math.min(directorySystemIconMaximum(), paneSettings.systemIconSize))
        else
            root.iconSize = Math.max(104, Math.min(360, paneSettings.iconSize))
    }

    Connections {
        target: AppTheme
        function onUseSystemIconsChanged() {
            root.applyDirectoryIconModeSize()
            root.scheduleGroupedCategoryReload()
        }
    }

    function pointInsideItem(item, x, y) {
        if (!item || !item.visible)
            return false
        const p = item.mapToItem(root, 0, 0)
        return x >= p.x && x <= p.x + item.width && y >= p.y && y <= p.y + item.height
    }

    function dismissTextFocus() {
        if (locationField.activeFocus || searchInput.activeFocus || inlineRenameUrl.length > 0)
            root.forceActiveFocus()
    }

    function openTextEditContext(editor, item, x, y) {
        if (!editor || !item)
            return
        closeContextMenus()
        textEditContextMenu.close()
        textEditTarget = editor
        const p = item.mapToItem(root, x, y)
        textEditContextMenu.x = Math.max(8, Math.min(root.width - textEditContextMenu.implicitWidth - 8, p.x))
        textEditContextMenu.y = Math.max(8, Math.min(root.height - textEditContextMenu.implicitHeight - 8, p.y))
        textEditContextMenu.open()
    }


    function reloadMediaGallery() {
        if (!root.mediaGalleryCategory) {
            root.mediaGalleryFiles = []
            root.mediaGalleryBytes = 0
            root.mediaGalleryHomeBytes = 0
            return
        }

        // The directory model is the authoritative snapshot for category:/... .
        // Rebuild from it every time the category load finishes so leaving and
        // re-entering Images/Videos cannot leave the separate gallery cache empty.
        const items = []
        let totalBytes = 0
        for (let row = 0; row < directory.count; ++row) {
            const entry = directory.itemAt(row)
            const localPath = String(entry.localPath || "")
            if (!localPath.length || entry.isDir)
                continue
            let modifiedMs = 0
            if (entry.modified) {
                const d = new Date(entry.modified)
                if (!isNaN(d.getTime()))
                    modifiedMs = d.getTime()
            }
            const itemSize = Number(entry.size || 0)
            totalBytes += Math.max(0, itemSize)
            items.push({
                path: localPath,
                url: String(entry.itemUrl || ""),
                name: String(entry.name || ""),
                parentPath: localPath.lastIndexOf("/") > 0
                            ? localPath.substring(0, localPath.lastIndexOf("/")) : "/",
                suffix: String(entry.suffix || "").toLowerCase(),
                size: itemSize,
                modifiedMs: modifiedMs,
                modelIndex: row
            })
        }
        root.mediaGalleryFiles = items
        root.mediaGalleryBytes = totalBytes
        root.mediaGalleryHomeBytes = Number(directory.storageTotalBytes || 0)
    }

    function scheduleMediaGalleryReload() {
        if (root.mediaGalleryCategory)
            mediaGalleryReloadTimer.restart()
    }

    Timer {
        id: mediaGalleryReloadTimer
        interval: 0
        repeat: false
        // Location changes set loading synchronously. Waiting for the finished
        // snapshot prevents briefly rebuilding the gallery from the category
        // that is being left.
        onTriggered: {
            if (root.mediaGalleryCategory && !directory.loading)
                root.reloadMediaGallery()
        }
    }

    function galleryFilesForView() {
        const source = root.mediaGalleryFiles || []
        if (!root.searchVisible || !searchInput.text.trim().length)
            return source
        const query = searchInput.text.trim().toLowerCase()
        const filtered = []
        for (let i = 0; i < source.length; ++i) {
            const item = source[i]
            if (String(item.name || "").toLowerCase().indexOf(query) >= 0)
                filtered.push(item)
        }
        return filtered
    }

    function categoryGroupedFilesForView(ignoreSearch) {
        if (!root.groupedCategory || !root.visible || directory.loading)
            return []
        const result = []
        for (let row = 0; row < directory.count; ++row) {
            const entry = directory.itemAt(row)
            if (!entry || !entry.itemUrl)
                continue
            if (!ignoreSearch && root.searchVisible && searchInput.text.trim().length) {
                const query = searchInput.text.trim().toLowerCase()
                if (String(entry.name || "").toLowerCase().indexOf(query) < 0)
                    continue
            }
            let modifiedMs = 0
            let createdMs = 0
            if (entry.modified) {
                const md = new Date(entry.modified)
                if (!isNaN(md.getTime())) modifiedMs = md.getTime()
            }
            if (entry.created) {
                const cd = new Date(entry.created)
                if (!isNaN(cd.getTime())) createdMs = cd.getTime()
            }
            if (!createdMs) createdMs = modifiedMs
            const iconName = root.iconForItem(!!entry.isDir, String(entry.suffix || ""),
                                              String(entry.systemIconName || ""), String(entry.name || ""))
            result.push({
                modelIndex: row,
                name: String(entry.name || ""),
                itemUrl: String(entry.itemUrl || ""),
                localPath: String(entry.localPath || ""),
                isDir: !!entry.isDir,
                size: Number(entry.size || 0),
                suffix: String(entry.suffix || ""),
                mimeType: String(entry.mimeType || ""),
                systemIconName: String(entry.systemIconName || ""),
                modifiedValue: entry.modified,
                modifiedMs: modifiedMs,
                createdMs: createdMs,
                groupDateMs: directory.sortMode === "created" ? createdMs : modifiedMs,
                iconSource: iconName,
                thumbnailSource: !entry.isDir ? root.thumbSource(String(entry.localPath || ""), String(entry.suffix || ""), entry.modified, entry.size, entry.previewRevision) : "",
                metaText: entry.isDir
                          ? (lang.language === "tr" ? "Klasör" : "Folder")
                          : directory.formatBytes(Number(entry.size || 0))
            })
        }
        return result
    }

    function scheduleGroupedCategoryReload() {
        if (root.groupedCategory && root.visible) groupedCategoryReloadTimer.restart()
        else root.groupedCategoryFiles = []
    }
    onCategoryViewRevisionChanged: root.scheduleGroupedCategoryReload()
    onGroupedCategoryChanged: root.scheduleGroupedCategoryReload()
    onVisibleChanged: root.scheduleGroupedCategoryReload()
    Timer {
        id: groupedCategoryReloadTimer
        interval: 0
        onTriggered: {
            if (directory.loading)
                return
            root.groupedCategoryFiles = root.categoryGroupedFilesForView(false)
            if (root.categoryKey === "music" && root.musicPlayer && root.musicPlayer.libraryManager)
                root.musicPlayer.libraryManager.setItems(root.categoryGroupedFilesForView(true))
        }
    }

    function galleryFormatBytes(bytes) {
        const n = Number(bytes || 0)
        if (n < 1024) return n + " B"
        if (n < 1024 * 1024) return (n / 1024).toFixed(1) + " KiB"
        if (n < 1024 * 1024 * 1024) return (n / (1024 * 1024)).toFixed(1) + " MiB"
        if (n < 1024 * 1024 * 1024 * 1024) return (n / (1024 * 1024 * 1024)).toFixed(1) + " GiB"
        return (n / (1024 * 1024 * 1024 * 1024)).toFixed(2) + " TiB"
    }

    onCategoryKeyChanged: {
        root.dlnaSourceForwardAvailable = false
        if (root.categoryKey !== "videos")
            root.dlnaViewActive = false
        if (root.musicPlayer && root.musicPlayer.opened) {
            if (root.categoryKey === "music")
                root.musicPlayer.restorePanel()
            else if (root.musicPlayer.backgroundPlayback)
                root.musicPlayer.minimize()
            else
                root.musicPlayer.closePlayer()
        }
        root.groupedCategoryFiles = []
        root.scheduleGroupedCategoryReload()
        if (!root.mediaGalleryCategory)
            root.reloadMediaGallery()
        else
            root.scheduleMediaGalleryReload()
    }
    onContentIndexModelChanged: root.scheduleMediaGalleryReload()

    Connections {
        target: directory
        function onCountChanged() {
            root.categoryViewRevision += 1
            root.scheduleMediaGalleryReload()
        }
        function onDataChanged() { root.categoryViewRevision += 1 }
        function onModelReset() { root.categoryViewRevision += 1 }
        function onSortModeChanged() {
            root.categoryViewRevision += 1
            if (root.mediaGalleryCategory) root.scheduleMediaGalleryReload()
        }
        function onSortAscendingChanged() {
            root.categoryViewRevision += 1
            if (root.mediaGalleryCategory) root.scheduleMediaGalleryReload()
        }
        function onLoadingChanged() {
            if (!directory.loading) root.categoryViewRevision += 1
            if (root.mediaGalleryCategory && !directory.loading)
                root.scheduleMediaGalleryReload()
        }
        function onRefreshCompleted() {
            root.categoryViewRevision += 1
            root.scheduleMediaGalleryReload()
        }
    }

    function recentLocationDisplay(location) {
        const value = String(location || "")
        if (value.indexOf("file://") === 0) {
            try {
                return decodeURIComponent(value.replace(/^file:\/\//, ""))
            } catch (error) {
                return value.replace(/^file:\/\//, "")
            }
        }
        return value
    }

    function openRecentLocationsMenu() {
        root.dismissLocationCompletion()
        if (recentLocationsMenu.interactionOpen) {
            recentLocationsMenu.requestClose()
            return
        }
        if (!root.quickAccessModel)
            return
        root.quickAccessModel.pruneRecentLocations()
        Qt.callLater(function() {
            const point = recentLocationsButton.parent.mapToItem(root,
                recentLocationsButton.x, recentLocationsButton.y + recentLocationsButton.height + 6)
            recentLocationsMenu.x = Math.max(8, Math.min(root.width - recentLocationsMenu.implicitWidth - 8,
                                    point.x + recentLocationsButton.width - recentLocationsMenu.implicitWidth))
            recentLocationsMenu.y = point.y
            recentLocationsMenu.requestOpen()
        })
    }

    function buildBreadcrumbItems(displayLocation) {
        if (root.isCategoryLocation) {
            const key = directory.location.substring("category:/".length)
            const title = root.contentIndexModel
                          ? root.contentIndexModel.titleForCategory(key, lang.language) : key
            return [{ "label": lang.language === "tr" ? "Kategoriler" : "Categories", "location": "" },
                    { "label": title, "location": directory.location }]
        }
        const raw = String(displayLocation || "")
        if (!raw.length)
            return []

        const items = []
        if (raw.startsWith("/")) {
            items.push({ "label": "/", "location": "/" })
            const parts = raw.split("/").filter(function(part) { return part.length > 0 })
            let current = ""
            for (let i = 0; i < parts.length; ++i) {
                current += "/" + parts[i]
                items.push({ "label": parts[i], "location": current + "/" })
            }
            return items
        }

        const schemeIndex = raw.indexOf("://")
        if (schemeIndex >= 0) {
            const prefix = raw.substring(0, schemeIndex + 3)
            const rest = raw.substring(schemeIndex + 3)
            const parts = rest.split("/").filter(function(part) { return part.length > 0 })
            if (parts.length === 0)
                return [{ "label": raw, "location": directory.location }]
            let current = prefix + parts[0]
            items.push({ "label": current, "location": current })
            for (let i = 1; i < parts.length; ++i) {
                current += "/" + parts[i]
                items.push({ "label": parts[i], "location": current + "/" })
            }
            return items
        }

        return [{ "label": raw, "location": directory.location }]
    }

    function editableLocationText(value) {
        let text = String(value || "")
        // Native local directory paths are most useful with the trailing slash:
        // completion immediately starts in that directory instead of requiring
        // a preliminary Tab press. Keep virtual/scheme locations untouched.
        if (text.length > 1 && text.startsWith("/") && !text.endsWith("/"))
            text += "/"
        return text
    }

    function enterLocationEdit(selectEverything) {
        locationEditMode = true
        locationField.text = root.editableLocationText(directory.displayLocation)
        Qt.callLater(function() {
            locationField.forceActiveFocus()
            if (selectEverything)
                locationField.selectAll()
            else
                locationField.cursorPosition = locationField.text.length
        })
    }

    function resetLocationCompletionCycle() {
        locationCompletionExplicit = false
        locationCompletionCycling = false
        locationCompletionCycleItems = []
        locationCompletionBaseText = ""
    }

    function leaveLocationEdit() {
        locationCompletions = []
        locationCompletionIndex = 0
        resetLocationCompletionCycle()
        locationEditMode = false
    }

    function updateLocationCompletions() {
        if (!locationField.activeFocus) {
            locationCompletions = []
            locationCompletionIndex = 0
            resetLocationCompletionCycle()
            return
        }
        const matches = directory.pathCompletions(locationField.text)
        locationCompletions = matches
        if (!matches || matches.length === 0) {
            locationCompletionIndex = 0
            return
        }
        locationCompletionIndex = Math.max(0, Math.min(locationCompletionIndex, matches.length - 1))
    }

    function beginLocationCompletionCycle(backwards) {
        if (!locationCompletions || locationCompletions.length === 0)
            return false
        locationCompletionCycling = true
        locationCompletionExplicit = true
        locationCompletionBaseText = locationField.text
        locationCompletionCycleItems = locationCompletions.slice ? locationCompletions.slice(0) : locationCompletions
        locationCompletionIndex = backwards ? locationCompletionCycleItems.length - 1 : 0
        return applyLocationCompletionCycleItem()
    }

    function applyLocationCompletionCycleItem() {
        if (!locationCompletionCycleItems || locationCompletionCycleItems.length === 0)
            return false
        const count = locationCompletionCycleItems.length
        locationCompletionIndex = ((locationCompletionIndex % count) + count) % count
        const candidate = locationCompletionCycleItems[locationCompletionIndex]
        if (!candidate || !candidate.value)
            return false
        // Tab/arrow navigation is a real selection: mirror it into the address
        // field.  Do NOT recompute completions here, otherwise the next Tab
        // would enter the selected directory instead of cycling its siblings.
        locationField.text = candidate.value
        locationField.cursorPosition = locationField.text.length
        locationField.forceActiveFocus()
        locationCompletions = locationCompletionCycleItems
        locationCompletionExplicit = true
        return true
    }

    function cycleLocationCompletion(step) {
        if (!locationCompletionCycling)
            return beginLocationCompletionCycle(step < 0)
        if (!locationCompletionCycleItems || locationCompletionCycleItems.length === 0)
            return false
        locationCompletionIndex += step
        return applyLocationCompletionCycleItem()
    }

    function acceptLocationCompletion() {
        if (!locationCompletions || locationCompletions.length === 0)
            return false
        const index = Math.max(0, Math.min(locationCompletionIndex, locationCompletions.length - 1))
        const candidate = locationCompletions[index]
        if (!candidate || !candidate.value)
            return false
        locationField.text = candidate.value
        locationField.cursorPosition = locationField.text.length
        locationField.forceActiveFocus()
        locationCompletionExplicit = true
        return true
    }

    NumberAnimation {
        id: wheelScrollAnimation
        property: "contentY"
        easing.type: Easing.OutCubic
    }

    function normalizedWheelDelta(event, view, rowExtent) {
        const angleY = event.angleDelta.y
        const pixelY = event.pixelDelta.y
        const mousePixelOnly = angleY === 0 && pixelY !== 0
                               && (event.phase === Qt.NoScrollPhase
                                   || (event.device && event.device.type === PointerDevice.Mouse))

        if (angleY !== 0 || mousePixelOnly) {
            const now = Date.now()
            const direction = (angleY !== 0 ? angleY : pixelY) > 0 ? 1 : -1
            const gap = wheelBurstLastMs > 0 ? now - wheelBurstLastMs : 9999

            if (direction !== wheelBurstDirection || gap > 300)
                wheelBurstCount = 0
            else
                wheelBurstCount = Math.min(5, wheelBurstCount + 1)

            wheelBurstLastMs = now
            wheelBurstDirection = direction

            const perNotch = AppTheme.wheelScrollStep
            const rawFraction = angleY !== 0 ? Math.abs(angleY) / 120.0
                                             : Math.abs(pixelY) / 30.0
            const stepFraction = Math.max(0.125, Math.min(2.0, rawFraction))
            const acceleration = 1.0 + wheelBurstCount * 0.08
            return direction * perNotch * stepFraction * acceleration
        }

        if (pixelY !== 0) {
            wheelBurstCount = 0
            wheelBurstDirection = 0
            wheelBurstLastMs = 0
            return pixelY * 2.0
        }

        return 0
    }

    function scrollViewFromWheel(view, event, rowExtent, activityTimer) {
        const delta = normalizedWheelDelta(event, view, rowExtent)
        if (delta === 0)
            return
        const top = view.originY
        const bottom = top + Math.max(0, view.contentHeight - view.height)
        const continuing = wheelScrollAnimation.running && wheelScrollAnimation.target === view
        const previousTarget = continuing ? wheelScrollAnimation.to : view.contentY
        const sameDirection = !continuing || Math.sign(previousTarget - view.contentY) === -Math.sign(delta)
        const base = sameDirection ? previousTarget : view.contentY
        const limit = Math.max(AppTheme.wheelScrollStep, view.height * 1.8, rowExtent * 6)
        const destination = Math.max(top, Math.min(bottom,
                                Math.max(view.contentY - limit,
                                         Math.min(view.contentY + limit, base - delta))))
        wheelScrollAnimation.stop()
        if (Math.abs(destination - view.contentY) < 0.5)
            return
        wheelScrollAnimation.target = view
        wheelScrollAnimation.from = view.contentY
        wheelScrollAnimation.to = destination
        wheelScrollAnimation.duration = Math.min(400, Math.max(220,
                                        160 + Math.abs(destination - view.contentY) / 4))
        wheelScrollAnimation.start()
        if (activityTimer)
            activityTimer.restart()
    }

    function formatTransferBytes(value) {
        const bytes = Math.max(0, Number(value || 0))
        if (bytes < 1024) return Math.round(bytes) + " B"
        if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + " KiB"
        if (bytes < 1024 * 1024 * 1024) return (bytes / (1024 * 1024)).toFixed(1) + " MiB"
        return (bytes / (1024 * 1024 * 1024)).toFixed(2) + " GiB"
    }

    function formatRemaining(seconds) {
        const value = Math.max(0, Math.ceil(Number(seconds || 0)))
        if (value < 60)
            return value + " " + (lang.language === "tr" ? "sn" : "sec")
        if (value < 3600)
            return Math.floor(value / 60) + " " + (lang.language === "tr" ? "dk" : "min")
                   + " " + (value % 60) + " " + (lang.language === "tr" ? "sn" : "sec")
        return Math.floor(value / 3600) + " " + (lang.language === "tr" ? "sa" : "hr")
               + " " + Math.floor((value % 3600) / 60) + " " + (lang.language === "tr" ? "dk" : "min")
    }

    function transferPercent(operation) {
        const reported = Number(operation.percent === undefined ? -1 : operation.percent)
        if (reported >= 0)
            return Math.max(0, Math.min(100, reported))
        const total = Number(operation.totalBytes || 0)
        return total > 0 ? Math.max(0, Math.min(100, Number(operation.processedBytes || 0) * 100 / total)) : -1
    }

    function transferMetrics(operation) {
        const parts = []
        const processedItems = Number(operation.processedItems || 0)
        const totalItems = Number(operation.totalItems || 0)
        if (totalItems > 0 && processedItems <= totalItems)
            parts.push(processedItems + " / " + totalItems + " " + (lang.language === "tr" ? "öğe" : "items"))
        else if (processedItems > 0)
            parts.push(processedItems + " " + (lang.language === "tr" ? "öğe işlendi" : "items processed"))

        const processedBytes = Number(operation.processedBytes || 0)
        const totalBytes = Number(operation.totalBytes || 0)
        if (totalBytes > 0)
            parts.push(formatTransferBytes(processedBytes) + " / " + formatTransferBytes(totalBytes))
        else if (processedBytes > 0)
            parts.push(formatTransferBytes(processedBytes))
        if (parts.length)
            return parts.join("  ·  ")
        if (operation.kind === "extract")
            return lang.language === "tr" ? "Arşiv içeriği çıkarılıyor…" : "Extracting archive contents…"
        if (operation.kind === "clipboard")
            return lang.language === "tr" ? "Dosya listesi panoya hazırlanıyor…" : "Preparing file list for clipboard…"
        return lang.language === "tr" ? "İşlem hazırlanıyor…" : "Preparing operation…"
    }

    function transferRemaining(operation) {
        if (operation.paused)
            return lang.language === "tr" ? "Duraklatıldı" : "Paused"
        if (operation.kind === "clipboard")
            return lang.language === "tr" ? "Dosya listesi hazırlanıyor" : "Preparing file list"
        const speed = Number(operation.speed || 0)
        const remainingBytes = Number(operation.totalBytes || 0) - Number(operation.processedBytes || 0)
        if (speed <= 0 || remainingBytes <= 0)
            return lang.language === "tr" ? "Kalan süre hesaplanıyor" : "Calculating remaining time"
        return (lang.language === "tr" ? "Kalan " : "Remaining ") + formatRemaining(remainingBytes / speed)
    }

    function isPreviewable(suffix) {
        const s = suffix.toLowerCase()
        return ["png", "jpg", "jpeg", "webp", "bmp", "gif", "svg",
                "mp4", "mkv", "avi", "mov", "webm", "m4v", "pdf", "mp3",
                "appimage",
                "srt", "lrc", "lrclib", "vtt", "ass", "ssa", "txt", "md", "log", "nfo",
                "json", "xml", "yaml", "yml", "toml", "ini", "conf", "cfg", "desktop",
                "service", "csv", "tsv", "m3u", "m3u8", "pls"].indexOf(s) >= 0
    }

    function folderItemCountText(count) {
        if (count < 0)
            return lang.t("folder")
        if (lang.language === "tr")
            return count + " öğe"
        return count + (count === 1 ? " item" : " items")
    }

    function folderPreviewSource(localPath) {
        if (!localPath)
            return ""
        return "image://gfilethumb/" + encodeURIComponent(localPath) + "|folder-" + thumbnailRevision
    }

    function isArchive(name, mimeType) {
        const lowerName = (name || "").toLowerCase()
        const archiveSuffixes = [
            ".zip", ".rar", ".7z", ".tar", ".tar.gz", ".tgz", ".tar.bz2",
            ".tbz2", ".tar.xz", ".txz", ".tar.zst", ".gz", ".bz2", ".xz", ".zst"
        ]
        for (let i = 0; i < archiveSuffixes.length; ++i) {
            if (lowerName.endsWith(archiveSuffixes[i]))
                return true
        }
        const mime = (mimeType || "").toLowerCase()
        return mime.indexOf("zip") >= 0 || mime.indexOf("rar") >= 0
                || mime.indexOf("7z") >= 0 || mime.indexOf("tar") >= 0
                || mime.indexOf("archive") >= 0 || mime.indexOf("compressed") >= 0
    }

    function isAppImage(name, mimeType) {
        return (name || "").toLowerCase().endsWith(".appimage")
                || (mimeType || "").toLowerCase().indexOf("appimage") >= 0
    }

    function iconForItem(isDir, suffix, systemIconName, itemName) {
        // Bundled/default icon mode must always return to Lurviko's own folder
        // artwork, even if the directory has a custom .directory icon.
        if (AppTheme.useSystemIcons)
            return AppTheme.systemIcon(isDir
                                       ? AppTheme.folderSystemIcon(itemName, systemIconName)
                                       : (systemIconName || "text-x-generic"))
        if (isDir) return AppTheme.icon("folder.svg")
        const s = suffix.toLowerCase()
        if (["png", "jpg", "jpeg", "webp", "bmp", "gif", "svg"].indexOf(s) >= 0) return AppTheme.icon("image.svg")
        if (["mp4", "mkv", "avi", "mov", "webm", "m4v", "ts", "mpeg", "mpg"].indexOf(s) >= 0) return AppTheme.icon("video.svg")
        if (["mp3", "flac", "wav", "m4a", "ogg"].indexOf(s) >= 0) return AppTheme.icon("audio.svg")
        if (["zip", "rar", "7z", "tar", "gz", "xz"].indexOf(s) >= 0) return AppTheme.icon("archive.svg")
        if (s === "pdf") return AppTheme.icon("pdf.svg")
        return AppTheme.icon("file.svg")
    }

    function linkBadgeText(type) {
        if (type === "symlink") return "SYMLINK"
        if (type === "hardlink") return "HARDLINK"
        if (type === "shortcut") return lang.language === "tr" ? "KISAYOL" : "SHORTCUT"
        return ""
    }

    function linkTypeText(type) {
        if (type === "symlink") return lang.language === "tr" ? "Sembolik bağlantı (symlink)" : "Symbolic link (symlink)"
        if (type === "hardlink") return lang.language === "tr" ? "Sabit bağlantı (hardlink)" : "Hard link"
        if (type === "shortcut") return lang.language === "tr" ? "Kısayol (.desktop Link)" : "Shortcut (.desktop Link)"
        return ""
    }

    function thumbSource(localPath, suffix, modified, size, previewRevision) {
        if (!localPath || !isPreviewable(suffix)) return ""
        let stamp = thumbnailRevision
        if (previewRevision)
            return "image://gfilethumb/" + encodeURIComponent(localPath) + "|" + stamp + "-" + previewRevision
        if (modified) {
            if (modified.getTime)
                stamp += "-" + modified.getTime()
            else
                stamp += "-" + String(modified)
        }
        stamp += "-" + String(size || 0)
        // The revision makes the QML Image source change after an in-place
        // overwrite/replacement. ThumbnailProvider strips it before opening the
        // real file path.
        return "image://gfilethumb/" + encodeURIComponent(localPath) + "|" + stamp
    }

    function isCloudUrl(url) {
        return url && (url.startsWith("gdrive://") || url.startsWith("onedrive://"))
    }

    function administratorUrlForSelection() {
        if (root.selectedCount !== 1 || !root.selectedIsDir || root.selectedLocalPath.length === 0)
            return ""
        if (root.selectedUrl.startsWith("admin:"))
            return root.selectedUrl
        var path = root.selectedLocalPath
        if (!path.startsWith("/"))
            path = "/" + path
        // admin:// + an absolute POSIX path intentionally yields admin:///...
        // which is the canonical kio-admin form. KIO/Polkit will reuse an
        // existing authorization grant or prompt when one is required.
        return "admin://" + path
    }


    function administratorUrlForCurrentLocation() {
        if (root.isCloudUrl(directory.location) || directory.remote || directory.location.startsWith("admin:"))
            return ""
        var path = directory.displayLocation
        if (!path || !path.startsWith("/"))
            return ""
        return "admin://" + path
    }

    function cloudManagerFor(url) {
        if (url && url.startsWith("onedrive://"))
            return root.oneDrive
        return root.googleDrive
    }

    function currentCloudManager() {
        return directory.location.startsWith("onedrive://") ? root.oneDrive : root.googleDrive
    }

    function openLocationInput(input) {
        const info = fileOps.inspectPath(input)
        if (info && info.exists && !info.isDir) {
            root.locationCompletions = []
            openWithModel.openDefault(info.url, info.mimeType || "")
            // The address bar represents the current directory, not the file
            // that was just launched. Restore it after accepting a file path.
            locationField.text = directory.displayLocation
            root.forceActiveFocus()
            return true
        }
        root.navigateTo(input)
        return false
    }

    function selectedModelIndex() {
        if (!selectedUrl || !selectedUrl.length)
            return -1
        for (let i = 0; i < directory.count; ++i) {
            const item = directory.itemAt(i)
            if (item && item.itemUrl === selectedUrl)
                return i
        }
        return -1
    }

    function findTypeAheadIndex(prefix, startIndex) {
        if (!prefix || !prefix.length || directory.count <= 0)
            return -1
        const folded = prefix.toLowerCase()
        const start = Math.max(0, Number(startIndex || 0)) % directory.count
        for (let step = 0; step < directory.count; ++step) {
            const i = (start + step) % directory.count
            const item = directory.itemAt(i)
            if (item && item.name && item.name.toLowerCase().startsWith(folded))
                return i
        }
        return -1
    }

    function typeAheadSelect(text) {
        if (!text || !text.length || text === " ")
            return false
        const typed = text.toLowerCase()
        const previous = String(typeAheadBuffer || "")
        // Repeating the same single key cycles through every item beginning
        // with that key (F, F, F...), like pressing Tab through matches.
        if (typeAheadReset.running && previous.length === 1
                && previous.toLowerCase() === typed) {
            const current = selectedModelIndex()
            const index = findTypeAheadIndex(text, current >= 0 ? current + 1 : 0)
            typeAheadBuffer = text
            typeAheadReset.restart()
            if (index < 0)
                return false
            selectIndex(index, Qt.NoModifier)
            focusResultIndex(index)
            return true
        }

        let candidate = typeAheadReset.running ? previous + text : text
        let index = findTypeAheadIndex(candidate, 0)
        // If the accumulated prefix no longer matches, immediately start a new
        // prefix with the latest character instead of making the user wait for
        // the timeout.
        if (index < 0 && candidate.length > text.length) {
            candidate = text
            index = findTypeAheadIndex(candidate, 0)
        }
        typeAheadBuffer = candidate
        typeAheadReset.restart()
        if (index < 0)
            return false
        selectIndex(index, Qt.NoModifier)
        focusResultIndex(index)
        return true
    }

    function setActiveIconSize(value) {
        if (root.categoryKey === "music") {
            const nextSize = Math.max(104, Math.min(360, Math.round(value)))
            if (nextSize !== root.musicIconSize) {
                musicLibraryView.prepareForResize()
                AppTheme.musicIconSize = nextSize
            }
            return
        }
        if (root.isCategoryLocation) {
            const nextSize = Math.max(104, Math.min(360, Math.round(value)))
            if (nextSize === root.categoryIconSize)
                return
            if (root.mediaGalleryCategory && mediaGalleryLoader.item)
                mediaGalleryLoader.item.prepareForResize()
            else if (root.groupedCategory)
                groupedCategoryView.prepareForResize()
            AppTheme.categoryIconSize = nextSize
            return
        }
        if (AppTheme.useSystemIcons) {
            root.iconSize = Math.max(directorySystemIconMinimum(),
                                     Math.min(directorySystemIconMaximum(), Math.round(value)))
            return
        }
        root.iconSize = Math.max(104, Math.min(360, Math.round(value)))
    }

    function adjustIconSize(delta) {
        root.setActiveIconSize(root.activeIconSize + delta)
    }

    function openTerminalHere() {
        fileOps.openTerminal(root.isCategoryLocation
                             ? StandardPaths.writableLocation(StandardPaths.HomeLocation)
                             : directory.location)
    }

    function isSelected(url) {
        // Touch the revision so delegates re-evaluate after selection changes.
        const revision = selectionRevision
        if (rubberSelecting)
            return rubberPreviewSelection.indexOf(url) >= 0
        return selectedLookup[url] === true
    }

    function tabScrollOffset() {
        if (directory.searchActive || root.isCategoryLocation)
            return -1
        const view = gridMode ? grid : listView
        return Math.max(0, view.contentY - view.originY)
    }

    function prepareTabScrollRestore(location, offset) {
        const numeric = Number(offset)
        pendingTabScrollLocation = String(location || "")
        pendingTabScrollOffset = Number.isFinite(numeric) && numeric >= 0 ? numeric : 0
        pendingTabScrollAttempts = 0
        pendingNavigationSelection = null
        pendingNavigationPrimaryUrl = ""
        tabScrollSnapshotTimer.stop()
        tabScrollRestoreRetry.stop()
        // A tab switch is not navigation inside the tab; do not let the normal
        // navigation-to-top path win the race while its model is loading.
        pendingNavigationTopLocation = ""
    }

    function navigationViewState() {
        return { "viewScrollY": directory.loading || pendingTabScrollOffset >= 0 ? -1 : tabScrollOffset(), "selectedUrls": selectedUrls.slice(),
                 "selectedUrl": selectedUrl }
    }

    function prepareNavigationViewRestore(location, state) {
        prepareTabScrollRestore(location, state ? state.viewScrollY : 0)
        pendingNavigationSelection = state && state.selectedUrls ? state.selectedUrls.slice() : []
        pendingNavigationPrimaryUrl = state ? String(state.selectedUrl || "") : ""
    }

    function restorePendingTabScroll() {
        if (pendingTabScrollOffset < 0 || directory.loading)
            return
        if (pendingTabScrollLocation.length && !root.locationsEquivalent(pendingTabScrollLocation, directory.location))
            return
        const offset = pendingTabScrollOffset
        const location = pendingTabScrollLocation
        pendingNavigationTopLocation = ""
        Qt.callLater(function() {
            if (directory.loading || directory.searchActive || root.isCategoryLocation
                    || root.pendingTabScrollLocation !== location || !root.locationsEquivalent(directory.location, location)
                    || root.pendingTabScrollOffset !== offset)
                return
            wheelScrollAnimation.stop()
            const view = root.gridMode ? grid : listView
            view.cancelFlick()
            view.forceLayout()
            const bottom = Math.max(0, view.contentHeight - view.height)
            view.contentY = view.originY + Math.min(bottom, offset)

            // Large directories can report a temporary contentHeight while the
            // Grid/List view is still laying out recycled delegates. Do not
            // consume the saved tab offset at that transient zero/short height.
            if (bottom + 2 < offset && root.pendingTabScrollAttempts < 10) {
                ++root.pendingTabScrollAttempts
                tabScrollRestoreRetry.restart()
                return
            }

            root.pendingTabScrollOffset = -1
            root.pendingTabScrollLocation = ""
            root.pendingTabScrollAttempts = 0
            if (root.pendingNavigationSelection !== null) {
                const urls = root.pendingNavigationSelection.filter(function(url) { return directory.indexOfUrl(url) >= 0 })
                const primary = directory.indexOfUrl(root.pendingNavigationPrimaryUrl)
                root.setSelectedUrls(urls, primary, primary)
                root.pendingNavigationSelection = null
                root.pendingNavigationPrimaryUrl = ""
            }
        })
    }

    Timer {
        id: tabScrollRestoreRetry
        interval: 55
        repeat: false
        onTriggered: root.restorePendingTabScroll()
    }

    function rememberScrollPosition() {
        // A second refresh can arrive while the first model reset is waiting to
        // restore the view.  Never replace the real saved position with the
        // temporary origin (usually 0) from that reset.
        if (scrollRestorePending || pendingTabScrollOffset >= 0)
            return
        scrollRestoreGridMode = gridMode
        scrollRestoreY = gridMode ? grid.contentY : listView.contentY
        scrollRestorePending = true
    }

    function restoreScrollPosition() {
        if (!scrollRestorePending)
            return
        wheelScrollAnimation.stop()
        const view = scrollRestoreGridMode ? grid : listView
        const bottom = view.originY + Math.max(0, view.contentHeight - view.height)
        view.contentY = Math.max(view.originY, Math.min(bottom, scrollRestoreY))
        scrollRestorePending = false
    }

    function refreshPreservingScroll() {
        rememberScrollPosition()
        directory.refresh()
        Qt.callLater(root.restoreScrollPosition)
    }

    function resetNavigationViewToTop() {
        const location = directory.location
        if (!pendingNavigationTopLocation.length || pendingNavigationTopLocation !== location || directory.loading)
            return
        pendingNavigationTopLocation = ""
        wheelScrollAnimation.stop()
        const view = gridMode ? grid : listView
        view.contentY = view.originY
        clearSelection()
        focusDirectoryView()
    }

    function setPrimaryFromItem(item, index) {
        if (!item || !item.itemUrl) {
            selectedUrl = ""
            selectedLocalPath = ""
            selectedName = ""
            selectedIsDir = false
            selectedSize = 0
            selectedModified = null
            selectedMimeType = ""
            selectedSuffix = ""
            selectedSystemIconName = ""
            selectedLinkType = ""
            selectedLinkTarget = ""
            primaryIndex = -1
            serviceMenuModel.clear()
            return
        }
        selectedUrl = item.itemUrl
        selectedLocalPath = item.localPath || ""
        selectedName = item.name || ""
        selectedIsDir = !!item.isDir
        selectedSize = item.size || 0
        selectedModified = item.modified || null
        selectedMimeType = item.mimeType || ""
        selectedSuffix = item.suffix || ""
        selectedSystemIconName = item.systemIconName || ""
        selectedLinkType = item.linkType || ""
        selectedLinkTarget = item.linkTarget || ""
        primaryIndex = index
        root.updateServiceMenuContext()
    }

    function buildSelectionLookup(urls) {
        const lookup = ({})
        for (let i = 0; i < urls.length; ++i)
            lookup[urls[i]] = true
        return lookup
    }

    function setSelectedUrls(urls, primary, anchor) {
        const nextUrls = urls.slice()
        selectedLookup = buildSelectionLookup(nextUrls)
        selectedUrls = nextUrls
        selectionRevision++
        selectionAnchorIndex = anchor === undefined ? selectionAnchorIndex : anchor
        if (selectedUrls.length === 0) {
            setPrimaryFromItem(null, -1)
            return
        }
        let nextIndex = primary
        if (nextIndex === undefined || nextIndex < 0)
            nextIndex = directory.indexOfUrl(selectedUrls[selectedUrls.length - 1])
        const item = directory.itemAt(nextIndex)
        if (item && item.itemUrl && selectedLookup[item.itemUrl] === true)
            setPrimaryFromItem(item, nextIndex)
        else {
            const fallbackIndex = directory.indexOfUrl(selectedUrls[selectedUrls.length - 1])
            setPrimaryFromItem(directory.itemAt(fallbackIndex), fallbackIndex)
        }
    }

    function addUrlUnique(list, url) {
        if (url && list.indexOf(url) < 0)
            list.push(url)
    }

    function selectIndex(index, modifiers) {
        dismissTextFocus()
        const item = directory.itemAt(index)
        if (!item || !item.itemUrl)
            return
        const ctrl = (modifiers & Qt.ControlModifier) !== 0
        const shift = (modifiers & Qt.ShiftModifier) !== 0

        if (shift) {
            const anchor = selectionAnchorIndex >= 0 ? selectionAnchorIndex
                                                    : (primaryIndex >= 0 ? primaryIndex : index)
            let next = ctrl ? selectedUrls.slice() : []
            const first = Math.min(anchor, index)
            const last = Math.max(anchor, index)
            for (let i = first; i <= last; ++i) {
                const row = directory.itemAt(i)
                if (row && row.itemUrl)
                    addUrlUnique(next, row.itemUrl)
            }
            setSelectedUrls(next, index, anchor)
            revealIndex(index)
            return
        }

        if (ctrl) {
            let next = selectedUrls.slice()
            const pos = next.indexOf(item.itemUrl)
            if (pos >= 0)
                next.splice(pos, 1)
            else
                next.push(item.itemUrl)
            setSelectedUrls(next, pos >= 0 ? -1 : index, index)
            revealIndex(index)
            return
        }

        setSelectedUrls([item.itemUrl], index, index)
        revealIndex(index)
    }

    function reconcileSelection() {
        if (!selectedUrls.length)
            return
        let next = []
        for (let i = 0; i < selectedUrls.length; ++i) {
            if (directory.indexOfUrl(selectedUrls[i]) >= 0)
                next.push(selectedUrls[i])
        }
        if (next.length !== selectedUrls.length) {
            const primary = selectedUrl.length ? directory.indexOfUrl(selectedUrl) : -1
            setSelectedUrls(next, primary, primary >= 0 ? primary : -1)
        } else if (selectedUrl.length) {
            primaryIndex = directory.indexOfUrl(selectedUrl)
        }
    }

    function selectAll() {
        const next = directory.allItemUrls()
        setSelectedUrls(next, next.length ? 0 : -1, next.length ? 0 : -1)
    }

    function togglePrimarySelection() {
        if (primaryIndex < 0) {
            if (directory.count > 0)
                selectIndex(0, Qt.NoModifier)
            return
        }
        selectIndex(primaryIndex, Qt.ControlModifier)
    }

    function clearSelection() {
        selectedLookup = ({})
        selectedUrls = []
        selectionRevision++
        selectionAnchorIndex = -1
        primaryIndex = -1
        selectedUrl = ""
        selectedLocalPath = ""
        selectedName = ""
        selectedIsDir = false
        selectedSize = 0
        selectedModified = null
        selectedMimeType = ""
        selectedSuffix = ""
        selectedSystemIconName = ""
        serviceMenuModel.clear()
    }

    function selectedLocalOrRemoteUrls() {
        return selectedUrls.slice()
    }

    function dragUrlsFor(itemUrl) {
        if (root.isSelected(itemUrl) && root.selectedUrls.length > 1)
            return root.selectedUrlsInViewOrder()
        return itemUrl && itemUrl.length ? [itemUrl] : []
    }

    function dragMimeDataFor(itemUrl) {
        return ({ "text/uri-list": root.dragUrlsFor(itemUrl).join("\r\n") })
    }

    function dragPreviewEntries(itemUrl, preparedUrls) {
        const urls = preparedUrls && preparedUrls.length ? preparedUrls : root.dragUrlsFor(itemUrl)
        const ordered = []
        if (itemUrl && urls.indexOf(itemUrl) >= 0)
            ordered.push(itemUrl)
        for (let i = 0; i < urls.length; ++i) {
            if (urls[i] !== itemUrl)
                ordered.push(urls[i])
        }

        const preview = []
        for (let i = 0; i < ordered.length && preview.length < 3; ++i) {
            const index = directory.indexOfUrl(ordered[i])
            const item = index >= 0 ? directory.itemAt(index) : null
            if (!item)
                continue
            preview.push({
                "name": item.name || "",
                "iconSource": root.iconForItem(item.isDir, item.suffix, item.systemIconName, item.name)
            })
        }
        return preview
    }

    function dropUrls(sourceUrls, destination, copyMode) {
        if (!sourceUrls || sourceUrls.length === 0 || !destination)
            return

        const filtered = []
        for (let i = 0; i < sourceUrls.length; ++i) {
            const url = String(sourceUrls[i] || "")
            if (!url.length || url === destination)
                continue
            // Dropping an item back onto the background of the directory it
            // already belongs to is a cancelled drag, not a copy/move.
            if (fileOps.isSourceAlreadyInDestination(url, destination))
                continue
            filtered.push(url)
        }
        if (filtered.length === 0)
            return

        const destinationIsCloud = root.isCloudUrl(destination)
        if (destinationIsCloud) {
            const destinationManager = root.cloudManagerFor(destination)
            for (let i = 0; i < filtered.length; ++i) {
                const sourceUrl = filtered[i]
                const sourceIsCloud = root.isCloudUrl(sourceUrl)
                if (sourceIsCloud) {
                    if ((sourceUrl.startsWith("gdrive://") && destination.startsWith("gdrive://"))
                            || (sourceUrl.startsWith("onedrive://") && destination.startsWith("onedrive://"))) {
                        if (copyMode)
                            destinationManager.copyItem(sourceUrl, destination)
                        else
                            destinationManager.moveItem(sourceUrl, destination)
                    } else {
                        root.requestToast(lang.language === "tr" ? "Bulut sağlayıcıları arasında doğrudan sürükle-bırak henüz desteklenmiyor." : "Direct drag and drop between cloud providers is not supported yet.")
                        return
                    }
                } else {
                    destinationManager.uploadFile(sourceUrl, destination)
                }
            }
            return
        }

        for (let i = 0; i < filtered.length; ++i) {
            if (root.isCloudUrl(filtered[i])) {
                // Preserve the existing cloud-to-local limitation instead of
                // partially moving a mixed selection.
                root.dropUrl(filtered[i], destination, copyMode)
                return
            }
        }

        root.armResultSelectionForUrls(filtered, destination)
        if (copyMode)
            fileOps.copyMany(filtered, destination)
        else
            fileOps.moveMany(filtered, destination)
    }

    function selectedUrlsInViewOrder() {
        if (!selectedUrls.length)
            return []
        const items = selectedItemsInViewOrder()
        const ordered = []
        for (let index = 0; index < items.length; ++index)
            ordered.push(items[index].itemUrl)
        return ordered.length === selectedUrls.length ? ordered : selectedUrls.slice()
    }

    function selectedItemsInViewOrder() {
        if (!selectedUrls.length)
            return []
        // Keep bindings aware of arriving rows, but marshal only selected
        // items into JavaScript instead of rebuilding every result's map.
        const count = directory.count
        return count > 0 ? directory.itemsForUrls(selectedUrls) : []
    }

    function updateServiceMenuContext() {
        if (root.selectedCount <= 0) {
            serviceMenuModel.clear()
            return
        }

        const items = root.selectedItemsInViewOrder()
        if (items.length !== root.selectedCount) {
            serviceMenuModel.clear()
            return
        }

        // Preserve the old single-folder behavior, but allow KDE service
        // actions to evaluate a real multi-selection.  KFileItemActions then
        // exposes only actions that are applicable to the selection as a whole.
        if (items.length === 1 && items[0].isDir) {
            serviceMenuModel.clear()
            return
        }

        const urls = []
        const mimeTypes = []
        for (let i = 0; i < items.length; ++i) {
            const url = String(items[i].itemUrl || "")
            if (!url.length || root.isCloudUrl(url) || url.startsWith("admin:")) {
                serviceMenuModel.clear()
                return
            }
            urls.push(url)
            mimeTypes.push(String(items[i].mimeType || ""))
        }
        serviceMenuModel.setContexts(urls, mimeTypes)
    }

    function compressionSelectionUrls() {
        const items = root.selectedItemsInViewOrder()
        if (!items.length || items.length !== root.selectedCount)
            return []
        const urls = []
        for (let i = 0; i < items.length; ++i) {
            const url = String(items[i].itemUrl || "")
            const localPath = String(items[i].localPath || "")
            if (!url.length || !localPath.length || root.isCloudUrl(url) || url.startsWith("admin:"))
                return []
            urls.push(url)
        }
        return urls
    }

    function openWithSelectionUrls() {
        // Resolve directly from the selected URL set rather than relying on the
        // delegate/view ordering. This keeps Open With available even when the
        // selection was created by rubber-band, Ctrl/Shift selection or after a
        // model refresh.
        const urls = []
        const source = selectedUrls && selectedUrls.length ? selectedUrls : (selectedUrl.length ? [selectedUrl] : [])
        for (let i = 0; i < source.length; ++i) {
            const url = source[i]
            const index = directory.indexOfUrl(url)
            const item = index >= 0 ? directory.itemAt(index) : null
            // A just-refreshed selection can temporarily outlive the matching
            // model row. Do not disable Open With solely because itemAt() is
            // momentarily unavailable; the selected URL is still authoritative.
            if ((item && item.isDir) || root.isCloudUrl(url) || String(url).startsWith("admin:"))
                return []
            urls.push(url)
        }
        return urls
    }

    function canOpenSelectionWithSameApp() {
        const urls = openWithSelectionUrls()
        return urls.length > 0
    }

    function localUploadSelectionUrls() {
        const items = selectedItemsInViewOrder()
        const urls = []
        for (let i = 0; i < items.length; ++i) {
            const item = items[i]
            if (!item || root.isCloudUrl(item.itemUrl) || String(item.itemUrl).startsWith("admin:")
                    || !item.localPath || !item.localPath.length)
                return []
            urls.push(item.itemUrl)
        }
        return urls
    }

    function shareSelectionContainsDirectory() {
        const items = selectedItemsInViewOrder()
        for (let i = 0; i < items.length; ++i) {
            if (items[i] && items[i].isDir)
                return true
        }
        return false
    }

    function prepareOpenWithSelection() {
        const urls = openWithSelectionUrls()
        if (urls.length > 0)
            openWithModel.setContextMany(urls)
    }

    function visibleUrlsSnapshot() {
        const urls = []
        for (let index = 0; index < directory.count; ++index) {
            const item = directory.itemAt(index)
            if (item && item.itemUrl)
                urls.push(item.itemUrl)
        }
        return urls
    }

    function visibleItemSignatureSnapshot() {
        const signatures = ({})
        for (let index = 0; index < directory.count; ++index) {
            const item = directory.itemAt(index)
            if (!item || !item.itemUrl)
                continue
            let modified = ""
            if (item.modified) {
                if (item.modified.getTime)
                    modified = String(item.modified.getTime())
                else
                    modified = String(item.modified)
            }
            signatures[item.itemUrl] = String(item.size || 0) + "|" + modified
        }
        return signatures
    }

    function resultNameFromUrl(url) {
        if (!url || !url.length)
            return ""
        let clean = String(url)
        const query = clean.indexOf("?")
        if (query >= 0)
            clean = clean.substring(0, query)
        while (clean.length > 1 && clean.endsWith("/"))
            clean = clean.substring(0, clean.length - 1)
        const slash = clean.lastIndexOf("/")
        const encoded = slash >= 0 ? clean.substring(slash + 1) : clean
        try { return decodeURIComponent(encoded) } catch (e) { return encoded }
    }

    function clearPendingResultSelection() {
        pendingResultSelection = false
        pendingResultAwaitRefresh = false
        pendingResultMode = ""
        pendingResultLocation = ""
        pendingResultNames = []
        pendingRenameSourceUrls = []
        pendingRenameResultUrls = []
        pendingResultBeforeUrls = []
        pendingResultRevealSingle = false
        pendingResultBeforeSignatures = ({})
        pendingResultKeepUntilMatch = false
        pendingResultTimeout.stop()
    }

    function armResultSelectionForUrls(sourceUrls, destinationLocation) {
        if (destinationLocation !== directory.location) {
            clearPendingResultSelection()
            return
        }
        const names = []
        for (let i = 0; i < sourceUrls.length; ++i) {
            const name = resultNameFromUrl(sourceUrls[i])
            if (name.length && names.indexOf(name) < 0)
                names.push(name)
        }
        pendingResultSelection = names.length > 0
        pendingResultAwaitRefresh = false
        pendingResultMode = "names"
        pendingResultLocation = destinationLocation
        pendingResultNames = names
        pendingResultBeforeUrls = []
        pendingResultBeforeSignatures = ({})
        pendingResultKeepUntilMatch = false
        pendingResultRevealSingle = true
    }

    function armResultSelectionForNames(names, destinationLocation) {
        if (destinationLocation !== directory.location) {
            clearPendingResultSelection()
            return
        }
        const uniqueNames = []
        for (let i = 0; i < names.length; ++i) {
            const name = String(names[i] || "").trim()
            if (name.length && uniqueNames.indexOf(name) < 0)
                uniqueNames.push(name)
        }
        pendingResultSelection = uniqueNames.length > 0
        pendingResultAwaitRefresh = false
        pendingResultMode = "names"
        pendingResultLocation = destinationLocation
        pendingResultNames = uniqueNames
        pendingResultBeforeUrls = []
        pendingResultBeforeSignatures = ({})
        pendingResultKeepUntilMatch = true
        pendingResultRevealSingle = true
        if (pendingResultSelection)
            pendingResultTimeout.restart()
    }

    function armResultSelectionForNewItems(destinationLocation, revealIfSingle) {
        if (destinationLocation !== directory.location) {
            clearPendingResultSelection()
            return
        }
        pendingResultSelection = true
        pendingResultAwaitRefresh = false
        pendingResultMode = "newItems"
        pendingResultLocation = destinationLocation
        pendingResultNames = []
        pendingResultBeforeUrls = visibleUrlsSnapshot()
        pendingResultBeforeSignatures = ({})
        pendingResultKeepUntilMatch = false
        pendingResultRevealSingle = revealIfSingle === undefined ? true : !!revealIfSingle
    }

    function armResultSelectionForChangedItems(destinationLocation) {
        if (destinationLocation !== directory.location) {
            clearPendingResultSelection()
            return
        }
        pendingResultSelection = true
        pendingResultAwaitRefresh = false
        pendingResultMode = "changedItems"
        pendingResultLocation = destinationLocation
        pendingResultNames = []
        pendingResultBeforeUrls = []
        pendingResultBeforeSignatures = visibleItemSignatureSnapshot()
        pendingResultKeepUntilMatch = true
        pendingResultRevealSingle = true
        pendingResultTimeout.restart()
    }

    function armResultSelectionForRename(sourceUrls, knownResultUrls) {
        clearPendingResultSelection()
        pendingResultSelection = sourceUrls.length > 0
        pendingResultMode = "rename"
        pendingResultLocation = directory.location
        pendingRenameSourceUrls = sourceUrls.slice()
        pendingRenameResultUrls = knownResultUrls ? knownResultUrls.slice() : []
        pendingResultKeepUntilMatch = true
        pendingResultRevealSingle = true
        pendingResultTimeout.restart()
    }

    function finalizePendingResultSelection() {
        if (!pendingResultSelection || !pendingResultAwaitRefresh || directory.loading)
            return
        if (pendingResultLocation !== directory.location) {
            clearPendingResultSelection()
            return
        }

        const matches = []
        let primary = -1
        for (let index = 0; index < directory.count; ++index) {
            const item = directory.itemAt(index)
            if (!item || !item.itemUrl)
                continue
            let include = false
            if (pendingResultMode === "newItems")
                include = pendingResultBeforeUrls.indexOf(item.itemUrl) < 0
            else if (pendingResultMode === "names")
                include = pendingResultNames.indexOf(item.name) >= 0
            else if (pendingResultMode === "rename")
                include = pendingRenameResultUrls.indexOf(String(item.itemUrl)) >= 0
            else if (pendingResultMode === "changedItems") {
                let modified = ""
                if (item.modified) {
                    if (item.modified.getTime)
                        modified = String(item.modified.getTime())
                    else
                        modified = String(item.modified)
                }
                const signature = String(item.size || 0) + "|" + modified
                include = pendingResultBeforeSignatures[item.itemUrl] === undefined
                       || pendingResultBeforeSignatures[item.itemUrl] !== signature
            }
            if (include) {
                matches.push(item.itemUrl)
                if (primary < 0)
                    primary = index
            }
        }

        if (!matches.length && pendingResultKeepUntilMatch)
            return

        const revealResult = pendingResultRevealSingle && matches.length > 0
        if (matches.length)
            setSelectedUrls(matches, primary, primary)
        clearPendingResultSelection()
        if (revealResult && primary >= 0) {
            const primaryUrl = matches[0]
            Qt.callLater(function() {
                root.focusDirectoryView()
                root.focusResultIndex(directory.indexOfUrl(primaryUrl))
            })
        }
    }

    function copySelection() {
        if (selectedUrls.length)
            fileOps.copyToClipboard(selectedLocalOrRemoteUrls(), false)
    }

    function cutSelection() {
        if (selectedUrls.length)
            fileOps.copyToClipboard(selectedLocalOrRemoteUrls(), true)
    }

    function pasteHere() {
        if (root.isCategoryLocation)
            return
        if (!root.isCloudUrl(directory.location) && fileOps.canPaste) {
            // Do not infer pasted results from destination mtime/size changes.
            // A moved source directory can change its own mtime while its
            // children are removed, and an overwrite may keep an identical
            // size/timestamp.  FileOperations emits the exact top-level target
            // URLs that KIO actually completed; Skip/Skip All items are absent.
            root.clearPendingResultSelection()
            fileOps.pasteFromClipboard(directory.location)
        }
    }

    function pasteIntoSelection() {
        if (selectedCount === 1 && selectedIsDir && !selectedIsCloud && fileOps.canPaste)
            fileOps.pasteFromClipboard(selectedUrl)
    }

    function duplicateSelection() {
        if (!selectedUrls.length)
            return
        if (root.isCategoryLocation && selectedUrls.length > 1) {
            root.requestToast(lang.language === "tr"
                              ? "Farklı klasörlerdeki dosyaları birlikte çoğaltmak için içeren klasörlerini açın."
                              : "Open the containing folders to duplicate files from different locations together.")
            return
        }
        if (root.isCloudUrl(directory.location)) {
            const manager = root.currentCloudManager()
            for (let i = 0; i < selectedUrls.length; ++i)
                manager.copyItem(selectedUrls[i], directory.location)
        } else if (selectedUrls.length === 1) {
            root.armResultSelectionForNewItems(directory.location)
            fileOps.duplicateItem(selectedUrls[0])
        } else {
            root.armResultSelectionForNewItems(directory.location)
            fileOps.duplicateMany(selectedLocalOrRemoteUrls(), directory.location)
        }
    }

    function undoLastOperation() {
        if (!fileOps.undoLastOperation())
            root.requestToast(lang.language === "tr" ? "Geri alınacak işlem yok." : "There is no operation to undo.")
    }

    function renameSelection() {
        if (selectedCount < 1 || !selectedUrl.length)
            return
        if (selectedCount > 1 && root.isCloudUrl(directory.location)) {
            root.requestToast(lang.language === "tr"
                              ? "Bulut öğeleri yalnızca tek tek yeniden adlandırılabilir."
                              : "Cloud items can only be renamed one at a time.")
            return
        }
        if (selectedCount === 1) {
            beginInlineRename()
            return
        }
        renameDialog.batchMode = selectedCount > 1
        renameDialog.targetUrls = selectedUrlsInViewOrder()
        renameDialog.errorText = ""
        renameInput.text = renameDialog.batchMode
                         ? (lang.language === "tr" ? "Yeni ad ###" : "New name ###")
                         : selectedName
        batchRenameStart.value = 1
        renameDialog.open()
    }

    function beginInlineRename() {
        if (selectedCount !== 1 || !selectedUrl.length)
            return
        closeContextMenus()

        // Starting rename on another item must never be blocked by a stale
        // inline editor. Treat the unfinished editor like an explicit cancel;
        // its deferred focus-loss callback sees a different URL and becomes a
        // harmless no-op. This also keeps F2/right-click rename deterministic.
        if (inlineRenameUrl.length && inlineRenameUrl !== selectedUrl) {
            inlineRenameUrl = ""
            inlineRenameOriginalName = ""
            inlineRenameEditor = null
        }

        inlineRenameOriginalName = selectedName
        inlineRenameUrl = selectedUrl
    }

    function prepareInlineRenameEditor(editor, itemName, itemIsDir) {
        if (!editor || !editor.visible)
            return
        inlineRenameEditor = editor
        editor.text = itemName
        editor.forceActiveFocus()
        const dot = itemIsDir ? -1 : itemName.lastIndexOf(".")
        if (dot > 0)
            editor.select(0, dot)
        else
            editor.selectAll()
    }

    function cancelInlineRename() {
        if (!inlineRenameUrl.length)
            return
        inlineRenameUrl = ""
        inlineRenameOriginalName = ""
        inlineRenameEditor = null
        root.forceActiveFocus()
    }

    function needsHiddenNameConfirmation(name, originalName) {
        return !AppTheme.skipHiddenNameConfirmation && name.startsWith(".")
               && name !== "." && name !== ".." && !String(originalName || "").startsWith(".")
    }

    function requestHiddenNameConfirmation(actionType, name, targetUrl) {
        hiddenRenameDialog.actionType = actionType
        hiddenRenameDialog.targetUrl = targetUrl
        hiddenRenameDialog.newName = name
        hiddenRenameDialog.open()
    }

    function commitInlineRename(itemUrl, value, allowHidden, advancing) {
        if (!inlineRenameUrl.length || inlineRenameUrl !== itemUrl)
            return true
        // Opening a modal moves focus out of the inline field. Its deferred
        // focus-loss callback must not submit the rename a second time.
        if (hiddenRenameDialog.actionType === "rename" && hiddenRenameDialog.targetUrl.length && !allowHidden)
            return false
        const newName = value.trim()
        if (!newName.length) {
            root.requestToast(lang.language === "tr" ? "Ad boş bırakılamaz." : "Name cannot be empty.")
            return false
        }
        if (newName === inlineRenameOriginalName) {
            cancelInlineRename()
            return true
        }

        if (!allowHidden && needsHiddenNameConfirmation(newName, inlineRenameOriginalName)) {
            requestHiddenNameConfirmation("rename", newName, itemUrl)
            return false
        }

        const targetUrl = inlineRenameUrl
        const originalName = inlineRenameOriginalName
        inlineRenameUrl = ""
        inlineRenameOriginalName = ""
        // Hidden entries cannot be selected in a view that excludes them.
        // Tab/Shift+Tab keeps focus on the next editor instead of the result.
        if (!advancing && (!newName.startsWith(".") || root.showHiddenFiles || root.isCategoryLocation))
            armResultSelectionForRename([targetUrl], root.isCloudUrl(targetUrl) ? [targetUrl] : [])
        if (targetUrl.startsWith("gdrive://")) {
            directory.googleRenameItem(targetUrl, newName)
            root.forceActiveFocus()
            return true
        }
        if (targetUrl.startsWith("onedrive://")) {
            directory.oneDriveRenameItem(targetUrl, newName)
            root.forceActiveFocus()
            return true
        }
        if (fileOps.renameItem(targetUrl, newName)) {
            root.forceActiveFocus()
            return true
        }

        inlineRenameOriginalName = originalName
        inlineRenameUrl = targetUrl
        clearPendingResultSelection()
        root.requestToast(lang.localizeMessage(fileOps.lastError))
        return false
    }

    // File-manager style sequential rename: Tab commits the current editor and
    // immediately opens the next visible row/card in rename mode.  Capture the
    // neighbour URL before committing because a successful rename may change
    // the current row's sort position.
    function commitInlineRenameAndAdvance(itemUrl, value, backwards) {
        const currentIndex = directory.indexOfUrl(itemUrl)
        let nextUrl = ""
        if (currentIndex >= 0) {
            const nextIndex = backwards ? currentIndex - 1 : currentIndex + 1
            if (nextIndex >= 0 && nextIndex < directory.count) {
                const nextItem = directory.itemAt(nextIndex)
                if (nextItem && nextItem.itemUrl)
                    nextUrl = nextItem.itemUrl
            }
        }

        if (!commitInlineRename(itemUrl, value, false, true))
            return false

        if (!nextUrl.length)
            return true

        // The rename can cause a lightweight model update. Resolve the saved
        // neighbour by URL after the current key event has fully unwound.
        Qt.callLater(function() {
            const nextIndex = directory.indexOfUrl(nextUrl)
            if (nextIndex < 0)
                return
            root.selectIndex(nextIndex, Qt.NoModifier)
            Qt.callLater(function() {
                if (root.selectedCount === 1 && root.selectedUrl === nextUrl)
                    root.beginInlineRename()
            })
        })
        return true
    }

    function batchRenamePreviewName(url, position) {
        const itemIndex = directory.indexOfUrl(url)
        const item = itemIndex >= 0 ? directory.itemAt(itemIndex) : null
        const originalName = item ? item.name : decodeURIComponent(url.substring(url.lastIndexOf("/") + 1))
        const isDirectory = item ? item.isDir : false
        let extension = ""
        if (!isDirectory) {
            const dot = originalName.lastIndexOf(".")
            if (dot > 0)
                extension = originalName.substring(dot)
        }
        let pattern = renameInput.text.trim()
        const numberValue = batchRenameStart.value + position
        const match = pattern.match(/#+/)
        if (match) {
            let numberText = String(numberValue)
            while (numberText.length < match[0].length)
                numberText = "0" + numberText
            pattern = pattern.replace(match[0], numberText)
        } else {
            pattern += numberValue
        }
        return pattern + extension
    }

    function batchRenamePreviewText() {
        const urls = renameDialog.targetUrls || []
        const count = Math.min(urls.length, 3)
        const rows = []
        for (let index = 0; index < count; ++index)
            rows.push(batchRenamePreviewName(urls[index], index))
        if (urls.length > count)
            rows.push(lang.language === "tr" ? "… ve " + (urls.length - count) + " öğe daha" : "… and " + (urls.length - count) + " more")
        return rows.join("\n")
    }

    function commitRename(allowHidden) {
        const newName = renameInput.text.trim()
        if (!newName.length) {
            renameDialog.errorText = lang.language === "tr" ? "Ad boş bırakılamaz." : "Name cannot be empty."
            return false
        }

        if (renameDialog.batchMode) {
            if (!allowHidden && renameDialog.targetUrls.some(function(url) {
                const itemIndex = directory.indexOfUrl(url)
                const item = itemIndex >= 0 ? directory.itemAt(itemIndex) : null
                const oldName = item ? item.name : decodeURIComponent(url.substring(url.lastIndexOf("/") + 1))
                return root.needsHiddenNameConfirmation(newName, oldName)
            })) {
                requestHiddenNameConfirmation("batch_rename", newName, directory.location)
                return false
            }
            if (!newName.startsWith(".") || root.showHiddenFiles || root.isCategoryLocation)
                armResultSelectionForRename(renameDialog.targetUrls)
            if (fileOps.batchRename(renameDialog.targetUrls, newName, batchRenameStart.value))
                renameDialog.close()
            else {
                renameDialog.errorText = lang.localizeMessage(fileOps.lastError)
                clearPendingResultSelection()
                return false
            }
            return true
        }

        armResultSelectionForRename([root.selectedUrl], root.selectedIsCloud ? [root.selectedUrl] : [])
        if (root.selectedIsGoogleDrive) {
            directory.googleRenameItem(root.selectedUrl, newName)
            renameDialog.close()
        } else if (root.selectedIsOneDrive) {
            directory.oneDriveRenameItem(root.selectedUrl, newName)
            renameDialog.close()
        } else if (fileOps.renameItem(root.selectedUrl, newName)) {
            renameDialog.close()
        } else {
            renameDialog.errorText = lang.localizeMessage(fileOps.lastError)
            clearPendingResultSelection()
            return false
        }
        return true
    }

    function openSelected() {
        if (!selectedUrls.length)
            return

        const items = selectedItemsInViewOrder()
        if (items.length <= 1) {
            if (selectedUrl.length)
                openItem(selectedUrl, selectedIsDir, selectedName, selectedMimeType)
            return
        }

        const ordinaryUrls = []
        for (let i = 0; i < items.length; ++i) {
            const item = items[i]
            if (!item || !item.itemUrl)
                continue
            if (item.isDir) {
                root.requestNewTab(item.itemUrl)
            } else if (root.isCloudUrl(item.itemUrl)) {
                root.cloudManagerFor(item.itemUrl).downloadAndOpen(item.itemUrl, item.name || "", item.mimeType || "")
            } else if (root.isAppImage(item.name || "", item.mimeType || "")) {
                fileOps.runExecutable(item.itemUrl)
            } else if (String(item.itemUrl).startsWith("admin://")) {
                root.adminEditor.openUrl(item.itemUrl)
            } else {
                ordinaryUrls.push(item.itemUrl)
            }
        }
        if (ordinaryUrls.length > 0)
            openWithModel.openDefaults(ordinaryUrls)
    }

    function trashSelection() {
        if (!selectedUrls.length)
            return
        if (directory.location.startsWith("gdrive://")) {
            for (let i = 0; i < selectedUrls.length; ++i)
                directory.googleTrashItem(selectedUrls[i])
        } else if (directory.location.startsWith("onedrive://")) {
            for (let i = 0; i < selectedUrls.length; ++i)
                directory.oneDriveTrashItem(selectedUrls[i])
        } else if (selectedUrls.length === 1) {
            fileOps.trashItem(selectedUrls[0])
        } else {
            fileOps.trashMany(selectedLocalOrRemoteUrls())
        }
    }

    function performPermanentDelete() {
        if (!selectedUrls.length)
            return
        if (directory.location.startsWith("gdrive://")) {
            for (let i = 0; i < selectedUrls.length; ++i)
                directory.googleDeleteItem(selectedUrls[i])
        } else if (directory.location.startsWith("onedrive://")) {
            for (let i = 0; i < selectedUrls.length; ++i)
                directory.oneDriveDeleteItem(selectedUrls[i])
        } else if (selectedUrls.length === 1) {
            fileOps.removeItem(selectedUrls[0])
        } else {
            fileOps.removeMany(selectedUrls)
        }
    }

    function deleteSelection() {
        if (!selectedUrls.length)
            return
        if (AppTheme.skipPermanentDeleteConfirmation) {
            performPermanentDelete()
            return
        }
        confirmDeleteDialog.open()
    }

    function showPropertiesForSelection() {
        if (selectedCount === 1 && selectedUrl.length)
            openPropertiesForSelection()
    }

    function prepareLinkDialog(dialog, nameInput, targetInput, fromSelection) {
        if (fromSelection) {
            if (!root.selectedCanCreateLink)
                return
            const info = fileOps.navigationTarget(root.selectedUrl, root.selectedLocalPath)
            if (!info.parentLocation || !info.localPath)
                return
            // Search and category results may belong to another local folder.
            dialog.destinationLocation = root.locationsEquivalent(info.parentLocation, directory.location)
                                         ? directory.location : String(info.parentLocation)
            targetInput.text = String(info.localPath)
        } else {
            if (!root.currentLocationIsLocal)
                return
            dialog.destinationLocation = directory.location
            targetInput.text = ""
        }
        nameInput.text = ""
        dialog.open()
        Qt.callLater(function() { nameInput.forceActiveFocus() })
    }

    function showNewSymlinkDialog(fromSelection) {
        prepareLinkDialog(newSymlinkDialog, newSymlinkNameInput, newSymlinkTargetInput, fromSelection)
    }

    function showNewHardlinkDialog(fromSelection) {
        if (fromSelection && (root.selectedIsDir || root.selectedLinkType === "symlink"))
            return
        prepareLinkDialog(newHardlinkDialog, newHardlinkNameInput, newHardlinkTargetInput, fromSelection)
    }

    function showNewFolderDialog() {
        if (root.isCategoryLocation)
            return
        newFolderInput.text = lang.t("new_folder")
        newFolderDialog.open()
        newFolderInput.forceActiveFocus()
        newFolderInput.selectAll()
    }

    function showNewFileDialog() {
        if (root.isCloudUrl(directory.location) || root.isCategoryLocation)
            return
        newFileInput.text = lang.language === "tr" ? "Yeni Dosya.txt" : "New File.txt"
        newFileDialog.open()
        newFileInput.forceActiveFocus()
        newFileInput.selectAll()
    }

    function focusLocationField() {
        enterLocationEdit(true)
    }

    function toggleHiddenFiles() {
        directory.showHidden = !directory.showHidden
    }

    function searchState() {
        const view = gridMode ? grid : listView
        return {
            "searchVisible": searchVisible,
            "searchQuery": searchInput.text || "",
            "searchEverywhere": searchEverywhere,
            "searchSessionId": directory.searchSessionId || "",
            "searchScrollY": directory.searchActive ? Math.max(0, view.contentY - view.originY) : -1
        }
    }

    function detachSearchStateForTabSwitch() {
        // Merely switching tabs must never start a search. A typed query is
        // preserved as draft text until the user explicitly presses Enter or
        // one of the search buttons.
        wheelScrollAnimation.stop()
        const view = gridMode ? grid : listView
        view.cancelFlick()
        var state = searchState()
        if (directory.searchActive)
            state.searchSessionId = directory.detachSearchSession()

        restoringSearchState = true
        searchVisible = false
        searchEverywhere = false
        searchInput.text = ""
        restoringSearchState = false
        return state
    }

    function restoreSearchState(state, sessionOnly) {
        const visible = !!(state && state.searchVisible)
        const query = state && state.searchQuery !== undefined ? String(state.searchQuery) : ""
        const everywhere = !!(state && state.searchEverywhere)
        const sessionId = state && state.searchSessionId !== undefined ? String(state.searchSessionId) : ""

        restoringSearchState = true
        searchVisible = visible
        searchEverywhere = everywhere
        searchInput.text = query
        if (!visible || !query.trim().length) {
            restoringSearchState = false
            if (!sessionOnly && directory.searchActive)
                directory.clearSearch()
            return false
        }

        // A tab owns a C++ search session. Reattaching restores the exact
        // accumulated rows immediately; an unfinished scan keeps feeding that
        // same session instead of restarting from zero.
        if (sessionId.length && directory.restoreSearchSession(sessionId)) {
            restoringSearchState = false
            pendingNavigationTopLocation = ""
            scrollRestorePending = false
            const offset = state.searchScrollY !== undefined ? Number(state.searchScrollY) : -1
            if (Number.isFinite(offset) && offset >= 0) {
                // A model reset discards the view's scroll position. Restore
                // after its rows and the search toolbar have been laid out,
                // even when this session is still streaming results.
                Qt.callLater(function() {
                    if (!directory.searchActive || directory.searchSessionId !== sessionId)
                        return
                    wheelScrollAnimation.stop()
                    const view = root.gridMode ? grid : listView
                    view.cancelFlick()
                    view.forceLayout()
                    const bottom = Math.max(0, view.contentHeight - view.height)
                    view.contentY = view.originY + Math.min(bottom, offset)
                })
            }
            return true
        }
        restoringSearchState = false

        // If the session no longer exists, keep the query as draft text only.
        // Search execution is exclusively user-triggered (Enter / From Here /
        // Everywhere / KFind); restoring a tab must never launch it implicitly.
        return false
    }

    function releaseSearchSession(sessionId) {
        if (sessionId && String(sessionId).length)
            directory.releaseSearchSession(String(sessionId))
    }

    function showSearch() {
        searchVisible = true
        Qt.callLater(function() {
            searchInput.forceActiveFocus()
            searchInput.selectAll()
        })
    }

    function startSearch(query, everywhere) {
        searchVisible = true
        searchEverywhere = !!everywhere
        searchInput.text = query || ""
        Qt.callLater(function() {
            runBuiltInSearch()
            searchInput.forceActiveFocus()
            searchInput.selectAll()
        })
    }

    function closeSearch() {
        // Restore the directory model before clearing the TextField. Without
        // this guard, onTextChanged calls cancelSearchForInput(), empties the
        // model, and clearSearch() then sees no active search to restore.
        if (directory.searchActive || (directory.searchSessionId && directory.searchSessionId.length))
            directory.clearSearch()
        restoringSearchState = true
        searchInput.text = ""
        restoringSearchState = false
        searchVisible = false
        searchEverywhere = false
        if (root.groupedCategory)
            root.scheduleGroupedCategoryReload()
        clearSelection()
    }

    function dismissSearchForNavigation() {
        if (!searchVisible && !directory.searchActive)
            return false
        closeSearch()
        focusDirectoryView()
        return true
    }

    function runBuiltInSearch() {
        if (!searchVisible)
            return
        const query = searchInput.text.trim()
        // category:/ and DLNA are virtual views, not KIO protocols. Their
        // search is an in-memory filter over the already loaded catalogue.
        if (root.localSearchMode) {
            if (root.groupedCategory)
                root.scheduleGroupedCategoryReload()
            return
        }
        if (!query.length) {
            directory.clearSearch()
            return
        }
        directory.search(query, searchEverywhere)
    }

    function openKFindHere() {
        if (root.localSearchMode)
            return
        fileOps.openKFind(directory.location)
    }

    function searchParentLabel(itemUrl, localPath) {
        let value = localPath && localPath.length ? localPath : itemUrl
        if (!value || !value.length)
            return ""
        if (value.startsWith("file://"))
            value = decodeURIComponent(value.substring(7))
        const slash = value.lastIndexOf("/")
        if (slash <= 0)
            return value
        return value.substring(0, slash)
    }

    function refreshDirectory() {
        if (root.isCategoryLocation && root.contentIndexModel)
            root.contentIndexModel.refresh()
        else
            directory.refresh()
    }

    function navigateUp() {
        root.handleUpNavigation()
    }

    function resetIconSize() {
        root.setActiveIconSize(168)
    }

    function setGridView() {
        gridMode = true
    }

    function setListView() {
        gridMode = false
    }

    function revealIndex(index) {
        if (index < 0)
            return
        if (root.groupedCategory)
            groupedCategoryView.positionModelIndex(index, ListView.Contain)
        else if (gridMode)
            grid.positionViewAtIndex(index, GridView.Contain)
        else
            listView.positionViewAtIndex(index, ListView.Contain)
    }

    function focusResultIndex(index) {
        if (index < 0)
            return
        if (root.groupedCategory)
            groupedCategoryView.positionModelIndex(index, ListView.Center)
        else if (gridMode)
            grid.positionViewAtIndex(index, GridView.Center)
        else
            listView.positionViewAtIndex(index, ListView.Center)
    }

    function moveSelection(direction, extend) {
        if (directory.count <= 0)
            return
        let columns = root.groupedCategory ? groupedCategoryView.layoutColumns : grid.columnCount
        let step = 0
        if (direction === "left") step = -1
        else if (direction === "right") step = 1
        else if (direction === "up") step = root.groupedCategory || gridMode ? -columns : -1
        else if (direction === "down") step = root.groupedCategory || gridMode ? columns : 1
        let current = primaryIndex >= 0 ? primaryIndex : 0
        let next = Math.max(0, Math.min(directory.count - 1, current + step))
        selectIndex(next, extend ? Qt.ShiftModifier : Qt.NoModifier)
    }

    function selectEdge(last, extend) {
        if (directory.count <= 0)
            return
        selectIndex(last ? directory.count - 1 : 0, extend ? Qt.ShiftModifier : Qt.NoModifier)
    }

    function cancelCurrentAction() {
        closeContextMenus()
        if (inlineRenameUrl.length) {
            cancelInlineRename()
            return
        }
        if (searchVisible) {
            closeSearch()
            return
        }
        clearSelection()
    }

    function selectItem(url, localPath, name, isDir, size, modified, mimeType, suffix, systemIconName, index, modifiers) {
        root.forceActiveFocus()
        if (index === undefined || index < 0) {
            index = directory.indexOfUrl(url)
        }
        selectIndex(index, modifiers === undefined ? Qt.NoModifier : modifiers)
    }

    function focusDirectoryView() {
        // Keep printable-key type-ahead ready after navigation, but do not
        // enqueue focus work while a directory is still being constructed or
        // restored at application startup.
        typeAheadBuffer = ""
        typeAheadReset.stop()
        if (root.visible)
            root.forceActiveFocus()
    }

    function revealExternalItem(location, itemUrl, centerItem) {
        if (!location || !location.length || !itemUrl || !itemUrl.length)
            return
        const centerTarget = centerItem === undefined ? true : !!centerItem
        // Reveal against the directory's full model: the target need not match
        // the query. Keep the open search field and its text as a draft so Enter
        // can run it again. Selection-only tab restoration retains its session.
        if (centerTarget && directory.searchActive)
            directory.clearSearch()
        pendingExternalRevealLocation = location
        pendingExternalRevealUrl = itemUrl
        pendingExternalRevealAttempts = 0
        pendingExternalRevealRefreshRequested = false
        pendingExternalRevealCenter = centerTarget
        // External reveal owns the final scroll/selection for this navigation.
        // Suppress the normal "fresh folder at top" positioning while the
        // requested item is being revealed.
        pendingNavigationTopLocation = ""
        if (!locationsEquivalent(directory.location, location))
            navigateTo(location)
        // navigateTo() can decide that two differently written URLs point to
        // the same folder and therefore emit no location/loading signal. Always
        // schedule a reveal attempt so a trailing slash or encoded character
        // can never strand the requested file without a selection.
        Qt.callLater(root.finalizeExternalReveal)
    }

    function restoreTabRevealSelection(location, itemUrl) {
        // A tab restore must bring back the selected/revealed item without
        // re-centering it. The tab's saved viewport is restored separately.
        revealExternalItem(location, itemUrl, false)
    }

    function comparableLocation(value) {
        let normalized = String(value || "").trim()
        if (normalized.startsWith("file://")) {
            try { normalized = decodeURIComponent(normalized.substring(7)) }
            catch (e) { normalized = normalized.substring(7) }
        }
        while (normalized.length > 1 && normalized.endsWith("/"))
            normalized = normalized.substring(0, normalized.length - 1)
        return normalized
    }

    function locationsEquivalent(first, second) {
        return comparableLocation(first) === comparableLocation(second)
    }

    function revealNavigationTarget(info, newTab) {
        if (!info || !info.parentLocation || !info.itemUrl) {
            root.requestToast(lang.language === "tr" ? "Hedef konumu çözümlenemedi." : "The target location could not be resolved.")
            return
        }
        if (info.exists === false) {
            root.requestToast(lang.language === "tr" ? "Hedef bulunamadı." : "Target not found.")
            return
        }
        if (newTab)
            root.requestRevealInNewTab(String(info.parentLocation), String(info.itemUrl))
        else
            root.revealExternalItem(String(info.parentLocation), String(info.itemUrl))
    }

    function openSelectedContainingFolder(newTab) {
        if (root.selectedCount !== 1 || !root.selectedUrl.length)
            return
        const info = fileOps.navigationTarget(root.selectedUrl, root.selectedLocalPath)
        root.revealNavigationTarget(info, newTab)
    }

    function showSelectedLinkTarget(newTab) {
        if (root.selectedCount !== 1 || !root.selectedLinkType.length)
            return
        if (root.selectedLinkType === "hardlink") {
            root.pendingHardLinkRevealSource = root.selectedUrl
            root.pendingHardLinkRevealNewTab = newTab
            root.requestToast(lang.language === "tr" ? "Hardlink eşi aranıyor…" : "Finding hard-link peer…")
            fileOps.findHardLinkPeer(root.selectedUrl)
            return
        }
        if (!root.selectedLinkTarget.length) {
            root.requestToast(lang.language === "tr" ? "Bağlantı hedefi bulunamadı." : "Link target is unavailable.")
            return
        }
        const info = fileOps.navigationTarget(root.selectedLinkTarget, root.selectedLocalPath)
        root.revealNavigationTarget(info, newTab)
    }

    function finalizeExternalReveal() {
        if (!pendingExternalRevealUrl.length || directory.loading)
            return
        if (!locationsEquivalent(pendingExternalRevealLocation, directory.location)) {
            pendingExternalRevealUrl = ""
            pendingExternalRevealLocation = ""
            pendingExternalRevealAttempts = 0
            pendingExternalRevealRefreshRequested = false
            pendingExternalRevealCenter = true
            return
        }
        const index = directory.indexOfUrl(pendingExternalRevealUrl)
        if (index < 0) {
            // "Show in folder" often arrives immediately after a browser has
            // completed a download.  The directory watcher/model can be one
            // event-loop turn behind, so refresh once and keep the reveal alive
            // briefly instead of silently dropping the requested item.
            if (!pendingExternalRevealRefreshRequested || pendingExternalRevealAttempts % 10 === 9) {
                pendingExternalRevealRefreshRequested = true
                directory.refresh()
            }
            if (pendingExternalRevealAttempts < 100) {
                ++pendingExternalRevealAttempts
                externalRevealRetry.restart()
                return
            }
            pendingExternalRevealUrl = ""
            pendingExternalRevealLocation = ""
            pendingExternalRevealAttempts = 0
            pendingExternalRevealRefreshRequested = false
            pendingExternalRevealCenter = true
            return
        }
        const revealUrl = pendingExternalRevealUrl
        const revealLocation = pendingExternalRevealLocation
        const centerItem = pendingExternalRevealCenter
        pendingExternalRevealUrl = ""
        pendingExternalRevealLocation = ""
        pendingExternalRevealAttempts = 0
        pendingExternalRevealRefreshRequested = false
        pendingExternalRevealCenter = true
        setSelectedUrls([revealUrl], index, index)
        Qt.callLater(function() {
            if (centerItem)
                root.focusResultIndex(index)
            root.forceActiveFocus()
            Qt.callLater(function() {
                if (centerItem) {
                    // Initial reveal: persist the viewport produced by centering
                    // the target. A tab-restore reveal (centerItem == false) is
                    // selection-only and must never overwrite the tab's saved
                    // scroll offset with the temporary top-of-view value.
                    const offset = root.tabScrollOffset()
                    if (offset >= 0)
                        root.externalRevealResolved(revealLocation, revealUrl, offset)
                } else if (root.pendingTabScrollOffset >= 0) {
                    // Restore selection first, then the saved viewport. This
                    // avoids a Grid/List model/layout reset winning after the
                    // scroll restoration and snapping the tab back to the top.
                    root.restorePendingTabScroll()
                }
            })
        })
    }

    Timer {
        id: externalRevealRetry
        interval: 60
        repeat: false
        onTriggered: root.finalizeExternalReveal()
    }

    function selectVideoSource(remote, fromHistory) {
        const nextRemote = !!remote
        if (root.dlnaViewActive === nextRemote)
            return
        root.dlnaViewActive = nextRemote
        if (!fromHistory)
            root.dlnaSourceForwardAvailable = false
        if (nextRemote && !dlnaMedia.selectedServerId)
            dlnaMedia.discover()
        root.focusDirectoryView()
    }

    function handleBackNavigation() {
        if (root.dismissSearchForNavigation())
            return
        if (root.closeMusicGroup()) return
        if (root.categoryKey === "videos" && root.dlnaViewActive) {
            if (root.activeDlnaNavigator && root.activeDlnaNavigator.canNavigateBack) {
                root.activeDlnaNavigator.navigateBack()
                return
            }
            // category:/videos is the parent of the remote catalogue. Do not
            // leak Back to BrowsePage here or it would jump to Discover.
            root.dlnaViewActive = false
            root.dlnaSourceForwardAvailable = true
            root.focusDirectoryView()
            return
        }
        root.backNavigationRequested()
    }

    function handleForwardNavigation() {
        if (root.categoryKey === "videos" && root.dlnaViewActive) {
            if (root.activeDlnaNavigator && root.activeDlnaNavigator.canNavigateForward)
                root.activeDlnaNavigator.navigateForward()
            // Never leak a remote-catalogue Forward action into the outer
            // filesystem/tab history. The two histories are intentionally
            // isolated.
            return
        }
        if (root.categoryKey === "videos" && root.dlnaSourceForwardAvailable) {
            root.selectVideoSource(true, true)
            root.dlnaSourceForwardAvailable = false
            return
        }
        root.forwardNavigationRequested()
    }

    function handleUpNavigation() {
        if (root.dismissSearchForNavigation())
            return
        if (root.closeMusicGroup()) return
        if (root.categoryKey === "videos" && root.dlnaViewActive) {
            if (root.activeDlnaNavigator && root.activeDlnaNavigator.navigateUp())
                return
            // At a DLNA section root, Up means the parent video source, not
            // the user's physical home directory.
            root.dlnaViewActive = false
            root.dlnaSourceForwardAvailable = true
            root.focusDirectoryView()
            return
        }
        if (root.isCategoryLocation) {
            // category:/... is virtual; DirectoryModel.parentLocation points
            // at a filesystem path. Return through navigation history instead.
            root.backNavigationRequested()
            return
        }
        root.navigateTo(directory.parentLocation)
    }

    function closeMusicGroup() {
        if (root.categoryKey !== "music" || !musicLibraryView.groupSelection) return false
        musicLibraryView.closeGroup()
        return true
    }

    function navigateTo(location) {
        if (!location || !location.length)
            return
        if (location === directory.location) {
            focusDirectoryView()
            return
        }
        tabScrollSnapshotTimer.stop()
        root.navigationAboutToStart(directory.location, root.navigationViewState())
        pendingTabScrollOffset = -1
        pendingTabScrollLocation = ""
        pendingNavigationSelection = null
        pendingNavigationPrimaryUrl = ""
        tabScrollRestoreRetry.stop()
        const keepSearchInput = pendingExternalRevealUrl.length > 0
                                && locationsEquivalent(pendingExternalRevealLocation, location)
        if (searchVisible && !keepSearchInput) {
            searchVisible = false
            searchInput.text = ""
            searchEverywhere = false
        }
        // Focus is restored from directory.onLocationChanged. Doing it here
        // used to queue focus changes before the model finished switching and
        // could make application startup / restored tabs noticeably slower.
        directory.location = location
    }

    function openItem(url, isDir, name, mimeType) {
        if (isDir) {
            root.navigateTo(url)
        } else if (root.isCloudUrl(url)) {
            root.cloudManagerFor(url).downloadAndOpen(url, name || root.selectedName, mimeType || root.selectedMimeType)
        } else if (root.isAppImage(name || root.selectedName, mimeType || root.selectedMimeType)) {
            fileOps.runExecutable(url)
        } else if (url.startsWith("admin://")) {
            root.adminEditor.openUrl(url)
        } else {
            // Use the same KDE MIME association path as “Birlikte Aç”.
            // This makes application/json and other text formats honor their
            // configured default application on double click.
            openWithModel.openDefault(url, mimeType || root.selectedMimeType)
        }
    }

    function beginRubberSelection(viewName, x, y, modifiers) {
        dismissTextFocus()
        rubberSelecting = true
        rubberView = viewName
        rubberStartX = x
        rubberStartY = y
        rubberCurrentX = x
        rubberCurrentY = y
        const view = viewName === "grid" ? grid : listView
        rubberPointerViewportY = y - view.contentY
        rubberModifiers = modifiers
        rubberBaseSelection = ((modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) !== 0)
                              ? selectedUrls.slice() : []
        rubberPreviewSelection = rubberBaseSelection.slice()
        if (rubberBaseSelection.length === 0 && (modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) === 0)
            clearSelection()
        selectionRevision++
    }

    function rectIntersects(ax, ay, aw, ah, bx, by, bw, bh) {
        return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by
    }

    function rubberSelectionForRect(viewName) {
        const x = Math.min(rubberStartX, rubberCurrentX)
        const y = Math.min(rubberStartY, rubberCurrentY)
        const w = Math.abs(rubberCurrentX - rubberStartX)
        const h = Math.abs(rubberCurrentY - rubberStartY)
        let next = rubberBaseSelection.slice()
        const ctrl = (rubberModifiers & Qt.ControlModifier) !== 0
        const additive = (rubberModifiers & Qt.ShiftModifier) !== 0

        if (w < 3 && h < 3)
            return (ctrl || additive) ? next : []

        if (viewName === "grid") {
            const columns = grid.columnCount
            for (let i = 0; i < directory.count; ++i) {
                const col = i % columns
                const row = Math.floor(i / columns)
                const hit = rectIntersects(x, y, w, h,
                                           col * grid.cellWidth, row * grid.cellHeight,
                                           grid.cellWidth, grid.cellHeight)
                if (!hit)
                    continue
                const item = directory.itemAt(i)
                if (!item || !item.itemUrl)
                    continue
                const pos = next.indexOf(item.itemUrl)
                if (ctrl && rubberBaseSelection.indexOf(item.itemUrl) >= 0) {
                    if (pos >= 0) next.splice(pos, 1)
                } else if (pos < 0) {
                    next.push(item.itemUrl)
                }
            }
        } else {
            const rowHeight = Math.round(48 + (root.activeIconSize - 118) * 0.13)
            const stride = rowHeight + listView.spacing
            for (let i = 0; i < directory.count; ++i) {
                const hit = rectIntersects(x, y, w, h,
                                           0, i * stride,
                                           listView.width, rowHeight)
                if (!hit)
                    continue
                const item = directory.itemAt(i)
                if (!item || !item.itemUrl)
                    continue
                const pos = next.indexOf(item.itemUrl)
                if (ctrl && rubberBaseSelection.indexOf(item.itemUrl) >= 0) {
                    if (pos >= 0) next.splice(pos, 1)
                } else if (pos < 0) {
                    next.push(item.itemUrl)
                }
            }
        }
        return next
    }

    function updateRubberSelection(x, y) {
        if (!rubberSelecting)
            return
        rubberCurrentX = x
        rubberCurrentY = y
        const view = rubberView === "grid" ? grid : listView
        rubberPointerViewportY = y - view.contentY
        rubberPreviewSelection = rubberSelectionForRect(rubberView)
        selectionRevision++
    }

    function autoScrollRubberSelection() {
        if (!rubberSelecting)
            return
        const view = rubberView === "grid" ? grid : listView
        if (!view || view.height <= 0 || view.contentHeight <= view.height)
            return

        const edge = 54
        let delta = 0
        if (rubberPointerViewportY < edge) {
            delta = -Math.min(30, Math.max(4, (edge - rubberPointerViewportY) * 0.55))
        } else if (rubberPointerViewportY > view.height - edge) {
            delta = Math.min(30, Math.max(4, (rubberPointerViewportY - (view.height - edge)) * 0.55))
        }
        if (delta === 0)
            return

        const oldY = view.contentY
        const minY = view.originY
        const maxY = Math.max(minY, view.originY + view.contentHeight - view.height)
        view.contentY = Math.max(minY, Math.min(maxY, oldY + delta))
        const moved = view.contentY - oldY
        if (Math.abs(moved) < 0.01)
            return

        // The pointer stays at the same viewport position while the content
        // moves underneath it, so advance the content-space end point too.
        rubberCurrentY += moved
        rubberPreviewSelection = rubberSelectionForRect(rubberView)
        selectionRevision++
    }

    function finishRubberSelection(viewName) {
        if (!rubberSelecting || rubberView !== viewName)
            return
        const next = rubberSelectionForRect(viewName)
        const primary = next.length ? directory.indexOfUrl(next[next.length - 1]) : -1
        rubberSelecting = false
        rubberView = ""
        rubberPreviewSelection = []
        setSelectedUrls(next, primary, primary)
    }

    function positionMenu(menu, item, x, y) {
        const p = item.mapToItem(root, x, y)
        menu.x = Math.max(8, Math.min(root.width - menu.implicitWidth - 8, p.x))
        menu.y = Math.max(8, Math.min(root.height - menu.implicitHeight - 8, p.y))
    }

    function shortcutSuffix(action) {
        const keys = KeyboardShortcuts.bindings[action] || []
        return keys.length ? "    " + keys.map(function(key) { return KeyboardShortcuts.displaySequence(key) }).join(" / ") : ""
    }

    function closeContextMenus() {
        breadcrumbContextMenu.close()
        copyActionsMenu.close()
        applicationServiceMenu.close()
        compressMenu.close()
        shareMenu.close()
        fileActionsMenu.close()
        openWithMenu.close()
        sortMenu.close()
        newItemMenu.close()
        itemContextMenu.close()
        backgroundContextMenu.requestClose()
        textEditContextMenu.close()
    }

    function showContextMenu(menu, item, x, y) {
        contextReopenTimer.stop()
        pendingContextMenu = null
        if (menu === backgroundContextMenu) {
            directoryMenuOpener = null
        }
        const p = item.mapToItem(root, x, y)
        const targetX = Math.max(8, Math.min(root.width - menu.implicitWidth - 8, p.x))
        const targetY = Math.max(8, Math.min(root.height - menu.implicitHeight - 8, p.y))
        const hadOpenMenu = itemContextMenu.visible || backgroundContextMenu.visible
                            || breadcrumbContextMenu.visible || applicationServiceMenu.visible || compressMenu.visible
                            || shareMenu.visible || fileActionsMenu.visible || openWithMenu.visible || sortMenu.visible
                            || newItemMenu.visible || copyActionsMenu.visible
        closeContextMenus()
        if (hadOpenMenu) {
            pendingContextMenu = menu
            pendingContextX = targetX
            pendingContextY = targetY
            contextReopenTimer.restart()
        } else {
            menu.x = targetX
            menu.y = targetY
            menu.open()
        }
    }

    function closeDirectoryMenu() {
        contextReopenTimer.stop()
        pendingContextMenu = null
        if (backgroundContextMenu.visible)
            backgroundContextMenu.requestClose()
    }

    function openDirectoryMenu(button) {
        root.dismissLocationCompletion()
        if (backgroundContextMenu.interactionOpen) {
            backgroundContextMenu.requestClose()
            return
        }
        dismissTextFocus()
        clearSelection()
        contextReopenTimer.stop()
        pendingContextMenu = null
        closeContextMenus()
        directoryMenuOpener = button || null
        const point = button
            ? button.parent.mapToItem(backgroundContextMenu.parent,
                button.x + button.width - backgroundContextMenu.implicitWidth, button.y + button.height + 6)
            : root.mapToItem(backgroundContextMenu.parent, Math.max(8, root.width - backgroundContextMenu.implicitWidth - 20), 56)
        backgroundContextMenu.x = point.x
        backgroundContextMenu.y = point.y
        backgroundContextMenu.requestOpen()
    }

    function openItemContextAt(item, mouse) {
        dismissTextFocus()
        showContextMenu(itemContextMenu, item, mouse.x, mouse.y)
    }

    function openBackgroundContextAt(item, mouse) {
        dismissTextFocus()
        clearSelection()
        showContextMenu(backgroundContextMenu, item, mouse.x, mouse.y)
    }

    Timer {
        id: contextReopenTimer
        interval: 70
        repeat: false
        onTriggered: {
            if (!root.pendingContextMenu)
                return
            root.pendingContextMenu.x = root.pendingContextX
            root.pendingContextMenu.y = root.pendingContextY
            root.pendingContextMenu.open()
            root.pendingContextMenu = null
        }
    }

    Timer {
        id: submenuArmTimer
        interval: 180
        repeat: false
        onTriggered: root.submenuActivationReady = itemContextMenu.opened
    }

    function dropUrl(sourceUrl, destination, copyMode) {
        if (!sourceUrl || !destination || sourceUrl === destination
                || fileOps.isSourceAlreadyInDestination(sourceUrl, destination))
            return

        const destinationIsCloud = root.isCloudUrl(destination)
        const sourceIsCloud = root.isCloudUrl(sourceUrl)
        if (destinationIsCloud) {
            const destinationManager = root.cloudManagerFor(destination)
            if (sourceIsCloud) {
                if ((sourceUrl.startsWith("gdrive://") && destination.startsWith("gdrive://"))
                        || (sourceUrl.startsWith("onedrive://") && destination.startsWith("onedrive://"))) {
                    if (copyMode)
                        destinationManager.copyItem(sourceUrl, destination)
                    else
                        destinationManager.moveItem(sourceUrl, destination)
                } else {
                    root.requestToast(lang.language === "tr" ? "Bulut sağlayıcıları arasında doğrudan sürükle-bırak henüz desteklenmiyor." : "Direct drag and drop between cloud providers is not supported yet.")
                }
            } else {
                destinationManager.uploadFile(sourceUrl, destination)
            }
            return
        }

        if (!sourceIsCloud) {
            root.armResultSelectionForUrls([sourceUrl], destination)
            if (copyMode)
                fileOps.copyItem(sourceUrl, destination)
            else
                fileOps.moveItem(sourceUrl, destination)
        }
    }

    function droppedLocalUrls(drop) {
        var raw = ""
        try { raw = drop.getDataAsString("text/uri-list") } catch (e) { raw = "" }
        if (!raw || !raw.length)
            return []
        return raw.split(/\r?\n/).filter(function(line) {
            return line.length > 0 && line[0] !== "#"
        })
    }

    function extractArkDrop(drop, destination) {
        if (!destination || root.isCloudUrl(destination))
            return false
        var service = ""
        var objectPath = ""
        try {
            service = drop.getDataAsString("application/x-kde-ark-dndextract-service").trim()
            objectPath = drop.getDataAsString("application/x-kde-ark-dndextract-path").trim()
        } catch (e) {
            return false
        }
        if (!service.length || !objectPath.length)
            return false
        if (destination === directory.location)
            root.armResultSelectionForChangedItems(destination)
        fileOps.extractArkDrag(service, objectPath, destination)
        return true
    }

    function openWithDialogForSelection() {
        const urls = openWithSelectionUrls()
        if (!urls.length)
            return
        openWithModel.setContextMany(urls)
        openWithPopup.open()
    }

    function openPropertiesForSelection() {
        openWithModel.setContext(selectedUrl, selectedMimeType)
        if (selectedLocalPath.length > 0)
            fileProperties.inspect(selectedUrl)
        propertiesDialog.open()
    }

    DlnaMediaManager {
        id: dlnaMedia
    }

    DirectoryModel {
        id: directory
        heavyIoBusy: fileOps.ioBusy
        folderPreviewsEnabled: root.folderPreviewsEnabled
        contentIndexSource: root.contentIndexModel
        googleAccessToken: root.googleDrive.accessToken
        oneDriveAccessToken: root.oneDrive.accessToken
        onGoogleOperationFinished: function(success, message) {
            if (root.pendingResultSelection) {
                root.pendingResultAwaitRefresh = success
                if (!success)
                    root.clearPendingResultSelection()
            }
            root.requestToast(message)
        }
        onOneDriveOperationFinished: function(success, message) {
            if (root.pendingResultSelection) {
                root.pendingResultAwaitRefresh = success
                if (!success)
                    root.clearPendingResultSelection()
            }
            root.requestToast(message)
        }
    }

    DirectoryModel {
        id: driveFolderPickerModel
        googleAccessToken: root.googleDrive.accessToken
        oneDriveAccessToken: root.oneDrive.accessToken
        location: "gdrive://root"
    }

    FileOperations {
        id: fileOps
        kioProgressEnabled: AppTheme.kioProgressEnabled
        onIoBusyChanged: {
            // DeferredThumbnail keeps already-decoded previews on screen while
            // I/O is busy and resumes pending requests afterwards. Do not bump
            // a global revision here: that would force every visible preview to
            // disappear/reload after each transfer.
        }
        onTransferItemsFinished: function(destinationLocation, resultUrls) {
            if (destinationLocation === directory.location
                    && !root.pendingResultSelection && resultUrls.length > 0)
                root.armResultSelectionForUrls(resultUrls, destinationLocation)
        }
        onItemsRenamed: function(sourceUrls, resultUrls) {
            if (!root.pendingResultSelection || root.pendingResultMode !== "rename"
                || root.pendingResultLocation !== directory.location || !sourceUrls.length)
                return
            for (let i = 0; i < sourceUrls.length; ++i) {
                if (root.pendingRenameSourceUrls.indexOf(String(sourceUrls[i])) < 0)
                    return
            }
            if (!resultUrls.length) {
                root.clearPendingResultSelection()
                return
            }
            root.pendingRenameResultUrls = resultUrls.slice()
        }
        onOperationFinished: function(success, message, refreshNeeded, externallyPresented) {
            if (!success)
                root.clearPendingResultSelection()
            if (success && refreshNeeded) {
                if (root.pendingResultSelection)
                    root.pendingResultAwaitRefresh = true
                directory.requestRefresh()
            } else if (root.pendingResultSelection && !refreshNeeded && !root.pendingResultKeepUntilMatch) {
                root.clearPendingResultSelection()
            }
            if (!externallyPresented)
                root.requestToast(message)
        }
        onHardLinkPeerResolved: function(sourceUrl, peerUrl, error) {
            if (sourceUrl !== root.pendingHardLinkRevealSource)
                return
            const newTab = root.pendingHardLinkRevealNewTab
            root.pendingHardLinkRevealSource = ""
            root.pendingHardLinkRevealNewTab = false
            if (!peerUrl || !peerUrl.length) {
                root.requestToast(lang.language === "tr" ? "Hardlink eşi bulunamadı." : "No hard-link peer was found.")
                return
            }
            const info = fileOps.navigationTarget(peerUrl, "")
            root.revealNavigationTarget(info, newTab)
        }
        onArchiveOverwriteConfirmationRequired: function(sourceUrl, destinationLocation, createSubfolder, conflicts) {
            root.pendingArchiveConflictSource = sourceUrl
            root.pendingArchiveConflictDestination = destinationLocation
            root.pendingArchiveConflictSubfolder = createSubfolder
            root.pendingArchiveConflicts = conflicts
            archiveOverwriteDialog.open()
        }
    }

    OpenWithModel {
        id: openWithModel
        onError: function(message) { root.requestToast(message) }
    }

    ServiceMenuModel {
        id: serviceMenuModel
        onError: function(message) { root.requestToast(message) }
    }

    IconPickerManager { id: iconPicker }

    FilePropertiesManager {
        id: fileProperties
        onFolderIconChanged: function(path, iconName) {
            directory.requestRefresh()
            root.requestToast(lang.language === "tr" ? "Klasör ikonu değiştirildi." : "Folder icon changed.")
        }
        onError: function(message) { root.requestToast(message) }
    }

    Component.onCompleted: {
        root.applyDirectoryIconModeSize()
        root.reloadMediaGallery()
        if (root.initialLocation.length)
            directory.location = root.initialLocation
        // InitialLocation is construction state, not user navigation. Suppress
        // the first location signal so a Home -> disk transition does not
        // accidentally create a phantom history entry that consumes Back once.
        Qt.callLater(function() { root.userLocationSignalsReady = true })
    }

    Component.onDestruction: {
        // Do not lose the last slider value if the page is closed during the
        // short persistence debounce.
        if (directoryIconSizeSaveTimer.running) {
            if (AppTheme.useSystemIcons)
                paneSettings.systemIconSize = root.iconSize
            else
                paneSettings.iconSize = root.iconSize
        }
    }

    Connections {
        target: directory
        function onRefreshCompleted() {
            if (root.pendingResultSelection && root.pendingResultAwaitRefresh)
                Qt.callLater(root.finalizePendingResultSelection)
            // Local refreshes are incremental and intentionally do not toggle
            // DirectoryModel.loading. A freshly downloaded file can therefore
            // appear only on refreshCompleted; retry the pending FileManager1
            // ShowItems reveal here as well as on loadingChanged.
            if (root.pendingExternalRevealUrl.length)
                Qt.callLater(root.finalizeExternalReveal)
            if (root.pendingTabScrollOffset >= 0)
                root.restorePendingTabScroll()
            else if (root.pendingNavigationTopLocation === directory.location)
                root.resetNavigationViewToTop()
        }
        function onLocationChanged() {
            tabScrollSnapshotTimer.stop()
            wheelScrollAnimation.stop()
            root.inlineRenameUrl = ""
            root.inlineRenameOriginalName = ""
            root.scrollRestorePending = false
            root.clearPendingResultSelection()
            root.clearSelection()
            if (root.pendingTabScrollOffset >= 0
                    || root.restoringSearchState || (root.pendingExternalRevealUrl.length
                    && root.pendingExternalRevealLocation === directory.location))
                root.pendingNavigationTopLocation = ""
            else
                root.pendingNavigationTopLocation = directory.location
            if (root.userLocationSignalsReady) {
                root.locationChangedByUser(directory.location)
                // Only real post-startup navigation restores keyboard focus.
                // Initial application construction stays completely untouched.
                Qt.callLater(root.focusDirectoryView)
            }
            if (root.pendingExternalRevealUrl.length)
                Qt.callLater(root.finalizeExternalReveal)
        }
        function onRefreshAboutToStart(preserveView) {
            if (preserveView)
                root.rememberScrollPosition()
            else
                root.scrollRestorePending = false
        }
        function onLoadingChanged() {
            if (directory.loading && directory.count === 0) {
                root.loadingOverlayReady = false
                loadingOverlayDelay.restart()
            } else {
                loadingOverlayDelay.stop()
                root.loadingOverlayReady = false
            }
            if (!directory.loading && root.scrollRestorePending)
                Qt.callLater(root.restoreScrollPosition)
            if (!directory.loading && root.pendingTabScrollOffset >= 0)
                root.restorePendingTabScroll()
            if (!directory.loading && root.pendingResultSelection && root.pendingResultAwaitRefresh)
                Qt.callLater(root.finalizePendingResultSelection)
            if (!directory.loading && root.pendingExternalRevealUrl.length)
                Qt.callLater(root.finalizeExternalReveal)
            if (!directory.loading && root.pendingNavigationTopLocation === directory.location)
                root.resetNavigationViewToTop()
        }
        function onCountChanged() { Qt.callLater(root.reconcileSelection) }
    }

    Connections {
        target: root.googleDrive
        function onRefreshRequested() { if (directory.location.startsWith("gdrive://")) directory.refresh() }
    }
    Connections {
        target: root.oneDrive
        function onRefreshRequested() { if (directory.location.startsWith("onedrive://")) directory.refresh() }
    }

    DropArea {
        anchors.fill: parent
        onDropped: function(drop) {
            if (root.isCategoryLocation)
                return
            const copyMode = (drop.modifiers & Qt.ControlModifier) !== 0
            if (drop.source && drop.source.dragUrls && drop.source.dragUrls.length > 0) {
                root.dropUrls(drop.source.dragUrls, directory.location, copyMode)
                drop.acceptProposedAction()
                return
            }
            if (drop.source && drop.source.dragUrl) {
                root.dropUrl(drop.source.dragUrl, directory.location, copyMode)
                drop.acceptProposedAction()
                return
            }
            if (root.extractArkDrop(drop, directory.location)) {
                drop.acceptProposedAction()
                return
            }
            if (root.isCloudUrl(directory.location)) {
                const urls = root.droppedLocalUrls(drop)
                if (urls.length > 0) {
                    root.currentCloudManager().uploadPaths(urls, directory.location)
                    drop.acceptProposedAction()
                }
                return
            }
            const urls = root.droppedLocalUrls(drop)
            if (urls.length > 0) {
                root.dropUrls(urls, directory.location, true)
                drop.acceptProposedAction()
            }
        }
    }

    Timer {
        id: pendingResultTimeout
        interval: 30000
        repeat: false
        onTriggered: root.clearPendingResultSelection()
    }

    Timer {
        id: rubberAutoScrollTimer
        interval: 16
        repeat: true
        running: root.rubberSelecting
        onTriggered: root.autoScrollRubberSelection()
    }

    // A tap anywhere outside the two persistent text editors releases their
    // keyboard focus. PointerHandlers observe the tap without replacing the
    // existing MouseAreas used by cards, menus and toolbar controls.
    TapHandler {
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: function(eventPoint, button) {
            const p = eventPoint.position
            if (!root.pointInsideItem(locationField, p.x, p.y)
                    && !root.pointInsideItem(searchInput, p.x, p.y))
                root.dismissTextFocus()
        }
    }

    // Ark does not expose dragged archive entries as regular URLs. It sends
    // a D-Bus service and object path, so keep a top-level receiver above the
    // file delegates and ask Ark to extract the selected entries here.
    DropArea {
        id: arkDropArea
        anchors.fill: parent
        z: 20000
        keys: ["application/x-kde-ark-dndextract-service",
               "application/x-kde-ark-dndextract-path"]
        onEntered: function(drag) {
            drag.accepted = !root.isCloudUrl(directory.location) && !root.isCategoryLocation
        }
        onDropped: function(drop) {
            if (root.extractArkDrop(drop, directory.location))
                drop.acceptProposedAction()
        }
    }

    // Listen for dedicated mouse Back/Forward buttons without covering the
    // entire pane with a MouseArea.  The old full-screen MouseArea won the
    // cursor hit-test and prevented item/card hover cursors from appearing.
    TapHandler {
        id: navigationMouseButtons
        acceptedButtons: Qt.BackButton | Qt.ForwardButton
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: function(eventPoint, button) {
            if (button === Qt.BackButton)
                root.handleBackNavigation()
            else if (button === Qt.ForwardButton)
                root.handleForwardNavigation()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            GButton {
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                text: ""
                display: AbstractButton.IconOnly
                icon.source: AppTheme.icon("nav-back.svg")
                icon.width: 16; icon.height: 16
                enabled: root.effectiveBackNavigationEnabled
                implicitWidth: 34
                implicitHeight: 38
                onClicked: root.handleBackNavigation()
                background: Rectangle { radius: 9; color: parent.hovered ? AppTheme.surfaceHover : AppTheme.surface; border.color: AppTheme.border; opacity: parent.enabled ? 1 : 0.48 }
            }

            GButton {
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                text: ""
                display: AbstractButton.IconOnly
                icon.source: AppTheme.icon("nav-forward.svg")
                icon.width: 16; icon.height: 16
                enabled: root.effectiveForwardNavigationEnabled
                implicitWidth: 34
                implicitHeight: 38
                onClicked: root.handleForwardNavigation()
                background: Rectangle { radius: 9; color: parent.hovered ? AppTheme.surfaceHover : AppTheme.surface; border.color: AppTheme.border; opacity: parent.enabled ? 1 : 0.48 }
            }

            GButton {
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                text: ""
                display: AbstractButton.IconOnly
                icon.source: AppTheme.icon("nav-up.svg")
                icon.width: 16; icon.height: 16
                implicitWidth: 38
                implicitHeight: 38
                onClicked: root.handleUpNavigation()
                background: Rectangle { radius: 10; color: parent.hovered ? AppTheme.surfaceHover : AppTheme.surface; border.color: AppTheme.border }
            }

            GButton {
                id: recentLocationsButton
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                implicitWidth: 42
                implicitHeight: 38
                enabled: root.quickAccessModel !== null
                onClicked: root.openRecentLocationsMenu()
                GToolTip {
                    text: lang.language === "tr" ? "Son Konumlar" : "Recent Locations"
                }
                contentItem: CrispIcon {
                    anchors.centerIn: parent
                    width: 22
                    height: 22
                    source: AppTheme.icon("history.svg")
                    opacity: recentLocationsButton.enabled ? 0.92 : 0.42
                }
                background: Rectangle {
                    radius: 10
                    color: recentLocationsButton.hovered ? AppTheme.surfaceHover : AppTheme.surface
                    border.color: recentLocationsButton.hovered ? AppTheme.accentBorder : AppTheme.border
                }
            }

            Rectangle {
                id: locationBar
                Layout.fillWidth: true
                Layout.preferredHeight: 38
                radius: 10
                color: AppTheme.surface
                border.color: root.locationEditMode && locationField.activeFocus ? AppTheme.accent : AppTheme.border
                clip: true

                MouseArea {
                    anchors.fill: parent
                    z: 0
                    acceptedButtons: Qt.LeftButton
                    hoverEnabled: true
                    cursorShape: Qt.IBeamCursor
                    onClicked: root.enterLocationEdit(false)
                }

                Flickable {
                    id: breadcrumbFlick
                    anchors.fill: parent
                    anchors.leftMargin: 7
                    anchors.rightMargin: 7
                    visible: !root.locationEditMode
                    z: 1
                    clip: true
                    contentWidth: Math.max(width, breadcrumbRow.implicitWidth)
                    contentHeight: height
                    boundsBehavior: Flickable.StopAtBounds
                    interactive: breadcrumbRow.implicitWidth > width

                    MouseArea {
                        width: breadcrumbFlick.contentWidth
                        height: breadcrumbFlick.height
                        z: 0
                        acceptedButtons: Qt.LeftButton
                        cursorShape: Qt.IBeamCursor
                        onClicked: root.enterLocationEdit(false)
                    }

                    Row {
                        id: breadcrumbRow
                        z: 1
                        height: parent.height
                        spacing: 2

                        Repeater {
                            model: root.breadcrumbItems
                            delegate: Row {
                                required property int index
                                required property var modelData
                                height: breadcrumbRow.height
                                spacing: 2

                                Text {
                                    visible: index > 0
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "›"
                                    color: AppTheme.textFaint
                                    font.pixelSize: 14
                                }

                                Rectangle {
                                    id: crumbChip
                                    height: 28
                                    width: Math.max(28, crumbLabel.implicitWidth + 16)
                                    anchors.verticalCenter: parent.verticalCenter
                                    radius: 7
                                    color: crumbMouse.containsMouse ? AppTheme.surfaceHover : "transparent"
                                    border.width: index === root.breadcrumbItems.length - 1 ? 1 : 0
                                    border.color: index === root.breadcrumbItems.length - 1 ? AppTheme.accentBorder : "transparent"

                                    Text {
                                        id: crumbLabel
                                        anchors.centerIn: parent
                                        text: modelData.label
                                        color: index === root.breadcrumbItems.length - 1 ? AppTheme.text : AppTheme.textMuted
                                        font.pixelSize: 11
                                        font.weight: index === root.breadcrumbItems.length - 1 ? Font.DemiBold : Font.Medium
                                    }

                                    MouseArea {
                                        id: crumbMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: function(mouse) {
                                            if (mouse.button === Qt.RightButton) {
                                                if (modelData.location.length) {
                                                    root.breadcrumbContextLocation = modelData.location
                                                    root.showContextMenu(breadcrumbContextMenu, crumbMouse, mouse.x, mouse.y)
                                                }
                                            } else if (mouse.button === Qt.MiddleButton) {
                                                if (modelData.location.length)
                                                    root.requestNewTab(modelData.location)
                                            } else if (root.isCategoryLocation && index === 0) {
                                                root.handleBackNavigation()
                                            } else {
                                                root.navigateTo(modelData.location)
                                            }
                                            mouse.accepted = true
                                        }
                                    }
                                }
                            }
                        }
                    }

                    onContentWidthChanged: contentX = Math.max(0, contentWidth - width)
                    onWidthChanged: contentX = Math.max(0, contentWidth - width)
                }

                GTextField {
                    id: locationField
                    anchors.fill: parent
                    visible: root.locationEditMode
                    z: 2
                    text: directory.displayLocation
                    color: AppTheme.text
                    selectByMouse: true
                    font.pixelSize: 11
                    leftPadding: 10
                    rightPadding: 10
                    background: Item {}
                    onAccepted: {
                        // Enter always opens exactly what is visible in the address field.
                        // Tab/up/down already copy an explicitly selected completion into it.
                        const target = text
                        root.locationCompletions = []
                        root.openLocationInput(target)
                        root.leaveLocationEdit()
                        root.forceActiveFocus()
                    }
                    onTextEdited: {
                        // Real typing starts a fresh completion session. Programmatic text
                        // changes made by Tab do not emit textEdited, so sibling cycling is
                        // preserved until the user types again.
                        root.locationCompletionIndex = 0
                        root.resetLocationCompletionCycle()
                        root.updateLocationCompletions()
                    }
                    onActiveFocusChanged: {
                        if (activeFocus)
                            root.updateLocationCompletions()
                        else if (root.locationEditMode)
                            root.leaveLocationEdit()
                    }
                    Keys.onPressed: function(event) {
                        if (event.key === Qt.Key_Down && root.locationCompletions.length > 0) {
                            root.cycleLocationCompletion(1)
                            event.accepted = true
                        } else if (event.key === Qt.Key_Up && root.locationCompletions.length > 0) {
                            root.cycleLocationCompletion(-1)
                            event.accepted = true
                        } else if ((event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab)
                                   && root.locationCompletions.length > 0) {
                            const backwards = event.key === Qt.Key_Backtab
                                              || (event.modifiers & Qt.ShiftModifier) !== 0
                            root.cycleLocationCompletion(backwards ? -1 : 1)
                            event.accepted = true
                        } else if (event.key === Qt.Key_Escape) {
                            root.leaveLocationEdit()
                            root.forceActiveFocus()
                            event.accepted = true
                        }
                    }
                    Connections {
                        target: directory
                        function onLocationChanged() {
                            if (!locationField.activeFocus)
                                locationField.text = root.editableLocationText(directory.displayLocation)
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        z: 100
                        acceptedButtons: Qt.RightButton
                        hoverEnabled: true
                        cursorShape: Qt.IBeamCursor
                        preventStealing: true
                        onPressed: function(mouse) {
                            locationField.forceActiveFocus()
                            root.openTextEditContext(locationField, locationField, mouse.x, mouse.y)
                            mouse.accepted = true
                        }
                    }
                }
            }

            GButton {
                id: searchButton
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                text: ""
                implicitWidth: 38
                implicitHeight: 38
                GToolTip {
                    text: (lang.language === "tr" ? "Ara" : "Search") + root.shortcutSuffix("find")
                }
                onClicked: root.searchVisible ? root.closeSearch() : root.showSearch()
                contentItem: CrispIcon {
                    anchors.centerIn: parent
                    width: 22
                    height: 22
                    source: AppTheme.icon("search.svg")
                    opacity: root.searchVisible ? 1.0 : 0.90
                }
                background: Rectangle {
                    radius: 10
                    color: root.searchVisible ? AppTheme.accentSoft : (searchButton.hovered ? AppTheme.surfaceHover : AppTheme.surface)
                    border.color: root.searchVisible ? AppTheme.accent : AppTheme.border
                }
            }

            Rectangle {
                Layout.preferredWidth: 210
                Layout.preferredHeight: 38
                radius: 10
                color: AppTheme.surface
                border.color: AppTheme.border
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 7
                    anchors.rightMargin: 7
                    spacing: 5
                    GToolButton {
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        text: "−"
                        implicitWidth: 26
                        implicitHeight: 28
                        onClicked: root.adjustIconSize(-8)
                        background: Rectangle { radius: 7; color: parent.hovered ? AppTheme.surfaceHover : "transparent" }
                    }
                    GSlider {
                        id: iconSizeSlider
                        objectName: "iconSizeSlider"
                        Layout.fillWidth: true
                        from: root.isCategoryLocation ? 104
                              : (AppTheme.useSystemIcons ? root.directorySystemIconMinimum() : 104)
                        to: root.isCategoryLocation ? 360
                            : (AppTheme.useSystemIcons ? root.directorySystemIconMaximum() : 360)
                        stepSize: root.isCategoryLocation ? 1 : (AppTheme.useSystemIcons ? 1 : 4)
                        // Navigation changes the range as well as the size. Qt
                        // can clamp value before the new range arrives; include
                        // both bounds so the binding restores the actual size.
                        value: Math.max(from, Math.min(to, root.activeIconSize))
                        onMoved: root.setActiveIconSize(value)
                        onPressedChanged: {
                            if (root.mediaGalleryCategory && mediaGalleryLoader.item) {
                                if (pressed)
                                    mediaGalleryLoader.item.beginResizeGesture()
                                else
                                    mediaGalleryLoader.item.endResizeGesture()
                            }
                        }
                        background: Rectangle {
                            x: iconSizeSlider.leftPadding
                            y: iconSizeSlider.topPadding + iconSizeSlider.availableHeight / 2 - height / 2
                            width: iconSizeSlider.availableWidth
                            height: 4
                            radius: 2
                            color: AppTheme.borderStrong
                            Rectangle {
                                width: iconSizeSlider.visualPosition * parent.width
                                height: parent.height
                                radius: 2
                                color: AppTheme.accent
                            }
                        }
                        handle: Rectangle {
                            x: iconSizeSlider.leftPadding + iconSizeSlider.visualPosition
                               * (iconSizeSlider.availableWidth - width)
                            y: iconSizeSlider.topPadding + iconSizeSlider.availableHeight / 2 - height / 2
                            width: 14
                            height: 14
                            radius: 7
                            color: iconSizeSlider.pressed ? AppTheme.accent : AppTheme.surface
                            border.color: AppTheme.accent
                            border.width: 2
                        }
                    }
                    GToolButton {
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        text: ""
                        display: AbstractButton.IconOnly
                        icon.source: AppTheme.icon("plus.svg")
                        icon.width: 16; icon.height: 16
                        implicitWidth: 30
                        implicitHeight: 30
                        onClicked: root.adjustIconSize(8)
                        background: Rectangle { radius: 7; color: parent.hovered ? AppTheme.surfaceHover : "transparent" }
                    }
                }
            }

            GButton {
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                visible: root.isCloudUrl(directory.location)
                text: "⇧"
                implicitWidth: 38
                implicitHeight: 38
                GToolTip {
                    text: lang.language === "tr" ? "Dosya yükle" : "Upload file"
                }
                onClicked: cloudUploadDialog.open()
                background: Rectangle { radius: 10; color: parent.hovered ? AppTheme.surfaceHover : AppTheme.surface; border.color: AppTheme.border }
            }

            GButton {
                id: emptyTrashButton
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                visible: root.isTrashLocation
                implicitHeight: 38
                implicitWidth: emptyTrashContent.implicitWidth + 24
                onClicked: confirmEmptyTrashDialog.open()
                GToolTip {
                    text: lang.language === "tr" ? "Çöpü Boşalt" : "Empty Trash"
                }
                contentItem: RowLayout {
                    id: emptyTrashContent
                    spacing: 7
                    CrispIcon {
                        Layout.preferredWidth: 18
                        Layout.preferredHeight: 18
                        source: AppTheme.icon("trashfull.svg")
                    }
                    Text {
                        text: lang.language === "tr" ? "Çöpü Boşalt" : "Empty Trash"
                        color: AppTheme.text
                        font.pixelSize: 11
                        font.weight: Font.Medium
                    }
                }
                background: Rectangle {
                    radius: 10
                    color: emptyTrashButton.hovered ? AppTheme.surfaceHover : AppTheme.surface
                    border.color: emptyTrashButton.hovered ? AppTheme.danger : AppTheme.border
                }
            }

            GButton {
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                visible: !root.isCategoryLocation && !root.isTrashLocation
                text: ""
                display: AbstractButton.IconOnly
                icon.source: AppTheme.icon("plus.svg")
                icon.width: 16; icon.height: 16
                enabled: visible
                implicitWidth: 38
                implicitHeight: 38
                GToolTip {
                    text: lang.t("new_folder")
                }
                onClicked: {
                    root.showNewFolderDialog()
                }
                background: Rectangle { radius: 10; color: parent.hovered ? AppTheme.surfaceHover : AppTheme.surface; border.color: AppTheme.border }
            }
        }

        GMenu {
            id: recentLocationsMenu
            parent: root
            closePolicy: Popup.CloseOnEscape
            implicitWidth: 430
            maximumPopupHeight: 440

            GMenuItem {
                enabled: false
                text: lang.language === "tr" ? "Son 10 Konum" : "Last 10 Locations"
            }

            Instantiator {
                model: root.quickAccessModel ? root.quickAccessModel.recentLocations : []
                delegate: GMenuItem {
                    required property int index
                    required property var modelData
                    text: (index + 1) + ".  " + String(modelData.title || "")
                          + "   ·   " + root.recentLocationDisplay(modelData.location)

                    onMiddleClicked: {
                        const targetLocation = String(modelData.location || "")
                        recentLocationsMenu.close()
                        if (targetLocation.length > 0)
                            root.requestNewTab(targetLocation)
                    }

                    onTriggered: {
                        const targetLocation = String(modelData.location || "")
                        recentLocationsMenu.close()
                        if (targetLocation.length > 0)
                            root.navigateTo(targetLocation)
                    }
                }
                onObjectAdded: function(index, object) { recentLocationsMenu.insertItem(index + 1, object) }
                onObjectRemoved: function(index, object) { recentLocationsMenu.removeItem(object) }
            }

            GMenuItem {
                visible: !root.quickAccessModel || root.quickAccessModel.recentLocations.length === 0
                enabled: false
                text: lang.language === "tr" ? "Henüz son konum yok" : "No recent locations yet"
            }
        }

        Popup {
            id: locationCompletionPopup
            parent: root
            x: locationBar.mapToItem(root, 0, 0).x
            y: locationBar.mapToItem(root, 0, 0).y + locationBar.height + 4
            width: locationBar.width
            height: Math.min(230, Math.max(0, locationCompletionList.contentHeight) + 8)
            visible: locationField.activeFocus && root.locationCompletions.length > 0
            modal: false
            focus: false
            closePolicy: Popup.NoAutoClose
            padding: 4
            z: 12000
            background: Rectangle {
                radius: 10
                color: AppTheme.surface
                border.color: AppTheme.borderStrong
            }
            contentItem: ListView {
                id: locationCompletionList
                clip: true
                model: root.locationCompletions
                currentIndex: root.locationCompletionIndex
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: locationCompletionList.contentHeight > locationCompletionList.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }
                onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
                delegate: Rectangle {
                    required property int index
                    required property var modelData
                    width: locationCompletionList.width
                    height: 34
                    radius: 7
                    color: index === root.locationCompletionIndex ? AppTheme.accentSoft : (completionMouse.containsMouse ? AppTheme.surfaceHover : "transparent")
                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        verticalAlignment: Text.AlignVCenter
                        text: modelData.label || modelData.value
                        color: AppTheme.text
                        elide: Text.ElideMiddle
                        font.pixelSize: 11
                    }
                    MouseArea {
                        id: completionMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onEntered: {
                            if (!root.locationCompletionCycling)
                                root.locationCompletionIndex = index
                        }
                        onClicked: {
                            root.locationCompletionIndex = index
                            root.locationCompletionCycling = false
                            root.acceptLocationCompletion()
                        }
                    }
                }
            }
        }

        Rectangle {
            visible: root.searchVisible
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 50 : 0
            radius: 12
            color: AppTheme.surface
            border.color: searchInput.activeFocus ? AppTheme.accentBorder : AppTheme.border

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 8
                spacing: 7

                Text {
                    text: "⌕"
                    color: AppTheme.textMuted
                    font.pixelSize: 18
                }

                GTextField {
                    id: searchInput
                    Layout.fillWidth: true
                    Layout.preferredHeight: 34
                    placeholderText: root.localSearchMode
                                     ? (lang.language === "tr" ? "Bu medya kataloğunda ara…" : "Search this media catalogue…")
                                     : (lang.language === "tr"
                                        ? "Dosya veya klasör adı ara…  (.mp3, *.jpg, rapor*2026)"
                                        : "Search file or folder names…  (.mp3, *.jpg, report*2026)")
                    color: AppTheme.text
                    selectByMouse: true
                    font.pixelSize: 11
                    onTextChanged: {
                        if (root.restoringSearchState)
                            return

                        // Typing only edits the draft query. It must never start
                        // recursive I/O on its own. If a previous query is still
                        // active, changing the text explicitly cancels it and
                        // restores the underlying directory; the replacement
                        // query waits for Enter or a search button.
                        const query = text.trim()
                        if (root.localSearchMode) {
                            if (root.groupedCategory)
                                root.scheduleGroupedCategoryReload()
                            return
                        }
                        if (directory.searchActive && directory.searchQuery !== query)
                            directory.clearSearch()
                    }
                    onAccepted: root.runBuiltInSearch()
                    background: Rectangle {
                        radius: 9
                        color: AppTheme.surfaceRaised
                        border.color: searchInput.activeFocus ? AppTheme.accent : AppTheme.border
                    }
                    MouseArea {
                        anchors.fill: parent
                        z: 100
                        acceptedButtons: Qt.RightButton
                        hoverEnabled: true
                        cursorShape: Qt.IBeamCursor
                        preventStealing: true
                        onPressed: function(mouse) {
                            searchInput.forceActiveFocus()
                            root.openTextEditContext(searchInput, searchInput, mouse.x, mouse.y)
                            mouse.accepted = true
                        }
                    }
                }

                GButton {
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    id: fromHereButton
                    visible: !root.localSearchMode
                    text: lang.language === "tr" ? "Buradan" : "From Here"
                    implicitHeight: 32
                    onClicked: {
                        root.searchEverywhere = false
                        root.runBuiltInSearch()
                    }
                    background: Rectangle {
                        radius: 9
                        color: !root.searchEverywhere ? AppTheme.accentSoft : (parent.hovered ? AppTheme.surfaceHover : "transparent")
                        border.color: !root.searchEverywhere ? AppTheme.accent : AppTheme.border
                    }
                }

                GButton {
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    id: everywhereButton
                    visible: !root.localSearchMode
                    text: lang.language === "tr" ? "Her Yerde" : "Everywhere"
                    implicitHeight: 32
                    onClicked: {
                        root.searchEverywhere = true
                        root.runBuiltInSearch()
                    }
                    background: Rectangle {
                        radius: 9
                        color: root.searchEverywhere ? AppTheme.accentSoft : (parent.hovered ? AppTheme.surfaceHover : "transparent")
                        border.color: root.searchEverywhere ? AppTheme.accent : AppTheme.border
                    }
                }

                GButton {
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    visible: !root.localSearchMode
                    text: lang.language === "tr" ? "K Bul" : "KFind"
                    implicitHeight: 32
                    GToolTip {
                        text: lang.language === "tr"
                                  ? "Gelişmiş K Bul'u bu dizinde aç (Ctrl+Shift+F)"
                                  : "Open advanced KFind in this folder (Ctrl+Shift+F)"
                    }
                    onClicked: root.openKFindHere()
                    background: Rectangle {
                        radius: 9
                        color: parent.hovered ? AppTheme.surfaceHover : "transparent"
                        border.color: AppTheme.border
                    }
                }

                Text {
                    visible: directory.searchActive || (root.localSearchMode && searchInput.text.trim().length > 0)
                    text: root.localSearchMode
                          ? ((root.categoryKey === "videos" && root.dlnaViewActive
                              ? (dlnaMedia && dlnaMedia.displayEntries ? dlnaMedia.displayEntries.length : 0)
                              : (root.mediaGalleryCategory ? root.galleryFilesForView().length : root.groupedCategoryFiles.length))
                             + " " + (lang.language === "tr" ? "sonuç" : "results"))
                          : (directory.loading
                             ? ((lang.language === "tr" ? "Aranıyor…" : "Searching…")
                                + (directory.count > 0 ? " · " + directory.count + " " + (lang.language === "tr" ? "sonuç" : "results") : ""))
                             : directory.count + " " + (lang.language === "tr" ? "sonuç" : "results"))
                    color: AppTheme.textMuted
                    font.pixelSize: 10
                }

                GToolButton {
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    text: ""
                    display: AbstractButton.IconOnly
                    icon.source: AppTheme.icon("close-ui.svg")
                    icon.width: 16; icon.height: 16
                    implicitWidth: 32
                    implicitHeight: 32
                    onClicked: root.closeSearch()
                    background: Rectangle { radius: 8; color: parent.hovered ? AppTheme.surfaceHover : "transparent" }
                }
            }
        }


        Rectangle {
            visible: directory.errorString.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 38 : 0
            radius: 10
            color: AppTheme.surfaceRaised
            Text {
                anchors.fill: parent
                anchors.margins: 10
                text: lang.localizeMessage(directory.errorString)
                color: AppTheme.danger
                elide: Text.ElideRight
                font.pixelSize: 11
            }
        }

        StackLayout {
            id: viewStack
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.mediaGalleryCategory ? 2 : (root.groupedCategory ? 3 : (root.gridMode ? 0 : 1))

            Item {
                id: gridPage

                GridView {
                    id: grid
                    objectName: "fileGridView"
                    anchors.fill: parent
                    anchors.leftMargin: 4
                    // Reserve a real gutter for the overlay scrollbar so the
                    // rightmost selection/rename editor never sits underneath it.
                    anchors.rightMargin: 14
                    clip: true
                    // StackLayout keeps hidden pages alive. Only the active
                    // view may create file delegates or request previews.
                    model: root.visible && !root.isCategoryLocation && root.gridMode ? directory : null
                    reuseItems: true
                    cacheBuffer: Math.max(cellHeight, Math.min(height * 0.45, cellHeight * 2))

                    // The slider controls the icon itself, not a fixed card width.
                    // Columns consume the full viewport so there is no random right-side gap
                    // when the requested icon size does not divide the available width evenly.
                    property real requestedCellWidth: AppTheme.useSystemIcons
                                                        ? Math.max(72, root.activeIconSize + 34)
                                                        : Math.max(104, root.activeIconSize)
                    // Pick the closest column count. floor() dropped a whole
                    // column slightly too early and left a conspicuous empty
                    // strip at a few slider values, especially with fractional
                    // desktop scaling.
                    property int columnCount: Math.max(1, Math.round(Math.max(1, width) / requestedCellWidth))
                    property real iconExtent: AppTheme.useSystemIcons
                                                   ? Math.min(256, root.activeIconSize)
                                                   : Math.max(58, Math.round(root.activeIconSize * 0.68))
                    // Text must follow the *visual* icon extent, not the raw slider value.
                    // Bundled icons use a larger logical slider range than system-theme icons
                    // to reach the same on-screen size; using activeIconSize here made folder/file
                    // names look larger in bundled-icon mode.  One shared visual metric keeps
                    // both icon modes typographically identical.
                    property int itemNameFontPixels: Math.max(12, Math.min(15, Math.round(iconExtent / 15 + 2)))
                    // GridView internally calculates its own column count from
                    // cellWidth. A fractional division can evaluate just below
                    // the intended integer (6.999999...), dropping the final
                    // column and leaving one full cell empty on the right.
                    cellWidth: width > 0 ? Math.max(1, Math.floor(width / columnCount)) : requestedCellWidth
                    // Icon + name + metadata need a little vertical breathing
                    // room.  The old +52 was already tight in normal mode and
                    // overflowed as soon as the F2 editor grew by a few pixels.
                    cellHeight: Math.round(iconExtent + 62)
                    boundsBehavior: Flickable.StopAtBounds
                    onContentYChanged: root.scheduleTabScrollSnapshot()

                    WheelHandler {
                        target: null
                        blocking: true
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                        acceptedModifiers: Qt.ControlModifier
                        onWheel: function(event) {
                            const dy = event.angleDelta.y !== 0 ? event.angleDelta.y : event.pixelDelta.y
                            if (dy !== 0)
                                root.adjustIconSize(dy > 0 ? 8 : -8)
                            event.accepted = true
                        }
                    }

                    WheelHandler {
                        target: null
                        blocking: true
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                        acceptedModifiers: Qt.NoModifier
                        onWheel: function(event) {
                            root.scrollViewFromWheel(grid, event, grid.cellHeight, gridScrollActivity)
                            event.accepted = true
                        }
                    }

                MouseArea {
                    id: gridBackgroundMouse
                    parent: grid.contentItem
                    width: grid.width
                    height: Math.max(grid.height, grid.contentHeight)
                    z: -10
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    preventStealing: true
                    cursorShape: Qt.ArrowCursor
                    onPressed: function(mouse) {
                        if (mouse.button === Qt.RightButton) {
                            root.openBackgroundContextAt(gridBackgroundMouse, mouse)
                            return
                        }
                        root.beginRubberSelection("grid", mouse.x, mouse.y, mouse.modifiers)
                    }
                    onPositionChanged: function(mouse) {
                        if (root.rubberSelecting && (mouse.buttons & Qt.LeftButton))
                            root.updateRubberSelection(mouse.x, mouse.y)
                    }
                    onReleased: function(mouse) {
                        if (mouse.button === Qt.LeftButton)
                            root.finishRubberSelection("grid")
                    }
                }

                Rectangle {
                    parent: grid.contentItem
                    visible: root.rubberSelecting && root.rubberView === "grid"
                    z: 1000
                    x: Math.min(root.rubberStartX, root.rubberCurrentX)
                    y: Math.min(root.rubberStartY, root.rubberCurrentY)
                    width: Math.abs(root.rubberCurrentX - root.rubberStartX)
                    height: Math.abs(root.rubberCurrentY - root.rubberStartY)
                    radius: 4
                    color: Qt.rgba(AppTheme.accent.r, AppTheme.accent.g, AppTheme.accent.b, 0.14)
                    border.color: AppTheme.accent
                    border.width: 1
                }

                delegate: Item {
                    id: gridDelegate
                    required property int index
                    required property string name
                    required property string itemUrl
                    required property string localPath
                    required property bool isDir
                    required property var size
                    required property var modified
                    required property string previewRevision
                    required property string suffix
                    required property string mimeType
                    required property string systemIconName
                    required property string videoInfo
                    required property int childCount
                    required property var folderPreviewPaths
                    required property string linkType
                    required property string linkTarget
                    property string dragUrl: itemUrl
                    property var dragUrls: []
                    width: grid.cellWidth
                    height: grid.cellHeight

                    // Expensive ffprobe work is requested only for delegates
                    // that the view actually creates near the viewport.
                    Component.onCompleted: directory.requestVideoMetadata(index)
                    GridView.onReused: directory.requestVideoMetadata(index)
                    Connections {
                        target: directory
                        function onLoadingChanged() {
                            if (!directory.loading)
                                directory.requestVideoMetadata(gridDelegate.index)
                        }
                    }

                    Drag.dragType: Drag.Automatic
                    Drag.source: gridDelegate
                    Drag.supportedActions: Qt.MoveAction | Qt.CopyAction
                    Drag.proposedAction: Qt.MoveAction
                    Drag.mimeData: ({ "text/uri-list": dragUrls.join("\r\n") })

                    Item {
                        id: card
                        anchors.fill: parent
                        opacity: fileOps.clipboardCut && fileOps.isCutUrl(itemUrl) ? 0.46 : 1.0
                        Behavior on opacity { NumberAnimation { duration: 120 } }

                        // No permanent tile/card background: the icon and labels own the space.
                        // A lightweight highlight appears only for selection/hover feedback.
                        Rectangle {
                            anchors.fill: parent
                            anchors.leftMargin: 7
                            anchors.rightMargin: 7
                            anchors.topMargin: 3
                            anchors.bottomMargin: 7
                            radius: 12
                            readonly property bool contentHovered: itemMouse.containsMouse
                                                                   && itemMouse.isContentHit(itemMouse.mouseX)
                            visible: root.isSelected(itemUrl) || contentHovered
                            color: root.isSelected(itemUrl) ? AppTheme.accentSoft : AppTheme.surfaceHover
                            border.width: 1
                            border.color: root.isSelected(itemUrl) ? AppTheme.accent : AppTheme.accentBorder
                            opacity: visible ? 1.0 : 0.0
                            Behavior on opacity { NumberAnimation { duration: 90 } }
                            Behavior on color { ColorAnimation { duration: 100 } }
                            Behavior on border.color { ColorAnimation { duration: 100 } }
                        }

                        DropArea {
                            anchors.fill: parent
                            enabled: isDir
                            onDropped: function(drop) {
                                if (drop.source && drop.source.dragUrls && drop.source.dragUrls.length > 0) {
                                    const copyMode = (drop.modifiers & Qt.ControlModifier) !== 0
                                    root.dropUrls(drop.source.dragUrls, itemUrl, copyMode)
                                    drop.acceptProposedAction()
                                    return
                                }
                                if (drop.source && drop.source.dragUrl && drop.source.dragUrl !== itemUrl) {
                                    const copyMode = (drop.modifiers & Qt.ControlModifier) !== 0
                                    root.dropUrl(drop.source.dragUrl, itemUrl, copyMode)
                                    drop.acceptProposedAction()
                                    return
                                }
                                if (root.extractArkDrop(drop, itemUrl)) {
                                    drop.acceptProposedAction()
                                    return
                                }
                                if (root.isCloudUrl(itemUrl)) {
                                    const urls = root.droppedLocalUrls(drop)
                                    if (urls.length > 0) {
                                        root.cloudManagerFor(itemUrl).uploadPaths(urls, itemUrl)
                                        drop.acceptProposedAction()
                                    }
                                    return
                                }
                                const urls = root.droppedLocalUrls(drop)
                                if (urls.length > 0) {
                                    root.dropUrls(urls, itemUrl, true)
                                    drop.acceptProposedAction()
                                }
                            }
                        }

                        ColumnLayout {
                            // The card's selection MouseArea is a sibling of this
                            // layout. Raising only its nested TextField leaves
                            // the card's hand cursor above the rename editor.
                            z: root.inlineRenameUrl === itemUrl ? 1 : 0
                            anchors.fill: parent
                            anchors.leftMargin: 7
                            anchors.rightMargin: 7
                            anchors.topMargin: 7
                            anchors.bottomMargin: 9
                            spacing: 4

                            Item {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.preferredWidth: Math.min(grid.iconExtent, Math.max(36, grid.cellWidth - 20))
                                Layout.preferredHeight: Math.min(grid.iconExtent, Math.max(36, grid.cellWidth - 20))
                                CrispIcon {
                                    anchors.fill: parent
                                    source: AppTheme.useSystemIcons
                                            ? AppTheme.systemIconAtSize(root.iconForItem(isDir, suffix, systemIconName, name), Math.round(parent.width))
                                            : root.iconForItem(isDir, suffix, systemIconName, name)
                                    visible: isDir || gridPreview.status !== Image.Ready
                                }
                                DeferredThumbnail {
                                    id: gridPreview
                                    anchors.fill: parent
                                    // Request enough source pixels for a crisp preview without
                                    // decoding/rendering a 4x image for every delegate while scrolling.
                                    sourceSize.width: Math.max(160, Math.ceil(parent.width * 1.75))
                                    sourceSize.height: Math.max(160, Math.ceil(parent.height * 1.75))
                                    candidateSource: !isDir ? root.thumbSource(localPath, suffix, modified, size, previewRevision) : ""
                                    paused: fileOps.ioBusy
                                    fileIdentity: localPath
                                    fileSuffix: suffix
                                    fileModified: modified
                                    fileSize: size
                                    visible: !isDir && status === Image.Ready
                                }

                                Rectangle {
                                    visible: linkType.length > 0
                                    z: 80
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    anchors.rightMargin: 1
                                    anchors.bottomMargin: 1
                                    width: Math.max(38, gridLinkBadgeText.implicitWidth + 12)
                                    height: 20
                                    radius: 7
                                    color: AppTheme.surfaceRaised
                                    border.width: 1
                                    border.color: AppTheme.accent
                                    Text {
                                        id: gridLinkBadgeText
                                        anchors.centerIn: parent
                                        text: root.linkBadgeText(linkType)
                                        color: AppTheme.accent
                                        font.pixelSize: 8
                                        font.bold: true
                                        font.letterSpacing: 0.3
                                    }
                                }

                                Item {
                                    id: folderPreviewScatter
                                    visible: isDir && root.folderPreviewsEnabled && folderPreviewPaths.length > 0
                                    anchors.fill: parent

                                    Repeater {
                                        model: Math.min(4, folderPreviewPaths.length)

                                        delegate: Item {
                                            required property int index
                                            readonly property int previewCount: Math.min(4, folderPreviewPaths.length)
                                            readonly property real cardWidth: folderPreviewScatter.width * (previewCount === 1 ? 0.50 : 0.36)
                                            readonly property real cardHeight: folderPreviewScatter.height * (previewCount === 1 ? 0.44 : 0.31)

                                            width: cardWidth
                                            height: cardHeight
                                            x: {
                                                if (previewCount === 1)
                                                    return folderPreviewScatter.width * 0.22
                                                if (previewCount === 2)
                                                    return index === 0 ? folderPreviewScatter.width * 0.13 : folderPreviewScatter.width * 0.49
                                                if (previewCount === 3) {
                                                    if (index === 0) return folderPreviewScatter.width * 0.10
                                                    if (index === 1) return folderPreviewScatter.width * 0.50
                                                    return folderPreviewScatter.width * 0.29
                                                }
                                                return (index % 2 === 0)
                                                       ? folderPreviewScatter.width * 0.10
                                                       : folderPreviewScatter.width * 0.51
                                            }
                                            y: {
                                                if (previewCount === 1)
                                                    return folderPreviewScatter.height * 0.34
                                                if (previewCount === 2)
                                                    return index === 0 ? folderPreviewScatter.height * 0.34 : folderPreviewScatter.height * 0.43
                                                if (previewCount === 3) {
                                                    if (index === 0) return folderPreviewScatter.height * 0.31
                                                    if (index === 1) return folderPreviewScatter.height * 0.34
                                                    return folderPreviewScatter.height * 0.54
                                                }
                                                return index < 2
                                                       ? folderPreviewScatter.height * (index === 0 ? 0.30 : 0.34)
                                                       : folderPreviewScatter.height * (index === 2 ? 0.55 : 0.51)
                                            }
                                            rotation: {
                                                if (previewCount === 1) return -2
                                                const angles = [-8, 7, 5, -6]
                                                return angles[index]
                                            }
                                            z: index + 2
                                            transformOrigin: Item.Center

                                            Item {
                                                anchors.fill: parent

                                                Item {
                                                    id: previewShadowLayer
                                                    x: 0
                                                    y: 3
                                                    width: parent.width
                                                    height: parent.height
                                                    layer.enabled: true
                                                    layer.smooth: true
                                                    layer.effect: MultiEffect {
                                                        blurEnabled: true
                                                        blur: 0.85
                                                        blurMax: 20
                                                        autoPaddingEnabled: true
                                                    }

                                                    Rectangle {
                                                        id: previewShadow
                                                        anchors.fill: parent
                                                        radius: Math.max(4, Math.min(width, height) * 0.12)
                                                        color: Qt.rgba(0, 0, 0, 0.12)
                                                        antialiasing: true
                                                    }
                                                }

                                                Rectangle {
                                                    id: previewFrame
                                                    anchors.fill: parent
                                                    radius: Math.max(4, Math.min(width, height) * 0.12)
                                                    color: "transparent"
                                                    antialiasing: true

                                                    Item {
                                                        id: previewClip
                                                        anchors.fill: parent
                                                        layer.enabled: true
                                                        layer.smooth: true
                                                        layer.effect: MultiEffect {
                                                            maskEnabled: true
                                                            maskSource: previewMask
                                                            maskThresholdMin: 0.01
                                                            maskThresholdMax: 1.0
                                                            maskSpreadAtMin: 0.0
                                                            maskSpreadAtMax: 0.0
                                                            autoPaddingEnabled: false
                                                        }

                                                        DeferredThumbnail {
                                                            anchors.fill: parent
                                                            sourceSize.width: Math.max(192, Math.ceil(width * 2.4))
                                                            sourceSize.height: Math.max(192, Math.ceil(height * 2.4))
                                                            candidateSource: root.folderPreviewSource(folderPreviewPaths[index])
                                                            paused: fileOps.ioBusy
                                                            fileIdentity: folderPreviewPaths[index]
                                                            fillMode: Image.PreserveAspectCrop
                                                            smooth: true
                                                            mipmap: true
                                                        }
                                                    }

                                                    Rectangle {
                                                        id: previewMask
                                                        width: previewClip.width
                                                        height: previewClip.height
                                                        radius: previewFrame.radius
                                                        color: "white"
                                                        visible: false
                                                        antialiasing: true
                                                        layer.enabled: true
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            Item {
                                Layout.fillWidth: true
                                Layout.preferredHeight: root.inlineRenameUrl === itemUrl ? 26 : 22

                                Text {
                                    anchors.fill: parent
                                    visible: root.inlineRenameUrl !== itemUrl
                                    text: name
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideMiddle
                                    color: AppTheme.text
                                    font.pixelSize: grid.itemNameFontPixels
                                    font.bold: isDir
                                }

                                GTextField {
                                    id: gridRenameInput
                                    objectName: "gridRenameInput"
                                    anchors.fill: parent
                                    visible: root.inlineRenameUrl === itemUrl
                                    z: 20
                                    selectByMouse: true
                                    HoverHandler { cursorShape: Qt.IBeamCursor }
                                    MouseArea {
                                        anchors.fill: parent
                                        z: 1000
                                        acceptedButtons: Qt.NoButton
                                        hoverEnabled: true
                                        cursorShape: Qt.IBeamCursor
                                    }
                                    horizontalAlignment: TextInput.AlignHCenter
                                    font.pixelSize: grid.itemNameFontPixels
                                    leftPadding: 7
                                    rightPadding: 7
                                    topPadding: 1
                                    bottomPadding: 1
                                    onVisibleChanged: {
                                        if (visible)
                                            Qt.callLater(function() {
                                                root.prepareInlineRenameEditor(gridRenameInput, name, isDir)
                                            })
                                    }
                                    Component.onCompleted: {
                                        if (visible)
                                            Qt.callLater(function() {
                                                root.prepareInlineRenameEditor(gridRenameInput, name, isDir)
                                            })
                                    }
                                    onAccepted: root.commitInlineRename(itemUrl, text)
                                    onActiveFocusChanged: {
                                        if (!activeFocus && visible)
                                            Qt.callLater(function() {
                                                if (root.inlineRenameUrl === itemUrl)
                                                    root.commitInlineRename(itemUrl, gridRenameInput.text)
                                            })
                                    }
                                    Keys.onPressed: function(event) {
                                        if (event.key === Qt.Key_Escape) {
                                            root.cancelInlineRename()
                                            event.accepted = true
                                        }
                                    }
                                    Keys.onTabPressed: function(event) {
                                        root.commitInlineRenameAndAdvance(itemUrl, text, false)
                                        event.accepted = true
                                    }
                                    Keys.onBacktabPressed: function(event) {
                                        root.commitInlineRenameAndAdvance(itemUrl, text, true)
                                        event.accepted = true
                                    }
                                    background: Rectangle {
                                        radius: 7
                                        color: AppTheme.surface
                                        border.width: 2
                                        border.color: AppTheme.accent
                                    }
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                text: directory.searchActive
                                      ? root.searchParentLabel(itemUrl, localPath)
                                      : (isDir ? root.folderItemCountText(childCount)
                                               : directory.formatBytes(size)
                                                 + (videoInfo.length ? " · " + videoInfo : ""))
                                horizontalAlignment: Text.AlignHCenter
                                elide: Text.ElideRight
                                color: AppTheme.textFaint
                                font.pixelSize: 10
                            }
                        }

                        MouseArea {
                            id: itemMouse
                            anchors.fill: parent
                            enabled: !renameDialog.visible && root.inlineRenameUrl !== itemUrl
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                            cursorShape: isContentHit(mouseX) ? Qt.PointingHandCursor : Qt.ArrowCursor
                            property bool backgroundGesture: false
                            property bool preserveSelectionOnPress: false
                            property bool dragStartedFromSelection: false

                            function isContentHit(mouseX) {
                                const hitWidth = Math.min(grid.cellWidth - 10, Math.max(84, grid.iconExtent + 42))
                                const leftEdge = (grid.cellWidth - hitWidth) / 2
                                return mouseX >= leftEdge && mouseX <= leftEdge + hitWidth
                            }

                            onPressed: function(mouse) {
                                if (mouse.button === Qt.MiddleButton) {
                                    if (isDir)
                                        root.requestNewTab(itemUrl)
                                    mouse.accepted = true
                                    return
                                }

                                // The grid cell itself fills the complete column, but only the
                                // centered icon/label area is an item hit target.  Clicking its
                                // side padding behaves like real directory background: a simple
                                // click clears Ctrl+A selection, while dragging still starts the
                                // normal rubber-band selection.
                                if ((mouse.button === Qt.LeftButton || mouse.button === Qt.RightButton)
                                        && !isContentHit(mouse.x)) {
                                    if (mouse.button === Qt.RightButton) {
                                        root.openBackgroundContextAt(itemMouse, mouse)
                                        return
                                    }
                                    backgroundGesture = true
                                    const p = itemMouse.mapToItem(grid.contentItem, mouse.x, mouse.y)
                                    root.beginRubberSelection("grid", p.x, p.y, mouse.modifiers)
                                    mouse.accepted = true
                                    return
                                }

                                backgroundGesture = false
                                preserveSelectionOnPress = false
                                dragStartedFromSelection = false
                                if (mouse.button === Qt.RightButton) {
                                    if (!root.isSelected(itemUrl))
                                        root.selectIndex(index, Qt.NoModifier)
                                    else
                                        root.setPrimaryFromItem(directory.itemAt(index), index)
                                    root.openItemContextAt(itemMouse, mouse)
                                    return
                                }
                                const plainLeftPress = mouse.button === Qt.LeftButton
                                                       && (mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) === 0
                                preserveSelectionOnPress = plainLeftPress && root.selectedCount > 1 && root.isSelected(itemUrl)
                                if (preserveSelectionOnPress)
                                    root.setPrimaryFromItem(directory.itemAt(index), index)
                                else
                                    root.selectItem(itemUrl, localPath, name, isDir, size, modified, mimeType, suffix, systemIconName, index, mouse.modifiers)
                            }
                            onPositionChanged: function(mouse) {
                                if (backgroundGesture && root.rubberSelecting && (mouse.buttons & Qt.LeftButton)) {
                                    const p = itemMouse.mapToItem(grid.contentItem, mouse.x, mouse.y)
                                    root.updateRubberSelection(p.x, p.y)
                                }
                            }
                            onReleased: function(mouse) {
                                if (preserveSelectionOnPress && mouse.button === Qt.LeftButton) {
                                    if (!dragStartedFromSelection)
                                        root.selectIndex(index, Qt.NoModifier)
                                    preserveSelectionOnPress = false
                                    dragStartedFromSelection = false
                                }
                                if (backgroundGesture && mouse.button === Qt.LeftButton) {
                                    const p = itemMouse.mapToItem(grid.contentItem, mouse.x, mouse.y)
                                    root.updateRubberSelection(p.x, p.y)
                                    root.finishRubberSelection("grid")
                                    backgroundGesture = false
                                    mouse.accepted = true
                                }
                            }
                            onCanceled: {
                                backgroundGesture = false
                                if (root.rubberSelecting && root.rubberView === "grid")
                                    root.finishRubberSelection("grid")
                            }
                            onClicked: function(mouse) {
                                if (!AppTheme.singleClickOpen || mouse.button !== Qt.LeftButton
                                        || (mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) !== 0
                                        || !isContentHit(mouse.x))
                                    return
                                root.openItem(itemUrl, isDir, name, mimeType)
                            }
                            onDoubleClicked: function(mouse) {
                                if (!AppTheme.singleClickOpen && mouse.button === Qt.LeftButton && isContentHit(mouse.x))
                                    root.openItem(itemUrl, isDir, name, mimeType)
                            }
                        }

                        DragPreview {
                            id: gridDragPreview
                            parent: root
                            x: -10000
                            y: -10000
                            itemCount: gridDelegate.dragUrls.length
                            primaryName: gridDelegate.name
                            turkish: lang.language === "tr"
                            entries: []
                        }

                        DragHandler {
                            id: dragHandler
                            target: null
                            enabled: !renameDialog.visible && root.inlineRenameUrl !== itemUrl
                            acceptedButtons: Qt.LeftButton
                            onActiveChanged: {
                                if (active) {
                                    if (itemMouse.preserveSelectionOnPress)
                                        itemMouse.dragStartedFromSelection = true
                                    gridDelegate.dragUrls = root.dragUrlsFor(gridDelegate.itemUrl)
                                    gridDragPreview.itemCount = gridDelegate.dragUrls.length
                                    gridDragPreview.primaryName = gridDelegate.name
                                    gridDragPreview.entries = root.dragPreviewEntries(gridDelegate.itemUrl, gridDelegate.dragUrls)
                                    gridDragPreview.visible = true
                                    gridDragPreview.grabToImage(function(result) {
                                        if (!dragHandler.active) {
                                            gridDragPreview.visible = false
                                            return
                                        }
                                        gridDelegate.Drag.imageSource = result.url
                                        gridDelegate.Drag.hotSpot = Qt.point(28, 28)
                                        gridDelegate.Drag.active = true
                                        gridDragPreview.visible = false
                                    })
                                } else {
                                    gridDelegate.Drag.active = false
                                    gridDelegate.dragUrls = []
                                    gridDragPreview.entries = []
                                    gridDragPreview.visible = false
                                }
                            }
                        }
                    }
                    }
                }

                Timer { id: gridScrollActivity; interval: 850 }

                ScrollBar {
                    id: gridScrollBar
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.right: parent.right
                    width: 10
                    orientation: Qt.Vertical
                    size: grid.visibleArea.heightRatio
                    visible: size < 0.999
                    active: visible && (pressed || hovered || grid.moving || grid.flicking
                                        || gridScrollActivity.running)
                    policy: ScrollBar.AsNeeded
                    onPressedChanged: if (pressed) wheelScrollAnimation.stop()
                    onPositionChanged: {
                        if (pressed)
                            grid.contentY = grid.originY + position * Math.max(0, grid.contentHeight - grid.height)
                    }
                    contentItem: Rectangle {
                        implicitWidth: 6
                        radius: 3
                        color: gridScrollBar.pressed ? AppTheme.accent : AppTheme.textMuted
                        opacity: gridScrollBar.active ? 0.82 : 0.0
                        Behavior on opacity { NumberAnimation { duration: 180 } }
                    }
                    background: Item {}
                }
                Binding {
                    target: gridScrollBar
                    property: "position"
                    value: grid.visibleArea.yPosition
                    when: !gridScrollBar.pressed
                    restoreMode: Binding.RestoreBinding
                }
            }

            Item {
                id: listPage

                ListView {
                    id: listView
                    objectName: "fileListView"
                    anchors.fill: parent
                    reuseItems: true
                    cacheBuffer: Math.max(120, Math.min(height * 0.35, 320))
                    anchors.rightMargin: 14
                    clip: true
                    model: root.visible && !root.isCategoryLocation && !root.gridMode ? directory : null
                    spacing: 5
                    boundsBehavior: Flickable.StopAtBounds
                    onContentYChanged: root.scheduleTabScrollSnapshot()

                    WheelHandler {
                        target: null
                        blocking: true
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                        acceptedModifiers: Qt.ControlModifier
                        onWheel: function(event) {
                            const dy = event.angleDelta.y !== 0 ? event.angleDelta.y : event.pixelDelta.y
                            if (dy !== 0)
                                root.adjustIconSize(dy > 0 ? 8 : -8)
                            event.accepted = true
                        }
                    }

                    WheelHandler {
                        target: null
                        blocking: true
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                        acceptedModifiers: Qt.NoModifier
                        onWheel: function(event) {
                            const rowExtent = Math.max(44, Math.round(48 + (root.activeIconSize - 118) * 0.13))
                            root.scrollViewFromWheel(listView, event, rowExtent, listScrollActivity)
                            event.accepted = true
                        }
                    }

                MouseArea {
                    id: listBackgroundMouse
                    parent: listView.contentItem
                    width: listView.width
                    height: Math.max(listView.height, listView.contentHeight)
                    z: -10
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    preventStealing: true
                    onPressed: function(mouse) {
                        if (mouse.button === Qt.RightButton) {
                            root.openBackgroundContextAt(listBackgroundMouse, mouse)
                            return
                        }
                        root.beginRubberSelection("list", mouse.x, mouse.y, mouse.modifiers)
                    }
                    onPositionChanged: function(mouse) {
                        if (root.rubberSelecting && (mouse.buttons & Qt.LeftButton))
                            root.updateRubberSelection(mouse.x, mouse.y)
                    }
                    onReleased: function(mouse) {
                        if (mouse.button === Qt.LeftButton)
                            root.finishRubberSelection("list")
                    }
                }

                Rectangle {
                    parent: listView.contentItem
                    visible: root.rubberSelecting && root.rubberView === "list"
                    z: 1000
                    x: Math.min(root.rubberStartX, root.rubberCurrentX)
                    y: Math.min(root.rubberStartY, root.rubberCurrentY)
                    width: Math.abs(root.rubberCurrentX - root.rubberStartX)
                    height: Math.abs(root.rubberCurrentY - root.rubberStartY)
                    radius: 4
                    color: Qt.rgba(AppTheme.accent.r, AppTheme.accent.g, AppTheme.accent.b, 0.14)
                    border.color: AppTheme.accent
                    border.width: 1
                }

                delegate: Item {
                    id: listDelegate
                    required property int index
                    required property string name
                    required property string itemUrl
                    required property string localPath
                    required property bool isDir
                    required property var size
                    required property var modified
                    required property string previewRevision
                    required property string suffix
                    required property string mimeType
                    required property string systemIconName
                    required property string videoInfo
                    required property int childCount
                    required property var folderPreviewPaths
                    required property string linkType
                    required property string linkTarget
                    property string dragUrl: itemUrl
                    property var dragUrls: []
                    // Keep a small real background strip on the right even when every
                    // visible row is occupied. This gives the mouse an always-available place
                    // to clear a full selection or open the directory context menu.
                    width: Math.max(0, listView.width - 26)
                    height: AppTheme.useSystemIcons ? Math.max(42, root.activeIconSize + 20) : Math.round(48 + (root.activeIconSize - 118) * 0.13)

                    Component.onCompleted: directory.requestVideoMetadata(index)
                    ListView.onReused: directory.requestVideoMetadata(index)
                    Connections {
                        target: directory
                        function onLoadingChanged() {
                            if (!directory.loading)
                                directory.requestVideoMetadata(listDelegate.index)
                        }
                    }

                    Drag.dragType: Drag.Automatic
                    Drag.source: listDelegate
                    Drag.supportedActions: Qt.MoveAction | Qt.CopyAction
                    Drag.proposedAction: Qt.MoveAction
                    Drag.mimeData: ({ "text/uri-list": dragUrls.join("\r\n") })

                    Rectangle {
                        id: rowCard
                        anchors.fill: parent
                        opacity: fileOps.clipboardCut && fileOps.isCutUrl(itemUrl) ? 0.46 : 1.0
                        Behavior on opacity { NumberAnimation { duration: 120 } }
                        radius: 12
                        color: root.isSelected(itemUrl) ? AppTheme.accentSoft
                              : (listMouse.containsMouse ? AppTheme.surfaceHover : AppTheme.surface)
                        border.color: root.isSelected(itemUrl) ? AppTheme.accent
                                    : (listMouse.containsMouse ? AppTheme.accentBorder : AppTheme.border)
                        Behavior on color { ColorAnimation { duration: 100 } }
                        Behavior on border.color { ColorAnimation { duration: 100 } }

                        DropArea {
                            anchors.fill: parent
                            enabled: isDir
                            onDropped: function(drop) {
                                if (drop.source && drop.source.dragUrls && drop.source.dragUrls.length > 0) {
                                    const copyMode = (drop.modifiers & Qt.ControlModifier) !== 0
                                    root.dropUrls(drop.source.dragUrls, itemUrl, copyMode)
                                    drop.acceptProposedAction()
                                    return
                                }
                                if (drop.source && drop.source.dragUrl && drop.source.dragUrl !== itemUrl) {
                                    const copyMode = (drop.modifiers & Qt.ControlModifier) !== 0
                                    root.dropUrl(drop.source.dragUrl, itemUrl, copyMode)
                                    drop.acceptProposedAction()
                                    return
                                }
                                if (root.extractArkDrop(drop, itemUrl)) {
                                    drop.acceptProposedAction()
                                    return
                                }
                                if (root.isCloudUrl(itemUrl)) {
                                    const urls = root.droppedLocalUrls(drop)
                                    if (urls.length > 0) {
                                        root.cloudManagerFor(itemUrl).uploadPaths(urls, itemUrl)
                                        drop.acceptProposedAction()
                                    }
                                    return
                                }
                                const urls = root.droppedLocalUrls(drop)
                                if (urls.length > 0) {
                                    root.dropUrls(urls, itemUrl, true)
                                    drop.acceptProposedAction()
                                }
                            }
                        }

                        RowLayout {
                            z: root.inlineRenameUrl === itemUrl ? 1 : 0
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 12
                            Item {
                                width: AppTheme.useSystemIcons
                                       ? Math.min(256, root.activeIconSize)
                                       : Math.max(24, Math.min(38, root.activeIconSize * 0.18))
                                height: width
                                CrispIcon {
                                    anchors.fill: parent
                                    source: AppTheme.useSystemIcons
                                            ? AppTheme.systemIconAtSize(root.iconForItem(isDir, suffix, systemIconName, name), Math.round(parent.width))
                                            : root.iconForItem(isDir, suffix, systemIconName, name)
                                    visible: listPreview.status !== Image.Ready
                                }
                                DeferredThumbnail {
                                    id: listPreview
                                    anchors.fill: parent
                                    candidateSource: !isDir ? root.thumbSource(localPath, suffix, modified, size, previewRevision) : ""
                                    paused: fileOps.ioBusy
                                    fileIdentity: localPath
                                    fileSuffix: suffix
                                    fileModified: modified
                                    fileSize: size
                                    visible: status === Image.Ready
                                    sourceSize.width: 96
                                    sourceSize.height: 96
                                }
                            }
                            Rectangle {
                                visible: linkType.length > 0
                                Layout.preferredWidth: visible ? Math.max(48, listLinkBadgeText.implicitWidth + 14) : 0
                                Layout.preferredHeight: 22
                                Layout.alignment: Qt.AlignVCenter
                                radius: 7
                                color: AppTheme.surfaceRaised
                                border.width: 1
                                border.color: AppTheme.accent
                                Text {
                                    id: listLinkBadgeText
                                    anchors.centerIn: parent
                                    text: root.linkBadgeText(linkType)
                                    color: AppTheme.accent
                                    font.pixelSize: 8
                                    font.bold: true
                                }
                            }
                            Item {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                Text {
                                    anchors.fill: parent
                                    visible: root.inlineRenameUrl !== itemUrl
                                    text: name
                                    color: AppTheme.text
                                    font.pixelSize: 13
                                    font.bold: isDir
                                    elide: Text.ElideMiddle
                                    verticalAlignment: Text.AlignVCenter
                                }
                                GTextField {
                                    id: listRenameInput
                                    objectName: "listRenameInput"
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    height: 32
                                    visible: root.inlineRenameUrl === itemUrl
                                    z: 20
                                    selectByMouse: true
                                    HoverHandler { cursorShape: Qt.IBeamCursor }
                                    MouseArea {
                                        anchors.fill: parent
                                        z: 1000
                                        acceptedButtons: Qt.NoButton
                                        hoverEnabled: true
                                        cursorShape: Qt.IBeamCursor
                                    }
                                    font.pixelSize: 13
                                    leftPadding: 8
                                    rightPadding: 8
                                    topPadding: 3
                                    bottomPadding: 3
                                    onVisibleChanged: {
                                        if (visible)
                                            Qt.callLater(function() {
                                                root.prepareInlineRenameEditor(listRenameInput, name, isDir)
                                            })
                                    }
                                    Component.onCompleted: {
                                        if (visible)
                                            Qt.callLater(function() {
                                                root.prepareInlineRenameEditor(listRenameInput, name, isDir)
                                            })
                                    }
                                    onAccepted: root.commitInlineRename(itemUrl, text)
                                    onActiveFocusChanged: {
                                        if (!activeFocus && visible)
                                            Qt.callLater(function() {
                                                if (root.inlineRenameUrl === itemUrl)
                                                    root.commitInlineRename(itemUrl, listRenameInput.text)
                                            })
                                    }
                                    Keys.onPressed: function(event) {
                                        if (event.key === Qt.Key_Escape) {
                                            root.cancelInlineRename()
                                            event.accepted = true
                                        }
                                    }
                                    Keys.onTabPressed: function(event) {
                                        root.commitInlineRenameAndAdvance(itemUrl, text, false)
                                        event.accepted = true
                                    }
                                    Keys.onBacktabPressed: function(event) {
                                        root.commitInlineRenameAndAdvance(itemUrl, text, true)
                                        event.accepted = true
                                    }
                                    background: Rectangle {
                                        radius: 8
                                        color: AppTheme.surface
                                        border.width: 2
                                        border.color: AppTheme.accent
                                    }
                                }
                            }
                            Text {
                                text: directory.searchActive ? root.searchParentLabel(itemUrl, localPath) : (isDir ? root.folderItemCountText(childCount) : suffix.toUpperCase())
                                color: AppTheme.textMuted
                                font.pixelSize: 10
                                width: directory.searchActive ? 250 : 75
                                elide: Text.ElideMiddle
                                horizontalAlignment: Text.AlignRight
                            }
                            Text {
                                visible: !directory.searchActive
                                text: isDir ? "—" : directory.formatBytes(size)
                                      + (videoInfo.length ? " · " + videoInfo : "")
                                color: AppTheme.textMuted
                                font.pixelSize: 10
                                width: visible ? (videoInfo.length ? 210 : 85) : 0
                                horizontalAlignment: Text.AlignRight
                            }
                        }

                        MouseArea {
                            id: listMouse
                            anchors.fill: parent
                            enabled: !renameDialog.visible && root.inlineRenameUrl !== itemUrl
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                            cursorShape: Qt.PointingHandCursor
                            property bool preserveSelectionOnPress: false
                            property bool dragStartedFromSelection: false
                            onPressed: function(mouse) {
                                if (mouse.button === Qt.MiddleButton) {
                                    if (isDir)
                                        root.requestNewTab(itemUrl)
                                    mouse.accepted = true
                                    return
                                }
                                preserveSelectionOnPress = false
                                dragStartedFromSelection = false
                                if (mouse.button === Qt.RightButton) {
                                    if (!root.isSelected(itemUrl))
                                        root.selectIndex(index, Qt.NoModifier)
                                    else
                                        root.setPrimaryFromItem(directory.itemAt(index), index)
                                    root.openItemContextAt(listMouse, mouse)
                                    return
                                }
                                const plainLeftPress = mouse.button === Qt.LeftButton
                                                       && (mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) === 0
                                preserveSelectionOnPress = plainLeftPress && root.selectedCount > 1 && root.isSelected(itemUrl)
                                if (preserveSelectionOnPress)
                                    root.setPrimaryFromItem(directory.itemAt(index), index)
                                else
                                    root.selectItem(itemUrl, localPath, name, isDir, size, modified, mimeType, suffix, systemIconName, index, mouse.modifiers)
                            }
                            onReleased: function(mouse) {
                                if (preserveSelectionOnPress && mouse.button === Qt.LeftButton) {
                                    if (!dragStartedFromSelection)
                                        root.selectIndex(index, Qt.NoModifier)
                                    preserveSelectionOnPress = false
                                    dragStartedFromSelection = false
                                }
                            }
                            onClicked: function(mouse) {
                                if (!AppTheme.singleClickOpen || mouse.button !== Qt.LeftButton
                                        || (mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) !== 0)
                                    return
                                root.openItem(itemUrl, isDir, name, mimeType)
                            }
                            onDoubleClicked: function(mouse) {
                                if (!AppTheme.singleClickOpen && mouse.button === Qt.LeftButton)
                                    root.openItem(itemUrl, isDir, name, mimeType)
                            }
                        }

                        DragPreview {
                            id: listDragPreview
                            parent: root
                            x: -10000
                            y: -10000
                            itemCount: listDelegate.dragUrls.length
                            primaryName: listDelegate.name
                            turkish: lang.language === "tr"
                            entries: []
                        }

                        DragHandler {
                            id: listDrag
                            target: null
                            enabled: !renameDialog.visible && root.inlineRenameUrl !== itemUrl
                            acceptedButtons: Qt.LeftButton
                            onActiveChanged: {
                                if (active) {
                                    if (listMouse.preserveSelectionOnPress)
                                        listMouse.dragStartedFromSelection = true
                                    listDelegate.dragUrls = root.dragUrlsFor(listDelegate.itemUrl)
                                    listDragPreview.itemCount = listDelegate.dragUrls.length
                                    listDragPreview.primaryName = listDelegate.name
                                    listDragPreview.entries = root.dragPreviewEntries(listDelegate.itemUrl, listDelegate.dragUrls)
                                    listDragPreview.visible = true
                                    listDragPreview.grabToImage(function(result) {
                                        if (!listDrag.active) {
                                            listDragPreview.visible = false
                                            return
                                        }
                                        listDelegate.Drag.imageSource = result.url
                                        listDelegate.Drag.hotSpot = Qt.point(28, 28)
                                        listDelegate.Drag.active = true
                                        listDragPreview.visible = false
                                    })
                                } else {
                                    listDelegate.Drag.active = false
                                    listDelegate.dragUrls = []
                                    listDragPreview.entries = []
                                    listDragPreview.visible = false
                                }
                            }
                        }
                    }
                    }
                }

                Timer { id: listScrollActivity; interval: 850 }

                ScrollBar {
                    id: listScrollBar
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.right: parent.right
                    width: 10
                    orientation: Qt.Vertical
                    size: listView.visibleArea.heightRatio
                    visible: size < 0.999
                    active: visible && (pressed || hovered || listView.moving || listView.flicking
                                        || listScrollActivity.running)
                    policy: ScrollBar.AsNeeded
                    onPressedChanged: if (pressed) wheelScrollAnimation.stop()
                    onPositionChanged: {
                        if (pressed)
                            listView.contentY = listView.originY + position * Math.max(0, listView.contentHeight - listView.height)
                    }
                    contentItem: Rectangle {
                        implicitWidth: 6
                        radius: 3
                        color: listScrollBar.pressed ? AppTheme.accent : AppTheme.textMuted
                        opacity: listScrollBar.active ? 0.82 : 0.0
                        Behavior on opacity { NumberAnimation { duration: 180 } }
                    }
                    background: Item {}
                }
                Binding {
                    target: listScrollBar
                    property: "position"
                    value: listView.visibleArea.yPosition
                    when: !listScrollBar.pressed
                    restoreMode: Binding.RestoreBinding
                }
            }

            Item {
                id: mediaGalleryPage

                Loader {
                    id: mediaGalleryLoader
                    anchors.fill: parent
                    active: root.visible && root.mediaGalleryCategory
                    sourceComponent: Component {
                        ColumnLayout {
                            function prepareForResize() {
                                if (!root.dlnaViewActive)
                                    mediaGalleryView.prepareForResize()
                            }
                            function beginResizeGesture() {
                                if (!root.dlnaViewActive)
                                    mediaGalleryView.beginResizeGesture()
                            }
                            function endResizeGesture() {
                                if (!root.dlnaViewActive)
                                    mediaGalleryView.endResizeGesture()
                            }
                            anchors.fill: parent
                            spacing: 12

                            Rectangle {
                                id: mediaSourceHeader
                                Layout.fillWidth: true
                                Layout.preferredHeight: 70
                                radius: 14
                                color: AppTheme.surface
                                border.color: AppTheme.border

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 14
                                    anchors.rightMargin: 62
                                    spacing: 12

                                    Rectangle {
                                        Layout.preferredWidth: 42
                                        Layout.preferredHeight: 42
                                        radius: 12
                                        color: AppTheme.accentSoft
                                        border.color: AppTheme.accentBorder
                                        CrispIcon {
                                            anchors.centerIn: parent
                                            width: Math.round(Math.min(29, 23 * root.categoryTextScale))
                                            height: width
                                            source: root.categoryKey === "videos"
                                                    ? AppTheme.customIcon("video.svg")
                                                    : AppTheme.customIcon("image.svg")
                                        }
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 90
                                        Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                                        spacing: 2
                                        Text {
                                            Layout.fillWidth: true
                                            horizontalAlignment: Text.AlignLeft
                                            text: root.categoryKey === "videos" && root.dlnaViewActive
                                                  ? (dlnaMedia.selectedServerName.length
                                                     ? dlnaMedia.selectedServerName
                                                     : (lang.language === "tr" ? "DLNA medya sunucusu" : "DLNA media server"))
                                                  : (root.contentIndexModel
                                                     ? root.contentIndexModel.titleForCategory(root.categoryKey, lang.language)
                                                     : root.categoryKey)
                                            color: AppTheme.text
                                            font.pixelSize: Math.round(16 * root.categoryTextScale)
                                            font.weight: Font.DemiBold
                                            elide: Text.ElideRight
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            horizontalAlignment: Text.AlignLeft
                                            text: root.categoryKey === "videos" && root.dlnaViewActive
                                                  ? (dlnaMedia.selectedServerId
                                                     ? ((dlnaMedia.selectedProvider || "DLNA") + " · "
                                                        + (lang.language === "tr" ? "yerel ağ medya kataloğu" : "local network media catalog"))
                                                     : (dlnaMedia.discovering
                                                        ? (lang.language === "tr" ? "Sunucular aranıyor…" : "Searching for servers…")
                                                        : (lang.language === "tr" ? "GiG, Jellyfin veya Emby seç" : "Choose GiG, Jellyfin, or Emby")))
                                                  : (root.galleryFilesForView().length + " "
                                                     + (lang.language === "tr" ? "medya öğesi" : "media items"))
                                            color: AppTheme.textMuted
                                            font.pixelSize: Math.round(9 * root.categoryTextScale)
                                            elide: Text.ElideRight
                                        }
                                    }

                                    Rectangle {
                                        visible: root.categoryKey === "videos"
                                        Layout.preferredWidth: Math.min(250, Math.max(210, mediaSourceHeader.width * 0.20))
                                        Layout.preferredHeight: 40
                                        radius: 12
                                        color: AppTheme.surfaceRaised
                                        border.color: AppTheme.border

                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.margins: 3
                                            spacing: 3

                                            GToolButton {
                                                id: localMediaTab
                                                Layout.fillWidth: true
                                                Layout.fillHeight: true
                                                onClicked: root.selectVideoSource(false, false)
                                                background: Rectangle {
                                                    radius: 9
                                                    color: !root.dlnaViewActive
                                                           ? AppTheme.accentSoft
                                                           : (localMediaTab.hovered ? AppTheme.surfaceHover : "transparent")
                                                    border.width: !root.dlnaViewActive ? 1 : 0
                                                    border.color: AppTheme.accentBorder
                                                }
                                                contentItem: Text {
                                                    text: lang.language === "tr" ? "Bu Bilgisayar" : "This Computer"
                                                    color: !root.dlnaViewActive ? AppTheme.accent : AppTheme.text
                                                    font.pixelSize: 10
                                                    font.weight: !root.dlnaViewActive ? Font.DemiBold : Font.Medium
                                                    horizontalAlignment: Text.AlignHCenter
                                                    verticalAlignment: Text.AlignVCenter
                                                }
                                            }

                                            GToolButton {
                                                id: dlnaMediaTab
                                                Layout.fillWidth: true
                                                Layout.fillHeight: true
                                                onClicked: {
                                                    root.selectVideoSource(true, false)
                                                }
                                                background: Rectangle {
                                                    radius: 9
                                                    color: root.dlnaViewActive
                                                           ? AppTheme.accentSoft
                                                           : (dlnaMediaTab.hovered ? AppTheme.surfaceHover : "transparent")
                                                    border.width: root.dlnaViewActive ? 1 : 0
                                                    border.color: AppTheme.accentBorder
                                                }
                                                contentItem: Column {
                                                    spacing: 0
                                                    Text {
                                                        width: parent.width
                                                        text: dlnaMedia.selectedServerName.length
                                                              ? dlnaMedia.selectedServerName
                                                              : (lang.language === "tr" ? "Medya Sunucusu" : "Media Server")
                                                        color: root.dlnaViewActive ? AppTheme.accent : AppTheme.text
                                                        font.pixelSize: 10
                                                        font.weight: root.dlnaViewActive ? Font.DemiBold : Font.Medium
                                                        horizontalAlignment: Text.AlignHCenter
                                                        elide: Text.ElideRight
                                                    }
                                                    Text {
                                                        width: parent.width
                                                        visible: dlnaMedia.selectedProvider.length > 0
                                                        text: dlnaMedia.selectedProvider
                                                        color: AppTheme.textMuted
                                                        font.pixelSize: 7
                                                        horizontalAlignment: Text.AlignHCenter
                                                    }
                                                }
                                            }
                                        }
                                    }

                                    Rectangle {
                                        visible: !(root.categoryKey === "videos" && root.dlnaViewActive)
                                                 && mediaSourceHeader.width >= 980
                                        Layout.preferredWidth: 240
                                        Layout.preferredHeight: 50
                                        radius: 11
                                        color: AppTheme.surfaceRaised
                                        border.color: AppTheme.border

                                        Column {
                                            anchors.fill: parent
                                            anchors.margins: 8
                                            spacing: 5
                                            Row {
                                                width: parent.width
                                                Text {
                                                    width: parent.width * 0.58
                                                    text: lang.language === "tr" ? "Kategori boyutu" : "Category size"
                                                    color: AppTheme.textMuted
                                                    font.pixelSize: Math.round(9 * root.categoryTextScale)
                                                }
                                                Text {
                                                    width: parent.width * 0.42
                                                    text: root.galleryFormatBytes(root.mediaGalleryBytes)
                                                    color: AppTheme.text
                                                    font.pixelSize: Math.round(9 * root.categoryTextScale)
                                                    font.weight: Font.DemiBold
                                                    horizontalAlignment: Text.AlignRight
                                                }
                                            }
                                            Rectangle {
                                                width: parent.width
                                                height: 7
                                                radius: 4
                                                color: AppTheme.surfaceHover
                                                clip: true
                                                Rectangle {
                                                    width: Math.max(root.mediaGalleryBytes > 0 ? 5 : 0,
                                                                    parent.width * root.mediaGalleryStorageShare)
                                                    height: parent.height
                                                    radius: parent.radius
                                                    color: AppTheme.accent
                                                }
                                            }
                                        }
                                    }

                                    Rectangle {
                                        visible: root.categoryKey === "videos" && root.dlnaViewActive
                                                 && mediaSourceHeader.width >= 850
                                        Layout.preferredWidth: 150
                                        Layout.preferredHeight: 42
                                        radius: 11
                                        color: AppTheme.surfaceRaised
                                        border.color: AppTheme.border
                                        RowLayout {
                                            anchors.fill: parent
                                            anchors.margins: 8
                                            spacing: 8
                                            Rectangle {
                                                Layout.preferredWidth: 28
                                                Layout.preferredHeight: 28
                                                radius: 9
                                                color: AppTheme.accentSoft
                                                Text {
                                                    anchors.centerIn: parent
                                                    text: (dlnaMedia.selectedProvider || "D").substring(0, 1)
                                                    color: AppTheme.accent
                                                    font.pixelSize: 12
                                                    font.weight: Font.Bold
                                                }
                                            }
                                            ColumnLayout {
                                                Layout.fillWidth: true
                                                spacing: 0
                                                Text {
                                                    Layout.fillWidth: true
                                                    text: dlnaMedia.selectedProvider || "DLNA"
                                                    color: AppTheme.text
                                                    font.pixelSize: 9
                                                    font.weight: Font.DemiBold
                                                    elide: Text.ElideRight
                                                }
                                                Text {
                                                    Layout.fillWidth: true
                                                    text: dlnaMedia.loading
                                                          ? (lang.language === "tr" ? "Yükleniyor…" : "Loading…")
                                                          : (lang.language === "tr" ? "Doğrudan oynat" : "Direct play")
                                                    color: AppTheme.textMuted
                                                    font.pixelSize: 7
                                                    elide: Text.ElideRight
                                                }
                                            }
                                        }
                                    }

                                }

                                GToolButton {
                                    id: localMediaSettingsButton
                                    anchors.right: parent.right
                                    anchors.rightMargin: 12
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 38
                                    height: 38
                                    icon.source: AppTheme.icon("settings.svg")
                                    icon.width: 16
                                    icon.height: 16
                                    onClicked: mediaGalleryView.openSettings()
                                    background: Rectangle {
                                        radius: 10
                                        color: localMediaSettingsButton.hovered ? AppTheme.accentSoft : AppTheme.surfaceRaised
                                        border.width: 1
                                        border.color: localMediaSettingsButton.hovered ? AppTheme.accentBorder : AppTheme.border
                                    }
                                    ToolTip.visible: hovered
                                    ToolTip.text: root.categoryKey === "videos"
                                                  ? (lang.language === "tr" ? "Video Ayarları" : "Video Settings")
                                                  : (lang.language === "tr" ? "Resim Ayarları" : "Image Settings")
                                }
                            }

                            MediaGalleryView {
                                id: mediaGalleryView
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                visible: !root.dlnaViewActive || root.categoryKey !== "videos"
                                files: root.galleryFilesForView()
                                lang: root.lang
                                categoryKey: root.categoryKey
                                categoryIcon: root.categoryKey === "videos" ? "video.svg" : "image.svg"
                                gallerySize: root.categoryIconSize
                                contentIndexModel: root.contentIndexModel
                                hostWindow: root.hostWindow
                                selectionRevision: root.selectionRevision
                                isSelected: function(url) { return root.isSelected(url) }
                                onItemPressed: function(modelIndex, modifiers) { root.selectIndex(modelIndex, modifiers) }
                                onBrowseRequested: function(location) {
                                    if (location && location.length)
                                        root.navigateTo(location)
                                }
                                onZoomRequested: function(delta) { root.adjustIconSize(delta) }
                            }

                            DlnaMediaView {
                                id: dlnaMediaView
                                singleClickOpen: root.categorySingleClickOpen
                                gallerySize: root.categoryIconSize
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                visible: root.categoryKey === "videos" && root.dlnaViewActive
                                manager: dlnaMedia
                                lang: root.lang
                                hostWindow: root.hostWindow
                                searchText: root.searchVisible ? searchInput.text : ""
                                sortMode: directory.sortMode
                                sortAscending: directory.sortAscending
                                Component.onCompleted: root.activeDlnaNavigator = dlnaMediaView
                                Component.onDestruction: {
                                    if (root.activeDlnaNavigator === dlnaMediaView)
                                        root.activeDlnaNavigator = null
                                }
                            }
                        }
                    }
                }
            }

            Item {
                id: groupedCategoryPage

                Rectangle {
                    id: musicCategoryHeader
                    visible: root.groupedCategory
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: visible ? (root.categoryKey === "music" ? 42 + musicTabs.implicitHeight + 44 : 52) : 0
                    radius: 12
                    color: AppTheme.surface
                    border.color: AppTheme.border

                    RowLayout {
                        id: categoryHeaderTopRow
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.leftMargin: 14
                        anchors.rightMargin: 56
                        height: root.categoryKey === "music" ? 42 : 50
                        spacing: 10
                        Rectangle {
                            Layout.preferredWidth: 34
                            Layout.preferredHeight: 34
                            radius: 9
                            color: AppTheme.accentSoft
                            border.color: AppTheme.accentBorder
                            CrispIcon {
                                anchors.centerIn: parent
                                width: 22
                                height: 22
                                source: AppTheme.discoveryIcon(AppTheme.categoryDefaultIcon(root.categoryKey),
                                                               AppTheme.categoryDefaultIcon(root.categoryKey))
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            spacing: 1
                            Text {
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignLeft
                                text: root.contentIndexModel
                                      ? root.contentIndexModel.titleForCategory(root.categoryKey, lang.language)
                                      : root.categoryKey
                                elide: Text.ElideRight
                                color: AppTheme.text
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                            }
                            Text {
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignLeft
                                text: root.categoryKey === "music"
                                      ? ((root.musicLibraryTracks || []).length
                                         + (lang.language === "tr" ? " parça" : " tracks"))
                                      : (lang.language === "tr"
                                         ? "Bu kategori yalnızca seçtiğin dizinleri ve kuralları indeksler"
                                         : "This category indexes only its selected folders and rules")
                                color: AppTheme.textMuted
                                font.pixelSize: 8
                                elide: Text.ElideRight
                            }
                        }
                    }

                    GToolButton {
                        id: musicSettingsButton
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        anchors.top: parent.top
                        anchors.topMargin: root.categoryKey === "music" ? 4 : 8
                        width: 36
                        height: 36
                        icon.source: AppTheme.icon("settings.svg")
                        icon.width: 16
                        icon.height: 16
                        onClicked: categorySettingsPopup.open()
                        background: Rectangle {
                            radius: 9
                            color: musicSettingsButton.hovered ? AppTheme.accentSoft : AppTheme.surfaceRaised
                            border.width: 1
                            border.color: musicSettingsButton.hovered ? AppTheme.accentBorder : AppTheme.border
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: (root.contentIndexModel
                                       ? root.contentIndexModel.titleForCategory(root.categoryKey, lang.language)
                                       : root.categoryKey) + (lang.language === "tr" ? " Ayarları" : " Settings")
                    }

                    Flow {
                        id: musicTabs
                        objectName: "musicTabs"
                        visible: root.categoryKey === "music"
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: categoryHeaderTopRow.bottom
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 5
                        Repeater {
                            model: root.musicTabLabels
                            delegate: GButton {
                                required property string modelData
                                required property int index
                                height: 30
                                width: Math.max(70, tabLabel.implicitWidth + 20)
                                padding: 0
                                checked: root.musicLibraryTab === index
                                onClicked: root.musicLibraryTab = index
                                background: Rectangle {
                                    radius: 8
                                    color: parent.checked ? AppTheme.accentSoft
                                                          : (parent.hovered ? AppTheme.surfaceHover : "transparent")
                                    border.width: parent.checked ? 1 : 0
                                    border.color: parent.checked ? AppTheme.accentBorder : "transparent"
                                }
                                contentItem: Text {
                                    id: tabLabel
                                    text: modelData
                                    color: parent.checked ? AppTheme.accent : AppTheme.textMuted
                                    font.pixelSize: 10
                                    font.weight: parent.checked ? Font.DemiBold : Font.Medium
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                        }
                    }

                    RowLayout {
                        visible: root.categoryKey === "music"
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        height: 41
                        spacing: 6

                        GTextField {
                            id: musicFilterField
                            Layout.fillWidth: true
                            Layout.minimumWidth: 90
                            Layout.preferredHeight: 32
                            placeholderText: lang.language === "tr" ? "Müzikte filtrele…" : "Filter music…"
                            text: root.musicLibraryFilter
                            onTextChanged: root.musicLibraryFilter = text
                        }
                        GComboBox {
                            id: musicGenreCombo
                            objectName: "musicGenreCombo"
                            Layout.preferredWidth: 180
                            Layout.minimumWidth: 110
                            Layout.preferredHeight: 32
                            model: root.musicGenreOptions
                            textRole: "text"
                            valueRole: "value"
                            function syncSelection() {
                                const key = musicLibraryView.genreKey(root.musicGenreFilter)
                                for (let i = 0; i < root.musicGenreOptions.length; ++i)
                                    if (musicLibraryView.genreKey(root.musicGenreOptions[i].value) === key) {
                                        currentIndex = i
                                        return
                                    }
                                currentIndex = 0
                            }
                            onModelChanged: Qt.callLater(syncSelection)
                            onActivated: root.selectMusicGenre(currentValue)
                        }
                        GComboBox {
                            id: musicYearCombo
                            Layout.preferredWidth: 92
                            Layout.preferredHeight: 32
                            model: root.musicYearOptions
                            function syncSelection() { currentIndex = Math.max(0, root.musicYearOptions.indexOf(root.musicYearFilter)) }
                            onModelChanged: Qt.callLater(syncSelection)
                            onActivated: root.musicYearFilter = currentIndex <= 0 ? "" : currentText
                        }
                    }
                }

                CategoryGroupedView {
                    id: groupedCategoryView
                    visible: root.categoryKey !== "music"
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: musicDockHostItem.visible ? musicDockHostItem.top : parent.bottom
                    anchors.bottomMargin: 0
                    anchors.top: musicCategoryHeader.visible ? musicCategoryHeader.bottom : parent.top
                    anchors.topMargin: musicCategoryHeader.visible ? 10 : 0
                    files: root.groupedCategory && root.visible ? root.groupedCategoryFiles : []
                    lang: root.lang
                    cardSize: root.categoryIconSize
                    selectionRevision: root.selectionRevision
                    isSelected: function(url) { return root.isSelected(url) }
                    inlineRenameUrl: root.inlineRenameUrl
                    onRenamePrepare: function(editor, itemName, itemIsDir) {
                        root.prepareInlineRenameEditor(editor, itemName, itemIsDir)
                    }
                    dateAscending: (directory.sortMode === "date" || directory.sortMode === "created")
                                   ? directory.sortAscending : false
                    wheelStep: AppTheme.wheelScrollStep
                    singleClickOpen: root.categorySingleClickOpen
                    onZoomRequested: function(delta) { root.adjustIconSize(delta) }
                    onItemPressed: function(modelIndex, modifiers) {
                        root.selectIndex(modelIndex, modifiers)
                    }
                    onItemActivated: function(modelIndex) {
                        const item = directory.itemAt(modelIndex)
                        if (!item)
                            return
                        if (root.categoryKey === "music" && !item.isDir) {
                            let artwork = ""
                            for (let i = 0; i < root.groupedCategoryFiles.length; ++i) {
                                const entry = root.groupedCategoryFiles[i]
                                if (Number(entry.modelIndex) === Number(modelIndex)) {
                                    artwork = String(entry.thumbnailSource || "")
                                    break
                                }
                            }
                            if (root.musicPlayer)
                                root.musicPlayer.openUrl(String(item.itemUrl || ""), String(item.name || ""),
                                                         artwork, root.groupedCategoryFiles)
                            return
                        }
                        root.openItem(String(item.itemUrl || ""), !!item.isDir,
                                      String(item.name || ""), String(item.mimeType || ""))
                    }
                    onContextRequested: function(sourceItem, modelIndex, x, y) {
                        root.selectIndex(modelIndex, Qt.NoModifier)
                        root.showContextMenu(itemContextMenu, sourceItem, x, y)
                    }
                    onRenameCommit: function(itemUrl, value, backwards, advance) {
                        if (advance)
                            root.commitInlineRenameAndAdvance(itemUrl, value, backwards)
                        else
                            root.commitInlineRename(itemUrl, value)
                    }
                    onRenameCancel: root.cancelInlineRename()
                }


                MusicLibraryView {
                    id: musicLibraryView
                    objectName: "musicLibraryView"
                    visible: root.categoryKey === "music"
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: musicDockHostItem.visible ? musicDockHostItem.top : parent.bottom
                    anchors.top: musicCategoryHeader.visible ? musicCategoryHeader.bottom : parent.top
                    anchors.topMargin: musicCategoryHeader.visible ? 10 : 0
                    lang: root.lang
                    manager: root.musicPlayer ? root.musicPlayer.libraryManager : null
                    player: root.musicPlayer
                    tabIndex: root.musicLibraryTab
                    query: ((root.searchVisible ? searchInput.text : "") + " " + root.musicLibraryFilter).trim()
                    genreFilter: root.musicGenreFilter
                    yearFilter: root.musicYearFilter
                    cardSize: root.musicIconSize
                    wheelStep: AppTheme.wheelScrollStep
                    sortMode: directory.sortMode
                    sortAscending: directory.sortAscending
                }


                // Visual docking slot for the global music player. The audio/player
                // object still lives in Main.qml so playback survives navigation,
                // but while the Music category is visible its bar is positioned
                // exactly over this slot. This makes the player part of the page
                // instead of a floating card and eliminates the dead gap above it.
                Item {
                    id: musicDockHostItem
                    readonly property bool embeddedPlayer: true
                    visible: root.categoryKey === "music"
                             && root.musicPlayer
                             && root.musicPlayer.visible
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: visible && root.musicPlayer ? root.musicPlayer.implicitHeight : 0
                }

            }
        }

        Item {
            id: directoryMusicDockHost
            readonly property bool embeddedPlayer: false
            visible: root.categoryKey !== "music" && root.musicPlayer && root.musicPlayer.visible
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? root.musicPlayer.implicitHeight : 0
        }

        Rectangle {
            visible: !directory.searchActive
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 30 : 0
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 18

                Text {
                    Layout.fillWidth: true
                    text: root.categoryKey === "videos" && root.dlnaViewActive
                          ? ""
                          : (root.selectedCount === 1
                          ? root.selectedName
                          : (root.selectedCount > 1
                             ? (lang.language === "tr" ? root.selectedCount + " öğe seçildi" : root.selectedCount + " items selected")
                             : ""))
                    color: AppTheme.text
                    font.pixelSize: 10
                    font.weight: Font.Medium
                    elide: Text.ElideMiddle
                    GToolTip {
                        visible: statusNameHover.hovered && root.selectedCount === 1
                                 && !(root.categoryKey === "videos" && root.dlnaViewActive)
                        text: root.selectedName
                        delay: 400
                    }
                    HoverHandler { id: statusNameHover }
                }

                Text {
                    visible: root.selectedCount === 1 && !root.selectedIsDir
                             && !(root.categoryKey === "videos" && root.dlnaViewActive)
                    text: directory.formatBytes(root.selectedSize)
                    color: AppTheme.textMuted
                    font.pixelSize: 10
                }

                Rectangle {
                    visible: root.selectedCount > 0 && directory.storageTotalBytes > 0
                             && !(root.categoryKey === "videos" && root.dlnaViewActive)
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 14
                    color: AppTheme.border
                }

                Rectangle {
                    id: storageUsageButton
                    visible: directory.storageTotalBytes > 0 && root.currentLocationIsLocal
                             && !(root.categoryKey === "videos" && root.dlnaViewActive)
                    Layout.preferredWidth: storageUsageText.implicitWidth + 14
                    Layout.preferredHeight: 24
                    radius: 8
                    color: storageUsageMouse.containsMouse || filelightPopup.opened ? AppTheme.surfaceHover : "transparent"
                    border.width: storageUsageMouse.containsMouse || filelightPopup.opened ? 1 : 0
                    border.color: AppTheme.accentBorder
                    Behavior on color { ColorAnimation { duration: 100 } }

                    Text {
                        id: storageUsageText
                        anchors.centerIn: parent
                        text: (lang.language === "tr" ? "Toplam " : "Total ")
                              + directory.formatBytes(directory.storageTotalBytes)
                              + (lang.language === "tr" ? " / Boş " : " / Free ")
                              + directory.formatBytes(directory.storageAvailableBytes)
                        color: storageUsageMouse.containsMouse ? AppTheme.text : AppTheme.textMuted
                        font.pixelSize: 10
                        font.weight: storageUsageMouse.containsMouse ? Font.Medium : Font.Normal
                    }

                    MouseArea {
                        id: storageUsageMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.openFilelightPopover(storageUsageButton)
                    }

                    GToolTip {
                        visible: storageUsageHover.hovered && !filelightPopup.opened
                        text: lang.language === "tr" ? "Filelight ile disk kullanımını incele" : "Inspect disk usage with Filelight"
                        delay: 450
                    }
                    HoverHandler { id: storageUsageHover }
                }

                Rectangle {
                    visible: directory.storageTotalBytes > 0
                             && !(root.categoryKey === "videos" && root.dlnaViewActive)
                    Layout.preferredWidth: visible ? 1 : 0
                    Layout.preferredHeight: 14
                    color: AppTheme.border
                }

                Text {
                    readonly property int visibleCount: root.categoryKey === "videos" && root.dlnaViewActive
                                                        ? (dlnaMedia && dlnaMedia.displayEntries ? dlnaMedia.displayEntries.length : 0)
                                                        : (root.mediaGalleryCategory && root.searchVisible
                                                           ? root.galleryFilesForView().length
                                                           : (root.groupedCategory && root.searchVisible
                                                              ? root.groupedCategoryFiles.length : directory.count))
                    text: lang.language === "tr"
                          ? visibleCount + " öğe"
                          : visibleCount + (visibleCount === 1 ? " item" : " items")
                    color: AppTheme.textMuted
                    font.pixelSize: 10
                    font.weight: Font.Medium
                }
            }
        }
    }

    GModalPopup {
        id: categorySettingsPopup
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(560, Math.max(400, root.width - 40))
        height: Math.min(580, Math.max(360, root.height - 40))
        padding: 0
        property bool musicBackgroundDraft: true
        property int musicQueueDraft: 50
        onOpened: {
            categoryRootsEditor.reload()
            if (root.categoryKey === "music") {
                if (root.musicPlayer) {
                    musicBackgroundDraft = root.musicPlayer.backgroundPlayback
                    musicQueueDraft = root.musicPlayer.queueLimit
                }
            }
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.topMargin: 22
                Layout.bottomMargin: 14
                spacing: 7
                Text {
                    Layout.fillWidth: true
                    text: (root.contentIndexModel
                           ? root.contentIndexModel.titleForCategory(root.categoryKey, lang.language)
                           : root.categoryKey) + (lang.language === "tr" ? " Ayarları" : " Settings")
                    color: AppTheme.text
                    font.pixelSize: 20
                    font.weight: Font.Bold
                }
                Text {
                    Layout.fillWidth: true
                    text: lang.language === "tr"
                          ? "Bu kategorinin dizinlerini, gizli dosya davranışını ve hariç tutma kurallarını buradan yönetebilirsin."
                          : "Manage this category's folders, hidden-file behavior, and ignore rules here."
                    color: AppTheme.textMuted
                    font.pixelSize: 10
                    wrapMode: Text.Wrap
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: AppTheme.border }

            ScrollView {
                id: categorySettingsScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ScrollBar.vertical.policy: ScrollBar.AsNeeded

                ColumnLayout {
                    width: Math.max(1, categorySettingsScroll.availableWidth)
                    spacing: 12
                    Item { Layout.preferredHeight: 4 }
                    MediaRootsEditor {
                        id: categoryRootsEditor
                        Layout.fillWidth: true
                        Layout.leftMargin: 24
                        Layout.rightMargin: 24
                        contentIndexModel: root.contentIndexModel
                        categoryKey: root.categoryKey
                        lang: root.lang
                        globalSingleClickOpen: AppTheme.singleClickOpen
                    }
                    Rectangle {
                        visible: root.categoryKey === "music"
                        Layout.fillWidth: true
                        Layout.leftMargin: 24
                        Layout.rightMargin: 24
                        Layout.preferredHeight: visible ? 1 : 0
                        color: AppTheme.border
                    }
                    ColumnLayout {
                        visible: root.categoryKey === "music"
                        Layout.fillWidth: true
                        Layout.leftMargin: 24
                        Layout.rightMargin: 24
                        spacing: 10

                        Text {
                            Layout.fillWidth: true
                            text: lang.language === "tr" ? "Müzik etiketleri" : "Music tags"
                            color: AppTheme.text
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                        }

                        Text {
                            Layout.fillWidth: true
                            text: lang.language === "tr"
                                  ? "Albüm, sanatçı, yıl ve tür bilgileri arka planda okunur. Taramayı durdurup daha sonra kaldığın yerden devam ettirebilirsin."
                                  : "Album, artist, year and genre tags are read in the background. You can pause the scan and resume it later."
                            color: AppTheme.textMuted
                            font.pixelSize: 9
                            wrapMode: Text.Wrap
                        }

                        GProgressBar {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 8
                            from: 0
                            to: 1
                            value: root.musicPlayer && root.musicPlayer.libraryManager
                                   ? root.musicPlayer.libraryManager.scanProgress : 0
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    Layout.fillWidth: true
                                    readonly property var library: root.musicPlayer ? root.musicPlayer.libraryManager : null
                                    text: {
                                        if (!library)
                                            return ""
                                        const done = Number(library.scanCompleted || 0)
                                        const total = Number(library.scanTotal || 0)
                                        const remaining = Number(library.scanRemaining || 0)
                                        if (library.loading && library.scanPaused)
                                            return lang.language === "tr"
                                                   ? "Durduruldu · " + done + " / " + total + " okundu · " + remaining + " kaldı"
                                                   : "Paused · " + done + " / " + total + " read · " + remaining + " remaining"
                                        if (library.loading)
                                            return lang.language === "tr"
                                                   ? "Etiketler okunuyor · " + done + " / " + total + " okundu · " + remaining + " kaldı"
                                                   : "Reading tags · " + done + " / " + total + " read · " + remaining + " remaining"
                                        if (total > 0)
                                            return lang.language === "tr"
                                                   ? "Tamamlandı · " + done + " / " + total + " okundu"
                                                   : "Completed · " + done + " / " + total + " read"
                                        return lang.language === "tr" ? "Taranacak parça yok" : "No tracks to scan"
                                    }
                                    color: library && library.scanPaused ? AppTheme.warning
                                           : (library && library.loading ? AppTheme.accent : AppTheme.textMuted)
                                    font.pixelSize: 9
                                    font.weight: Font.Medium
                                    elide: Text.ElideRight
                                }
                                Text {
                                    readonly property var library: root.musicPlayer ? root.musicPlayer.libraryManager : null
                                    text: library && Number(library.scanTotal || 0) > 0
                                          ? Math.round(Number(library.scanProgress || 0) * 100) + "%" : ""
                                    color: AppTheme.textFaint
                                    font.pixelSize: 8
                                }
                            }
                            GButton {
                                readonly property var library: root.musicPlayer ? root.musicPlayer.libraryManager : null
                                visible: library && library.loading
                                Layout.preferredWidth: 92
                                Layout.preferredHeight: 32
                                text: library && library.scanPaused
                                      ? (lang.language === "tr" ? "Devam et" : "Resume")
                                      : (lang.language === "tr" ? "Durdur" : "Pause")
                                onClicked: {
                                    if (!library) return
                                    if (library.scanPaused) library.resumeMetadataScan()
                                    else library.pauseMetadataScan()
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 1
                            color: AppTheme.border
                        }

                        Text {
                            Layout.fillWidth: true
                            text: lang.language === "tr" ? "Oynatıcı" : "Player"
                            color: AppTheme.text
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                        }

                        GCheckBox {
                            text: lang.language === "tr"
                                  ? "Arka planda oynat"
                                  : "Keep playing in background"
                            checked: categorySettingsPopup.musicBackgroundDraft
                            onToggled: categorySettingsPopup.musicBackgroundDraft = checked
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    text: lang.language === "tr" ? "Kuyruk boyutu" : "Queue size"
                                    color: AppTheme.text
                                    font.pixelSize: 10
                                    font.weight: Font.Medium
                                }
                                Text {
                                    text: lang.language === "tr"
                                          ? "Yeni kuyruk oluşturulurken en fazla kaç parça ekleneceğini belirler."
                                          : "Maximum tracks added when a new queue is generated."
                                    color: AppTheme.textMuted
                                    font.pixelSize: 8
                                    wrapMode: Text.Wrap
                                }
                            }
                            GSpinBox {
                                from: 5
                                to: 500
                                stepSize: 5
                                editable: true
                                value: categorySettingsPopup.musicQueueDraft
                                onValueModified: categorySettingsPopup.musicQueueDraft = value
                                Layout.preferredWidth: 112
                            }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        Layout.leftMargin: 24
                        Layout.rightMargin: 24
                        text: lang.language === "tr"
                              ? "Bir USB/CIFS/NFS yolu geçici olarak bağlı değilse ayarda kalır; yeniden bağlandığında sonraki taramada tekrar indexlenir."
                              : "Temporarily unavailable USB/CIFS/NFS paths stay configured and are indexed again after they reconnect."
                        color: AppTheme.textFaint
                        font.pixelSize: 9
                        wrapMode: Text.Wrap
                    }
                    Item { Layout.preferredHeight: 8 }
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: AppTheme.border }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 18
                Layout.rightMargin: 18
                Layout.topMargin: 12
                Layout.bottomMargin: 14
                Item { Layout.fillWidth: true }
                GModalButton {
                    text: lang.language === "tr" ? "İptal" : "Cancel"
                    onClicked: categorySettingsPopup.close()
                }
                GModalButton {
                    primary: true
                    text: lang.language === "tr" ? "Kaydet" : "Save"
                    onClicked: {
                        categoryRootsEditor.save()
                        if (root.categoryKey === "music") {
                            if (root.musicPlayer) {
                                root.musicPlayer.backgroundPlayback = categorySettingsPopup.musicBackgroundDraft
                                root.musicPlayer.queueLimit = categorySettingsPopup.musicQueueDraft
                                if (root.musicPlayer.opened && root.musicPlayer.queue.length > root.musicPlayer.queueLimit) {
                                    root.musicPlayer.queue = root.musicPlayer.queue.slice(0, root.musicPlayer.queueLimit)
                                    root.musicPlayer.currentIndex = Math.max(0, Math.min(root.musicPlayer.currentIndex, root.musicPlayer.queue.length - 1))
                                    root.musicPlayer.rebuildPlaybackOrder(root.musicPlayer.currentIndex)
                                }
                            }
                        }
                        categorySettingsPopup.close()
                    }
                }
            }
        }
    }

    Popup {
        id: filelightPopup
        parent: root
        popupType: Popup.Item
        modal: false
        dim: false
        focus: true
        width: 248
        height: filelightPopoverContent.implicitHeight + 28
        padding: 14
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        enter: Transition {
            ParallelAnimation {
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 130; easing.type: Easing.OutCubic }
                NumberAnimation { property: "scale"; from: 0.97; to: 1; duration: 150; easing.type: Easing.OutBack }
            }
        }
        exit: Transition {
            ParallelAnimation {
                NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 80; easing.type: Easing.InCubic }
                NumberAnimation { property: "scale"; from: 1; to: 0.985; duration: 80; easing.type: Easing.InCubic }
            }
        }

        background: Rectangle {
            radius: 16
            color: AppTheme.surface
            border.width: 1
            border.color: AppTheme.borderStrong
        }

        contentItem: ColumnLayout {
            id: filelightPopoverContent
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                spacing: 9

                Rectangle {
                    Layout.preferredWidth: 30
                    Layout.preferredHeight: 30
                    radius: 9
                    color: AppTheme.accentSoft
                    CrispIcon {
                        anchors.centerIn: parent
                        width: 18
                        height: 18
                        source: AppTheme.icon("filelight.svg")
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Text {
                        text: "Filelight"
                        color: AppTheme.text
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: lang.language === "tr" ? "Disk kullanımını incele" : "Inspect disk usage"
                        color: AppTheme.textMuted
                        font.pixelSize: 9
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: AppTheme.border }

            GModalButton {
                Layout.fillWidth: true
                text: lang.language === "tr" ? "Konumda Aç" : "Open Location"
                onClicked: {
                    filelightPopup.close()
                    fileOps.openFilelight(directory.location)
                }
            }

            GModalButton {
                visible: !root.currentLocationIsHome
                Layout.fillWidth: true
                Layout.preferredHeight: visible ? implicitHeight : 0
                text: lang.language === "tr" ? "Ev Dizininde Aç" : "Open Home Folder"
                onClicked: {
                    filelightPopup.close()
                    fileOps.openFilelightHome()
                }
            }

            GModalButton {
                Layout.fillWidth: true
                text: lang.language === "tr" ? "Aygıtta Aç" : "Open Device"
                onClicked: {
                    filelightPopup.close()
                    fileOps.openFilelightDevice(directory.location)
                }
            }
        }
    }

    Rectangle {
        id: loadingOverlay
        anchors.fill: parent
        z: 15000
        visible: root.loadingOverlayReady && directory.loading && directory.count === 0
        color: AppTheme.dark ? "#B3121720" : "#D9F7F8FC"

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(360, parent.width - 48)
            height: 132
            radius: 22
            color: AppTheme.surface
            border.width: 1
            border.color: AppTheme.borderStrong

            Column {
                anchors.centerIn: parent
                width: parent.width - 42
                spacing: 10
                GBusyIndicator {
                    anchors.horizontalCenter: parent.horizontalCenter
                    running: loadingOverlay.visible
                    width: 38
                    height: 38
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: lang.language === "tr" ? "Dizin hazırlanıyor…" : "Preparing folder…"
                    color: AppTheme.text
                    font.pixelSize: 13
                    font.bold: true
                }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: directory.displayLocation
                    color: AppTheme.textMuted
                    font.pixelSize: 10
                    elide: Text.ElideMiddle
                }
            }
        }
    }

    TextEditMenu {
        id: textEditContextMenu
        parent: root
        editor: root.textEditTarget
        langManager: lang
        onClosed: {
            // Keep the selection/caret in the editor, but forget the menu target
            // after all trigger handlers have finished.
            Qt.callLater(function() {
                if (!textEditContextMenu.opened)
                    root.textEditTarget = null
            })
        }
    }

    GMenu {
        id: breadcrumbContextMenu
        parent: root
        GMenuItem {
            text: lang.language === "tr" ? "Aç" : "Open"
            onTriggered: root.navigateTo(root.breadcrumbContextLocation)
        }
        GMenuItem {
            text: lang.language === "tr" ? "Yeni sekmede aç" : "Open in New Tab"
            onTriggered: root.requestNewTab(root.breadcrumbContextLocation)
        }
        GMenuSeparator {}
        GMenuItem {
            text: lang.language === "tr" ? "Yolu kopyala" : "Copy Path"
            onTriggered: {
                fileOps.copyLocationToClipboard(root.breadcrumbContextLocation)
                root.requestToast(lang.language === "tr" ? "Yol kopyalandı" : "Path copied")
            }
        }
        GMenuItem {
            text: lang.language === "tr" ? "Terminali burada aç" : "Open Terminal Here"
            enabled: root.breadcrumbContextLocation.startsWith("/")
                     || root.breadcrumbContextLocation.startsWith("file://")
            onTriggered: fileOps.openTerminal(root.breadcrumbContextLocation)
        }
    }

    // FileOperations tracks Shift through QApplication key events, so this
    // stays reactive even while the pointer is completely still.
    Binding {
        target: root
        property: "contextShiftDelete"
        value: itemContextMenu.opened && fileOps.shiftPressed
    }

    GMenu {
        id: itemContextMenu
        parent: root
        onOpened: {
            root.submenuActivationReady = false
            copyActionsMenu.close()
            applicationServiceMenu.close()
            compressMenu.close()
            shareMenu.close()
            fileActionsMenu.close()
            openWithMenu.close()
            root.prepareOpenWithSelection()
            fileOps.refreshShareTargets()
            submenuArmTimer.restart()
        }
        onClosed: {
            root.submenuActivationReady = false
            copyActionsMenu.close()
            applicationServiceMenu.close()
            compressMenu.close()
            shareMenu.close()
            fileActionsMenu.close()
            openWithMenu.close()
        }

        GMenuItem {
            text: lang.t("open")
            enabled: root.selectedCount > 0
            onTriggered: root.openSelected()
        }

        GMenuItem {
            visible: (directory.searchActive || root.isCategoryLocation) && root.selectedCount === 1
                     && root.selectedUrl.length > 0
            text: lang.language === "tr" ? "Yolu aç" : "Open Containing Folder"
            onTriggered: root.openSelectedContainingFolder(false)
        }
        GMenuItem {
            visible: (directory.searchActive || root.isCategoryLocation) && root.selectedCount === 1
                     && root.selectedUrl.length > 0
            text: lang.language === "tr" ? "Yolu yeni sekmede aç" : "Open Containing Folder in New Tab"
            onTriggered: root.openSelectedContainingFolder(true)
        }

        GMenuItem {
            visible: root.selectedCount === 1 && root.selectedLinkType.length > 0
            text: lang.language === "tr" ? "Hedefi göster" : "Show Target"
            onTriggered: root.showSelectedLinkTarget(false)
        }
        GMenuItem {
            visible: root.selectedCount === 1 && root.selectedLinkType.length > 0
            text: lang.language === "tr" ? "Hedefi yeni sekmede göster" : "Show Target in New Tab"
            onTriggered: root.showSelectedLinkTarget(true)
        }

        GMenu {
            id: openWithMenu
            title: lang.t("open_with")
            // Do not tie submenu activation to the asynchronously rebuilt app
            // model. If local files are selected, Open With is always usable;
            // “More…” can populate/launch the complete chooser even when the
            // quick list is empty.
            visible: root.selectedCount > 0
                     && !root.selectedIsDir
                     && !root.selectedIsCloud
                     && !String(root.selectedUrl).startsWith("admin:")
            enabled: true
            onAboutToShow: root.prepareOpenWithSelection()

            Instantiator {
                model: openWithModel.quickApps
                delegate: GMenuItem {
                    required property var modelData
                    text: modelData.name + (modelData.isDefault ? (lang.language === "tr" ? "  · Varsayılan" : "  · Default") : "")
                    onTriggered: {
                        root.closeContextMenus()
                        openWithModel.openWith(modelData.desktopEntry)
                    }
                }
                onObjectAdded: function(index, object) { openWithMenu.insertItem(index, object) }
                onObjectRemoved: function(index, object) { openWithMenu.removeItem(object) }
            }

            GMenuSeparator { visible: openWithModel.quickApps.length > 0 }
            GMenuItem {
                text: lang.language === "tr" ? "Daha Fazla…" : "More…"
                onTriggered: {
                    root.closeContextMenus()
                    root.openWithDialogForSelection()
                }
            }
        }

        GMenuItem {
            visible: root.selectedCount === 1
                     && root.selectedIsDir
                     && root.selectedLocalPath.length > 0
                     && !root.selectedUrl.startsWith("admin:")
                     && !root.selectedIsCloud
            text: lang.t("open_as_administrator")
            onTriggered: {
                const adminUrl = root.administratorUrlForSelection()
                if (adminUrl.length > 0)
                    root.requestNewTab(adminUrl)
            }
        }

        GMenuItem {
            visible: root.selectedCount === 1 && !root.selectedIsDir && root.selectedIsCloud
            text: lang.language === "tr" ? "İndir ve aç" : "Download and open"
            onTriggered: root.cloudManagerFor(root.selectedUrl).downloadAndOpen(root.selectedUrl, root.selectedName, root.selectedMimeType)
        }
        GMenuItem {
            visible: root.selectedCount === 1 && !root.selectedIsDir && root.selectedIsCloud
            text: lang.language === "tr" ? "İndir…" : "Download…"
            onTriggered: cloudDownloadFolderDialog.open()
        }
        GMenuItem {
            visible: root.selectedCount === 1 && root.selectedIsDir && root.selectedIsCloud
            text: lang.language === "tr" ? "ZIP olarak indir…" : "Download as ZIP…"
            onTriggered: cloudDownloadFolderDialog.open()
        }

        GMenuItem {
            visible: root.selectedCount === 1 && !root.selectedIsDir && root.selectedLocalPath.length > 0
                     && root.isAppImage(root.selectedName, root.selectedMimeType)
            text: lang.language === "tr" ? "Çalıştır" : "Run"
            onTriggered: fileOps.runExecutable(root.selectedUrl)
        }

        GMenuSeparator {}

        GMenuItem {
            text: (lang.language === "tr" ? "Kes" : "Cut") + root.shortcutSuffix("cut")
            enabled: root.selectedCount > 0
            onTriggered: root.cutSelection()
        }
        GMenuItem {
            text: (lang.language === "tr" ? "Kopyala" : "Copy") + root.shortcutSuffix("copy")
            enabled: root.selectedCount > 0
            onTriggered: root.copySelection()
        }
        GMenu {
            id: copyActionsMenu
            title: lang.language === "tr" ? "Kopyalama Eylemleri" : "Copy Actions"
            enabled: root.selectedCount > 0 && root.submenuActivationReady

            GMenuItem {
                text: (lang.language === "tr" ? "Symlink Oluştur…" : "Create Symlink…") + root.shortcutSuffix("symlink")
                enabled: root.selectedCanCreateLink
                onTriggered: root.showNewSymlinkDialog(true)
            }
            GMenuItem {
                text: (lang.language === "tr" ? "Hardlink Oluştur…" : "Create Hardlink…") + root.shortcutSuffix("hardlink")
                enabled: root.selectedCanCreateLink && !root.selectedIsDir
                         && root.selectedLinkType !== "symlink"
                onTriggered: root.showNewHardlinkDialog(true)
            }
            GMenuSeparator {}
            GMenuItem {
                text: lang.t("duplicate") + root.shortcutSuffix("duplicate")
                enabled: root.selectedCount > 0
                onTriggered: root.duplicateSelection()
            }
        }
        GMenuItem {
            visible: root.selectedCount === 1 && root.selectedIsDir && !root.selectedIsCloud
            text: (lang.language === "tr" ? "Klasöre yapıştır" : "Paste into folder") + root.shortcutSuffix("paste_into")
            enabled: fileOps.canPaste
            onTriggered: root.pasteIntoSelection()
        }

        GMenuSeparator { visible: serviceMenuModel.favoriteEntries.length > 0 }

        // Use a dense, already-filtered favorite list here. Inserting delegates
        // from the full ServiceMenu model left "ghost" slots whenever an
        // inapplicable favorite (for example MediaInfo on a .js file) was
        // absent from the current context.
        Instantiator {
            model: serviceMenuModel.favoriteEntries
            delegate: GMenuItem {
                required property var modelData
                text: "★  " + modelData.label
                onTriggered: serviceMenuModel.triggerKey(modelData.actionKey)
            }
            onObjectAdded: function(index, object) { itemContextMenu.insertItem(index + 5, object) }
            onObjectRemoved: function(index, object) { itemContextMenu.removeItem(object) }
        }

        GMenuItem {
            text: root.selectedCount > 1
                  ? (lang.language === "tr" ? "Toplu yeniden adlandır…" : "Batch rename…")
                  : lang.t("rename")
            enabled: root.selectedCount > 0
                     && (root.selectedCount === 1 || !root.isCloudUrl(directory.location))
            onTriggered: root.renameSelection()
        }

        GMenu {
            id: shareMenu
            title: lang.language === "tr" ? "Paylaş" : "Share"
            property var uploadUrls: root.localUploadSelectionUrls()
            property bool hasDirectory: root.shareSelectionContainsDirectory()
            enabled: uploadUrls.length > 0 && uploadUrls.length === root.selectedCount
                     && root.submenuActivationReady

            property bool hasCloudTarget:
                root.cloudShareTargetAvailable("google")
                || root.cloudShareTargetAvailable("onedrive")
                || root.cloudShareTargetAvailable("dropbox")
                || root.cloudShareTargetAvailable("nextcloud")
                || root.cloudShareTargetAvailable("owncloud")
                || root.cloudShareTargetAvailable("mega")
                || root.cloudShareTargetAvailable("pcloud")
                || root.cloudShareTargetAvailable("webdav")

            GMenuItem {
                visible: shareMenu.uploadUrls.length > 0 && root.cloudShareTargetAvailable("google")
                text: lang.language === "tr" ? "Google Drive'a gönder" : "Send to Google Drive"
                onTriggered: root.googleDrive.uploadPaths(shareMenu.uploadUrls, "gdrive://root")
            }

            GMenuItem {
                visible: shareMenu.uploadUrls.length > 0 && root.cloudShareTargetAvailable("onedrive")
                text: lang.language === "tr" ? "OneDrive'a gönder" : "Send to OneDrive"
                onTriggered: root.oneDrive.uploadPaths(shareMenu.uploadUrls, "onedrive://root")
            }

            GMenuItem {
                visible: shareMenu.uploadUrls.length > 0 && root.cloudShareTargetAvailable("dropbox")
                text: lang.language === "tr" ? "Dropbox'a gönder" : "Send to Dropbox"
                onTriggered: root.sendSelectionToConfiguredCloud("dropbox", shareMenu.uploadUrls)
            }

            GMenuItem {
                visible: shareMenu.uploadUrls.length > 0 && root.cloudShareTargetAvailable("nextcloud")
                text: lang.language === "tr" ? "Nextcloud'a gönder" : "Send to Nextcloud"
                onTriggered: root.sendSelectionToConfiguredCloud("nextcloud", shareMenu.uploadUrls)
            }

            GMenuItem {
                visible: shareMenu.uploadUrls.length > 0 && root.cloudShareTargetAvailable("owncloud")
                text: lang.language === "tr" ? "ownCloud'a gönder" : "Send to ownCloud"
                onTriggered: root.sendSelectionToConfiguredCloud("owncloud", shareMenu.uploadUrls)
            }

            GMenuItem {
                visible: shareMenu.uploadUrls.length > 0 && root.cloudShareTargetAvailable("mega")
                text: lang.language === "tr" ? "MEGA'ya gönder" : "Send to MEGA"
                onTriggered: root.sendSelectionToConfiguredCloud("mega", shareMenu.uploadUrls)
            }

            GMenuItem {
                visible: shareMenu.uploadUrls.length > 0 && root.cloudShareTargetAvailable("pcloud")
                text: lang.language === "tr" ? "pCloud'a gönder" : "Send to pCloud"
                onTriggered: root.sendSelectionToConfiguredCloud("pcloud", shareMenu.uploadUrls)
            }

            GMenuItem {
                visible: shareMenu.uploadUrls.length > 0 && root.cloudShareTargetAvailable("webdav")
                text: lang.language === "tr" ? "WebDAV'a gönder" : "Send to WebDAV"
                onTriggered: root.sendSelectionToConfiguredCloud("webdav", shareMenu.uploadUrls)
            }

            GMenuSeparator {
                visible: shareMenu.hasCloudTarget
                         && (fileOps.kdeConnectAvailable || fileOps.bluetoothAvailable
                             || fileOps.emailShareAvailable)
            }

            Instantiator {
                model: fileOps.kdeConnectDevices
                delegate: GMenuItem {
                    required property var modelData
                    text: lang.language === "tr"
                          ? "KDE Connect ile " + modelData.name + "'e gönder"
                          : "Send to " + modelData.name + " with KDE Connect"
                    onTriggered: fileOps.shareViaKdeConnect(shareMenu.uploadUrls, modelData.id)
                }
                onObjectAdded: function(index, object) { shareMenu.insertItem(index + 9, object) }
                onObjectRemoved: function(index, object) { shareMenu.removeItem(object) }
            }

            GMenuItem {
                visible: fileOps.kdeConnectAvailable && fileOps.kdeConnectDevices.length === 0
                enabled: false
                text: lang.language === "tr" ? "Bağlı KDE Connect cihazı yok" : "No connected KDE Connect device"
            }

            GMenuItem {
                visible: fileOps.bluetoothAvailable && !shareMenu.hasDirectory
                text: lang.language === "tr" ? "Bluetooth ile gönder…" : "Send via Bluetooth…"
                onTriggered: fileOps.shareViaBluetooth(shareMenu.uploadUrls)
            }

            GMenuItem {
                visible: fileOps.emailShareAvailable && !shareMenu.hasDirectory
                text: lang.language === "tr" ? "E-posta ile gönder…" : "Send via Email…"
                onTriggered: fileOps.shareViaEmail(shareMenu.uploadUrls)
            }

            GMenuSeparator { visible: root.selectedCount === 1 }

            GMenuItem {
                visible: root.selectedCount === 1
                text: lang.language === "tr" ? "Yolu kopyala" : "Copy Path"
                onTriggered: {
                    fileOps.copyLocationToClipboard(root.selectedUrl)
                    root.requestToast(lang.language === "tr" ? "Yol kopyalandı" : "Path copied")
                }
            }
        }

        GMenu {
            id: applicationServiceMenu
            title: lang.t("application_actions")
            enabled: serviceMenuModel.count > 0 && root.submenuActivationReady
            visible: serviceMenuModel.count > 0

            Instantiator {
                model: serviceMenuModel
                delegate: GMenuItem {
                    required property int index
                    required property string label
                    required property string actionKey
                    required property bool favorite
                    text: label
                    favoriteActionVisible: true
                    favoriteActionChecked: favorite
                    onFavoriteActionClicked: serviceMenuModel.toggleFavoriteKey(actionKey)
                    onTriggered: serviceMenuModel.triggerKey(actionKey)
                }
                onObjectAdded: function(index, object) { applicationServiceMenu.insertItem(index, object) }
                onObjectRemoved: function(index, object) { applicationServiceMenu.removeItem(object) }
            }
        }

        GMenu {
            id: compressMenu
            title: lang.t("compress")
            property var sourceUrls: root.compressionSelectionUrls()
            enabled: sourceUrls.length > 0
                     && sourceUrls.length === root.selectedCount
            GMenuItem { text: "Zip"; onTriggered: fileOps.compressItems(compressMenu.sourceUrls, "zip") }
            GMenuItem { text: "7z"; onTriggered: fileOps.compressItems(compressMenu.sourceUrls, "7z") }
            GMenuSeparator {}
            GMenuItem { text: "Tar"; onTriggered: fileOps.compressItems(compressMenu.sourceUrls, "tar") }
            GMenuItem { text: "Tar.gz"; onTriggered: fileOps.compressItems(compressMenu.sourceUrls, "tar.gz") }
            GMenuItem { text: "Tar.bz2"; onTriggered: fileOps.compressItems(compressMenu.sourceUrls, "tar.bz2") }
            GMenuItem { text: "Tar.xz"; onTriggered: fileOps.compressItems(compressMenu.sourceUrls, "tar.xz") }
            GMenuItem { text: "Tar.zst"; onTriggered: fileOps.compressItems(compressMenu.sourceUrls, "tar.zst") }
        }

        GMenuSeparator { visible: root.selectedIsArchive }

        GMenuItem {
            visible: root.selectedIsArchive
            text: lang.language === "tr" ? "Buraya çıkar" : "Extract here"
            onTriggered: {
                root.armResultSelectionForChangedItems(directory.location)
                fileOps.extractArchive(root.selectedUrl, directory.location, false)
            }
        }
        GMenuItem {
            visible: root.selectedIsArchive
            text: lang.language === "tr" ? "Adıyla yeni dizine çıkar" : "Extract to named folder"
            onTriggered: {
                root.armResultSelectionForChangedItems(directory.location)
                fileOps.extractArchive(root.selectedUrl, directory.location, true)
            }
        }
        GMenuItem {
            visible: root.selectedIsArchive
            text: lang.language === "tr" ? "Dizine çıkar…" : "Extract to folder…"
            onTriggered: {
                root.pendingArchiveUrl = root.selectedUrl
                archiveDestinationDialog.open()
            }
        }

        GMenuItem {
            visible: root.selectedCount === 1 && root.selectedIsDir
            text: favoritesModel.contains(root.selectedUrl) ? lang.t("remove_favorite") : lang.t("add_favorite")
            onTriggered: {
                if (favoritesModel.contains(root.selectedUrl))
                    favoritesModel.removeFavorite(root.selectedUrl)
                else
                    favoritesModel.addFavorite(root.selectedUrl)
            }
        }

        GMenu {
            id: fileActionsMenu
            title: lang.language === "tr" ? "Eylemler" : "Actions"
            enabled: root.selectedCount > 0 && root.submenuActivationReady

            GMenuItem {
                text: lang.t("copy_to")
                onTriggered: {
                    root.actionMode = "copy"
                    if (root.selectedIsCloud) {
                        root.drivePickerAction = "copy"
                        driveFolderPickerModel.location = directory.location
                        driveFolderPicker.open()
                    } else {
                        destinationInput.text = directory.parentLocation
                        destinationDialog.open()
                    }
                }
            }

            GMenuItem {
                text: lang.t("move_to")
                onTriggered: {
                    root.actionMode = "move"
                    if (root.selectedIsCloud) {
                        root.drivePickerAction = "move"
                        driveFolderPickerModel.location = directory.location
                        driveFolderPicker.open()
                    } else {
                        destinationInput.text = directory.parentLocation
                        destinationDialog.open()
                    }
                }
            }
        }

        GMenuSeparator {}

        GMenuItem {
            visible: root.isTrashLocation && root.selectedCount > 0
            text: lang.language === "tr" ? "Eski konumuna taşı" : "Restore to former location"
            onTriggered: fileOps.restoreFromTrash(root.selectedUrls)
        }

        GMenuItem {
            text: (root.isTrashLocation || root.contextShiftDelete)
                  ? lang.t("delete_permanently") : lang.t("move_trash")
            enabled: root.selectedCount > 0
            onTriggered: {
                if (root.isTrashLocation || root.contextShiftDelete)
                    root.deleteSelection()
                else
                    root.trashSelection()
            }
        }
        GMenuSeparator { visible: root.selectedCount === 1 }

        GMenuItem {
            visible: root.selectedCount === 1
            text: lang.t("properties")
            onTriggered: root.openPropertiesForSelection()
        }
    }

    GMenu {
        id: backgroundContextMenu
        maximumPopupHeight: Math.max(180, Math.min(620, root.height - 24))
        parent: root
        closePolicy: Popup.CloseOnEscape
        onOpened: {
                sortMenu.close()
                newItemMenu.close()
        }
        onClosed: {
                sortMenu.close()
                newItemMenu.close()
        }

        GMenu {
            id: newItemMenu
            closePolicy: Popup.CloseOnEscape
            title: lang.language === "tr" ? "Yeni" : "New"
            enabled: !root.isCategoryLocation && !root.isTrashLocation

            GMenuItem {
                text: lang.t("new_folder")
                onTriggered: root.showNewFolderDialog()
            }
            GMenuItem {
                visible: !root.isCloudUrl(directory.location)
                text: lang.language === "tr" ? "Yeni dosya…" : "New file…"
                onTriggered: root.showNewFileDialog()
            }
            GMenuSeparator { visible: root.currentLocationIsLocal }
            GMenuItem {
                visible: root.currentLocationIsLocal
                text: lang.language === "tr" ? "Yeni symlink…" : "New symlink…"
                onTriggered: root.showNewSymlinkDialog()
            }
            GMenuItem {
                visible: root.currentLocationIsLocal
                text: lang.language === "tr" ? "Yeni hardlink…" : "New hardlink…"
                onTriggered: root.showNewHardlinkDialog()
            }
        }

        GMenuItem {
            visible: !root.isCloudUrl(directory.location) && !root.isCategoryLocation
            text: (lang.language === "tr" ? "Yapıştır" : "Paste") + root.shortcutSuffix("paste")
            enabled: fileOps.canPaste
            onTriggered: root.pasteHere()
        }

        GMenuItem {
            visible: root.isCloudUrl(directory.location)
            text: lang.language === "tr" ? "Dosya yükle…" : "Upload file…"
            onTriggered: cloudUploadDialog.open()
        }

        GMenuItem {
            visible: root.administratorUrlForCurrentLocation().length > 0
            text: lang.t("open_as_administrator")
            onTriggered: {
                const adminUrl = root.administratorUrlForCurrentLocation()
                if (adminUrl.length > 0)
                    root.requestNewTab(adminUrl)
            }
        }

        GMenuSeparator {}
        GMenuItem { text: lang.t("select_all") + root.shortcutSuffix("select_all"); onTriggered: root.selectAll() }
        GMenuItem { text: lang.t("clear_selection") + root.shortcutSuffix("clear_selection"); enabled: root.selectedCount > 0; onTriggered: root.clearSelection() }

        GMenuSeparator {}

        GMenu {
            id: sortMenu
            closePolicy: Popup.CloseOnEscape
            title: lang.t("sort_by")
            GMenuItem { text: lang.t("sort_name"); checkable: true; checked: directory.sortMode === "name"; onTriggered: directory.sortMode = "name" }
            GMenuItem { text: lang.t("sort_date"); checkable: true; checked: directory.sortMode === "date"; onTriggered: directory.sortMode = "date" }
            GMenuItem {
                text: root.categoryKey === "videos" && root.dlnaViewActive && lang.language === "tr"
                      ? "Son eklenen" : lang.t("sort_created")
                checkable: true
                checked: directory.sortMode === "created"
                onTriggered: {
                    directory.sortMode = "created"
                    if (root.categoryKey === "videos" && root.dlnaViewActive)
                        directory.sortAscending = false
                }
            }
            GMenuItem { text: lang.t("sort_size"); checkable: true; checked: directory.sortMode === "size"; onTriggered: directory.sortMode = "size" }
            GMenuItem { text: lang.t("sort_type"); checkable: true; checked: directory.sortMode === "type"; onTriggered: directory.sortMode = "type" }
            GMenuSeparator {}
            GMenuItem { text: lang.t("ascending"); checkable: true; checked: directory.sortAscending; onTriggered: directory.sortAscending = true }
            GMenuItem { text: lang.t("descending"); checkable: true; checked: !directory.sortAscending; onTriggered: directory.sortAscending = false }
        }

        GMenuSeparator {}
        GMenuItem { text: lang.t("refresh"); onTriggered: root.refreshDirectory() }
    }

    GModalPopup {
        id: openWithPopup
        parent: Overlay.overlay
        modal: true
        focus: true
        anchors.centerIn: parent
        width: 430
        height: 420
        padding: 18
        background: GModalSurface { }

        ColumnLayout {
            anchors.fill: parent
            spacing: 12

            Text { text: lang.t("open_with"); font.pixelSize: 17; font.bold: true; color: AppTheme.text }
            Text {
                text: root.selectedCount > 1
                      ? (lang.language === "tr" ? root.selectedCount + " öğe seçili" : root.selectedCount + " items selected")
                      : root.selectedName
                color: AppTheme.textMuted
                font.pixelSize: 11
                elide: Text.ElideMiddle
                Layout.fillWidth: true
            }

            ListView {
                id: openWithList
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: openWithModel
                spacing: 5
                clip: true
                delegate: GButton {
                    required property string name
                    required property string iconName
                    required property string desktopEntry
                    required property bool isDefault
                    width: openWithList.width
                    height: 48
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    onClicked: {
                        openWithModel.openWith(desktopEntry)
                        openWithPopup.close()
                    }
                    background: Rectangle { radius: 10; color: parent.hovered ? AppTheme.accentSoft : AppTheme.surfaceRaised; border.color: AppTheme.border }
                    contentItem: RowLayout {
                        spacing: 10
                        CrispIcon {
                            Layout.preferredWidth: 26
                            Layout.preferredHeight: 26
                            source: AppTheme.systemIcon(iconName || "application-x-executable")
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                        }
                        Text { text: name; color: AppTheme.text; font.pixelSize: 12; Layout.fillWidth: true; elide: Text.ElideRight }
                        Text { visible: isDefault; text: lang.t("default_app"); color: AppTheme.accent; font.pixelSize: 9; font.bold: true }
                    }
                }
            }

            Text {
                visible: openWithModel.count === 0
                text: lang.t("no_compatible_apps")
                color: AppTheme.textMuted
                font.pixelSize: 11
            }

            GModalButton {
                Layout.alignment: Qt.AlignRight
                text: lang.t("cancel")
                onClicked: openWithPopup.close()
            }
        }
    }

    GModalPopup {
        id: propertiesDialog
        parent: Overlay.overlay
        modal: true
        focus: true
        anchors.centerIn: parent
        width: Math.min(620, Overlay.overlay.width - 40)
        height: Math.min(root.selectedIsDir ? 720 : 700, Overlay.overlay.height - 40)
        padding: 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onOpened: {
            openWithModel.setContext(root.selectedUrl, root.selectedMimeType)
            if (root.selectedLocalPath.length > 0) {
                fileProperties.inspect(root.selectedUrl)
                linkTargetInput.text = fileProperties.linkTarget
            } else {
                linkTargetInput.text = ""
            }
            Qt.callLater(function() {
                defaultAppCombo.currentIndex = openWithModel.indexForDesktopEntry(openWithModel.defaultDesktopEntry)
            })
        }
        background: GModalSurface { }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 22
            spacing: 16

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 118
                radius: 18
                color: AppTheme.surfaceRaised
                border.color: AppTheme.border

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 15

                    Rectangle {
                        Layout.preferredWidth: 72
                        Layout.preferredHeight: 72
                        radius: 18
                        color: AppTheme.accentSoft
                        border.color: AppTheme.accentBorder
                        CrispIcon {
                            anchors.centerIn: parent
                            source: root.iconForItem(root.selectedIsDir, root.selectedSuffix,
                                                     root.selectedIsDir && root.selectedLocalPath.length > 0
                                                     ? fileProperties.iconName : root.selectedSystemIconName,
                                                     root.selectedName)
                            width: 48
                            height: 48
                        }
                        MouseArea {
                            anchors.fill: parent
                            visible: root.selectedIsDir && root.selectedLocalPath.length > 0
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                const chosen = iconPicker.chooseSystemIcon(fileProperties.iconName || "folder",
                                    lang.language === "tr" ? "Klasör ikonu seç" : "Choose folder icon")
                                if (chosen && chosen.length)
                                    fileProperties.setFolderIcon(chosen)
                            }
                        }
                        GToolTip {
                            visible: folderIconHover.hovered && root.selectedIsDir && root.selectedLocalPath.length > 0
                            text: lang.language === "tr" ? "İkonu değiştir" : "Change icon"
                        }
                        HoverHandler { id: folderIconHover }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        Text {
                            text: lang.language === "tr" ? "ÖZELLİKLER" : "PROPERTIES"
                            color: AppTheme.accent
                            font.pixelSize: 9
                            font.bold: true
                            font.letterSpacing: 1.1
                        }
                        Text {
                            text: root.selectedName
                            color: AppTheme.text
                            font.pixelSize: 19
                            font.weight: Font.DemiBold
                            Layout.fillWidth: true
                            elide: Text.ElideMiddle
                        }
                        Text {
                            text: root.selectedIsDir
                                  ? (lang.language === "tr" ? "Klasör" : "Folder")
                                  : (root.selectedMimeType || (lang.language === "tr" ? "Dosya" : "File"))
                            color: AppTheme.textMuted
                            font.pixelSize: 11
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                        Text {
                            visible: root.selectedIsDir && root.selectedLocalPath.length > 0
                            text: lang.language === "tr" ? "Klasör ikonunu değiştirmek için simgeye tıkla" : "Click the icon to customize this folder"
                            color: AppTheme.accent
                            font.pixelSize: 9
                        }
                    }

                    GToolButton {
                        id: propertiesCloseButton
                        Layout.alignment: Qt.AlignTop
                        implicitWidth: 34
                        implicitHeight: 34
                        text: ""
                        display: AbstractButton.IconOnly
                        icon.source: AppTheme.icon("close-ui.svg")
                        icon.width: 16
                        icon.height: 16
                        onClicked: propertiesDialog.close()
                        background: Rectangle {
                            radius: 10
                            color: propertiesCloseButton.hovered ? AppTheme.surfaceHover : "transparent"
                        }
                    }
                }
            }

            ScrollView {
                id: propertiesScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth
                rightPadding: 12
                ScrollBar.vertical.policy: ScrollBar.AsNeeded

                ColumnLayout {
                    width: propertiesScroll.availableWidth
                    spacing: 14

                    Text {
                        text: lang.language === "tr" ? "GENEL BİLGİLER" : "GENERAL"
                        color: AppTheme.textMuted
                        font.pixelSize: 10
                        font.bold: true
                        font.letterSpacing: 0.8
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: propertiesGrid.implicitHeight + 34
                        radius: 16
                        color: AppTheme.surfaceRaised
                        border.color: AppTheme.border

                        GridLayout {
                            id: propertiesGrid
                            anchors.fill: parent
                            anchors.margins: 17
                            columns: 2
                            columnSpacing: 18
                            rowSpacing: 12

                            Text { text: lang.t("location"); color: AppTheme.textMuted; font.pixelSize: 11; Layout.preferredWidth: 105 }
                            Text { text: root.selectedLocalPath.length ? root.selectedLocalPath : root.selectedUrl; color: AppTheme.text; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideMiddle }

                            Text { text: lang.t("size"); color: AppTheme.textMuted; font.pixelSize: 11 }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 7
                                GBusyIndicator { visible: root.selectedIsDir && fileProperties.calculating; running: visible; Layout.preferredWidth: 18; Layout.preferredHeight: 18 }
                                Text {
                                    text: root.selectedLocalPath.length > 0
                                          ? (fileProperties.calculating
                                             ? (lang.language === "tr" ? "Hesaplanıyor…" : "Calculating…")
                                             : fileProperties.formatBytes(fileProperties.totalSize))
                                          : directory.formatBytes(root.selectedSize)
                                    color: AppTheme.text; font.pixelSize: 11
                                }
                            }

                            Text { visible: root.selectedIsDir && root.selectedLocalPath.length > 0; text: lang.language === "tr" ? "İçerik" : "Contents"; color: AppTheme.textMuted; font.pixelSize: 11 }
                            Text {
                                visible: root.selectedIsDir && root.selectedLocalPath.length > 0
                                text: fileProperties.calculating ? "…" : (fileProperties.fileCount + (lang.language === "tr" ? " dosya · " : " files · ") + fileProperties.folderCount + (lang.language === "tr" ? " klasör" : " folders"))
                                color: AppTheme.text; font.pixelSize: 11
                            }

                            Text { visible: root.selectedLocalPath.length > 0; text: lang.language === "tr" ? "Oluşturma" : "Created"; color: AppTheme.textMuted; font.pixelSize: 11 }
                            Text { visible: root.selectedLocalPath.length > 0; text: fileProperties.created && !isNaN(fileProperties.created.getTime()) ? Qt.formatDateTime(fileProperties.created, "dd.MM.yyyy HH:mm") : "—"; color: AppTheme.text; font.pixelSize: 11 }

                            Text { text: lang.t("modified"); color: AppTheme.textMuted; font.pixelSize: 11 }
                            Text { text: root.selectedLocalPath.length > 0 && fileProperties.modified ? Qt.formatDateTime(fileProperties.modified, "dd.MM.yyyy HH:mm") : (root.selectedModified ? Qt.formatDateTime(root.selectedModified, "dd.MM.yyyy HH:mm") : "—"); color: AppTheme.text; font.pixelSize: 11 }

                            Text { visible: root.selectedLocalPath.length > 0; text: lang.language === "tr" ? "Son erişim" : "Last accessed"; color: AppTheme.textMuted; font.pixelSize: 11 }
                            Text { visible: root.selectedLocalPath.length > 0; text: fileProperties.accessed && !isNaN(fileProperties.accessed.getTime()) ? Qt.formatDateTime(fileProperties.accessed, "dd.MM.yyyy HH:mm") : "—"; color: AppTheme.text; font.pixelSize: 11 }

                            Text { visible: root.selectedLocalPath.length > 0; text: lang.language === "tr" ? "İzinler" : "Permissions"; color: AppTheme.textMuted; font.pixelSize: 11 }
                            Text { visible: root.selectedLocalPath.length > 0; text: fileProperties.permissionsText.length ? fileProperties.permissionsText : "—"; color: AppTheme.text; font.pixelSize: 11 }

                            Text { text: lang.t("mime_type"); color: AppTheme.textMuted; font.pixelSize: 11 }
                            Text { text: root.selectedMimeType || "—"; color: AppTheme.text; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight }
                        }
                    }

                    Text {
                        visible: root.selectedLocalPath.length > 0 && fileProperties.linkType.length > 0
                        text: lang.language === "tr" ? "BAĞLANTI" : "LINK"
                        color: AppTheme.textMuted
                        font.pixelSize: 10
                        font.bold: true
                        font.letterSpacing: 0.8
                    }

                    Rectangle {
                        visible: root.selectedLocalPath.length > 0 && fileProperties.linkType.length > 0
                        Layout.fillWidth: true
                        Layout.preferredHeight: linkDetails.implicitHeight + 30
                        radius: 16
                        color: AppTheme.surfaceRaised
                        border.color: AppTheme.border

                        ColumnLayout {
                            id: linkDetails
                            anchors.fill: parent
                            anchors.margins: 15
                            spacing: 10

                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    text: lang.language === "tr" ? "Tür" : "Type"
                                    color: AppTheme.textMuted
                                    font.pixelSize: 11
                                    Layout.preferredWidth: 105
                                }
                                Text {
                                    text: root.linkTypeText(fileProperties.linkType)
                                    color: AppTheme.text
                                    font.pixelSize: 11
                                    font.weight: Font.DemiBold
                                    Layout.fillWidth: true
                                }
                            }

                            RowLayout {
                                visible: fileProperties.linkType === "hardlink"
                                Layout.fillWidth: true
                                Text { text: "Inode"; color: AppTheme.textMuted; font.pixelSize: 11; Layout.preferredWidth: 105 }
                                Text { text: String(fileProperties.inode); color: AppTheme.text; font.pixelSize: 11; Layout.fillWidth: true }
                            }
                            RowLayout {
                                visible: fileProperties.linkType === "hardlink"
                                Layout.fillWidth: true
                                Text { text: lang.language === "tr" ? "Bağ sayısı" : "Link count"; color: AppTheme.textMuted; font.pixelSize: 11; Layout.preferredWidth: 105 }
                                Text { text: String(fileProperties.linkCount); color: AppTheme.text; font.pixelSize: 11; Layout.fillWidth: true }
                            }

                            Text {
                                visible: fileProperties.linkType === "hardlink"
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: lang.language === "tr"
                                      ? "Hardlink'in tek bir hedefi yoktur; tüm adlar aynı inode'a eşittir. Aşağıya başka bir dosya seçersen bu dizin girdisi o dosyanın inode'una yeniden bağlanır."
                                      : "A hard link has no single target; every name is an equal reference to the same inode. Enter another file below to re-link this directory entry to that file's inode."
                                color: AppTheme.textFaint
                                font.pixelSize: 10
                            }

                            RowLayout {
                                visible: fileProperties.linkType !== "hardlink"
                                Layout.fillWidth: true
                                Text { text: lang.language === "tr" ? "Hedef" : "Target"; color: AppTheme.textMuted; font.pixelSize: 11; Layout.preferredWidth: 105 }
                                Text {
                                    text: fileProperties.linkTarget.length ? fileProperties.linkTarget : "—"
                                    color: AppTheme.text
                                    font.pixelSize: 11
                                    Layout.fillWidth: true
                                    elide: Text.ElideMiddle
                                }
                            }

                            RowLayout {
                                visible: fileProperties.linkType === "symlink"
                                Layout.fillWidth: true
                                Text { text: lang.language === "tr" ? "Hedef durumu" : "Target status"; color: AppTheme.textMuted; font.pixelSize: 11; Layout.preferredWidth: 105 }
                                Text {
                                    text: fileProperties.linkTargetExists
                                          ? (lang.language === "tr" ? "Mevcut" : "Available")
                                          : (lang.language === "tr" ? "Bulunamadı / kırık bağlantı" : "Missing / broken link")
                                    color: fileProperties.linkTargetExists ? AppTheme.success : AppTheme.danger
                                    font.pixelSize: 11
                                    font.weight: Font.DemiBold
                                }
                            }

                            GTextField {
                                id: linkTargetInput
                                Layout.fillWidth: true
                                placeholderText: fileProperties.linkType === "hardlink"
                                                 ? (lang.language === "tr" ? "Yeni bağlanacak dosyanın yolu" : "Path of the file to link to")
                                                 : (lang.language === "tr" ? "Yeni hedef konumu veya URL" : "New target path or URL")
                                selectByMouse: true
                            }

                            GModalButton {
                                Layout.alignment: Qt.AlignRight
                                primary: true
                                enabled: linkTargetInput.text.trim().length > 0
                                text: fileProperties.linkType === "hardlink"
                                      ? (lang.language === "tr" ? "Başka dosyaya bağla" : "Link to another file")
                                      : (lang.language === "tr" ? "Hedefi değiştir" : "Change target")
                                onClicked: {
                                    if (fileProperties.setLinkTarget(linkTargetInput.text)) {
                                        linkTargetInput.text = fileProperties.linkTarget
                                        directory.requestRefresh()
                                        root.requestToast(lang.language === "tr" ? "Bağlantı hedefi güncellendi." : "Link target updated.")
                                    }
                                }
                            }
                        }
                    }

                    Text {
                        visible: !root.selectedIsDir
                        text: lang.language === "tr" ? "VARSAYILAN UYGULAMA" : "DEFAULT APPLICATION"
                        color: AppTheme.textMuted
                        font.pixelSize: 10
                        font.bold: true
                        font.letterSpacing: 0.8
                    }

                    Rectangle {
                        visible: !root.selectedIsDir
                        Layout.fillWidth: true
                        Layout.preferredHeight: 128
                        radius: 16
                        color: AppTheme.surfaceRaised
                        border.color: AppTheme.border

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 10
                            GComboBox {
                                id: defaultAppCombo
                                Layout.fillWidth: true
                                model: openWithModel
                                textRole: "name"
                                valueRole: "desktopEntry"
                            }
                            GModalButton {
                                id: setDefaultButton
                                primary: true
                                Layout.fillWidth: true
                                enabled: defaultAppCombo.currentIndex >= 0
                                text: lang.t("set_default_app")
                                onClicked: {
                                    if (openWithModel.setDefaultApplication(defaultAppCombo.currentValue)) {
                                        root.requestToast(lang.t("default_app_changed"))
                                        propertiesDialog.close()
                                    }
                                }
                            }
                        }
                    }

                    Item { Layout.preferredHeight: 1 }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: root.selectedIsDir && root.selectedLocalPath.length > 0
                          ? (lang.language === "tr" ? "Simgeyi başlıktan değiştirebilirsin" : "You can change the icon from the header")
                          : ""
                    color: AppTheme.textFaint
                    font.pixelSize: 9
                }
                GModalButton {
                    id: propertiesDoneButton
                    primary: true
                    text: lang.t("close")
                    onClicked: propertiesDialog.close()
                }
            }
        }
    }

    GModalPopup {
        id: renameDialog
        property bool batchMode: false
        property var targetUrls: []
        property string errorText: ""
        parent: Overlay.overlay
        modal: true
        focus: true
        anchors.centerIn: parent
        width: Math.min(520, Overlay.overlay.width - 40)
        height: batchMode ? 410 : 250
        padding: 22
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onOpened: Qt.callLater(function() {
            renameInput.forceActiveFocus()
            if (!renameDialog.batchMode && !root.selectedIsDir) {
                const name = renameInput.text
                const dot = name.lastIndexOf(".")
                // Keep the extension outside the selection, like Dolphin.
                // Dotfiles such as .bashrc have no filename extension here.
                if (dot > 0)
                    renameInput.select(0, dot)
                else
                    renameInput.selectAll()
            } else {
                renameInput.selectAll()
            }
        })
        background: GModalSurface { }
        ColumnLayout {
            anchors.fill: parent
            spacing: 14

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Rectangle {
                    Layout.preferredWidth: 42
                    Layout.preferredHeight: 42
                    radius: 12
                    color: AppTheme.accentSoft
                    Text {
                        anchors.centerIn: parent
                        text: renameDialog.batchMode ? "#" : "✎"
                        color: AppTheme.accent
                        font.pixelSize: 20
                        font.bold: true
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        text: renameDialog.batchMode
                              ? (lang.language === "tr" ? "Toplu yeniden adlandır" : "Batch rename")
                              : lang.t("rename")
                        color: AppTheme.text
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: renameDialog.batchMode
                              ? renameDialog.targetUrls.length + (lang.language === "tr" ? " öğe seçildi" : " items selected")
                              : root.selectedName
                        color: AppTheme.textMuted
                        font.pixelSize: 10
                        Layout.fillWidth: true
                        elide: Text.ElideMiddle
                    }
                }
            }

            Text {
                text: renameDialog.batchMode
                      ? (lang.language === "tr" ? "Ad kalıbı" : "Name pattern")
                      : (lang.language === "tr" ? "Yeni ad" : "New name")
                color: AppTheme.textMuted
                font.pixelSize: 10
                font.bold: true
            }
            GTextField {
                id: renameInput
                Layout.fillWidth: true
                selectByMouse: true
                onTextChanged: renameDialog.errorText = ""
                onAccepted: root.commitRename()
                background: Rectangle {
                    radius: 11
                    color: AppTheme.surfaceRaised
                    border.width: renameInput.activeFocus ? 2 : 1
                    border.color: renameInput.activeFocus ? AppTheme.accent : AppTheme.border
                }
            }

            RowLayout {
                visible: renameDialog.batchMode
                Layout.fillWidth: true
                Text {
                    text: lang.language === "tr" ? "Başlangıç numarası" : "Start number"
                    color: AppTheme.textMuted
                    font.pixelSize: 11
                    Layout.fillWidth: true
                }
                GSpinBox {
                    id: batchRenameStart
                    from: 0
                    to: 999999
                    value: 1
                    editable: true
                    Layout.preferredWidth: 120
                }
            }

            Rectangle {
                visible: renameDialog.batchMode
                Layout.fillWidth: true
                Layout.preferredHeight: 102
                radius: 13
                color: AppTheme.surfaceRaised
                border.color: AppTheme.border
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 5
                    Text {
                        text: lang.language === "tr" ? "ÖNİZLEME" : "PREVIEW"
                        color: AppTheme.textMuted
                        font.pixelSize: 9
                        font.bold: true
                        font.letterSpacing: 0.7
                    }
                    Text {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        text: root.batchRenamePreviewText()
                        color: AppTheme.text
                        font.pixelSize: 10
                        lineHeight: 1.25
                        elide: Text.ElideRight
                    }
                }
            }

            Text {
                visible: renameDialog.batchMode
                text: lang.language === "tr"
                      ? "# numarayı gösterir; ### kullanırsan 001, 002 biçiminde ilerler. Dosya uzantıları korunur."
                      : "# inserts the number; ### produces 001, 002, and so on. File extensions are preserved."
                color: AppTheme.textMuted
                font.pixelSize: 9
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }

            Text {
                visible: renameDialog.errorText.length > 0
                text: renameDialog.errorText
                color: AppTheme.danger
                font.pixelSize: 10
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }

            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignRight
                Item { Layout.fillWidth: true }
                GModalButton {
                    id: renameCancelButton
                    text: lang.t("cancel")
                    onClicked: renameDialog.close()
                }
                GModalButton {
                    id: renameSaveButton
                    primary: true
                    text: renameDialog.batchMode
                          ? (lang.language === "tr" ? "Tümünü yeniden adlandır" : "Rename all")
                          : lang.t("save")
                    enabled: renameInput.text.trim().length > 0
                    onClicked: root.commitRename()
                }
            }
        }
    }

    GModalPopup {
        id: newFolderDialog
        parent: Overlay.overlay
        modal: true
        focus: true
        anchors.centerIn: parent
        width: 360
        height: 170
        padding: 18
        background: GModalSurface { }

        function createFolder(allowHidden) {
            const folderName = newFolderInput.text.trim()
            if (!folderName.length)
                return false
            if (!allowHidden && root.needsHiddenNameConfirmation(folderName, "")) {
                root.requestHiddenNameConfirmation("create_folder", folderName, directory.location)
                return false
            }

            root.armResultSelectionForNames([folderName], directory.location)
            if (directory.location.startsWith("gdrive://")) {
                directory.googleCreateFolder(folderName)
                newFolderDialog.close()
            } else if (directory.location.startsWith("onedrive://")) {
                directory.oneDriveCreateFolder(folderName)
                newFolderDialog.close()
            } else if (fileOps.createFolder(directory.location, folderName)) {
                newFolderDialog.close()
            } else {
                root.clearPendingResultSelection()
                root.requestToast(lang.localizeMessage(fileOps.lastError))
                return false
            }
            return true
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 12
            Text { text: lang.t("new_folder"); font.pixelSize: 16; font.bold: true; color: AppTheme.text }
            GTextField {
                id: newFolderInput
                Layout.fillWidth: true
                onAccepted: newFolderDialog.createFolder()
            }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                GModalButton { text: lang.t("cancel"); onClicked: newFolderDialog.close() }
                GModalButton {
                    primary: true
                    text: lang.t("create")
                    enabled: newFolderInput.text.trim().length > 0
                    onClicked: newFolderDialog.createFolder()
                }
            }
        }
    }

    GModalPopup {
        id: newFileDialog
        parent: Overlay.overlay
        modal: true
        focus: true
        anchors.centerIn: parent
        width: 360
        height: 170
        padding: 18
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: GModalSurface { }

        function createFile(allowHidden) {
            const fileName = newFileInput.text.trim()
            if (!allowHidden && root.needsHiddenNameConfirmation(fileName, "")) {
                root.requestHiddenNameConfirmation("create_file", fileName, directory.location)
                return false
            }
            if (fileOps.createFile(directory.location, fileName)) {
                newFileDialog.close()
                return true
            }
            return false
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 12
            Text {
                text: lang.language === "tr" ? "Yeni dosya" : "New file"
                font.pixelSize: 16
                font.bold: true
                color: AppTheme.text
            }
            GTextField {
                id: newFileInput
                Layout.fillWidth: true
                onAccepted: newFileDialog.createFile()
            }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                GModalButton {
                    text: lang.t("cancel")
                    onClicked: newFileDialog.close()
                }
                GModalButton {
                    primary: true
                    text: lang.t("create")
                    onClicked: newFileDialog.createFile()
                }
            }
        }
    }

    GModalPopup {
        id: newSymlinkDialog
        property string destinationLocation: ""
        parent: Overlay.overlay
        modal: true
        focus: true
        anchors.centerIn: parent
        width: 460
        height: 245
        padding: 18
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: GModalSurface { }

        function createLink() {
            const name = newSymlinkNameInput.text.trim()
            const target = newSymlinkTargetInput.text.trim()
            if (!name.length || !target.length)
                return
            root.armResultSelectionForNames([name], destinationLocation)
            if (fileOps.createSymbolicLink(destinationLocation, name, target)) {
                newSymlinkDialog.close()
            } else {
                root.clearPendingResultSelection()
                root.requestToast(lang.localizeMessage(fileOps.lastError))
            }
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            Text {
                text: lang.language === "tr" ? "Yeni symlink" : "New symlink"
                color: AppTheme.text
                font.pixelSize: 16
                font.bold: true
            }
            GTextField {
                id: newSymlinkNameInput
                Layout.fillWidth: true
                placeholderText: lang.language === "tr" ? "Bağlantı adı" : "Link name"
                onAccepted: newSymlinkDialog.createLink()
            }
            GTextField {
                id: newSymlinkTargetInput
                Layout.fillWidth: true
                placeholderText: lang.language === "tr" ? "Hedef yol (mutlak veya göreli)" : "Target path (absolute or relative)"
                onAccepted: newSymlinkDialog.createLink()
            }
            Text {
                Layout.fillWidth: true
                text: lang.language === "tr"
                      ? "Symlink dosya veya klasöre gidebilir; hedef henüz yoksa da oluşturulabilir."
                      : "A symlink may point to a file or folder, and the target may not exist yet."
                color: AppTheme.textMuted
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                GModalButton { text: lang.t("cancel"); onClicked: newSymlinkDialog.close() }
                GModalButton {
                    primary: true
                    text: lang.t("create")
                    enabled: newSymlinkNameInput.text.trim().length > 0 && newSymlinkTargetInput.text.trim().length > 0
                    onClicked: newSymlinkDialog.createLink()
                }
            }
        }
    }

    GModalPopup {
        id: newHardlinkDialog
        property string destinationLocation: ""
        parent: Overlay.overlay
        modal: true
        focus: true
        anchors.centerIn: parent
        width: 460
        height: 245
        padding: 18
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: GModalSurface { }

        function createLink() {
            const name = newHardlinkNameInput.text.trim()
            const target = newHardlinkTargetInput.text.trim()
            if (!name.length || !target.length)
                return
            root.armResultSelectionForNames([name], destinationLocation)
            if (fileOps.createHardLink(destinationLocation, name, target)) {
                newHardlinkDialog.close()
            } else {
                root.clearPendingResultSelection()
                root.requestToast(lang.localizeMessage(fileOps.lastError))
            }
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            Text {
                text: lang.language === "tr" ? "Yeni hardlink" : "New hardlink"
                color: AppTheme.text
                font.pixelSize: 16
                font.bold: true
            }
            GTextField {
                id: newHardlinkNameInput
                Layout.fillWidth: true
                placeholderText: lang.language === "tr" ? "Bağlantı adı" : "Link name"
                onAccepted: newHardlinkDialog.createLink()
            }
            GTextField {
                id: newHardlinkTargetInput
                Layout.fillWidth: true
                placeholderText: lang.language === "tr" ? "Mevcut dosyanın yolu" : "Path to an existing file"
                onAccepted: newHardlinkDialog.createLink()
            }
            Text {
                Layout.fillWidth: true
                text: lang.language === "tr"
                      ? "Hardlink yalnız mevcut normal dosyaya ve aynı dosya sistemi içinde oluşturulabilir."
                      : "A hard link requires an existing regular file on the same filesystem."
                color: AppTheme.textMuted
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                GModalButton { text: lang.t("cancel"); onClicked: newHardlinkDialog.close() }
                GModalButton {
                    primary: true
                    text: lang.t("create")
                    enabled: newHardlinkNameInput.text.trim().length > 0 && newHardlinkTargetInput.text.trim().length > 0
                    onClicked: newHardlinkDialog.createLink()
                }
            }
        }
    }

    GModalPopup {
        id: driveFolderPicker
        parent: Overlay.overlay
        modal: true
        focus: true
        anchors.centerIn: parent
        width: 500
        height: 520
        padding: 18
        background: GModalSurface { }

        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            Text {
                text: root.drivePickerAction === "copy"
                    ? ((driveFolderPickerModel.location.startsWith("onedrive://") ? "OneDrive" : "Google Drive") + (lang.language === "tr" ? " hedef klasörü — Kopyala" : " destination — Copy"))
                    : ((driveFolderPickerModel.location.startsWith("onedrive://") ? "OneDrive" : "Google Drive") + (lang.language === "tr" ? " hedef klasörü — Taşı" : " destination — Move"))
                color: AppTheme.text
                font.pixelSize: 16
                font.bold: true
            }
            RowLayout {
                Layout.fillWidth: true
                GButton {
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    text: ""
                    display: AbstractButton.IconOnly
                    icon.source: AppTheme.icon("nav-up.svg")
                    icon.width: 16; icon.height: 16
                    enabled: driveFolderPickerModel.location !== "gdrive://root" && driveFolderPickerModel.location !== "onedrive://root"
                    onClicked: driveFolderPickerModel.location = driveFolderPickerModel.parentLocation
                }
                Text {
                    Layout.fillWidth: true
                    text: driveFolderPickerModel.displayLocation
                    color: AppTheme.textMuted
                    elide: Text.ElideMiddle
                    font.pixelSize: 11
                }
            }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: AppTheme.border }
            ListView {
                id: driveFolderList
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: driveFolderPickerModel
                spacing: 5
                clip: true
                delegate: Rectangle {
                    required property string name
                    required property string itemUrl
                    required property bool isDir
                    required property string systemIconName
                    visible: isDir
                    width: driveFolderList.width
                    height: isDir ? 48 : 0
                    radius: 11
                    color: folderPickerMouse.containsMouse ? AppTheme.accentSoft : AppTheme.surfaceRaised
                    border.color: folderPickerMouse.containsMouse ? AppTheme.accentBorder : AppTheme.border
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 10
                        CrispIcon {
                            source: root.iconForItem(true, "", systemIconName, name)
                            width: 25
                            height: 25
                        }
                        Text { text: name; color: AppTheme.text; font.pixelSize: 12; Layout.fillWidth: true; elide: Text.ElideRight }
                        Text { text: "›"; color: AppTheme.textMuted; font.pixelSize: 17 }
                    }
                    MouseArea {
                        id: folderPickerMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onDoubleClicked: driveFolderPickerModel.location = itemUrl
                    }
                }
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                GModalButton { text: lang.t("cancel"); onClicked: driveFolderPicker.close() }
                GModalButton {
                    primary: true
                    text: root.drivePickerAction === "copy" ? lang.t("copy") : lang.t("move")
                    onClicked: {
                        for (let i = 0; i < root.selectedUrls.length; ++i) {
                            const manager = root.cloudManagerFor(root.selectedUrls[i])
                            if (root.drivePickerAction === "copy")
                                manager.copyItem(root.selectedUrls[i], driveFolderPickerModel.location)
                            else
                                manager.moveItem(root.selectedUrls[i], driveFolderPickerModel.location)
                        }
                        driveFolderPicker.close()
                    }
                }
            }
        }
    }

    GModalPopup {
        id: destinationDialog
        parent: Overlay.overlay
        modal: true
        focus: true
        anchors.centerIn: parent
        width: 430
        height: 195
        padding: 18
        background: GModalSurface { }
        ColumnLayout {
            anchors.fill: parent
            spacing: 12
            Text { text: root.actionMode === "copy" ? lang.t("copy_to") : lang.t("move_to"); font.pixelSize: 16; font.bold: true; color: AppTheme.text }
            GTextField { id: destinationInput; Layout.fillWidth: true }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                GModalButton { text: lang.t("cancel"); onClicked: destinationDialog.close() }
                GModalButton {
                    primary: true
                    text: root.actionMode === "copy" ? lang.t("copy") : lang.t("move")
                    onClicked: {
                        if (root.isCloudUrl(directory.location)) {
                            const manager = root.currentCloudManager()
                            for (let i = 0; i < root.selectedUrls.length; ++i) {
                                if (root.actionMode === "copy")
                                    manager.copyItem(root.selectedUrls[i], destinationInput.text)
                                else
                                    manager.moveItem(root.selectedUrls[i], destinationInput.text)
                            }
                        } else {
                            if (root.selectedUrls.length <= 1) {
                                if (root.actionMode === "copy")
                                    fileOps.copyItem(root.selectedUrl, destinationInput.text)
                                else
                                    fileOps.moveItem(root.selectedUrl, destinationInput.text)
                            } else if (root.actionMode === "copy") {
                                fileOps.copyMany(root.selectedUrls, destinationInput.text)
                            } else {
                                fileOps.moveMany(root.selectedUrls, destinationInput.text)
                            }
                        }
                        destinationDialog.close()
                    }
                }
            }
        }
    }

    GModalPopup {
        id: archiveOverwriteDialog
        property bool proceeding: false
        parent: Overlay.overlay
        modal: true
        focus: true
        anchors.centerIn: parent
        width: 460
        height: 224
        padding: 18
        background: GModalSurface { }
        onAboutToShow: proceeding = false

        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            Text {
                text: lang.language === "tr" ? "Aynı adlı öğeler zaten var" : "Items with the same name already exist"
                font.pixelSize: 16
                font.bold: true
                color: AppTheme.text
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: AppTheme.textMuted
                font.pixelSize: 12
                text: {
                    const count = root.pendingArchiveConflicts ? root.pendingArchiveConflicts.length : 0
                    let preview = ""
                    const previewCount = Math.min(count, 3)
                    for (let i = 0; i < previewCount; ++i) {
                        if (i > 0) preview += "\n"
                        preview += root.pendingArchiveConflicts[i]
                    }
                    const more = count > 3 ? (lang.language === "tr" ? "\n… ve " + (count - 3) + " öğe daha" : "\n… and " + (count - 3) + " more") : ""
                    return (lang.language === "tr"
                            ? "Arşivdeki " + count + " öğe hedef dizinde zaten bulunuyor. Üzerine yazılsın mı?"
                            : count + " archive item(s) already exist in the destination. Overwrite them?")
                            + (preview.length ? "\n\n" + preview + more : "")
                }
            }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                GModalButton {
                    text: lang.t("cancel")
                    onClicked: {
                        root.clearPendingResultSelection()
                        archiveOverwriteDialog.close()
                    }
                }
                GModalButton {
                    danger: true
                    text: lang.language === "tr" ? "Üzerine yaz" : "Overwrite"
                    onClicked: {
                        archiveOverwriteDialog.proceeding = true
                        if (root.pendingArchiveConflictDestination === directory.location)
                            root.armResultSelectionForChangedItems(directory.location)
                        fileOps.extractArchiveOverwrite(root.pendingArchiveConflictSource,
                                                        root.pendingArchiveConflictDestination,
                                                        root.pendingArchiveConflictSubfolder)
                        archiveOverwriteDialog.close()
                    }
                }
            }
        }

        onClosed: {
            if (!proceeding)
                root.clearPendingResultSelection()
            proceeding = false
            root.pendingArchiveConflictSource = ""
            root.pendingArchiveConflictDestination = ""
            root.pendingArchiveConflictSubfolder = false
            root.pendingArchiveConflicts = []
        }
    }

    GModalPopup {
        id: confirmEmptyTrashDialog
        parent: Overlay.overlay
        modal: true
        focus: true
        anchors.centerIn: parent
        width: 410
        height: 190
        padding: 18
        background: GModalSurface { }

        ColumnLayout {
            anchors.fill: parent
            spacing: 12
            Text {
                text: lang.language === "tr" ? "Çöp boşaltılsın mı?" : "Empty the Trash?"
                font.pixelSize: 16
                font.bold: true
                color: AppTheme.text
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: lang.language === "tr"
                      ? "Çöpteki tüm öğeler kalıcı olarak silinecek. Bu işlem geri alınamaz."
                      : "All items in the Trash will be permanently deleted. This action cannot be undone."
                color: AppTheme.textMuted
                font.pixelSize: 12
            }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                GModalButton {
                    text: lang.t("cancel")
                    onClicked: confirmEmptyTrashDialog.close()
                }
                GModalButton {
                    danger: true
                    text: lang.language === "tr" ? "Çöpü Boşalt" : "Empty Trash"
                    onClicked: {
                        fileOps.emptyTrash()
                        confirmEmptyTrashDialog.close()
                    }
                }
            }
        }
    }

    GModalPopup {
        id: hiddenRenameDialog
        objectName: "hiddenRenameDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(460, parent.width - 32)
        implicitHeight: hiddenRenameContent.implicitHeight + topPadding + bottomPadding
        closePolicy: Popup.CloseOnEscape
        property string actionType: "rename"
        property string targetUrl: ""
        property string newName: ""
        onAboutToShow: dontAskHiddenAgain.checked = false
        onOpened: hiddenRenameCancelButton.forceActiveFocus()
        onClosed: {
            const finishedAction = actionType
            const finishedTarget = targetUrl
            targetUrl = ""
            dontAskHiddenAgain.checked = false
            Qt.callLater(function() {
                if (finishedAction === "create_folder" && newFolderDialog.visible)
                    newFolderInput.forceActiveFocus()
                else if (finishedAction === "create_file" && newFileDialog.visible)
                    newFileInput.forceActiveFocus()
                else if (finishedAction === "batch_rename" && renameDialog.visible)
                    renameInput.forceActiveFocus()
                else if (finishedAction === "rename" && root.inlineRenameUrl === finishedTarget
                         && root.inlineRenameEditor && root.inlineRenameEditor.visible)
                    root.inlineRenameEditor.forceActiveFocus()
            })
        }
        function confirmAction() {
            const skipNextTime = dontAskHiddenAgain.checked
            let accepted = false
            if (actionType === "rename" && targetUrl.length && root.inlineRenameUrl === targetUrl)
                accepted = root.commitInlineRename(targetUrl, newName, true)
            else if (targetUrl === directory.location) {
                if (actionType === "create_folder" && newFolderDialog.visible && newFolderInput.text.trim() === newName)
                    accepted = newFolderDialog.createFolder(true)
                else if (actionType === "create_file" && newFileDialog.visible && newFileInput.text.trim() === newName)
                    accepted = newFileDialog.createFile(true)
                else if (actionType === "batch_rename" && renameDialog.visible && renameInput.text.trim() === newName)
                    accepted = root.commitRename(true)
            }
            // Cancelling or rejecting an invalid operation must not save the opt-out.
            if (accepted && skipNextTime)
                AppTheme.setSkipHiddenNameConfirmation(true)
            close()
        }
        contentItem: ColumnLayout {
            id: hiddenRenameContent
            spacing: 16
            Text {
                Layout.fillWidth: true
                text: {
                    if (hiddenRenameDialog.actionType === "create_folder")
                        return lang.language === "tr" ? "Gizli klasör oluşturulsun mu?" : "Create a hidden folder?"
                    if (hiddenRenameDialog.actionType === "create_file")
                        return lang.language === "tr" ? "Gizli dosya oluşturulsun mu?" : "Create a hidden file?"
                    return lang.language === "tr" ? "Gizli ad kullanılarak yeniden adlandırılsın mı?" : "Rename using a hidden name?"
                }
                color: AppTheme.text
                font.pixelSize: 16
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: hiddenRenameName.implicitHeight + 24
                radius: 10
                color: AppTheme.surfaceRaised
                border.color: AppTheme.border
                Text {
                    id: hiddenRenameName
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    text: hiddenRenameDialog.newName
                    textFormat: Text.PlainText
                    wrapMode: Text.WrapAnywhere
                    maximumLineCount: 3
                    elide: Text.ElideMiddle
                    color: AppTheme.text
                    font.pixelSize: 14
                    font.weight: Font.Medium
                }
            }
            Text {
                Layout.fillWidth: true
                text: lang.language === "tr"
                      ? "Noktayla başlayan dosya ve klasörler gizlenir. Bunları görüntülemek için “Gizli dosyaları göster” seçeneğini açmak gerekebilir."
                      : "Files and folders with names beginning with a dot are hidden. You may need to enable “Show hidden files” to see them."
                color: AppTheme.textMuted
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                GCheckBox {
                    id: dontAskHiddenAgain
                    objectName: "dontAskHiddenAgain"
                    Layout.fillWidth: true
                    text: lang.t("dont_ask_again")
                    font.pixelSize: 12
                }
                Text {
                    Layout.fillWidth: true
                    text: lang.language === "tr"
                          ? "Dosya ve klasör oluşturma ile yeniden adlandırma için geçerlidir. Görünüm menüsünden değiştirilebilir."
                          : "Applies to file and folder creation and renaming. You can change this in View."
                    color: AppTheme.textMuted
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 8
                GModalButton {
                    id: hiddenRenameCancelButton
                    text: lang.t("cancel")
                    onClicked: hiddenRenameDialog.close()
                }
                GModalButton {
                    objectName: "confirmHiddenRenameButton"
                    primary: true
                    text: hiddenRenameDialog.actionType.startsWith("create_") ? lang.t("create") : lang.t("rename")
                    onClicked: hiddenRenameDialog.confirmAction()
                }
            }
        }
    }

    GModalPopup {
        id: confirmDeleteDialog
        parent: Overlay.overlay
        modal: true
        focus: true
        anchors.centerIn: parent
        width: 400
        height: 218
        padding: 18
        onOpened: dontAskDeleteAgain.checked = false
        onClosed: dontAskDeleteAgain.checked = false
        background: GModalSurface { }
        ColumnLayout {
            anchors.fill: parent
            spacing: 12
            Text { text: lang.t("delete_permanently") + "?"; font.pixelSize: 16; font.bold: true; color: AppTheme.text }
            Text {
                text: root.selectedCount > 1
                      ? (lang.language === "tr" ? root.selectedCount + " öğe" : root.selectedCount + " items")
                      : root.selectedName
                color: AppTheme.textMuted
                font.pixelSize: 12
            }

            GCheckBox {
                id: dontAskDeleteAgain
                text: lang.t("dont_ask_again")
                font.pixelSize: 12
                palette.windowText: AppTheme.textMuted
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                indicator: Rectangle {
                    implicitWidth: 18
                    implicitHeight: 18
                    x: dontAskDeleteAgain.leftPadding
                    y: parent.height / 2 - height / 2
                    radius: 5
                    color: dontAskDeleteAgain.checked ? AppTheme.accent : AppTheme.surfaceRaised
                    border.color: dontAskDeleteAgain.checked ? AppTheme.accent : AppTheme.borderStrong
                    border.width: 1
                    Text {
                        anchors.centerIn: parent
                        text: "✓"
                        visible: dontAskDeleteAgain.checked
                        color: "white"
                        font.pixelSize: 13
                        font.bold: true
                    }
                }
                contentItem: Text {
                    leftPadding: dontAskDeleteAgain.indicator.width + 8
                    text: dontAskDeleteAgain.text
                    color: AppTheme.textMuted
                    font: dontAskDeleteAgain.font
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                GModalButton { text: lang.t("cancel"); onClicked: confirmDeleteDialog.close() }
                GModalButton {
                    danger: true
                    text: lang.t("delete")
                    onClicked: {
                        // The preference only becomes permanent when the destructive
                        // action itself is confirmed. Cancelling never changes it.
                        if (dontAskDeleteAgain.checked)
                            AppTheme.setSkipPermanentDeleteConfirmation(true)
                        root.performPermanentDelete()
                        confirmDeleteDialog.close()
                    }
                }
            }
        }
    }
    FileDialog {
        id: cloudUploadDialog
        title: directory.location.startsWith("onedrive://") ? (lang.language === "tr" ? "OneDrive'a dosya yükle" : "Upload file to OneDrive") : (lang.language === "tr" ? "Google Drive'a dosya yükle" : "Upload file to Google Drive")
        fileMode: FileDialog.OpenFile
        onAccepted: root.currentCloudManager().uploadFile(selectedFile.toString(), directory.location)
    }

    FolderDialog {
        id: cloudDownloadFolderDialog
        title: root.selectedIsDir
               ? (lang.language === "tr" ? "ZIP'in kaydedileceği klasörü seç" : "Choose folder for ZIP")
               : (lang.language === "tr" ? "İndirilecek klasörü seç" : "Choose download folder")
        onAccepted: {
            const manager = root.cloudManagerFor(root.selectedUrl)
            if (!manager)
                return
            if (root.selectedIsDir)
                manager.downloadFolderAsZip(root.selectedUrl, root.selectedName, selectedFolder.toString())
            else
                manager.downloadToFolder(root.selectedUrl, root.selectedName, root.selectedMimeType, selectedFolder.toString())
        }
    }

    FolderDialog {
        id: archiveDestinationDialog
        title: lang.language === "tr" ? "Arşivin çıkarılacağı dizini seç" : "Choose extraction folder"
        onAccepted: {
            const target = selectedFolder.toString()
            if (target === directory.location)
                root.armResultSelectionForChangedItems(directory.location)
            fileOps.extractArchive(root.pendingArchiveUrl, target, false)
            root.pendingArchiveUrl = ""
        }
        onRejected: root.pendingArchiveUrl = ""
    }

    Column {
        id: operationCards
        visible: !AppTheme.kioProgressEnabled
        z: 30000
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 14
        anchors.bottomMargin: 42
        width: Math.min(350, root.width - 28)
        spacing: 8

        Repeater {
            model: Math.min(3, fileOps.operations.length)
            delegate: Rectangle {
                id: operationCard
                required property int index
                readonly property var operation: fileOps.operations[Math.max(0, fileOps.operations.length - 3) + index]
                readonly property real shownPercent: root.transferPercent(operation)
                property real completionCountdown: 1.0
                property int trackedOperationId: -1
                property bool completionInitialized: false
                width: operationCards.width
                height: operation.finished ? 108 : (operation.controllable === false ? 140 : 174)

                function syncCompletionState() {
                    const currentId = Number(operation && operation.id !== undefined ? operation.id : -1)
                    if (currentId !== trackedOperationId) {
                        completionCountdownAnimation.stop()
                        completionCountdown = 1.0
                        completionInitialized = false
                        trackedOperationId = currentId
                    }
                    if (!operation || !operation.finished) {
                        completionCountdownAnimation.stop()
                        completionCountdown = 1.0
                        completionInitialized = false
                        return
                    }
                    if (completionInitialized)
                        return
                    completionInitialized = true
                    completionCountdown = 1.0
                    if (cardHover.hovered) {
                        fileOps.setOperationDismissPaused(operation.id, true)
                    } else {
                        completionCountdownAnimation.restart()
                    }
                }

                onOperationChanged: syncCompletionState()
                Component.onCompleted: syncCompletionState()

                NumberAnimation {
                    id: completionCountdownAnimation
                    target: operationCard
                    property: "completionCountdown"
                    from: 1.0
                    to: 0.0
                    duration: 5000
                    easing.type: Easing.Linear
                }

                HoverHandler {
                    id: cardHover
                    onHoveredChanged: {
                        if (!operation || !operation.finished)
                            return
                        completionCountdownAnimation.stop()
                        completionCountdown = 1.0
                        fileOps.setOperationDismissPaused(operation.id, hovered)
                        if (!hovered)
                            completionCountdownAnimation.restart()
                    }
                }

                Behavior on height { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                radius: 14
                color: cardHover.hovered && operation.finished ? AppTheme.surfaceHover : AppTheme.surface
                border.color: cardHover.hovered && operation.finished ? AppTheme.accentBorder : AppTheme.borderStrong
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Text {
                            Layout.fillWidth: true
                            color: AppTheme.text
                            font.pixelSize: 12
                            font.bold: true
                            text: operation.finished
                                  ? (operation.success
                                     ? (operation.kind === "copy" ? (lang.language === "tr" ? "Kopyalama tamamlandı" : "Copy complete")
                                        : operation.kind === "clipboard" ? (lang.language === "tr" ? "Panoya hazırlandı" : "Clipboard ready")
                                        : operation.kind === "move" ? (lang.language === "tr" ? "Taşıma tamamlandı" : "Move complete")
                                        : operation.kind === "duplicate" ? (lang.language === "tr" ? "Klonlama tamamlandı" : "Duplication complete")
                                        : operation.kind === "compress" ? (lang.language === "tr" ? "Arşiv oluşturuldu" : "Archive created")
                                        : (lang.language === "tr" ? "Arşiv çıkarıldı" : "Extraction complete"))
                                     : (lang.language === "tr" ? "İşlem tamamlanamadı" : "Operation failed"))
                                  : operation.kind === "copy" ? (lang.language === "tr" ? "Kopyalanıyor" : "Copying")
                                  : operation.kind === "clipboard" ? (lang.language === "tr" ? "Pano hazırlanıyor" : "Preparing clipboard")
                                  : operation.kind === "move" ? (lang.language === "tr" ? "Taşınıyor" : "Moving")
                                  : operation.kind === "duplicate" ? (lang.language === "tr" ? "Klonlanıyor" : "Duplicating")
                                  : operation.kind === "compress" ? (lang.language === "tr" ? "Arşiv oluşturuluyor" : "Creating archive")
                                  : (lang.language === "tr" ? "Arşiv çıkarılıyor" : "Extracting archive")
                        }
                        Text {
                            text: operation.finished ? (operation.success ? "✓" : "!")
                                                     : (shownPercent >= 0 ? Math.round(shownPercent) + "%" : "…")
                            color: operation.finished ? (operation.success ? AppTheme.success : AppTheme.danger) : AppTheme.accent
                            font.pixelSize: 11
                            font.bold: true
                        }
                        Rectangle {
                            id: dismissButton
                            visible: operation.finished
                            Layout.preferredWidth: 24
                            Layout.preferredHeight: 24
                            radius: 8
                            color: dismissHover.hovered ? AppTheme.surfaceHover : "transparent"
                            border.width: dismissHover.hovered ? 1 : 0
                            border.color: AppTheme.accentBorder

                            CrispIcon {
                                anchors.centerIn: parent
                                width: 15
                                height: 15
                                source: AppTheme.icon("close-ui.svg")
                                opacity: dismissHover.hovered ? 1.0 : 0.82
                            }
                            HoverHandler { id: dismissHover }
                            TapHandler {
                                acceptedButtons: Qt.LeftButton
                                onTapped: fileOps.dismissOperation(operation.id)
                            }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: operation.detail
                        color: AppTheme.textMuted
                        font.pixelSize: 11
                        elide: Text.ElideMiddle
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.transferMetrics(operation)
                        color: AppTheme.text
                        font.pixelSize: 10
                        elide: Text.ElideRight
                    }
                    Rectangle {
                        id: progressTrack
                        Layout.fillWidth: true
                        Layout.preferredHeight: 7
                        radius: 4
                        color: AppTheme.surfaceHover
                        clip: true
                        Rectangle {
                            property real indeterminateX: 0
                            x: !operation.finished && shownPercent < 0 ? indeterminateX : 0
                            width: operation.finished ? progressTrack.width * completionCountdown
                                  : shownPercent < 0 ? progressTrack.width * 0.28
                                  : progressTrack.width * shownPercent / 100
                            height: parent.height
                            radius: parent.radius
                            color: operation.finished ? (operation.success ? AppTheme.success : AppTheme.danger) : AppTheme.accent
                            SequentialAnimation on indeterminateX {
                                running: !operation.finished && shownPercent < 0 && !operation.paused
                                loops: Animation.Infinite
                                NumberAnimation { from: 0; to: progressTrack.width * 0.72; duration: 1050; easing.type: Easing.InOutSine }
                                NumberAnimation { from: progressTrack.width * 0.72; to: 0; duration: 1050; easing.type: Easing.InOutSine }
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: !operation.finished
                        Text {
                            Layout.fillWidth: true
                            text: root.transferRemaining(operation)
                            color: AppTheme.textMuted
                            font.pixelSize: 10
                            elide: Text.ElideRight
                        }
                        Text {
                            visible: !operation.paused && Number(operation.speed || 0) > 0
                            text: root.formatTransferBytes(operation.speed) + (lang.language === "tr" ? "/sn" : "/s")
                            color: AppTheme.text
                            font.pixelSize: 10
                            font.bold: true
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: !operation.finished && operation.controllable !== false
                        Item { Layout.fillWidth: true }
                        GButton {
                            text: operation.paused ? (lang.language === "tr" ? "Sürdür" : "Resume")
                                                   : (lang.language === "tr" ? "Duraklat" : "Pause")
                            onClicked: fileOps.toggleOperationPause(operation.id)
                        }
                        GButton {
                            text: lang.language === "tr" ? "İptal" : "Cancel"
                            onClicked: fileOps.cancelOperation(operation.id)
                        }
                    }
                }
            }
        }
    }

}
