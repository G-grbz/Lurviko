import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Lurviko.App
import Lurviko.Backend

Popup {
    id: viewer
    required property var lang
    property var files: []
    property int currentIndex: -1
    property real zoom: 1.0
    property int rotationAngle: 0
    property bool slideshowRunning: false
    property int slideshowIntervalMs: 3500
    property var hostWindow: null
    property bool controlsVisible: true
    // Fullscreen belongs to a dedicated viewer window. The Lurviko main
    // window never changes visibility mode when the photo viewer goes full screen.
    property bool viewerFullScreen: false
    property var normalOverlayParent: null
    property bool controlsSettling: false
    // Two image buffers are used so slideshow navigation can preload the next
    // photo and cross-fade only after it is actually ready.  This avoids the
    // abrupt source swap / empty frame that was visible in slideshow mode.
    property bool showingImageA: true
    property int pendingImageSlot: -1   // -1 none, 0 imageA, 1 imageB
    property var pendingImageItem: null
    property int imageTransitionDuration: 280
    property real lastPointerSceneX: -1
    property real lastPointerSceneY: -1
    signal browseRequested(string location)

    parent: viewerFullScreen && viewerFullScreenWindow.contentItem
            ? viewerFullScreenWindow.contentItem
            : (normalOverlayParent || Overlay.overlay)
    x: 0
    y: 0
    width: parent ? parent.width : 0
    height: parent ? parent.height : 0
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.NoAutoClose

    readonly property var currentItem: currentIndex >= 0 && currentIndex < files.length ? files[currentIndex] : null
    readonly property real controlsOpacity: viewerFullScreen && !controlsVisible ? 0.0 : 1.0

    function viewerIcon(name) {
        return "qrc:/qt/qml/Lurviko/App/assets/icons/" + name
    }

    function thumbSource(item) {
        if (!item || !item.path) return ""
        return "image://gfilethumb/" + encodeURIComponent(item.path)
                + "|viewer-strip-" + String(item.modifiedMs || 0) + "-" + String(item.size || 0)
    }

    function revealControls() {
        // Any explicit interaction wins over an in-progress fade.  This is
        // especially important for the side arrows: users often keep the
        // pointer stationary there and click through many photos.
        controlsSettleTimer.stop()
        controlsSettling = false
        controlsVisible = true
        if (viewerFullScreen)
            controlsHideTimer.restart()
        else
            controlsHideTimer.stop()
    }

    function handleStagePointerMotion(sourceItem, x, y) {
        if (!viewerFullScreen || !sourceItem)
            return
        const scenePoint = sourceItem.mapToItem(viewer.contentItem, x, y)
        if (lastPointerSceneX < 0 || lastPointerSceneY < 0) {
            lastPointerSceneX = scenePoint.x
            lastPointerSceneY = scenePoint.y
            return
        }
        const moved = Math.abs(scenePoint.x - lastPointerSceneX) >= 2
                   || Math.abs(scenePoint.y - lastPointerSceneY) >= 2
        lastPointerSceneX = scenePoint.x
        lastPointerSceneY = scenePoint.y
        if (moved)
            revealControls()
    }

    function openAt(index) {
        if (index < 0 || index >= files.length)
            return
        currentIndex = index
        resetView()
        open()
        revealControls()
        forceActiveFocus()
        Qt.callLater(function() { filmstrip.positionViewAtIndex(currentIndex, ListView.Center) })
    }

    function resetView() {
        zoom = 1.0
        rotationAngle = 0
        photoFlick.contentX = 0
        photoFlick.contentY = 0
    }

    function itemSource(item) {
        return item && item.url ? String(item.url) : ""
    }

    function syncCurrentImageImmediately() {
        imageAToB.stop()
        imageBToA.stop()
        pendingImageSlot = -1
        pendingImageItem = null
        const src = itemSource(currentItem)
        showingImageA = true
        imageA.opacity = 1.0
        imageB.opacity = 0.0
        imageA.source = src
        imageB.source = ""
        backdropA.opacity = 0.58
        backdropB.opacity = 0.0
        backdropA.source = currentItem ? thumbSource(currentItem) : ""
        backdropB.source = ""
    }

    function queueCurrentImageTransition() {
        const item = currentItem
        const src = itemSource(item)
        if (!src) {
            syncCurrentImageImmediately()
            return
        }

        // Opening the first image does not need a transition.
        const activeSource = showingImageA ? String(imageA.source) : String(imageB.source)
        if (!activeSource) {
            syncCurrentImageImmediately()
            return
        }
        if (src === activeSource)
            return

        imageAToB.stop()
        imageBToA.stop()
        pendingImageItem = item

        if (showingImageA) {
            pendingImageSlot = 1
            imageB.opacity = 0.0
            backdropB.opacity = 0.0
            backdropB.source = thumbSource(item)
            imageB.source = src
            if (imageB.status === Image.Ready)
                startPendingImageTransition(1)
        } else {
            pendingImageSlot = 0
            imageA.opacity = 0.0
            backdropA.opacity = 0.0
            backdropA.source = thumbSource(item)
            imageA.source = src
            if (imageA.status === Image.Ready)
                startPendingImageTransition(0)
        }
    }

    function startPendingImageTransition(slot) {
        if (slot !== pendingImageSlot)
            return
        if (slot === 1 && showingImageA)
            imageAToB.start()
        else if (slot === 0 && !showingImageA)
            imageBToA.start()
    }

    function handleImageStatus(slot, status) {
        if (slot !== pendingImageSlot)
            return
        if (status === Image.Ready) {
            startPendingImageTransition(slot)
        } else if (status === Image.Error) {
            // Do not fade the currently visible image away when the next file
            // cannot be decoded. Keep the good frame on screen.
            pendingImageSlot = -1
            pendingImageItem = null
        }
    }

    function finishImageTransition(showA) {
        showingImageA = showA
        pendingImageSlot = -1
        pendingImageItem = null
        if (showA) {
            imageA.opacity = 1.0
            backdropA.opacity = 0.58
            imageB.opacity = 0.0
            backdropB.opacity = 0.0
            imageB.source = ""
            backdropB.source = ""
        } else {
            imageB.opacity = 1.0
            backdropB.opacity = 0.58
            imageA.opacity = 0.0
            backdropA.opacity = 0.0
            imageA.source = ""
            backdropA.source = ""
        }
    }

    function setIndex(index, revealChrome) {
        if (!files.length) return
        currentIndex = Math.max(0, Math.min(files.length - 1, index))
        resetView()
        if (revealChrome === undefined || revealChrome)
            revealControls()
        Qt.callLater(function() { filmstrip.positionViewAtIndex(currentIndex, ListView.Center) })
    }

    function next(revealChrome) {
        if (!files.length) return
        setIndex((currentIndex + 1) % files.length, revealChrome)
    }

    function previous(revealChrome) {
        if (!files.length) return
        setIndex((currentIndex - 1 + files.length) % files.length, revealChrome)
    }

    function toggleViewerFullScreen() {
        if (!viewerFullScreen) {
            if (!normalOverlayParent)
                normalOverlayParent = parent
            viewerFullScreen = true
            viewerFullScreenWindow.showFullScreen()
            revealControls()
        } else {
            viewerFullScreen = false
            viewerFullScreenWindow.hide()
            controlsHideTimer.stop()
            controlsVisible = true
        }
    }

    function formatSize(bytes) {
        const n = Number(bytes || 0)
        if (n < 1024) return n + " B"
        if (n < 1024 * 1024) return (n / 1024).toFixed(1) + " KiB"
        if (n < 1024 * 1024 * 1024) return (n / (1024 * 1024)).toFixed(1) + " MiB"
        return (n / (1024 * 1024 * 1024)).toFixed(1) + " GiB"
    }

    function formatDate(ms) {
        const d = new Date(Number(ms || 0))
        if (isNaN(d.getTime())) return ""
        return d.toLocaleString(Qt.locale(lang.language === "tr" ? "tr_TR" : "en_US"), "dd MMM yyyy · HH:mm")
    }

    component ViewerIconButton: GToolButton {
        id: control
        property string iconName: ""
        property string toolTipText: ""
        property bool emphasized: false
        implicitWidth: 38
        implicitHeight: 38
        padding: 0
        HoverHandler {
            cursorShape: Qt.PointingHandCursor
            onHoveredChanged: if (hovered) viewer.revealControls()
        }
        Connections {
            target: control
            function onPressedChanged() { if (control.pressed) viewer.revealControls() }
            function onClicked() { viewer.revealControls() }
        }
        contentItem: Item {
            GViewerIcon {
                anchors.centerIn: parent
                width: 18
                height: 18
                source: viewer.viewerIcon(control.iconName)
                tintColor: AppTheme.viewerText
                iconOpacity: control.enabled ? 0.96 : 0.42
            }
        }
        background: GGlassPanel {
            sourceItem: photoScene
            radius: 9
            tintColor: control.down ? AppTheme.viewerGlassPressed
                                     : (control.hovered ? AppTheme.viewerGlassHover
                                                        : (control.emphasized ? AppTheme.viewerGlassEmphasis : AppTheme.viewerGlassSoft))
            borderColor: control.hovered || control.emphasized ? AppTheme.accentBorder : AppTheme.viewerGlassBorderSoft
            blurAmount: 0.46
        }
        GToolTip { text: control.toolTipText }
    }

    ApplicationWindow {
        id: viewerFullScreenWindow
        visible: false
        color: AppTheme.viewerBackground
        title: viewer.lang.language === "tr" ? "Lurviko Fotoğraf Görüntüleyici" : "Lurviko Photo Viewer"
        flags: Qt.Window | Qt.FramelessWindowHint
        onClosing: function(close) {
            if (viewer.viewerFullScreen) {
                close.accepted = false
                viewer.toggleViewerFullScreen()
            }
        }
    }

    background: Rectangle { color: AppTheme.viewerBackground }

    onOpened: {
        if (!normalOverlayParent)
            normalOverlayParent = parent
        syncCurrentImageImmediately()
        if (hostWindow && hostWindow.mediaViewerConsumesF11 !== undefined)
            hostWindow.mediaViewerConsumesF11 = true
    }
    onClosed: {
        slideshowRunning = false
        viewerFullScreen = false
        viewerFullScreenWindow.hide()
        if (hostWindow && hostWindow.mediaViewerConsumesF11 !== undefined)
            hostWindow.mediaViewerConsumesF11 = false
        controlsHideTimer.stop()
        controlsSettleTimer.stop()
        controlsSettling = false
        controlsVisible = true
        imageAToB.stop()
        imageBToA.stop()
        pendingImageSlot = -1
        pendingImageItem = null
        lastPointerSceneX = -1
        lastPointerSceneY = -1
    }
    onViewerFullScreenChanged: {
        controlsSettleTimer.stop()
        controlsSettling = false
        lastPointerSceneX = -1
        lastPointerSceneY = -1
        if (viewerFullScreen)
            revealControls()
        else {
            controlsHideTimer.stop()
            controlsVisible = true
        }
    }
    onCurrentIndexChanged: if (visible) {
        queueCurrentImageTransition()
        Qt.callLater(function() { filmstrip.positionViewAtIndex(currentIndex, ListView.Center) })
    }

    Timer {
        id: controlsHideTimer
        interval: 1800
        repeat: false
        onTriggered: {
            if (!viewer.viewerFullScreen) return
            if (topToolbarHover.hovered || bottomToolbarHover.hovered || stripHover.hovered || infoHover.hovered
                    || previousButton.hovered || nextButton.hovered) {
                restart()
                return
            }
            viewer.controlsSettling = true
            viewer.controlsVisible = false
            controlsSettleTimer.restart()
        }
    }
    Timer { id: controlsSettleTimer; interval: 220; onTriggered: viewer.controlsSettling = false }
    Timer {
        interval: Math.max(1200, viewer.slideshowIntervalMs)
        repeat: true
        running: viewer.visible && viewer.slideshowRunning
        onTriggered: viewer.next(false)
    }

    Shortcut { sequences: KeyboardShortcuts.bindings["photo_close"]; enabled: viewer.visible && !KeyboardShortcuts.editorOpen; onActivated: viewer.close() }
    Shortcut { sequences: KeyboardShortcuts.bindings["photo_previous"]; enabled: viewer.visible && !KeyboardShortcuts.editorOpen; onActivated: viewer.previous(true) }
    Shortcut { sequences: KeyboardShortcuts.bindings["photo_next"]; enabled: viewer.visible && !KeyboardShortcuts.editorOpen; onActivated: viewer.next(true) }
    Shortcut { sequences: KeyboardShortcuts.bindings["fullscreen"]; enabled: viewer.visible && !KeyboardShortcuts.editorOpen; context: Qt.ApplicationShortcut; onActivated: viewer.toggleViewerFullScreen() }
    Shortcut { sequences: KeyboardShortcuts.bindings["photo_slideshow"]; enabled: viewer.visible && !KeyboardShortcuts.editorOpen; onActivated: viewer.slideshowRunning = !viewer.slideshowRunning }
    Shortcut { sequences: KeyboardShortcuts.bindings["photo_zoom_in"]; enabled: viewer.visible && !KeyboardShortcuts.editorOpen; onActivated: viewer.zoom = Math.min(5.0, viewer.zoom * 1.15) }
    Shortcut { sequences: KeyboardShortcuts.bindings["photo_zoom_out"]; enabled: viewer.visible && !KeyboardShortcuts.editorOpen; onActivated: viewer.zoom = Math.max(0.25, viewer.zoom / 1.15) }
    Shortcut { sequences: KeyboardShortcuts.bindings["photo_reset"]; enabled: viewer.visible && !KeyboardShortcuts.editorOpen; onActivated: viewer.resetView() }

    Item {
        id: stage
        anchors.fill: parent
        clip: true

        Item {
            id: photoScene
            anchors.fill: parent

        // The backdrop is double-buffered together with the full-resolution
        // image so the whole stage changes as one smooth frame.
        Image {
            id: backdropA
            anchors.fill: parent
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            sourceSize.width: 96
            sourceSize.height: 96
            smooth: true
            opacity: 0.58
        }
        Image {
            id: backdropB
            anchors.fill: parent
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            sourceSize.width: 96
            sourceSize.height: 96
            smooth: true
            opacity: 0.0
        }
        Rectangle {
            anchors.fill: parent
            color: AppTheme.viewerScrim
        }

        Flickable {
            id: photoFlick
            anchors.fill: parent
            clip: true
            interactive: viewer.zoom > 1.0
            contentWidth: Math.max(width, imageHolder.width)
            contentHeight: Math.max(height, imageHolder.height)
            boundsBehavior: Flickable.StopAtBounds

            HoverHandler { cursorShape: viewer.zoom > 1.0 ? Qt.OpenHandCursor : Qt.ArrowCursor }

            Item {
                id: imageHolder
                width: Math.max(photoFlick.width, photoFlick.width * viewer.zoom)
                height: Math.max(photoFlick.height, photoFlick.height * viewer.zoom)

                Image {
                    id: imageA
                    anchors.centerIn: parent
                    width: Math.max(80, photoFlick.width * 0.965 * viewer.zoom)
                    height: Math.max(80, photoFlick.height * 0.94 * viewer.zoom)
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    cache: false
                    rotation: viewer.rotationAngle
                    smooth: true
                    mipmap: true
                    opacity: 1.0
                    onStatusChanged: viewer.handleImageStatus(0, status)
                }

                Image {
                    id: imageB
                    anchors.centerIn: parent
                    width: Math.max(80, photoFlick.width * 0.965 * viewer.zoom)
                    height: Math.max(80, photoFlick.height * 0.94 * viewer.zoom)
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    cache: false
                    rotation: viewer.rotationAngle
                    smooth: true
                    mipmap: true
                    opacity: 0.0
                    onStatusChanged: viewer.handleImageStatus(1, status)
                }
            }

            WheelHandler {
                acceptedModifiers: Qt.NoModifier
                onWheel: function(event) {
                    if (event.angleDelta.y > 0)
                        viewer.zoom = Math.min(5.0, viewer.zoom * 1.12)
                    else
                        viewer.zoom = Math.max(0.25, viewer.zoom / 1.12)
                    event.accepted = true
                }
            }
        }
        }

        ParallelAnimation {
            id: imageAToB
            NumberAnimation { target: imageA; property: "opacity"; from: 1.0; to: 0.0; duration: viewer.imageTransitionDuration; easing.type: Easing.InOutQuad }
            NumberAnimation { target: imageB; property: "opacity"; from: 0.0; to: 1.0; duration: viewer.imageTransitionDuration; easing.type: Easing.InOutQuad }
            NumberAnimation { target: backdropA; property: "opacity"; from: 0.58; to: 0.0; duration: viewer.imageTransitionDuration; easing.type: Easing.InOutQuad }
            NumberAnimation { target: backdropB; property: "opacity"; from: 0.0; to: 0.58; duration: viewer.imageTransitionDuration; easing.type: Easing.InOutQuad }
            onFinished: viewer.finishImageTransition(false)
        }

        ParallelAnimation {
            id: imageBToA
            NumberAnimation { target: imageB; property: "opacity"; from: 1.0; to: 0.0; duration: viewer.imageTransitionDuration; easing.type: Easing.InOutQuad }
            NumberAnimation { target: imageA; property: "opacity"; from: 0.0; to: 1.0; duration: viewer.imageTransitionDuration; easing.type: Easing.InOutQuad }
            NumberAnimation { target: backdropB; property: "opacity"; from: 0.58; to: 0.0; duration: viewer.imageTransitionDuration; easing.type: Easing.InOutQuad }
            NumberAnimation { target: backdropA; property: "opacity"; from: 0.0; to: 0.58; duration: viewer.imageTransitionDuration; easing.type: Easing.InOutQuad }
            onFinished: viewer.finishImageTransition(true)
        }

        MouseArea {
            id: motionTracker
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.NoButton
            cursorShape: viewer.zoom > 1.0 ? Qt.OpenHandCursor : Qt.ArrowCursor
            onPositionChanged: function(mouse) { viewer.handleStagePointerMotion(motionTracker, mouse.x, mouse.y) }
        }

        GBusyIndicator {
            anchors.centerIn: parent
            running: imageA.status === Image.Loading || imageB.status === Image.Loading
            visible: running
        }

        Item {
            id: controlsLayer
            anchors.fill: parent
            opacity: viewer.controlsOpacity
            visible: opacity > 0.01
            Behavior on opacity { NumberAnimation { duration: 150 } }

            GGlassPanel {
                id: infoCard
                sourceItem: photoScene
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.leftMargin: 18
                anchors.topMargin: 16
                width: Math.min(430, Math.max(220, titleText.implicitWidth + 78))
                height: Math.max(54, infoRow.implicitHeight + 12)
                radius: 12
                tintColor: AppTheme.viewerGlass
                borderColor: AppTheme.viewerGlassBorder
                HoverHandler { id: infoHover; onHoveredChanged: if (hovered) viewer.revealControls() }
                RowLayout {
                    id: infoRow
                    anchors.fill: parent
                    anchors.leftMargin: 7
                    anchors.rightMargin: 12
                    spacing: 9
                    ViewerIconButton {
                        Layout.preferredWidth: 36
                        Layout.preferredHeight: 36
                        iconName: "close-ui.svg"
                        toolTipText: viewer.lang.language === "tr" ? "Kapat (Esc)" : "Close (Esc)"
                        onClicked: viewer.close()
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            id: titleText
                            Layout.fillWidth: true
                            text: viewer.currentItem ? viewer.currentItem.name : ""
                            color: AppTheme.viewerText
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                            elide: Text.ElideMiddle
                        }
                        Text {
                            Layout.fillWidth: true
                            text: viewer.currentItem ? viewer.formatSize(viewer.currentItem.size) + " · " + viewer.formatDate(viewer.currentItem.modifiedMs) : ""
                            color: AppTheme.viewerTextMuted
                            font.pixelSize: 9
                            elide: Text.ElideRight
                        }
                    }
                }
            }

            GGlassPanel {
                id: topToolbar
                sourceItem: photoScene
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.topMargin: 18
                height: Math.max(52, topTools.implicitHeight + 12)
                width: topTools.implicitWidth + 18
                radius: 12
                tintColor: AppTheme.viewerGlass
                borderColor: AppTheme.viewerGlassBorder
                HoverHandler { id: topToolbarHover; onHoveredChanged: if (hovered) viewer.revealControls() }
                Row {
                    id: topTools
                    anchors.centerIn: parent
                    spacing: 6
                    ViewerIconButton { iconName: "viewer-fit.svg"; toolTipText: viewer.lang.language === "tr" ? "Sığdır / 1:1 (0)" : "Fit / 1:1 (0)"; onClicked: viewer.resetView() }
                    ViewerIconButton { iconName: "viewer-rotate-left.svg"; toolTipText: viewer.lang.language === "tr" ? "Sola döndür" : "Rotate left"; onClicked: viewer.rotationAngle = (viewer.rotationAngle - 90) % 360 }
                    ViewerIconButton { iconName: "viewer-rotate-right.svg"; toolTipText: viewer.lang.language === "tr" ? "Sağa döndür" : "Rotate right"; onClicked: viewer.rotationAngle = (viewer.rotationAngle + 90) % 360 }
                    ViewerIconButton {
                        iconName: "viewer-folder.svg"
                        toolTipText: viewer.lang.language === "tr" ? "Konumu aç" : "Open location"
                        onClicked: if (viewer.currentItem) { const p = viewer.currentItem.parentPath; viewer.close(); viewer.browseRequested(p) }
                    }
                    ViewerIconButton { iconName: "viewer-external.svg"; toolTipText: viewer.lang.language === "tr" ? "Harici aç" : "Open externally"; onClicked: if (viewer.currentItem) Qt.openUrlExternally(viewer.currentItem.url) }
                    ViewerIconButton { iconName: viewer.viewerFullScreen ? "viewer-fullscreen-exit.svg" : "viewer-fullscreen.svg"; toolTipText: viewer.lang.language === "tr" ? "Tam ekran (F11)" : "Full screen (F11)"; emphasized: viewer.viewerFullScreen; onClicked: viewer.toggleViewerFullScreen() }
                }
            }

            GGlassPanel {
                sourceItem: photoScene
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.topMargin: 18
                anchors.rightMargin: 18
                height: 36
                width: counterLabel.implicitWidth + 24
                radius: 10
                tintColor: AppTheme.viewerGlassSoft
                borderColor: AppTheme.viewerGlassBorderSoft
                Text {
                    id: counterLabel
                    anchors.centerIn: parent
                    text: viewer.currentIndex >= 0 ? (viewer.currentIndex + 1) + " / " + viewer.files.length : ""
                    color: AppTheme.viewerText
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                }
            }

            ViewerIconButton {
                id: previousButton
                anchors.left: parent.left
                anchors.leftMargin: 18
                anchors.verticalCenter: parent.verticalCenter
                width: 48
                height: 58
                iconName: "viewer-prev.svg"
                toolTipText: viewer.lang.language === "tr" ? "Önceki (←)" : "Previous (←)"
                onClicked: viewer.previous()
            }
            ViewerIconButton {
                id: nextButton
                anchors.right: parent.right
                anchors.rightMargin: 18
                anchors.verticalCenter: parent.verticalCenter
                width: 48
                height: 58
                iconName: "viewer-next.svg"
                toolTipText: viewer.lang.language === "tr" ? "Sonraki (→)" : "Next (→)"
                onClicked: viewer.next()
            }

            GGlassPanel {
                id: statusCard
                sourceItem: photoScene
                anchors.left: parent.left
                anchors.bottom: stripPanel.top
                anchors.leftMargin: 18
                anchors.bottomMargin: 12
                height: Math.max(36, statusRow.implicitHeight + 12)
                width: statusRow.implicitWidth + 20
                radius: 10
                tintColor: AppTheme.viewerGlassSoft
                borderColor: AppTheme.viewerGlassBorderSoft
                Row {
                    id: statusRow
                    anchors.centerIn: parent
                    spacing: 8
                    Text { text: Math.round(viewer.zoom * 100) + "%"; color: AppTheme.viewerTextMuted; font.pixelSize: 9 }
                    Rectangle { width: 1; height: 12; color: AppTheme.viewerGlassBorderSoft }
                    Text { text: viewer.currentIndex >= 0 ? (viewer.currentIndex + 1) + "/" + viewer.files.length : ""; color: AppTheme.viewerTextMuted; font.pixelSize: 9 }
                    Rectangle { width: 1; height: 12; color: AppTheme.viewerGlassBorderSoft; visible: (viewer.showingImageA ? imageA.sourceSize.width : imageB.sourceSize.width) > 0 }
                    Text { text: (viewer.showingImageA ? imageA.sourceSize.width : imageB.sourceSize.width) > 0
                                ? (viewer.showingImageA ? imageA.sourceSize.width + "×" + imageA.sourceSize.height
                                                        : imageB.sourceSize.width + "×" + imageB.sourceSize.height)
                                : ""; color: AppTheme.viewerTextMuted; font.pixelSize: 9; visible: text.length > 0 }
                    Rectangle { width: 1; height: 12; color: AppTheme.viewerGlassBorderSoft; visible: viewer.currentItem !== null }
                    Text { text: viewer.currentItem ? viewer.formatSize(viewer.currentItem.size) : ""; color: AppTheme.viewerTextMuted; font.pixelSize: 9 }
                }
            }

            GGlassPanel {
                id: bottomToolbar
                sourceItem: photoScene
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: stripPanel.top
                anchors.bottomMargin: 12
                height: Math.max(54, bottomTools.implicitHeight + 12)
                width: bottomTools.implicitWidth + 18
                radius: 13
                tintColor: AppTheme.viewerGlass
                borderColor: AppTheme.viewerGlassBorder
                HoverHandler { id: bottomToolbarHover; onHoveredChanged: if (hovered) viewer.revealControls() }
                Row {
                    id: bottomTools
                    anchors.centerIn: parent
                    spacing: 6
                    ViewerIconButton { iconName: "viewer-zoom-out.svg"; toolTipText: viewer.lang.language === "tr" ? "Uzaklaştır" : "Zoom out"; onClicked: viewer.zoom = Math.max(0.25, viewer.zoom / 1.18) }
                    Rectangle {
                        width: 52; height: 34; radius: 8; color: AppTheme.viewerGlassSoft; border.width: 1; border.color: AppTheme.viewerGlassBorderSoft
                        Text { anchors.centerIn: parent; text: Math.round(viewer.zoom * 100) + "%"; color: AppTheme.viewerText; font.pixelSize: 9; font.weight: Font.DemiBold }
                    }
                    ViewerIconButton { iconName: "viewer-zoom-in.svg"; toolTipText: viewer.lang.language === "tr" ? "Yakınlaştır" : "Zoom in"; onClicked: viewer.zoom = Math.min(5.0, viewer.zoom * 1.18) }
                    Rectangle { width: 1; height: 24; color: AppTheme.viewerGlassBorderSoft }
                    ViewerIconButton { iconName: "viewer-prev.svg"; toolTipText: viewer.lang.language === "tr" ? "Önceki" : "Previous"; onClicked: viewer.previous() }
                    ViewerIconButton { iconName: "viewer-next.svg"; toolTipText: viewer.lang.language === "tr" ? "Sonraki" : "Next"; onClicked: viewer.next() }
                    ViewerIconButton { iconName: viewer.slideshowRunning ? "viewer-pause.svg" : "viewer-play.svg"; toolTipText: viewer.lang.language === "tr" ? "Slayt gösterisi (Space)" : "Slideshow (Space)"; emphasized: viewer.slideshowRunning; onClicked: viewer.slideshowRunning = !viewer.slideshowRunning }
                }
            }

            GGlassPanel {
                id: stripPanel
                sourceItem: photoScene
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                anchors.bottomMargin: 14
                height: 76
                radius: 12
                tintColor: AppTheme.viewerGlassStrong
                borderColor: AppTheme.viewerGlassBorder
                clip: true
                HoverHandler { id: stripHover; onHoveredChanged: if (hovered) viewer.revealControls() }

                ListView {
                    id: filmstrip
                    anchors.fill: parent
                    anchors.margins: 7
                    orientation: ListView.Horizontal
                    spacing: 6
                    clip: true
                    model: viewer.files
                    boundsBehavior: Flickable.StopAtBounds
                    currentIndex: viewer.currentIndex
                    highlightMoveDuration: 120
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        width: 86
                        height: 60
                        radius: 8
                        color: AppTheme.dark ? "#16000000" : "#12FFFFFF"
                        border.width: index === viewer.currentIndex ? 2 : 1
                        border.color: index === viewer.currentIndex ? AppTheme.accent : AppTheme.viewerGlassBorderSoft
                        clip: true
                        Image {
                            anchors.fill: parent
                            anchors.margins: 2
                            source: viewer.thumbSource(modelData)
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                            cache: true
                            sourceSize.width: 192
                            sourceSize.height: 128
                        }
                        Rectangle {
                            anchors.fill: parent
                            radius: 7
                            color: mouse.containsMouse ? AppTheme.viewerGlassHover : "transparent"
                        }
                        MouseArea {
                            id: mouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: viewer.setIndex(index)
                        }
                    }
                }
            }
        }
    }
}
