import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Effects
import Lurviko.App
import Lurviko.Backend

Popup {
    id: viewer
    // Opens with the type's preferred application, like the file browser;
    // Qt.openUrlExternally asked for an application every time.
    OpenWithModel { id: photoOpenWith }
    ImageGeometry { id: imageGeometry }
    required property var lang
    property var files: []
    property int currentIndex: -1
    // 1.0 fits the whole picture in the stage; up to 2000%.
    property real zoom: 1.0
    readonly property real minZoom: 0.25
    readonly property real maxZoom: 20.0
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
    // Controls fade after a short idle period in the window as well as in
    // full screen; pointer motion, keys and clicks bring them back.
    readonly property real controlsOpacity: controlsVisible ? 1.0 : 0.0

    // Same resolver as the rest of the app (the glyphs live under
    // assets/icons/symbolic; the old direct qrc path no longer existed).
    function viewerIcon(name) {
        return AppTheme.icon(name, AppTheme.viewerText)
    }

    // Render size for vector images, in logical pixels: Qt Quick itself
    // renders scalable images at sourceSize × devicePixelRatio. Multiplying
    // by the ratio here as well asked for ~9000 px at 3x scaling and Qt
    // rejected the image (256 MB allocation limit), so it never appeared.
    // The device-pixel long side is capped at 4096.
    // Decode size for raster photos. Without a sourceSize every photo was
    // decoded at full resolution (a 24 MP JPEG is ~96 MB of RAM plus the
    // same again as a GPU texture, and more with mipmaps). Decode for the
    // stage in device pixels instead; zooming raises the size in powers of
    // two, up to the file's own size, with the previous picture retained
    // while the sharper one loads. undefined keeps the native size (remote
    // files, or pictures already smaller than the stage).
    function rasterSourceSize(source, zoomValue) {
        const src = String(source || "")
        if (!src.length)
            return undefined
        const dpr = Math.max(1, Screen.devicePixelRatio)
        const stageLong = Math.max(photoFlick.width, photoFlick.height)
                || Math.max(Screen.width, Screen.height)
        const tier = zoomValue <= 1.01 ? 1 : Math.pow(2, Math.ceil(Math.log(zoomValue) / Math.LN2))
        // 512 px steps: resizing the window does not decode on every pixel.
        const longSide = Math.min(8192, Math.ceil(stageLong * dpr * tier / 512) * 512)
        const size = imageGeometry.boundedSourceSize(src, longSide)
        return size.width > 0 ? size : undefined
    }

    function vectorRenderSize(width, height) {
        const dpr = Math.max(1, Screen.devicePixelRatio)
        const w = Math.max(1, width)
        const h = Math.max(1, height)
        const scale = Math.min(1, 4096 / (Math.max(w, h) * dpr))
        return Qt.size(Math.max(1, Math.round(w * scale)), Math.max(1, Math.round(h * scale)))
    }

    // On-screen size of the picture (rotation included) and whether it is
    // larger than the stage, which enables panning and the navigator.
    readonly property bool quarterTurn: Math.abs(rotationAngle % 180) === 90
    readonly property var activeImage: showingImageA ? imageA : imageB
    // Computed from the aspect ratio rather than paintedWidth, which for SVGs
    // only updates after the asynchronous re-render at the new size.
    readonly property real imageAspect: activeImage.knownAspect
    readonly property real boxWidth: activeImage.width
    readonly property real boxHeight: activeImage.height
    readonly property real paintedW: imageAspect <= 0 ? 0 : Math.min(boxWidth, boxHeight * imageAspect)
    readonly property real paintedH: imageAspect <= 0 ? 0 : Math.min(boxHeight, boxWidth / imageAspect)
    readonly property real displayWidth: quarterTurn ? paintedH : paintedW
    readonly property real displayHeight: quarterTurn ? paintedW : paintedH
    readonly property bool zoomedIn: displayWidth > photoFlick.width + 1 || displayHeight > photoFlick.height + 1

    // Changes the zoom while the picture point under (viewportX, viewportY)
    // stays put: the stage centre for buttons and keys, the pointer for the
    // wheel. Without this the picture grew from its top-left corner and
    // drifted off to the right.
    function setZoom(value, viewportX, viewportY) {
        const next = Math.max(minZoom, Math.min(maxZoom, value))
        if (Math.abs(next - zoom) < 0.0001)
            return
        const vx = viewportX === undefined ? photoFlick.width / 2 : viewportX
        const vy = viewportY === undefined ? photoFlick.height / 2 : viewportY
        const oldWidth = Math.max(1, displayWidth)
        const oldHeight = Math.max(1, displayHeight)
        const fx = (photoFlick.contentX + vx - (photoFlick.contentWidth - oldWidth) / 2) / oldWidth
        const fy = (photoFlick.contentY + vy - (photoFlick.contentHeight - oldHeight) / 2) / oldHeight
        zoom = next
        const width = displayWidth
        const height = displayHeight
        photoFlick.contentX = Math.max(0, Math.min(photoFlick.contentWidth - photoFlick.width,
                                                   (photoFlick.contentWidth - width) / 2 + fx * width - vx))
        photoFlick.contentY = Math.max(0, Math.min(photoFlick.contentHeight - photoFlick.height,
                                                   (photoFlick.contentHeight - height) / 2 + fy * height - vy))
    }
    function zoomBy(factor, viewportX, viewportY) { setZoom(zoom * factor, viewportX, viewportY) }

    // Scrolls so that (x, y) of the on-screen picture is in the stage centre.
    function centerOn(x, y) {
        photoFlick.contentX = Math.max(0, Math.min(photoFlick.contentWidth - photoFlick.width,
                                                   (photoFlick.contentWidth - displayWidth) / 2 + x - photoFlick.width / 2))
        photoFlick.contentY = Math.max(0, Math.min(photoFlick.contentHeight - photoFlick.height,
                                                   (photoFlick.contentHeight - displayHeight) / 2 + y - photoFlick.height / 2))
    }

    readonly property bool imageFailed: pendingImageSlot < 0
        && (showingImageA ? imageA.status : imageB.status) === Image.Error

    // Natural pixel size of the photo on screen; empty for vector images,
    // which are rendered at whatever size they are shown.
    readonly property string currentDimensions: {
        const image = showingImageA ? imageA : imageB
        if (image.vector) return "SVG"
        return image.status === Image.Ready && image.implicitWidth > 0
                ? Math.round(image.implicitWidth) + "×" + Math.round(image.implicitHeight) : ""
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
        controlsHideTimer.restart()
    }

    function handleStagePointerMotion(sourceItem, x, y) {
        if (!sourceItem)
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

    // The file at currentIndex, read directly. Handlers that run on
    // currentIndexChanged can see the currentItem binding before it updates,
    // which showed the previous photo (one step behind) under the new title.
    function itemAtCurrentIndex() {
        return currentIndex >= 0 && currentIndex < files.length ? files[currentIndex] : null
    }

    function itemSource(item) {
        return item && item.url ? String(item.url) : ""
    }

    function syncCurrentImageImmediately() {
        imageAToB.stop()
        imageBToA.stop()
        pendingImageSlot = -1
        pendingImageItem = null
        const item = itemAtCurrentIndex()
        const src = itemSource(item)
        showingImageA = true
        imageA.opacity = 1.0
        imageB.opacity = 0.0
        imageA.source = src
        imageB.source = ""
        backdropA.opacity = 0.58
        backdropB.opacity = 0.0
        backdropA.source = item ? thumbSource(item) : ""
        backdropB.source = ""
    }

    function queueCurrentImageTransition() {
        const item = itemAtCurrentIndex()
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
            // Still switch: keeping the previous picture under the new file's
            // name looked like the viewer was stuck. The failed slot shows an
            // explanation instead (imageFailed).
            startPendingImageTransition(slot)
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
            revealControls()
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
        // Solid face: hover and press only recolour the border. The old glass
        // tint swapped an opaque fill for a translucent one on hover.
        background: Rectangle {
            radius: 9
            color: control.emphasized ? AppTheme.accentSoft : AppTheme.viewerGlassSoft
            border.width: 1
            border.color: control.down || control.hovered ? AppTheme.accent
                        : control.emphasized ? AppTheme.accentBorder : AppTheme.viewerGlassBorderSoft
            antialiasing: true
            Behavior on border.color { ColorAnimation { duration: 110 } }
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
        // Release the decoded photos; the gallery page stays open after the
        // viewer closes, and the buffers used to stay in RAM and VRAM with it.
        imageA.source = ""
        imageB.source = ""
        backdropA.source = ""
        backdropB.source = ""
        showingImageA = true
    }
    onViewerFullScreenChanged: {
        controlsSettleTimer.stop()
        controlsSettling = false
        lastPointerSceneX = -1
        lastPointerSceneY = -1
        revealControls()
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
            if (!viewer.visible) return
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
    Shortcut { sequences: KeyboardShortcuts.bindings["photo_zoom_in"]; enabled: viewer.visible && !KeyboardShortcuts.editorOpen; onActivated: viewer.zoomBy(1.15) }
    Shortcut { sequences: KeyboardShortcuts.bindings["photo_zoom_out"]; enabled: viewer.visible && !KeyboardShortcuts.editorOpen; onActivated: viewer.zoomBy(1 / 1.15) }
    Shortcut { sequences: KeyboardShortcuts.bindings["photo_reset"]; enabled: viewer.visible && !KeyboardShortcuts.editorOpen; onActivated: viewer.resetView() }

    Item {
        id: stage
        anchors.fill: parent
        clip: true

        Item {
            id: photoScene
            anchors.fill: parent

        // The backdrop is double-buffered together with the full-resolution
        // image so the whole stage changes as one smooth frame. It extends
        // past the stage so the blur has no faded edges.
        Item {
            id: backdropLayer
            anchors.fill: parent
            anchors.margins: -72
            // Window-sized blur passes: allocate them only while the viewer
            // is shown. An always-enabled layer kept its textures until the
            // whole gallery page was destroyed.
            layer.enabled: viewer.visible
            layer.effect: MultiEffect {
                blurEnabled: true
                blur: 1.0
                blurMax: 48
                autoPaddingEnabled: false
            }
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
        }
        Rectangle {
            anchors.fill: parent
            color: AppTheme.viewerScrim
        }

        Flickable {
            id: photoFlick
            objectName: "photoFlick"
            // The picture uses the whole stage, like other viewers; the
            // controls float over it and fade out when idle.
            anchors.fill: parent
            clip: true
            interactive: viewer.zoomedIn
            contentWidth: Math.max(width, viewer.displayWidth)
            contentHeight: Math.max(height, viewer.displayHeight)
            boundsBehavior: Flickable.StopAtBounds

            HoverHandler { cursorShape: viewer.zoomedIn ? Qt.OpenHandCursor : Qt.ArrowCursor }

            Item {
                id: imageHolder
                width: photoFlick.contentWidth
                height: photoFlick.contentHeight

                Image {
                    id: imageA
                    objectName: "photoImageA"
                    anchors.centerIn: parent
                    width: Math.max(80, (viewer.quarterTurn ? photoFlick.height : photoFlick.width) * viewer.zoom)
                    height: Math.max(80, (viewer.quarterTurn ? photoFlick.width : photoFlick.height) * viewer.zoom)
                    // Vector images are rendered at their on-screen size and
                    // photos at the stage size (both again when zoomed).
                    readonly property bool vector: /\.svgz?$/i.test(String(source))
                    sourceSize: vector ? viewer.vectorRenderSize(width, height)
                                       : viewer.rasterSourceSize(source, viewer.zoom)
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    cache: false
                    // Keep the current frame while a zoom step re-decodes.
                    retainWhileLoading: true
                    rotation: viewer.rotationAngle
                    smooth: true
                    mipmap: true
                    opacity: 1.0
                    // Kept while an SVG re-renders at a new zoom (its implicit
                    // size is 0 meanwhile); reset for a different file.
                    property real knownAspect: 0
                    onSourceChanged: knownAspect = 0
                    onStatusChanged: {
                        if (status === Image.Ready && implicitWidth > 0 && implicitHeight > 0)
                            knownAspect = implicitWidth / implicitHeight
                        viewer.handleImageStatus(0, status)
                    }
                }

                Image {
                    id: imageB
                    objectName: "photoImageB"
                    anchors.centerIn: parent
                    width: Math.max(80, (viewer.quarterTurn ? photoFlick.height : photoFlick.width) * viewer.zoom)
                    height: Math.max(80, (viewer.quarterTurn ? photoFlick.width : photoFlick.height) * viewer.zoom)
                    // Vector images are rendered at their on-screen size and
                    // photos at the stage size (both again when zoomed).
                    readonly property bool vector: /\.svgz?$/i.test(String(source))
                    sourceSize: vector ? viewer.vectorRenderSize(width, height)
                                       : viewer.rasterSourceSize(source, viewer.zoom)
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    cache: false
                    // Keep the current frame while a zoom step re-decodes.
                    retainWhileLoading: true
                    rotation: viewer.rotationAngle
                    smooth: true
                    mipmap: true
                    opacity: 0.0
                    // Kept while an SVG re-renders at a new zoom (its implicit
                    // size is 0 meanwhile); reset for a different file.
                    property real knownAspect: 0
                    onSourceChanged: knownAspect = 0
                    onStatusChanged: {
                        if (status === Image.Ready && implicitWidth > 0 && implicitHeight > 0)
                            knownAspect = implicitWidth / implicitHeight
                        viewer.handleImageStatus(1, status)
                    }
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
            // The pointer hides with the controls and returns on any motion.
            cursorShape: !viewer.controlsVisible ? Qt.BlankCursor
                       : viewer.zoomedIn ? Qt.OpenHandCursor : Qt.ArrowCursor
            onPositionChanged: function(mouse) { viewer.handleStagePointerMotion(motionTracker, mouse.x, mouse.y) }
            // The wheel zooms around the pointer (120 units ≈ one 20% step;
            // touchpads send smaller steps for smooth zooming).
            onWheel: function(wheel) {
                const delta = wheel.angleDelta.y
                if (delta === 0) { wheel.accepted = false; return }
                const point = motionTracker.mapToItem(photoFlick, wheel.x, wheel.y)
                viewer.zoomBy(Math.pow(1.0015, delta), point.x, point.y)
            }
        }

        Rectangle {
            objectName: "photoErrorPanel"
            anchors.centerIn: parent
            visible: viewer.imageFailed
            width: Math.min(parent.width - 48, errorColumn.implicitWidth + 40)
            height: errorColumn.implicitHeight + 28
            radius: 12
            color: AppTheme.viewerGlass
            border.width: 1
            border.color: AppTheme.viewerGlassBorder
            Column {
                id: errorColumn
                anchors.centerIn: parent
                spacing: 4
                GText {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: viewer.lang.language === "tr" ? "Bu görsel açılamadı" : "This image could not be opened"
                    baseColor: AppTheme.viewerText
                    basePixelSize: 13
                    baseFontWeight: Font.DemiBold
                }
                GText {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Math.min(implicitWidth, 420)
                    text: viewer.currentItem ? viewer.currentItem.name : ""
                    baseColor: AppTheme.viewerTextMuted
                    basePixelSize: 10
                    elide: Text.ElideMiddle
                }
            }
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
                        GText {
                            id: titleText
                            Layout.fillWidth: true
                            text: viewer.currentItem ? viewer.currentItem.name : ""
                            baseColor: AppTheme.viewerText
                            basePixelSize: 12
                            baseFontWeight: Font.DemiBold
                            elide: Text.ElideMiddle
                        }
                        GText {
                            Layout.fillWidth: true
                            text: viewer.currentItem
                                  ? [viewer.currentDimensions, viewer.formatSize(viewer.currentItem.size),
                                     viewer.formatDate(viewer.currentItem.modifiedMs)].filter(function(part) { return part.length > 0 }).join(" · ")
                                  : ""
                            baseColor: AppTheme.viewerTextMuted
                            basePixelSize: 9
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
                    ViewerIconButton { iconName: "viewer-external.svg"; toolTipText: viewer.lang.language === "tr" ? "Harici aç" : "Open externally"; onClicked: if (viewer.currentItem) photoOpenWith.openDefault(viewer.currentItem.url, "") }
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
                GText {
                    id: counterLabel
                    anchors.centerIn: parent
                    text: viewer.currentIndex >= 0 ? (viewer.currentIndex + 1) + " / " + viewer.files.length : ""
                    baseColor: AppTheme.viewerText
                    basePixelSize: 10
                    baseFontWeight: Font.DemiBold
                }
            }

            ViewerIconButton {
                id: previousButton
                anchors.left: parent.left
                anchors.leftMargin: 18
                anchors.verticalCenter: parent.verticalCenter
                width: 48
                height: 58
                iconName: "nav-back.svg"
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
                iconName: "nav-forward.svg"
                toolTipText: viewer.lang.language === "tr" ? "Sonraki (→)" : "Next (→)"
                onClicked: viewer.next()
            }

            GGlassPanel {
                id: bottomToolbar
                sourceItem: photoScene
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: stripPanel.top
                anchors.bottomMargin: 10
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
                    ViewerIconButton { objectName: "photoZoomOut"; iconName: "viewer-zoom-out.svg"; toolTipText: viewer.lang.language === "tr" ? "Uzaklaştır" : "Zoom out"; autoRepeat: true; autoRepeatDelay: 350; autoRepeatInterval: 60; onClicked: viewer.zoomBy(1 / 1.12) }
                    Rectangle {
                        width: 52; height: 34; radius: 8; color: AppTheme.viewerGlassSoft; border.width: 1; border.color: AppTheme.viewerGlassBorderSoft
                        GText { anchors.centerIn: parent; text: Math.round(viewer.zoom * 100) + "%"; baseColor: AppTheme.viewerText; basePixelSize: 9; baseFontWeight: Font.DemiBold }
                    }
                    ViewerIconButton { objectName: "photoZoomIn"; iconName: "viewer-zoom-in.svg"; toolTipText: viewer.lang.language === "tr" ? "Yakınlaştır" : "Zoom in"; autoRepeat: true; autoRepeatDelay: 350; autoRepeatInterval: 60; onClicked: viewer.zoomBy(1.12) }
                    Rectangle { width: 1; height: 24; color: AppTheme.viewerGlassBorderSoft }
                    ViewerIconButton { iconName: viewer.slideshowRunning ? "viewer-pause.svg" : "viewer-play.svg"; toolTipText: viewer.lang.language === "tr" ? "Slayt gösterisi (Space)" : "Slideshow (Space)"; emphasized: viewer.slideshowRunning; onClicked: viewer.slideshowRunning = !viewer.slideshowRunning }
                }
            }

            GGlassPanel {
                id: stripPanel
                sourceItem: photoScene
                // Compact: only as wide as its thumbnails, centred.
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 10
                width: Math.min(parent.width - 36, filmstrip.contentWidth + 2 * filmstrip.anchors.margins)
                objectName: "photoStripPanel"
                height: 54
                radius: 11
                tintColor: AppTheme.viewerGlassStrong
                borderColor: AppTheme.viewerGlassBorder
                clip: true
                HoverHandler { id: stripHover; onHoveredChanged: if (hovered) viewer.revealControls() }

                ListView {
                    id: filmstrip
                    objectName: "photoFilmstrip"
                    anchors.fill: parent
                    anchors.margins: 5
                    orientation: ListView.Horizontal
                    spacing: 4
                    clip: true
                    // No strip thumbnails while the viewer is closed.
                    model: viewer.visible ? viewer.files : []
                    boundsBehavior: Flickable.StopAtBounds
                    currentIndex: viewer.currentIndex
                    highlightMoveDuration: 120
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        width: 62
                        height: 44
                        radius: 6
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
                            sourceSize.width: 144
                            sourceSize.height: 96
                        }
                        Rectangle {
                            anchors.fill: parent
                            radius: 5
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

        // Navigator for a picture larger than the stage: the visible part is
        // framed on a thumbnail and can be dragged; closing it fits the
        // picture again.
        GGlassPanel {
            id: navigator
            objectName: "photoNavigator"
            sourceItem: photoScene
            readonly property real thumbScale: Math.min(220 / Math.max(1, viewer.displayWidth),
                                                        150 / Math.max(1, viewer.displayHeight))
            readonly property real offsetX: (photoFlick.contentWidth - viewer.displayWidth) / 2
            readonly property real offsetY: (photoFlick.contentHeight - viewer.displayHeight) / 2
            visible: viewer.zoomedIn && !viewer.imageFailed
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.leftMargin: 18
            // Clear of the thumbnail strip; constant so it never moves mid-drag.
            anchors.bottomMargin: 74
            width: navigatorThumb.width + 16
            height: navigatorThumb.height + 16
            radius: 10
            tintColor: AppTheme.viewerGlass
            borderColor: AppTheme.viewerGlassBorder
            HoverHandler { onHoveredChanged: if (hovered) viewer.revealControls() }

            Item {
                id: navigatorThumb
                anchors.centerIn: parent
                width: Math.max(1, Math.round(viewer.displayWidth * navigator.thumbScale))
                height: Math.max(1, Math.round(viewer.displayHeight * navigator.thumbScale))
                clip: true
                Image {
                    anchors.centerIn: parent
                    width: viewer.quarterTurn ? parent.height : parent.width
                    height: viewer.quarterTurn ? parent.width : parent.height
                    rotation: viewer.rotationAngle
                    source: navigator.visible && viewer.currentItem ? viewer.thumbSource(viewer.currentItem) : ""
                    sourceSize.width: 320
                    sourceSize.height: 320
                    fillMode: Image.Stretch
                    asynchronous: true
                    cache: true
                    smooth: true
                }
                Rectangle {
                    anchors.fill: parent
                    color: "#66000000"
                }
                Rectangle {
                    id: navigatorFrame
                    objectName: "photoNavigatorFrame"
                    readonly property real frameLeft: Math.max(0, photoFlick.contentX - navigator.offsetX)
                    readonly property real frameTop: Math.max(0, photoFlick.contentY - navigator.offsetY)
                    readonly property real frameRight: Math.min(viewer.displayWidth, photoFlick.contentX - navigator.offsetX + photoFlick.width)
                    readonly property real frameBottom: Math.min(viewer.displayHeight, photoFlick.contentY - navigator.offsetY + photoFlick.height)
                    x: frameLeft * navigator.thumbScale
                    y: frameTop * navigator.thumbScale
                    width: Math.max(4, (frameRight - frameLeft) * navigator.thumbScale)
                    height: Math.max(4, (frameBottom - frameTop) * navigator.thumbScale)
                    color: "#26FFFFFF"
                    border.width: 2
                    border.color: AppTheme.accent
                    radius: 2
                }
                MouseArea {
                    id: navigatorArea
                    objectName: "photoNavigatorArea"
                    anchors.fill: parent
                    cursorShape: pressed ? Qt.ClosedHandCursor : Qt.SizeAllCursor
                    // Keeps the drag even if an ancestor (popup overlay)
                    // would like to take the pointer over.
                    preventStealing: true
                    // Offset between the pointer and the frame centre: the
                    // frame is carried from where it was grabbed instead of
                    // jumping under the pointer.
                    property real grabX: 0
                    property real grabY: 0
                    function follow(mouse) {
                        viewer.centerOn((mouse.x - grabX) / navigator.thumbScale, (mouse.y - grabY) / navigator.thumbScale)
                    }
                    onPressed: function(mouse) {
                        const inside = mouse.x >= navigatorFrame.x && mouse.x <= navigatorFrame.x + navigatorFrame.width
                                    && mouse.y >= navigatorFrame.y && mouse.y <= navigatorFrame.y + navigatorFrame.height
                        grabX = inside ? mouse.x - (navigatorFrame.x + navigatorFrame.width / 2) : 0
                        grabY = inside ? mouse.y - (navigatorFrame.y + navigatorFrame.height / 2) : 0
                        if (!inside)
                            follow(mouse)
                    }
                    onPositionChanged: function(mouse) { if (navigatorArea.pressed) follow(mouse) }
                    onWheel: function(wheel) {
                        if (wheel.angleDelta.y !== 0) viewer.zoomBy(Math.pow(1.0015, wheel.angleDelta.y))
                    }
                }
            }
            ViewerIconButton {
                objectName: "photoNavigatorClose"
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: 4
                width: 24
                height: 24
                iconName: "close-ui.svg"
                toolTipText: viewer.lang.language === "tr" ? "Kapat ve sığdır" : "Close and fit"
                onClicked: viewer.resetView()
            }
        }
    }
}
