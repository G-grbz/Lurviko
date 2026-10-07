pragma Singleton

import QtQuick
import QtCore

QtObject {
    property Settings settings: Settings {
        category: "Appearance"
        property bool darkMode: false
        property bool systemIcons: false
        property bool kioProgressEnabled: false
        property int sidebarIconSize: 19
        property int homeIconSize: 28
        property int systemSidebarIconSize: 22
        property int systemHomeIconSize: 48
        property int wheelScrollStep: 250
    }

    property Settings interactionSettings: Settings {
        category: "Interaction"
        property bool singleClickOpen: false
    }

    readonly property bool singleClickOpen: interactionSettings.singleClickOpen
    readonly property bool dark: settings.darkMode
    readonly property bool useSystemIcons: settings.systemIcons
    readonly property bool kioProgressEnabled: settings.kioProgressEnabled
    readonly property int sidebarIconSize: Math.max(14, Math.min(32, settings.sidebarIconSize))
    readonly property int homeIconSize: Math.max(20, Math.min(44, settings.homeIconSize))
    readonly property int systemSidebarIconSize: Math.max(8, Math.min(128, settings.systemSidebarIconSize))
    readonly property int systemHomeIconSize: Math.max(8, Math.min(128, settings.systemHomeIconSize))
    // Discover/sidebar icon geometry is shared across bundled and system icons.
    // Switching icon source must not make the layout jump to a different px scale.
    readonly property int effectiveSidebarIconSize: systemSidebarIconSize
    readonly property int effectiveHomeIconSize: systemHomeIconSize
    readonly property int wheelScrollStep: Math.max(10, Math.min(1000, settings.wheelScrollStep))
    // G-File UI design tokens.  Qt Quick Controls provide behaviour, focus,
    // accessibility and keyboard semantics; the visual language below belongs
    // to G-File and is intentionally independent from the desktop Qt theme.
    readonly property string designSystem: "G-File UI"
    readonly property int designSystemVersion: 2

    readonly property color background: dark ? "#080C16" : "#E9EDF4"
    readonly property color workspace: dark ? "#0B101D" : "#F5F7FB"
    readonly property color sidebar: dark ? "#101827" : "#FBFCFE"
    readonly property color commandBar: dark ? "#111A2B" : "#FFFFFF"
    readonly property color surface: dark ? "#121C2D" : "#FFFFFF"
    readonly property color surfaceRaised: dark ? "#18253A" : "#F8FAFD"
    readonly property color surfaceSunken: dark ? "#0D1524" : "#EEF2F7"
    readonly property color surfaceHover: dark ? "#1D2A42" : "#F0F3F9"
    readonly property color surfacePressed: dark ? "#263854" : "#E5EAF3"
    readonly property color surfaceActive: dark ? "#252C4B" : "#EEECFF"
    readonly property color cardSurface: dark ? "#121D30" : "#FFFFFF"
    readonly property color cardHover: dark ? "#192942" : "#FAFBFF"
    readonly property color border: dark ? "#22304A" : "#DDE3ED"
    readonly property color borderStrong: dark ? "#334462" : "#C7D0DE"
    readonly property color text: dark ? "#F4F7FC" : "#1A2030"
    readonly property color textMuted: dark ? "#A4AFC3" : "#596477"
    readonly property color textFaint: dark ? "#7F8BA3" : "#7B8598"
    readonly property color accent: dark ? "#8B7CF6" : "#6357E8"
    readonly property color accentHover: dark ? "#9E92FA" : "#5548D8"
    readonly property color accentSoft: dark ? "#29254B" : "#EEECFF"
    readonly property color accentBorder: dark ? "#504889" : "#D9D5FF"
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
    readonly property color viewerBackground: dark ? "#080B11" : "#E9EDF4"
    readonly property color viewerScrim: dark ? "#52080B11" : "#32FFFFFF"
    readonly property color viewerGlass: dark ? "#1B2533" : "#FBFCFE"
    readonly property color viewerGlassStrong: dark ? "#18212E" : "#F5F7FB"
    readonly property color viewerGlassSoft: dark ? "#232E3D" : "#EEF2F7"
    readonly property color viewerGlassBorder: dark ? "#48FFFFFF" : "#360B1220"
    readonly property color viewerGlassBorderSoft: dark ? "#30FFFFFF" : "#240B1220"
    readonly property color viewerGlassHover: dark ? "#2CFFFFFF" : "#180B1220"
    readonly property color viewerGlassPressed: dark ? "#38FFFFFF" : "#260B1220"
    readonly property color viewerGlassEmphasis: dark ? "#26FFFFFF" : "#165F52E8"
    readonly property color viewerText: dark ? "#F7FAFF" : "#1A2030"
    readonly property color viewerTextMuted: dark ? "#C7FFFFFF" : "#B2596477"
    readonly property color viewerTrack: dark ? "#36FFFFFF" : "#300B1220"
    readonly property color viewerHandle: dark ? "#FFFFFFFF" : "#FFFBFCFE"

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

    function toggle() { settings.darkMode = !settings.darkMode }
    function setSingleClickOpen(enabled) { interactionSettings.singleClickOpen = !!enabled }
    function setSystemIcons(enabled) { settings.systemIcons = enabled }
    function setKioProgressEnabled(enabled) { settings.kioProgressEnabled = enabled }
    function setSidebarIconSize(size) { setSystemSidebarIconSize(size) }
    function setHomeIconSize(size) { setSystemHomeIconSize(size) }
    function setSystemSidebarIconSize(size) { settings.systemSidebarIconSize = Math.max(8, Math.min(128, Math.round(size))) }
    function setSystemHomeIconSize(size) { settings.systemHomeIconSize = Math.max(8, Math.min(128, Math.round(size))) }
    function setWheelScrollStep(size) { settings.wheelScrollStep = Math.max(10, Math.min(1000, Math.round(size))) }
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
            "viewer-close.svg": "window-close",
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
        return "image://systemicon/" + encodeURIComponent(name || "text-x-generic")
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
        if (name.indexOf("system:") === 0)
            return systemIcon(name.substring(7))
        return icon(name)
    }
    // Bundled application/UI artwork is intentionally independent from the
    // "System icons" preference. That preference is applied explicitly by
    // file/folder/navigation delegates through systemIcon()/systemIconAtSize().
    // Keeping buttons, menus, player controls and toolbar actions on bundled
    // artwork prevents light-only/dark-only icon themes from making controls
    // disappear when G-File's own theme is switched.
    function icon(name) {
        const raw = String(name || "")
        if (raw.length === 0)
            return ""
        // Explicit system-icon selections are still respected for places where
        // the user deliberately picked a theme icon (for example sidebar item
        // customization).
        if (raw.indexOf("system:") === 0)
            return systemIcon(raw.substring(7))
        if (raw.indexOf("image://systemicon/") === 0
                || raw.indexOf("qrc:/") === 0
                || raw.indexOf("file:/") === 0)
            return raw
        return "qrc:/qt/qml/GFile/App/assets/icons/" + raw
    }
}
