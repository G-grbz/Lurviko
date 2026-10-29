pragma Singleton

import QtQuick
import QtCore
import Lurviko.Backend
import "FolderArtwork.js" as FolderArtwork

QtObject {
    property Settings settings: Settings {
        category: "Appearance"
        property bool darkMode: false
        property bool qtTheme: false
        property bool sidebarAutoHide: false
        property string sidebarRailMode: "both"
        property bool systemIcons: false
        property bool coloredFolderIcons: false
        property bool kioProgressEnabled: false
        property bool operationAutoHide: true
        property int operationAutoHideSeconds: 8
        property int sidebarIconSize: 19
        property int homeIconSize: 28
        property int systemSidebarIconSize: 22
        property int systemHomeIconSize: 48
        property int wheelScrollStep: 250
        property int fileTitleLines: 1
        property int fileSubtitleLines: 1
    }

    // Role settings are independent of the palette: empty color and zero px
    // follow the active Qt/Lurviko theme and the original responsive size.
    property Settings typographySettings: Settings {
        category: "Typography"
        property string rolesJson: "{}"
    }
    property var typography: {
        try { return JSON.parse(typographySettings.rolesJson) || ({}) } catch (error) { return ({}) }
    }
    readonly property var typographyRoles: ["title", "subtitle", "body", "sidebar", "description", "menu"]
    readonly property var availableFontFamilies: Qt.fontFamilies()
    readonly property string defaultFontFamily: availableFontFamilies.indexOf("Roboto") >= 0
        ? "Roboto" : Qt.application.font.family
    readonly property TypographyStyle titleTypography: TypographyStyle {
        specification: JSON.stringify(typography.title || ({}))
        defaultFamily: defaultFontFamily
        families: availableFontFamilies
    }
    readonly property TypographyStyle subtitleTypography: TypographyStyle {
        specification: JSON.stringify(typography.subtitle || ({}))
        defaultFamily: defaultFontFamily
        families: availableFontFamilies
    }
    readonly property TypographyStyle bodyTypography: TypographyStyle {
        specification: JSON.stringify(typography.body || ({}))
        defaultFamily: defaultFontFamily
        families: availableFontFamilies
    }
    readonly property TypographyStyle sidebarTypography: TypographyStyle {
        specification: JSON.stringify(typography.sidebar || ({}))
        defaultFamily: defaultFontFamily
        families: availableFontFamilies
    }
    readonly property TypographyStyle descriptionTypography: TypographyStyle {
        specification: JSON.stringify(typography.description || ({}))
        defaultFamily: defaultFontFamily
        families: availableFontFamilies
    }
    readonly property TypographyStyle menuTypography: TypographyStyle {
        specification: JSON.stringify(typography.menu || ({}))
        defaultFamily: defaultFontFamily
        families: availableFontFamilies
    }
    function styleFor(role) {
        switch (role) {
        case "title": return titleTypography
        case "subtitle": return subtitleTypography
        case "sidebar": return sidebarTypography
        case "description": return descriptionTypography
        case "menu": return menuTypography
        default: return bodyTypography
        }
    }
    function typographyValue(role) { return styleFor(role).options }
    function typographyFamily(role) { return styleFor(role).family }
    function typographySize(role, automaticPixels) { return styleFor(role).pixels || automaticPixels }
    function typographyColor(role, automaticColor) { return styleFor(role).customColor || automaticColor }
    function typographyFlag(role, field, automaticValue) { return styleFor(role).flag(field, automaticValue) }
    function typographyWeight(role, automaticWeight) { return styleFor(role).weight(automaticWeight) }
    function setTypography(role, field, value) {
        if (typographyRoles.indexOf(role) < 0 || ["family", "pixels", "color", "bold", "italic", "underline", "overline", "strikeout"].indexOf(field) < 0) return
        if (field === "family" && value && availableFontFamilies.indexOf(value) < 0) return
        if (field === "color" && value && !/^#[0-9a-fA-F]{6}$/.test(value)) return
        if (field === "pixels") value = Number(value) === 0 ? 0 : Math.max(8, Math.min(48, Math.round(Number(value) || 12)))
        const next = JSON.parse(JSON.stringify(typography))
        const entry = next[role] || ({})
        if (["bold", "italic", "underline", "overline", "strikeout"].indexOf(field) >= 0) {
            if (value === null) delete entry[field]
            else if (typeof value === "boolean") entry[field] = value
            else return
        } else entry[field] = value
        next[role] = entry
        typographySettings.rolesJson = JSON.stringify(next)
    }
    function resetTypography() { typographySettings.rolesJson = "{}" }

    property Settings interactionSettings: Settings {
        category: "Interaction"
        property bool singleClickOpen: false
    }

    property Settings musicPreviewSettings: Settings {
        category: "MusicPreview"
        property bool hoverEnabled: true
        property bool hoverAnimationsEnabled: true
        property int hoverDelayMs: 600
    }
    readonly property bool musicHoverEnabled: musicPreviewSettings.hoverEnabled
    readonly property bool musicHoverAnimationsEnabled: musicPreviewSettings.hoverAnimationsEnabled
    readonly property int musicHoverDelayMs: Math.max(120, Math.min(1200, musicPreviewSettings.hoverDelayMs))
    readonly property var hoverDelayOptions: [120, 200, 360, 500, 600, 700, 1000, 1200]
    function setMusicHoverPreferences(enabled, animations, delayMs) {
        musicPreviewSettings.hoverEnabled = !!enabled
        musicPreviewSettings.hoverAnimationsEnabled = !!animations
        musicPreviewSettings.hoverDelayMs = Math.max(120, Math.min(1200, Math.round(delayMs)))
        musicPreviewSettings.sync()
    }

    // Share the existing preference across tabs and panes without changing its saved key.
    property Settings browserInteractionSettings: Settings {
        category: "BrowserView"
        property bool skipPermanentDeleteConfirmation: false
        property bool skipHiddenNameConfirmation: false
        property int categoryIconSize: 168
        property int musicIconSize: 168
    }

    // One live value for every tab/page. Per-pane Settings instances do not
    // notify one another, so reopened categories could restore a stale size.
    property int categoryIconSize: Math.max(104, Math.min(360, browserInteractionSettings.categoryIconSize))
    property int musicIconSize: Math.max(104, Math.min(360, browserInteractionSettings.musicIconSize))
    onCategoryIconSizeChanged: categorySizeSaveTimer.restart()
    onMusicIconSizeChanged: categorySizeSaveTimer.restart()
    property Timer categorySizeSaveTimer: Timer {
        interval: 180
        onTriggered: flushCategoryIconSizes()
    }
    function flushCategoryIconSizes() {
        browserInteractionSettings.categoryIconSize = categoryIconSize
        browserInteractionSettings.musicIconSize = musicIconSize
    }
    Component.onDestruction: flushCategoryIconSizes()

    readonly property bool singleClickOpen: interactionSettings.singleClickOpen
    readonly property bool skipPermanentDeleteConfirmation: browserInteractionSettings.skipPermanentDeleteConfirmation
    readonly property bool skipHiddenNameConfirmation: browserInteractionSettings.skipHiddenNameConfirmation
    property DesktopAppearance desktopAppearance: DesktopAppearance {
        enabled: settings.qtTheme
    }
    readonly property bool useQtTheme: settings.qtTheme
    readonly property var qtColors: desktopAppearance.colors
    readonly property bool dark: useQtTheme ? desktopAppearance.dark : settings.darkMode
    readonly property bool translucentWindow: useQtTheme && background.a < 1
    readonly property color highlightedText: useQtTheme ? qtColors.highlightedText : "white"
    function mix(first, second, amount) {
        return Qt.rgba(first.r * (1 - amount) + second.r * amount,
                       first.g * (1 - amount) + second.g * amount,
                       first.b * (1 - amount) + second.b * amount,
                       first.a * (1 - amount) + second.a * amount)
    }
    function tint(color, alpha) { return Qt.rgba(color.r, color.g, color.b, alpha) }
    readonly property bool useSystemIcons: settings.systemIcons
    readonly property bool useColoredFolderIcons: settings.coloredFolderIcons
    readonly property real hiddenItemOpacity: 0.70
    function bundledFolderIcon(name, identity) {
        return icon(useColoredFolderIcons ? FolderArtwork.iconName(name) : "folder.svg")
    }
    function itemIsHidden(name, path, knownHidden) {
        if (knownHidden === true || String(name || "").startsWith(".")) return true
        var value = String(path || "").replace(/\/$/, "")
        var leaf = value.substring(value.lastIndexOf("/") + 1)
        try { leaf = decodeURIComponent(leaf) } catch (error) {}
        return leaf.startsWith(".")
    }
    readonly property bool kioProgressEnabled: settings.kioProgressEnabled
    readonly property bool operationAutoHide: settings.operationAutoHide
    readonly property int operationAutoHideSeconds: Math.max(1, Math.min(300, settings.operationAutoHideSeconds))
    function setOperationAutoHide(enabled) { settings.operationAutoHide = !!enabled }
    function setOperationAutoHideSeconds(seconds) {
        if (isFinite(seconds)) settings.operationAutoHideSeconds = Math.max(1, Math.min(300, Math.round(seconds)))
    }
    // Accepted drop targets must remain distinct from ordinary selection,
    // including when the desktop's highlight color is already green.
    readonly property color dropTargetColor: Math.abs(accent.r - success.r) + Math.abs(accent.g - success.g)
                                             + Math.abs(accent.b - success.b) > 0.45 ? success : warning
    readonly property color dropTargetSurface: mix(surface, dropTargetColor, dark ? 0.24 : 0.15)
    readonly property int sidebarIconSize: Math.max(14, Math.min(32, settings.sidebarIconSize))
    readonly property int homeIconSize: Math.max(20, Math.min(44, settings.homeIconSize))
    readonly property int systemSidebarIconSize: Math.max(8, Math.min(128, settings.systemSidebarIconSize))
    readonly property int systemHomeIconSize: Math.max(8, Math.min(128, settings.systemHomeIconSize))
    // Discover/sidebar icon geometry is shared across bundled and system icons.
    // Switching icon source must not make the layout jump to a different px scale.
    readonly property bool sidebarAutoHide: settings.sidebarAutoHide
    readonly property string sidebarRailMode: ["both", "title", "icon"].indexOf(settings.sidebarRailMode) >= 0
        ? settings.sidebarRailMode : "both"
    function setSidebarAutoHide(enabled) { settings.sidebarAutoHide = !!enabled }
    function setSidebarRailMode(mode) {
        if (["both", "title", "icon"].indexOf(mode) >= 0) settings.sidebarRailMode = mode
    }
    readonly property int sidebarSectionIconPixels: 13
    readonly property int sidebarSectionFontPixels: 10
    readonly property real sidebarSectionLetterSpacing: 0.8
    readonly property int effectiveSidebarIconSize: systemSidebarIconSize
    readonly property int effectiveHomeIconSize: systemHomeIconSize
    readonly property int wheelScrollStep: Math.max(10, Math.min(1000, settings.wheelScrollStep))
    readonly property int fileTitleLines: Math.max(1, Math.min(4, settings.fileTitleLines))
    readonly property int fileSubtitleLines: Math.max(1, Math.min(4, settings.fileSubtitleLines))
    function fileLabelLineHeight(pixels) { return Math.ceil(pixels * 1.5) }
    function previewResolution(extent, devicePixelRatio) {
        // Match physical display pixels, with reusable 64-pixel size buckets.
        const pixels = Math.max(1, extent) * Math.max(1, devicePixelRatio)
        return Math.max(96, Math.min(512, Math.ceil(pixels / 64) * 64))
    }
    function fileLabelExtraHeight(titlePixels, subtitlePixels) {
        const title = typographySize("body", titlePixels)
        const subtitle = typographySize("description", subtitlePixels)
        return (fileTitleLines - 1) * fileLabelLineHeight(title)
             + (fileSubtitleLines - 1) * fileLabelLineHeight(subtitle)
             + Math.max(0, fileLabelLineHeight(title) - fileLabelLineHeight(titlePixels))
             + Math.max(0, fileLabelLineHeight(subtitle) - fileLabelLineHeight(subtitlePixels))
    }
    // Lurviko UI design tokens.  Qt Quick Controls provide behaviour, focus,
    // accessibility and keyboard semantics. Optional desktop colors come from
    // the native QApplication palette, without changing the existing layout.
    readonly property string designSystem: "Lurviko UI"
    readonly property int designSystemVersion: 2

    readonly property color background: useQtTheme ? desktopAppearance.windowBackground : (dark ? "#080C16" : "#E9EDF4")
    readonly property color workspace: useQtTheme ? (translucentWindow ? "transparent" : qtColors.window) : (dark ? "#0B101D" : "#F5F7FB")
    readonly property color sidebar: useQtTheme ? (translucentWindow ? "transparent" : qtColors.window) : (dark ? "#101827" : "#FBFCFE")
    readonly property color commandBar: useQtTheme ? (translucentWindow ? Qt.rgba(qtColors.button.r, qtColors.button.g, qtColors.button.b, 0.08) : qtColors.window) : (dark ? "#111A2B" : "#FFFFFF")
    readonly property color popupSurface: useQtTheme ? tint(desktopAppearance.menuBackground, 1) : commandBar
    // Native view palettes may deliberately contain transparent Base/Window
    // brushes. Fields and fallback surfaces still need a readable solid fill.
    readonly property color surface: useQtTheme ? tint(qtColors.base, 1) : (dark ? "#121C2D" : "#FFFFFF")
    readonly property color surfaceRaised: useQtTheme ? tint(qtColors.window, 1) : (dark ? "#18253A" : "#F8FAFD")
    readonly property color surfaceSunken: useQtTheme ? tint(qtColors.alternateBase, 1) : (dark ? "#0D1524" : "#EEF2F7")
    readonly property color viewBackground: translucentWindow ? "transparent" : surfaceRaised
    readonly property color surfaceHover: useQtTheme ? mix(qtColors.button, qtColors.highlight, 0.10) : (dark ? "#1D2A42" : "#F0F3F9")
    // Keep RGB unchanged while fading in; transparent black darkens intermediate frames.
    readonly property color surfaceHoverTransparent: Qt.rgba(surfaceHover.r, surfaceHover.g, surfaceHover.b, 0)
    readonly property color surfacePressed: useQtTheme ? mix(qtColors.button, qtColors.highlight, 0.20) : (dark ? "#263854" : "#E5EAF3")
    readonly property color surfaceActive: useQtTheme ? mix(qtColors.base, qtColors.highlight, 0.16) : (dark ? "#252C4B" : "#EEECFF")
    readonly property color cardSurface: useQtTheme ? (translucentWindow ? tint(qtColors.base, 0.06) : qtColors.base) : (dark ? "#121D30" : "#FFFFFF")
    readonly property color cardHover: useQtTheme ? (translucentWindow ? tint(qtColors.highlight, 0.12) : mix(qtColors.base, qtColors.highlight, 0.06)) : (dark ? "#192942" : "#FAFBFF")
    // QPalette::Mid/Dark are bevel roles, not outline colors. Some native
    // themes set them to almost black, producing heavy boxes in a flat UI.
    readonly property color border: useQtTheme ? tint(qtColors.text, 0.13) : (dark ? "#22304A" : "#DDE3ED")
    readonly property color borderStrong: useQtTheme ? tint(qtColors.text, 0.23) : (dark ? "#334462" : "#C7D0DE")
    readonly property color panelSurface: useQtTheme && translucentWindow ? tint(qtColors.base, 0.04) : surfaceRaised
    readonly property color text: useQtTheme ? qtColors.text : (dark ? "#F4F7FC" : "#1A2030")
    readonly property color textMuted: useQtTheme ? mix(qtColors.text, qtColors.window, 0.30) : (dark ? "#A4AFC3" : "#596477")
    readonly property color textFaint: useQtTheme ? qtColors.placeholderText : (dark ? "#7F8BA3" : "#7B8598")
    readonly property color accent: useQtTheme ? qtColors.highlight : (dark ? "#8B7CF6" : "#6357E8")
    readonly property color accentHover: useQtTheme ? mix(qtColors.highlight, qtColors.text, 0.12) : (dark ? "#9E92FA" : "#5548D8")
    readonly property color accentSoft: useQtTheme ? mix(qtColors.base, qtColors.highlight, 0.13) : (dark ? "#29254B" : "#EEECFF")
    readonly property color accentBorder: useQtTheme ? mix(qtColors.mid, qtColors.highlight, 0.50) : (dark ? "#504889" : "#D9D5FF")
    readonly property color success: dark ? "#54D6A0" : "#18885B"
    readonly property color warning: dark ? "#F2BD66" : "#B56C09"
    readonly property color danger: dark ? "#FF8089" : "#D84B57"
    readonly property color shadow: dark ? "#80000000" : "#180B1220"
    readonly property color focusRing: Qt.rgba(accent.r, accent.g, accent.b, dark ? 0.34 : 0.22)
    readonly property color selection: accent

    // Media-viewer chrome palette.  Backdrop blur is intentionally disabled
    // for now: on some Qt/Wayland/RHI combinations ShaderEffectSource does
    // not produce a reliable backdrop blur for VideoOutput.  Use fully opaque
    // theme-aware surfaces instead so controls never look like broken glass.
    readonly property color viewerBackground: useQtTheme ? tint(qtColors.window, 1) : (dark ? "#080B11" : "#E9EDF4")
    readonly property color viewerScrim: dark ? "#52080B11" : "#32FFFFFF"
    readonly property color viewerGlass: useQtTheme ? surface : (dark ? "#1B2533" : "#FBFCFE")
    readonly property color viewerGlassStrong: useQtTheme ? surfaceRaised : (dark ? "#18212E" : "#F5F7FB")
    readonly property color viewerGlassSoft: useQtTheme ? surfaceSunken : (dark ? "#232E3D" : "#EEF2F7")
    readonly property color viewerGlassBorder: useQtTheme ? borderStrong : (dark ? "#48FFFFFF" : "#360B1220")
    readonly property color viewerGlassBorderSoft: useQtTheme ? border : (dark ? "#30FFFFFF" : "#240B1220")
    readonly property color viewerGlassHover: useQtTheme ? surfaceHover : (dark ? "#2CFFFFFF" : "#180B1220")
    readonly property color viewerGlassPressed: useQtTheme ? surfacePressed : (dark ? "#38FFFFFF" : "#260B1220")
    readonly property color viewerGlassEmphasis: useQtTheme ? accentSoft : (dark ? "#26FFFFFF" : "#165F52E8")
    readonly property color viewerText: useQtTheme ? qtColors.text : (dark ? "#F7FAFF" : "#1A2030")
    readonly property color viewerTextMuted: useQtTheme ? textMuted : (dark ? "#C7FFFFFF" : "#B2596477")
    readonly property color viewerTrack: useQtTheme ? qtColors.mid : (dark ? "#36FFFFFF" : "#300B1220")
    readonly property color viewerHandle: useQtTheme ? qtColors.button : (dark ? "#FFFFFFFF" : "#FFFBFCFE")

    readonly property int radiusSmall: 8
    readonly property int radiusMedium: 12
    readonly property int radiusLarge: 18
    readonly property int radiusXLarge: 24
    readonly property int panelRadius: 24
    readonly property int cardRadius: 20
    readonly property int commandRadius: 18
    readonly property int shellMargin: 10
    readonly property int buttonRadius: 14
    readonly property int buttonRadiusCompact: 12
    readonly property int fieldRadius: 12
    readonly property int itemRadius: 12

    readonly property int controlHeightCompact: 34
    readonly property int controlHeight: 40
    readonly property int controlHeightLarge: 46
    readonly property int buttonHeight: controlHeight
    readonly property int toolButtonSize: controlHeight

    readonly property int space1: 4
    readonly property int space2: 8
    readonly property int space3: 12
    readonly property int space4: 16
    readonly property int space5: 20
    readonly property int space6: 24

    readonly property int fontSizeCaption: 10
    readonly property int fontSizeBody: 12
    readonly property int fontSizeControl: 13
    readonly property int fontSizeTitle: 16

    readonly property int motionFast: 90
    readonly property int motionNormal: 140
    readonly property int motionSlow: 220

    function toggle() { if (!useQtTheme) settings.darkMode = !settings.darkMode }
    function setQtTheme(enabled) {
        settings.qtTheme = !!enabled
        // Settings' property autosave is deferred; also write the key before
        // syncing so an immediate close/reopen cannot restore the old mode.
        settings.setValue("qtTheme", !!enabled)
        settings.sync()
    }
    function setSingleClickOpen(enabled) { interactionSettings.singleClickOpen = !!enabled }
    function setSystemIcons(enabled) { settings.systemIcons = enabled }
    function setColoredFolderIcons(enabled) { settings.coloredFolderIcons = !!enabled }
    function setSkipPermanentDeleteConfirmation(enabled) { browserInteractionSettings.skipPermanentDeleteConfirmation = enabled }
    function setSkipHiddenNameConfirmation(enabled) { browserInteractionSettings.skipHiddenNameConfirmation = enabled }
    function setKioProgressEnabled(enabled) { settings.kioProgressEnabled = enabled }
    function setSidebarIconSize(size) { setSystemSidebarIconSize(size) }
    function setHomeIconSize(size) { setSystemHomeIconSize(size) }
    function setSystemSidebarIconSize(size) { settings.systemSidebarIconSize = Math.max(8, Math.min(128, Math.round(size))) }
    function setSystemHomeIconSize(size) { settings.systemHomeIconSize = Math.max(8, Math.min(128, Math.round(size))) }
    function setWheelScrollStep(size) { settings.wheelScrollStep = Math.max(10, Math.min(1000, Math.round(size))) }
    function setFileTitleLines(lines) { settings.fileTitleLines = Math.max(1, Math.min(4, Math.round(lines))) }
    function setFileSubtitleLines(lines) { settings.fileSubtitleLines = Math.max(1, Math.min(4, Math.round(lines))) }
    function systemIconName(name) {
        const names = {
            "folder.svg": "folder",
            "file.svg": "text-x-generic",
            "image.svg": "folder-pictures",
            "video.svg": "folder-videos",
            "audio.svg": "folder-music",
            "archive.svg": "package-x-generic",
            "pdf.svg": "application-pdf",
            "drive.svg": "drive-harddisk",
            "trash.svg": "user-trash",
            "trashfull.svg": "user-trash-full",
            "network.svg": "network-workgroup",
            "root.svg": "folder-root",
            "favorite.svg": "rating",
            "private.svg": "folder-locked",
            "dashboard.svg": "view-grid",
            "home-folder.svg": "user-home",
            "desktop.svg": "user-desktop",
            "downloads.svg": "folder-download",
            "documents.svg": "folder-documents",
            "settings.svg": "settings-configure",
            "cloud.svg": "folder-cloud",
            "plus.svg": "list-add",
            "edit-cut.svg": "edit-cut",
            "edit-copy.svg": "edit-copy",
            "edit-paste.svg": "edit-paste",
            "edit-rename.svg": "edit-rename",
            "delete.svg": "user-trash",
            "properties.svg": "document-properties",
            "refresh.svg": "view-refresh",
            "edit.svg": "document-edit",
            "moon.svg": "weather-clear-night",
            "sun.svg": "weather-clear",
            "info.svg": "help-about",
            "eye.svg": "view-visible",
            "split-view.svg": "view-split-left-right",
            "view-layout.svg": "view-list-icons",
            "history.svg": "document-open-recent",
            // Media controls need explicit theme names. Without these they fall
            // through to text-x-generic and appear as document icons (or empty
            // controls, depending on the active icon theme).
            "music-note.svg": "multimedia-player",
            "viewer-prev.svg": "media-skip-backward",
            "viewer-play.svg": "media-playback-start",
            "viewer-pause.svg": "media-playback-pause",
            "viewer-next.svg": "media-skip-forward",
            "music-shuffle.svg": "media-playlist-shuffle",
            "music-repeat.svg": "media-playlist-repeat",
            "music-repeat-one.svg": "media-playlist-repeat-song",
            "music-expand.svg": "view-fullscreen",
            "viewer-volume.svg": "audio-volume-high",
            "viewer-volume-muted.svg": "audio-volume-muted",
            "music-queue.svg": "view-media-playlist",
            "music-playlist.svg": "view-list-details",
            "close-ui.svg": "window-close",
            "appimage.svg": "folder-appimage",
            "filelight.svg": "filelight"
        }
        return names[name] || "text-x-generic"
    }
    function normalizedFolderName(name) {
        var value = (name || "").toLowerCase()
        value = value.replace(/ç/g, "c").replace(/ğ/g, "g").replace(/ı/g, "i")
                     .replace(/ö/g, "o").replace(/ş/g, "s").replace(/ü/g, "u")
        return value.replace(/[\s_.-]+/g, "")
    }

    function folderSystemIcon(name, currentIcon) {
        const existing = (currentIcon || "").trim()
        // Respect explicit .directory/KIO icons. Only generic folders are
        // automatically classified and colorized.
        if (existing.length > 0 && existing !== "folder" && existing !== "inode-directory"
                && existing !== "gnome-fs-directory" && existing !== "gtk-directory")
            return existing

        const key = normalizedFolderName(name)
        const semantic = {
            "music": "folder-music", "muzik": "folder-music", "songs": "folder-music",
            "audio": "folder-music", "musics": "folder-music",
            "video": "folder-videos", "videos": "folder-videos", "videolar": "folder-videos",
            "movies": "folder-videos", "movie": "folder-videos", "film": "folder-videos", "films": "folder-videos",
            "pictures": "folder-pictures", "picture": "folder-pictures", "images": "folder-pictures",
            "image": "folder-pictures", "resimler": "folder-pictures", "resim": "folder-pictures",
            "photos": "folder-camera", "photo": "folder-camera", "fotograflar": "folder-camera", "fotograf": "folder-camera",
            "documents": "folder-documents", "document": "folder-documents", "docs": "folder-documents",
            "belgeler": "folder-documents", "belge": "folder-documents",
            "downloads": "folder-download", "download": "folder-download", "indirilenler": "folder-download", "indirmeler": "folder-download",
            "home": "user-home", "ev": "user-home", "anaklasor": "user-home",
            "desktop": "user-desktop", "masaustu": "user-desktop",
            "templates": "folder-templates", "template": "folder-templates", "sablonlar": "folder-templates",
            "public": "folder-publicshare", "publicshare": "folder-publicshare", "shared": "folder-publicshare",
            "projects": "folder-development", "project": "folder-development", "projeler": "folder-development",
            "development": "folder-development", "dev": "folder-development", "code": "folder-development", "kod": "folder-development",
            "games": "folder-games", "game": "folder-games", "oyunlar": "folder-games", "oyun": "folder-games",
            "github": "folder-git", "git": "folder-git",
            "docker": "folder-docker", "android": "folder-android", "appimage": "folder-appimage",
            "build": "folder-build", "database": "folder-database", "db": "folder-database",
            "design": "folder-design", "tasarim": "folder-design", "flatpak": "folder-flatpak",
            "mail": "folder-mail", "email": "folder-mail", "notes": "folder-notes", "notlar": "folder-notes",
            "scripts": "folder-script", "script": "folder-script", "podcasts": "folder-podcast", "podcast": "folder-podcast",
            "books": "folder-book", "book": "folder-book", "kitaplar": "folder-book",
            "cloud": "folder-cloud", "bulut": "folder-cloud"
        }
        if (semantic[key])
            return semantic[key]

        // Use stable theme-provided folder colors for ordinary directories.
        // The same folder name always maps to the same color.
        const colors = ["folder-blue", "folder-green", "folder-orange", "folder-purple",
                        "folder-red", "folder-yellow", "folder-cyan", "folder-violet"]
        if (key.length === 0)
            return "folder"
        var hash = 0
        for (var i = 0; i < key.length; ++i)
            hash = ((hash * 31) + key.charCodeAt(i)) & 0x7fffffff
        return colors[hash % colors.length]
    }

    function systemIcon(name) {
        const local = localIconSource(name)
        if (local.length)
            return local
        const theme = String(name || "")
        return "image://systemicon/" + encodeURIComponent(/^[A-Za-z0-9._+-]+$/.test(theme) ? theme : "text-x-generic")
    }

    // File artwork is an explicit user choice in both modes. Older settings
    // sometimes contain "system:/absolute/path"; resolve these as files too.
    function localIconSource(name) {
        var raw = String(name || "").trim()
        if (raw.indexOf("system:") === 0)
            raw = raw.substring(7)
        if (raw.indexOf("\u0000") !== -1)
            return ""
        if (raw.indexOf("file:") === 0) {
            // Match persisted local references: no remote authority, credentials,
            // query/fragment or encoded NUL. Never turn an icon into a network URL.
            if (!/^file:(?:\/(?!\/)|\/\/\/(?!\/)|\/\/localhost\/)/i.test(raw)
                    || /[?#]/.test(raw))
                return ""
            try {
                return decodeURIComponent(raw).indexOf("\u0000") === -1 ? raw : ""
            } catch (error) {
                return ""
            }
        }
        if (raw.indexOf("/") === 0)
            return "file://" + encodeURI(raw).replace(/#/g, "%23").replace(/\?/g, "%3F")
        return ""
    }

    function iconSelection(name) {
        const raw = String(name || "").trim()
        if (!raw.length)
            return ""
        const local = localIconSource(raw)
        if (local.length)
            return local
        return /^(?:system:)?[A-Za-z0-9._+-]+$/.test(raw)
                ? (raw.indexOf("system:") === 0 ? raw : "system:" + raw) : ""
    }

    function iconPickerInitial(name) {
        const raw = String(name || "").trim()
        const local = localIconSource(raw)
        if (local.length)
            return local
        return raw.indexOf("system:") === 0 ? raw.substring(7) : systemIconName(raw)
    }

    // Keep the logical artwork size in the URL; sourceSize supplies the screen
    // pixel resolution separately. The asynchronous provider selects the theme
    // artwork at this size and caches its rendered pixels across navigation.
    function systemIconAtSize(source, size) {
        const raw = String(source || "")
        if (raw.indexOf("image://systemicon/") !== 0)
            return source
        // Layouts briefly give newly created delegates a zero/one-pixel icon
        // area. Wait for their real size instead of requesting throwaway SVGs.
        if (size <= 1)
            return ""
        const base = raw.replace(/\|[0-9]+$/, "")
        return base + "|" + Math.max(1, Math.round(size))
    }

    function discoveryIcon(name, fallbackName) {
        const raw = String(name || "").trim()
        const fallback = String(fallbackName || "folder.svg").trim() || "folder.svg"
        const local = localIconSource(raw)
        if (local.length)
            return local
        // A system-theme icon chosen while System Icons was active is a
        // per-mode customization. Returning to bundled/default icons must
        // restore the bundled fallback instead of keeping the old theme icon.
        if (!useSystemIcons && raw.indexOf("system:") === 0)
            return icon(fallback)
        return customIcon(raw.length ? raw : fallback)
    }

    function categoryDefaultIcon(key) {
        if (key === "private") return "private.svg"
        if (key === "documents") return "documents.svg"
        if (key === "appimage") return "appimage.svg"
        if (key === "archives") return "archive.svg"
        if (key === "images") return "image.svg"
        if (key === "music") return "audio.svg"
        if (key === "videos") return "video.svg"
        return "folder.svg"
    }

    function customIcon(name) {
        if (!name || name.length === 0)
            return icon("folder.svg")
        const local = localIconSource(name)
        if (local.length)
            return local
        if (name.indexOf("system:") === 0)
            return systemIcon(name.substring(7))
        return icon(name)
    }
    // Bundled application/UI artwork is intentionally independent from the
    // "System icons" preference. That preference is applied explicitly by
    // file/folder/navigation delegates through systemIcon()/systemIconAtSize().
    // Keeping buttons, menus, player controls and toolbar actions on bundled
    // artwork prevents light-only/dark-only icon themes from making controls
    // disappear when Lurviko's own theme is switched.
    function icon(name, foreground) {
        const raw = String(name || "")
        if (raw.length === 0)
            return ""
        const local = localIconSource(raw)
        if (local.length)
            return local
        // Explicit system-icon selections are still respected for places where
        // the user deliberately picked a theme icon (for example sidebar item
        // customization).
        if (raw.indexOf("system:") === 0)
            return systemIcon(raw.substring(7))
        if (raw.indexOf("image://systemicon/") === 0
                || raw.indexOf("image://bundledicon/") === 0
                || raw.indexOf("image://folderart/") === 0
                || raw.indexOf("qrc:/") === 0)
            return raw
        if (!/^[A-Za-z0-9._+-]+$/.test(raw))
            return icon("folder.svg")
        // Explicitly selected UI glyphs only: category artwork and logos keep
        // their original colors. Include the color in the URL for Qt's cache.
        const monochrome = ["history.svg", "info.svg", "keyboard.svg", "moon.svg", "sun.svg", "eye.svg", "music-note.svg", "lock.svg", "unlock.svg",
            "nav-back.svg", "nav-forward.svg", "nav-up.svg",
            "close-ui.svg", "edit.svg", "search.svg", "settings.svg", "split-view.svg", "split-view-off.svg", "view-layout.svg",
            "music-chevron-up.svg", "music-expand.svg", "music-minimize.svg", "music-playlist.svg",
            "music-queue.svg", "music-refresh.svg", "music-repeat.svg", "music-repeat-one.svg",
            "music-shuffle.svg", "music-stop.svg", "music-heart.svg", "music-heart-filled.svg",
            "clone.svg", "viewer-external.svg", "viewer-fit.svg", "viewer-folder.svg",
            "viewer-forward.svg", "viewer-next.svg", "viewer-pause.svg", "viewer-play.svg",
            "viewer-prev.svg", "viewer-rewind.svg", "viewer-rotate-left.svg", "viewer-rotate-right.svg",
            "viewer-fullscreen.svg", "viewer-fullscreen-exit.svg", "viewer-volume.svg", "viewer-volume-muted.svg",
            "viewer-zoom-in.svg", "viewer-zoom-out.svg"]
        if (monochrome.indexOf(raw) !== -1)
            return "image://bundledicon/" + raw + "|" + encodeURIComponent(String(foreground === undefined ? text : foreground))
        if (/^(?:folder|(?:blue|green|orange|pink|purple|red|turquoise|yellow)-(?:folder|documents|download|music|picture|video))\.svg$/.test(raw))
            return "image://folderart/" + raw
        return "qrc:/qt/qml/Lurviko/App/assets/icons/" + raw
    }
}
