import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Effects
import QtQuick.Window
import QtQml
import QtMultimedia
import GFile.App
import GFile.Backend

Popup {
    id: viewer
    required property var lang

    // The player has its own dark palette, independent of the file manager.
    QtObject {
        id: videoTheme
        readonly property bool dark: true
        readonly property color viewerBackground: "#080B11"
        readonly property color viewerGlass: "#1B2533"
        readonly property color viewerGlassStrong: "#18212E"
        readonly property color viewerGlassSoft: "#232E3D"
        readonly property color viewerGlassBorder: "#48FFFFFF"
        readonly property color viewerGlassBorderSoft: "#30FFFFFF"
        readonly property color viewerGlassHover: "#2CFFFFFF"
        readonly property color viewerGlassPressed: "#38FFFFFF"
        readonly property color viewerGlassEmphasis: "#26FFFFFF"
        readonly property color viewerText: "#F7FAFF"
        readonly property color viewerTextMuted: "#C7FFFFFF"
        readonly property color viewerTrack: "#36FFFFFF"
        readonly property color text: viewerText
        readonly property color textMuted: viewerTextMuted
        readonly property color textFaint: "#7F8BA3"
        readonly property color accent: "#8B7CF6"
        readonly property color accentHover: "#9E92FA"
        readonly property color accentSoft: "#29254B"
        readonly property color accentBorder: "#504889"
        readonly property color selection: accent
        readonly property color danger: "#FF8089"
        readonly property color border: "#22304A"
        readonly property color borderStrong: "#334462"
        readonly property color surface: "#121C2D"
        readonly property color surfaceRaised: "#18253A"
        readonly property color surfaceSunken: "#0D1524"
        readonly property color surfaceHover: "#1D2A42"
        readonly property color surfacePressed: "#263854"
    }

    palette.window: videoTheme.viewerGlassStrong
    palette.base: videoTheme.viewerGlassSoft
    palette.button: videoTheme.surfaceRaised
    palette.windowText: videoTheme.viewerText
    palette.text: videoTheme.viewerText
    palette.buttonText: videoTheme.viewerText
    palette.highlight: videoTheme.accent
    palette.highlightedText: "white"
    palette.mid: videoTheme.borderStrong
    palette.dark: videoTheme.surfaceSunken
    palette.light: videoTheme.borderStrong
    property var files: []
    property int currentIndex: -1
    property var hostWindow: null
    // Fullscreen belongs to a dedicated video-player window. The main G-File
    // window keeps its current Windowed/Maximized state, matching PhotoViewer.
    property bool viewerFullScreen: false
    property bool movingToWindow: false
    property bool playbackSessionOpen: false
    property var normalOverlayParent: null
    property bool controlsVisible: true
    property bool controlsSettling: false
    property bool pauseScreenVisible: false
    property var metadataManager: null
    // Optional host callback returning already-resolved TMDB metadata for an item.
    // Gallery/DLNA views use this so the pause screen reuses data they already have.
    property var metadataForItem: null
    property var pauseMetadata: ({ state: "loading" })
    property string pauseLookupSource: ""
    property real pendingResumePosition: -1
    property bool resumeAppliedForSource: false
    property int resumeRestoreAttempts: 0
    property int resumeConfirmTicks: 0
    property var pendingTrackPreferences: ({})
    property bool trackPreferencesAppliedForSource: false
    property int trackPreferenceRestoreAttempts: 0
    property real lastPointerSceneX: -1
    property real lastPointerSceneY: -1
    property string subtitleAiMode: "transcribe"
    property int subtitleAiSourceIndex: 0
    property int subtitleAiExistingSourceIndex: 0
    property var subtitleAiExistingSources: []
    property int subtitleAiProfileIndex: 2
    property string subtitleAiTargetCode: "tr"
    property string subtitleAiJobPath: ""
    property bool subtitleAiPanelMinimized: false
    property string subtitleStyleColorTarget: "subtitle"
    property string subtitleStyleFontTarget: "subtitle"
    property string subtitleStyleFontCurrent: ""
    property string subtitleStyleFontSearch: ""
    property var subtitleStyleFontFamilies: []
    readonly property var subtitleStyleFilteredFonts: {
        const query = String(subtitleStyleFontSearch || "").trim().toLocaleLowerCase()
        if (!query.length) return subtitleStyleFontFamilies
        return subtitleStyleFontFamilies.filter(function(name) {
            return String(name || "").toLocaleLowerCase().indexOf(query) >= 0
        })
    }
    property var selectedSubtitleSource: null
    property var subtitleAiCacheState: ({ sourceReady: false, detectedLanguage: "", translationReady: false, translationProgress: 0, translationCompletedUnits: 0, translationTotalUnits: 0 })
    readonly property var subtitleAiTranslationTargets: [
        { code: "", label: "Çeviri yok" },
        { code: "tr", label: "Türkçe" }, { code: "en", label: "English" },
        { code: "de", label: "Deutsch" }, { code: "fr", label: "Français" },
        { code: "es", label: "Español" }, { code: "it", label: "Italiano" },
        { code: "pt", label: "Português" }, { code: "nl", label: "Nederlands" },
        { code: "pl", label: "Polski" }, { code: "ru", label: "Rusça" },
        { code: "uk", label: "Ukraynaca" }, { code: "ar", label: "Arapça" },
        { code: "fa", label: "Farsça" }, { code: "he", label: "İbranice" },
        { code: "zh", label: "Çince" }, { code: "ja", label: "Japonca" },
        { code: "ko", label: "Korece" }, { code: "hi", label: "Hintçe" },
        { code: "bn", label: "Bengalce" }, { code: "ur", label: "Urduca" },
        { code: "el", label: "Yunanca" }, { code: "cs", label: "Čeština" },
        { code: "sk", label: "Slovenčina" }, { code: "hu", label: "Magyar" },
        { code: "ro", label: "Română" }, { code: "bg", label: "Bulgarsça" },
        { code: "sr", label: "Srpski" }, { code: "hr", label: "Hrvatski" },
        { code: "bs", label: "Bosanski" }, { code: "sl", label: "Slovenščina" },
        { code: "mk", label: "Makedonca" }, { code: "sq", label: "Shqip" },
        { code: "sv", label: "Svenska" }, { code: "no", label: "Norsk" },
        { code: "da", label: "Dansk" }, { code: "fi", label: "Suomi" },
        { code: "is", label: "Íslenska" }, { code: "et", label: "Eesti" },
        { code: "lv", label: "Latviešu" }, { code: "lt", label: "Lietuvių" },
        { code: "id", label: "Bahasa Indonesia" }, { code: "ms", label: "Bahasa Melayu" },
        { code: "vi", label: "Tiếng Việt" }, { code: "th", label: "Thai" },
        { code: "tl", label: "Filipino" }, { code: "sw", label: "Kiswahili" },
        { code: "az", label: "Azərbaycanca" }, { code: "hy", label: "Ermenice" },
        { code: "ka", label: "Gürcüce" }, { code: "kk", label: "Kazakça" },
        { code: "uz", label: "O‘zbekcha" }, { code: "mn", label: "Moğolca" },
        { code: "ta", label: "Tamilce" }, { code: "te", label: "Teluguca" },
        { code: "mr", label: "Marathice" }, { code: "gu", label: "Guceratça" },
        { code: "pa", label: "Pencapça" }, { code: "ne", label: "Nepalce" },
        { code: "my", label: "Birmanca" }, { code: "ca", label: "Català" },
        { code: "eu", label: "Euskara" }, { code: "gl", label: "Galego" },
        { code: "cy", label: "Cymraeg" }
    ]
    readonly property var subtitleAiProfiles: [
        { key: "fast", label: "Hızlı · turbo" },
        { key: "medium", label: "Orta · medium" },
        { key: "slow", label: "Kaliteli · large-v3" },
        { key: "slower", label: "large-v3+ · geniş arama" }
    ]
    // Keep small/medium sources at native size by default.  Stretching a
    // 720p clip across a large desktop makes it look much softer than it is.
    // The fit toggle lets the user explicitly allow upscaling.
    property bool allowVideoUpscale: false
    // Vault previews use short-lived decrypted runtime files. Clear the media
    // source completely on close so reopening the same vault item creates a
    // fresh QMediaPlayer source instead of retaining a stopped/deleted URL.
    property bool resetMediaOnClose: false
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
    // Kept as an alias so the existing auto-hide/chrome logic stays intact.
    readonly property bool hostFullScreen: viewerFullScreen
    readonly property bool playing: player.playbackState === MediaPlayer.PlayingState
    readonly property bool paused: player.playbackState === MediaPlayer.PausedState
    readonly property var frameMatchMediaPlayer: player
    readonly property real controlsOpacity: !controlsVisible ? 0.0 : 1.0
    readonly property int sourceVideoWidth: Math.max(0, Math.round(videoOutput.sourceRect.width))
    readonly property int sourceVideoHeight: Math.max(0, Math.round(videoOutput.sourceRect.height))
    readonly property string sourceVideoResolution: sourceVideoWidth > 0 && sourceVideoHeight > 0
                                                    ? sourceVideoWidth + "×" + sourceVideoHeight : ""

    function viewerIcon(name) { return "qrc:/qt/qml/GFile/App/assets/icons/" + name }
    function thumbSource(item) {
        if (!item) return ""
        if (item.thumbnailUrl && String(item.thumbnailUrl).length)
            return String(item.thumbnailUrl)
        if (!item.path || String(item.path).startsWith("dlna://")) return ""
        return "image://gfilethumb/" + encodeURIComponent(item.path)
                + "|video-strip-" + String(item.modifiedMs || 0) + "-" + String(item.size || 0)
    }
    function formatSize(bytes) {
        const n = Number(bytes || 0)
        if (n < 1024) return n + " B"
        if (n < 1024 * 1024) return (n / 1024).toFixed(1) + " KiB"
        if (n < 1024 * 1024 * 1024) return (n / (1024 * 1024)).toFixed(1) + " MiB"
        return (n / (1024 * 1024 * 1024)).toFixed(1) + " GiB"
    }
    function formatTime(ms) {
        const seconds = Math.max(0, Math.floor(Number(ms || 0) / 1000))
        const h = Math.floor(seconds / 3600)
        const m = Math.floor((seconds % 3600) / 60)
        const s = seconds % 60
        if (h > 0)
            return h + ":" + String(m).padStart(2, "0") + ":" + String(s).padStart(2, "0")
        return m + ":" + String(s).padStart(2, "0")
    }
    function currentLocalPath() {
        // Private Vault previews point at short-lived decrypted runtime files.
        // Do not write generated subtitles beside those temporary copies.
        if (resetMediaOnClose) return ""
        if (!currentItem) return ""
        const direct = String(currentItem.path || "")
        if (direct.length && !direct.startsWith("dlna://"))
            return direct
        const source = String(currentItem.url || "")
        if (!source.startsWith("file://"))
            return ""
        try {
            return decodeURIComponent(source.substring(7))
        } catch (_error) {
            return source.substring(7)
        }
    }
    function subtitleAiLanguageCode(value) {
        const raw = String(value || "").trim()
        if (!raw.length) return "und"
        const lower = raw.toLowerCase().replace("_", "-")
        if (lower === "und" || lower === "unknown" || lower.indexOf("undetermined") >= 0) return "und"
        const direct = lower.split("-", 1)[0]
        if (/^[a-z]{2,3}$/.test(direct)) return direct
        const aliases = {
            "english":"en", "ingilizce":"en", "french":"fr", "français":"fr", "fransızca":"fr",
            "german":"de", "deutsch":"de", "almanca":"de", "turkish":"tr", "türkçe":"tr",
            "spanish":"es", "español":"es", "ispanyolca":"es", "italian":"it", "italiano":"it",
            "portuguese":"pt", "português":"pt", "dutch":"nl", "nederlands":"nl",
            "polish":"pl", "polski":"pl", "russian":"ru", "rusça":"ru", "ukrainian":"uk",
            "arabic":"ar", "arapça":"ar", "persian":"fa", "hebrew":"he", "chinese":"zh",
            "japanese":"ja", "korean":"ko", "swedish":"sv", "svenska":"sv", "norwegian":"no",
            "danish":"da", "finnish":"fi", "greek":"el", "romanian":"ro", "hungarian":"hu"
        }
        for (const key in aliases) {
            if (lower === key || lower.indexOf(key + " ") === 0 || lower.indexOf(key + " (") === 0)
                return aliases[key]
        }
        return "und"
    }
    function subtitleAiSourceOptions() {
        const result = []
        const seen = ({})
        for (let i = 0; i < player.audioTracks.length; ++i) {
            const rawLanguage = trackMetadataString(player.audioTracks[i], 6)
            const code = subtitleAiLanguageCode(rawLanguage)
            seen[code] = Number(seen[code] || 0) + 1
            const label = seen[code] > 1 ? code + " · " + seen[code] : code
            result.push({ trackIndex: i, code: code, label: label })
        }
        if (!result.length)
            result.push({ trackIndex: 0, code: "und", label: "und" })
        return result
    }
    function subtitleAiTargetOptions() {
        return subtitleAiMode === "subtitle"
               ? subtitleAiTranslationTargets.filter(function(item) { return item.code.length > 0 })
               : subtitleAiTranslationTargets
    }
    function subtitleAiTargetIndex() {
        const options = subtitleAiTargetOptions()
        for (let i = 0; i < options.length; ++i)
            if (options[i].code === subtitleAiTargetCode) return i
        return 0
    }
    function ensureSubtitleAiTargetValid(sourceCode) {
        if (subtitleAiMode === "subtitle" && !subtitleAiTargetCode.length)
            subtitleAiTargetCode = "tr"
        if (sourceCode !== "und" && sourceCode.length && subtitleAiTargetCode === sourceCode)
            subtitleAiTargetCode = sourceCode === "tr" ? "en" : "tr"
    }
    function subtitleAiTargetLabel(code) {
        for (let i = 0; i < subtitleAiTranslationTargets.length; ++i)
            if (subtitleAiTranslationTargets[i].code === code) return subtitleAiTranslationTargets[i].label
        return code
    }
    function refreshSubtitleAiExistingSources() {
        const path = currentLocalPath()
        subtitleAiExistingSources = path.length ? subtitleAiManager.subtitleSources(path) : []
        if (subtitleAiExistingSourceIndex >= subtitleAiExistingSources.length)
            subtitleAiExistingSourceIndex = Math.max(0, subtitleAiExistingSources.length - 1)
    }
    function existingSubtitleLabel(item) {
        if (!item) return lang.language === "tr" ? "Altyazı bulunamadı" : "No subtitles found"
        const kind = String(item.kind || "")
        const language = String(item.language || "und")
        const title = String(item.title || "")
        const codec = String(item.codec || "")
        const fileName = String(item.fileName || "")
        let parts = []
        parts.push(kind === "external" ? (lang.language === "tr" ? "Dosya" : "File") : (lang.language === "tr" ? "Gömülü" : "Embedded"))
        parts.push(language.length ? language : "und")
        if (fileName.length) parts.push(fileName)
        else if (title.length) parts.push(title)
        if (codec.length) parts.push(codec)
        return parts.join("  ·  ")
    }
    function selectedExistingSubtitle() {
        if (!subtitleAiExistingSources.length) return null
        return subtitleAiExistingSources[Math.max(0, Math.min(subtitleAiExistingSourceIndex, subtitleAiExistingSources.length - 1))]
    }
    function refreshSubtitleAiCacheState() {
        const path = currentLocalPath()
        if (!path.length) {
            subtitleAiCacheState = ({ sourceReady: false, detectedLanguage: "", translationReady: false, translationProgress: 0, translationCompletedUnits: 0, translationTotalUnits: 0 })
            return
        }
        if (subtitleAiMode === "subtitle") {
            const source = selectedExistingSubtitle()
            if (!source) {
                subtitleAiCacheState = ({ sourceReady: false, detectedLanguage: "", translationReady: false, translationProgress: 0, translationCompletedUnits: 0, translationTotalUnits: 0 })
                return
            }
            subtitleAiCacheState = subtitleAiManager.subtitleTranslationCacheInfo(
                path, String(source.kind || ""), String(source.path || ""), Number(source.trackIndex || 0),
                String(source.language || "und"), subtitleAiTargetCode)
            return
        }
        const sources = subtitleAiSourceOptions()
        if (!sources.length) return
        const source = sources[Math.max(0, Math.min(subtitleAiSourceIndex, sources.length - 1))]
        const profile = subtitleAiProfiles[Math.max(0, Math.min(subtitleAiProfileIndex, subtitleAiProfiles.length - 1))].key
        subtitleAiCacheState = subtitleAiManager.cacheInfo(path, source.code, profile, subtitleAiTargetCode, source.trackIndex)
    }
    function subtitleAiActionLabel() {
        const cached = subtitleAiCacheState || ({})
        if (cached.translationReady && subtitleAiTargetCode.length)
            return lang.language === "tr" ? "Hazır çeviriyi kullan" : "Use cached translation"
        if (Number(cached.translationProgress || 0) > 0)
            return lang.language === "tr" ? "Çeviriye devam et" : "Resume translation"
        if (subtitleAiMode === "subtitle")
            return lang.language === "tr" ? "AI ile çevir" : "Translate with AI"
        if (cached.sourceReady) {
            if (!subtitleAiTargetCode.length)
                return lang.language === "tr" ? "Hazır altyazıyı kullan" : "Use cached subtitles"
            return lang.language === "tr" ? "AI çevirisini başlat" : "Start AI translation"
        }
        return lang.language === "tr" ? "Oluştur" : "Generate"
    }
    function openSubtitleAiGenerateDialog() {
        if (!currentLocalPath().length) return
        if (subtitleAiManager.busy) { restoreSubtitleAiPanel(); return }
        subtitleAiMode = "transcribe"
        subtitleAiPanelMinimized = false
        subtitleAiManager.resetResult()
        const sources = subtitleAiSourceOptions()
        subtitleAiSourceIndex = Math.max(0, Math.min(player.activeAudioTrack >= 0 ? player.activeAudioTrack : 0, sources.length - 1))
        ensureSubtitleAiTargetValid(sources[subtitleAiSourceIndex].code)
        refreshSubtitleAiCacheState()
        subtitleTrackPopup.close()
        subtitleAiSubtitlePopup.open()
    }
    function openSubtitleAiTranslateDialog() {
        if (!currentLocalPath().length) return
        if (subtitleAiManager.busy) { restoreSubtitleAiPanel(); return }
        subtitleAiMode = "subtitle"
        subtitleAiPanelMinimized = false
        subtitleAiManager.resetResult()
        refreshSubtitleAiExistingSources()
        if (!subtitleAiExistingSources.length) return
        // Prefer the currently selected embedded track when its ordinal exists.
        let preferred = 0
        if (selectedSubtitleSource) {
            for (let i = 0; i < subtitleAiExistingSources.length; ++i) {
                const item = subtitleAiExistingSources[i]
                if (String(item.kind || "") !== String(selectedSubtitleSource.kind || "")) continue
                if (String(item.kind || "") === "embedded"
                        && Number(item.trackIndex) === Number(selectedSubtitleSource.trackIndex)) { preferred = i; break }
                if (String(item.kind || "") === "external"
                        && String(item.path || "") === String(selectedSubtitleSource.path || "")) { preferred = i; break }
            }
        } else if (player.activeSubtitleTrack >= 0) {
            for (let i = 0; i < subtitleAiExistingSources.length; ++i) {
                const item = subtitleAiExistingSources[i]
                if (String(item.kind || "") === "embedded" && Number(item.trackIndex) === player.activeSubtitleTrack) { preferred = i; break }
            }
        }
        subtitleAiExistingSourceIndex = preferred
        const source = selectedExistingSubtitle()
        ensureSubtitleAiTargetValid(source ? String(source.language || "und") : "und")
        refreshSubtitleAiCacheState()
        subtitleTrackPopup.close()
        subtitleAiSubtitlePopup.open()
    }
    function minimizeSubtitleAiPanel() {
        if (!subtitleAiManager.busy) return
        subtitleAiPanelMinimized = true
        subtitleAiSubtitlePopup.close()
        revealControls()
    }
    function restoreSubtitleAiPanel() {
        subtitleAiPanelMinimized = false
        subtitleAiSubtitlePopup.open()
        revealControls()
    }
    function startSubtitleAi() {
        const path = currentLocalPath()
        if (!path.length) return
        subtitleAiJobPath = path
        subtitleAiPanelMinimized = false
        if (subtitleAiMode === "subtitle") {
            const source = selectedExistingSubtitle()
            if (!source) return
            const started = subtitleAiManager.startSubtitleTranslation(
                path, String(source.kind || ""), String(source.path || ""), Number(source.trackIndex || 0),
                String(source.language || "und"), subtitleAiTargetCode)
            if (started) activateLiveSubtitleSession()
            return
        }
        const sources = subtitleAiSourceOptions()
        const source = sources[Math.max(0, Math.min(subtitleAiSourceIndex, sources.length - 1))]
        const profile = subtitleAiProfiles[Math.max(0, Math.min(subtitleAiProfileIndex, subtitleAiProfiles.length - 1))].key
        if (subtitleAiManager.startTranscription(path, source.code, profile, subtitleAiTargetCode, source.trackIndex))
            activateLiveSubtitleSession()
    }
    function activateLiveSubtitleSession() {
        // An explicit AI selection takes precedence over deferred CC restore.
        trackPreferenceRestoreTimer.stop()
        trackPreferencesAppliedForSource = true
        player.disableNativeSubtitleTrack()
    }
    function trackMetadataString(track, key) {
        if (!track || typeof track.stringValue !== "function") return ""
        const value = track.stringValue(key)
        return value === undefined || value === null ? "" : String(value).trim()
    }
    function audioTrackLabel(index) {
        if (index < 0 || index >= player.audioTracks.length)
            return lang.language === "tr" ? "Ses yok" : "No audio"
        const track = player.audioTracks[index]
        const title = trackMetadataString(track, 0)   // QMediaMetaData::Title
        const language = trackMetadataString(track, 6) // QMediaMetaData::Language
        const codec = trackMetadataString(track, 14) // QMediaMetaData::AudioCodec
        let parts = []
        if (title) parts.push(title)
        if (language && parts.indexOf(language) < 0) parts.push(language)
        if (codec && parts.indexOf(codec) < 0) parts.push(codec)
        if (!parts.length) parts.push((lang.language === "tr" ? "Ses " : "Audio ") + (index + 1))
        return parts.join("  ·  ")
    }
    function subtitleTrackLabel(index) {
        if (index < 0 || index >= player.subtitleTracks.length)
            return lang.language === "tr" ? "Kapalı" : "Off"
        const track = player.subtitleTracks[index]
        const title = trackMetadataString(track, 0)
        const language = trackMetadataString(track, 6)
        let parts = []
        if (title) parts.push(title)
        if (language && parts.indexOf(language) < 0) parts.push(language)
        if (!parts.length) parts.push((lang.language === "tr" ? "Altyazı " : "Subtitle ") + (index + 1))
        return parts.join("  ·  ")
    }
    function subtitleExternalSources() {
        const result = []
        for (let i = 0; i < subtitleAiExistingSources.length; ++i) {
            const item = subtitleAiExistingSources[i]
            if (String(item.kind || "") === "external") result.push(item)
        }
        return result
    }
    function externalSubtitleLabel(item) {
        if (!item) return lang.language === "tr" ? "Hazır altyazı" : "Ready subtitle"
        const fileName = String(item.fileName || "")
        const language = String(item.language || "und")
        const generated = fileName.toLowerCase().indexOf(".generated.") >= 0
        const prefix = generated ? (lang.language === "tr" ? "Hazır" : "Ready") : (lang.language === "tr" ? "Dosya" : "File")
        return prefix + "  ·  " + language + (fileName.length ? "  ·  " + fileName : "")
    }
    function selectEmbeddedSubtitle(index) {
        const path = currentLocalPath()
        if (!path.length) return
        refreshSubtitleAiExistingSources()
        let source = null
        for (let i = 0; i < subtitleAiExistingSources.length; ++i) {
            const item = subtitleAiExistingSources[i]
            if (String(item.kind || "") === "embedded" && Number(item.trackIndex) === index) { source = item; break }
        }
        player.activeSubtitleTrack = -1
        if (subtitleAiManager.selectEmbeddedSubtitle(path, index)) {
            selectedSubtitleSource = source || ({ kind: "embedded", trackIndex: index, language: "und" })
            saveTrackPreferences()
        }
    }
    function selectExternalSubtitle(item) {
        if (!item || !String(item.path || "").length) return
        player.activeSubtitleTrack = -1
        if (subtitleAiManager.selectSubtitleFile(String(item.path))) {
            selectedSubtitleSource = item
            saveTrackPreferences()
        }
    }
    function clearSubtitleSelection() {
        player.activeSubtitleTrack = -1
        subtitleAiManager.clearSelectedSubtitle()
        selectedSubtitleSource = null
        saveTrackPreferences()
    }
    function selectExternalSubtitleByPath(path) {
        refreshSubtitleAiExistingSources()
        const wanted = String(path || "")
        for (let i = 0; i < subtitleAiExistingSources.length; ++i) {
            const item = subtitleAiExistingSources[i]
            if (String(item.kind || "") === "external" && String(item.path || "") === wanted) {
                selectExternalSubtitle(item)
                return true
            }
        }
        if (wanted.length && subtitleAiManager.selectSubtitleFile(wanted)) {
            player.activeSubtitleTrack = -1
            selectedSubtitleSource = ({ kind: "external", path: wanted, language: "und", fileName: wanted.split("/").pop() })
            saveTrackPreferences()
            return true
        }
        return false
    }
    function currentSubtitleLabel() {
        if (selectedSubtitleSource) {
            if (String(selectedSubtitleSource.kind || "") === "embedded")
                return subtitleTrackLabel(Number(selectedSubtitleSource.trackIndex || 0))
            return externalSubtitleLabel(selectedSubtitleSource)
        }
        return subtitleTrackLabel(player.activeSubtitleTrack)
    }
    function subtitlePlainText(value) {
        let text = String(value || "")
        text = text.replace(/<[^>]*>/g, "")
        text = text.replace(/\{\\[^}]*\}/g, "")
        return text.trim()
    }
    function subtitleIsUppercaseForced(value) {
        const text = subtitlePlainText(value)
        if (!text.length) return false
        const upper = text.toLocaleUpperCase()
        const lower = text.toLocaleLowerCase()
        // upper === lower means the cue contains no cased letters (e.g. "123").
        return upper !== lower && text === upper
    }
    function activeSubtitleSourceMetadata() {
        if (selectedSubtitleSource) return selectedSubtitleSource
        const index = player.activeSubtitleTrack
        if (index < 0) return null
        for (let i = 0; i < subtitleAiExistingSources.length; ++i) {
            const item = subtitleAiExistingSources[i]
            if (String(item.kind || "") === "embedded" && Number(item.trackIndex) === index)
                return item
        }
        return null
    }
    function activeSubtitleIsForcedTrack() {
        const meta = activeSubtitleSourceMetadata()
        if (meta && Boolean(meta.forced)) return true
        const label = currentSubtitleLabel().toLowerCase()
        return label.indexOf("forced") >= 0 || label.indexOf("zorunlu") >= 0
    }
    function activeSubtitleIsSdhTrack() {
        const meta = activeSubtitleSourceMetadata()
        if (meta && (Boolean(meta.hearingImpaired) || Boolean(meta.captions))) return true
        const label = currentSubtitleLabel().toLowerCase()
        return /(^|[ ._\-])(sdh|cc)([ ._\-]|$)/.test(label)
               || label.indexOf("hearing") >= 0 || label.indexOf("işitme") >= 0
    }
    function subtitleCueLooksSdh(value) {
        const text = subtitlePlainText(value)
        if (!text.length) return false
        return /^\s*\[[^\]]+\]\s*$/.test(text) || text.indexOf("♪") >= 0 || text.indexOf("♫") >= 0
    }
    function subtitleSemanticKind(value) {
        if (activeSubtitleIsForcedTrack() || subtitleIsUppercaseForced(value)) return "forced"
        if (activeSubtitleIsSdhTrack() || subtitleCueLooksSdh(value)) return "sdh"
        return "normal"
    }
    function openSubtitleStyleColor(target, currentColor) {
        subtitleStyleColorTarget = target
        subtitleStyleColorDialog.selectedColor = currentColor
        subtitleStyleColorDialog.open()
    }
    function openSubtitleStyleFont(target, family) {
        subtitleStyleFontTarget = target
        subtitleStyleFontCurrent = String(family || "")
        subtitleStyleFontSearch = ""
        // Keep the picker entirely inside the dark VideoPlayer surface. Using
        // the platform font dialog inherits the desktop light palette and can
        // make its selected-font label white-on-white.
        subtitleStyleFontFamilies = Qt.fontFamilies()
        subtitleStyleFontPicker.open()
    }
    function applySubtitleStyleColor(target, color) {
        if (target === "background") subtitleStyleManager.backgroundColor = color
        else if (target === "sdh") subtitleStyleManager.sdhColor = color
        else if (target === "forced") subtitleStyleManager.forcedColor = color
        else subtitleStyleManager.subtitleColor = color
    }
    function applySubtitleStyleFont(target, family) {
        if (target === "sdh") subtitleStyleManager.sdhFontFamily = family
        else if (target === "forced") subtitleStyleManager.forcedFontFamily = family
        else subtitleStyleManager.subtitleFontFamily = family
    }
    function subtitleStyleFontSizeFor(target) {
        if (target === "sdh") return subtitleStyleManager.sdhFontSize
        if (target === "forced") return subtitleStyleManager.forcedFontSize
        return subtitleStyleManager.subtitleFontSize
    }
    function applySubtitleStyleFontSize(target, size) {
        const value = Math.max(12, Math.min(72, Math.round(Number(size))))
        if (target === "sdh") subtitleStyleManager.sdhFontSize = value
        else if (target === "forced") subtitleStyleManager.forcedFontSize = value
        else subtitleStyleManager.subtitleFontSize = value
    }
    function revealControls() {
        settleTimer.stop()
        controlsSettling = false
        controlsVisible = true
        controlsHideTimer.restart()
    }
    function notePauseInteraction() {
        if (!paused) return
        pauseScreenVisible = false
        pauseScreenTimer.restart()
    }
    function refreshPauseMetadata() {
        if (!currentItem) {
            pauseLookupSource = ""
            pauseMetadata = ({ state: "loading" })
            return
        }

        // Prefer metadata the owning gallery already resolved. This avoids a
        // second TMDB request and makes the pause screen instant from cache.
        if (metadataForItem && typeof metadataForItem === "function") {
            const known = metadataForItem(currentItem)
            if (known && String(known.state || "") === "ready") {
                pauseLookupSource = String(known.sourcePath || currentItem.path || currentItem.url || "")
                pauseMetadata = known
                return
            }
        }

        if (!metadataManager) {
            pauseLookupSource = ""
            pauseMetadata = ({ state: "loading", parsedTitle: String(currentItem.name || "") })
            return
        }
        const sourcePath = String(currentItem.tmdbSource || currentItem.path || currentItem.url || "")
        if (!sourcePath.length) {
            pauseLookupSource = ""
            pauseMetadata = ({ state: "error", parsedTitle: String(currentItem.name || "") })
            return
        }
        const language = String(metadataManager.preferredLanguage || "tr")
        const cached = metadataManager.cachedLookup(sourcePath, language)
        if (cached && String(cached.state || "") === "ready") {
            pauseLookupSource = String(cached.sourcePath || sourcePath)
            pauseMetadata = cached
            return
        }

        let result = null
        const tmdbId = Number(currentItem.tmdbId || 0)
        const mediaType = String(currentItem.mediaType || currentItem.sourceMediaType || "")
        const year = Number(currentItem.year || 0)
        if (tmdbId > 0 && typeof metadataManager.lookupById === "function") {
            result = metadataManager.lookupById(sourcePath, tmdbId,
                                                 String(currentItem.name || ""), year,
                                                 mediaType, language)
        } else if (mediaType.length && typeof metadataManager.lookupTyped === "function") {
            result = metadataManager.lookupTyped(sourcePath, String(currentItem.name || ""),
                                                 year, mediaType, language)
        } else {
            result = metadataManager.lookup(sourcePath, String(currentItem.name || ""), language)
        }
        pauseLookupSource = String(result && result.sourcePath ? result.sourcePath : sourcePath)
        pauseMetadata = result || ({ state: "loading", parsedTitle: String(currentItem.name || "") })
    }

    function resumeKeyFor(item) {
        if (!item) return ""
        const stable = String(item.path || item.url || "")
        return stable.length ? stable : String(item.name || "")
    }
    function prepareResumeForCurrent() {
        const key = resumeKeyFor(currentItem)
        pendingResumePosition = key.length ? PlaybackResumeManager.positionFor(key) : -1
        pendingTrackPreferences = key.length ? PlaybackResumeManager.trackPreferencesFor(key) : ({})
        resumeAppliedForSource = false
        resumeRestoreAttempts = 0
        resumeConfirmTicks = 0
        trackPreferencesAppliedForSource = false
        trackPreferenceRestoreAttempts = 0
        if (visible && pendingResumePosition >= 3000)
            resumeRestoreTimer.restart()
        else
            resumeRestoreTimer.stop()
        trackPreferenceRestoreTimer.stop()
        if (visible && Object.keys(pendingTrackPreferences || {}).length)
            trackPreferenceRestoreTimer.restart()
        else
            trackPreferenceRestoreTimer.stop()
    }
    function clearResumeForCurrent() {
        const key = resumeKeyFor(currentItem)
        if (key.length) PlaybackResumeManager.clear(key)
        pendingResumePosition = -1
        resumeAppliedForSource = true
    }
    function saveResumePosition() {
        if (!currentItem || player.duration <= 0) return
        const key = resumeKeyFor(currentItem)
        if (!key.length) return
        PlaybackResumeManager.save(key, player.position, player.duration)
    }
    function saveTrackPreferences() {
        const key = resumeKeyFor(currentItem)
        if (!key.length) return
        let subtitleKind = "off"
        let subtitleTrack = -1
        let subtitlePath = ""
        if (selectedSubtitleSource) {
            subtitleKind = String(selectedSubtitleSource.kind || "off")
            if (subtitleKind === "embedded")
                subtitleTrack = Number(selectedSubtitleSource.trackIndex ?? -1)
            else if (subtitleKind === "external")
                subtitlePath = String(selectedSubtitleSource.path || "")
        }
        PlaybackResumeManager.saveTrackPreferences(key, Number(player.activeAudioTrack),
                                                   subtitleKind, subtitleTrack, subtitlePath)
    }
    function tryApplyTrackPreferences() {
        if (trackPreferencesAppliedForSource) return true
        const pref = pendingTrackPreferences || ({})
        if (!Object.keys(pref).length) {
            trackPreferencesAppliedForSource = true
            return true
        }

        const wantedAudio = Number(pref.audioTrack ?? -1)
        if (wantedAudio >= 0) {
            if (player.audioTracks.length <= wantedAudio)
                return false
            if (player.activeAudioTrack !== wantedAudio)
                player.activeAudioTrack = wantedAudio
        }

        const subtitleKind = String(pref.subtitleKind || "off")
        if (subtitleKind === "embedded") {
            const wantedSubtitle = Number(pref.subtitleTrack ?? -1)
            if (wantedSubtitle < 0) {
                clearSubtitleSelection()
            } else {
                if (player.subtitleTracks.length <= wantedSubtitle)
                    return false
                selectEmbeddedSubtitle(wantedSubtitle)
            }
        } else if (subtitleKind === "external") {
            const wantedPath = String(pref.subtitlePath || "")
            if (wantedPath.length)
                selectExternalSubtitleByPath(wantedPath)
            else
                clearSubtitleSelection()
        } else {
            clearSubtitleSelection()
        }

        trackPreferencesAppliedForSource = true
        return true
    }
    function tryApplyResume() {
        if (resumeAppliedForSource || pendingResumePosition < 3000 || player.duration <= 0)
            return false
        // Do not consume the saved point before play() has actually started
        // the backend.  Some FFmpeg streams reset a pre-play seek back to zero.
        if (player.playbackState === MediaPlayer.StoppedState)
            return false

        const target = Math.max(0, Math.min(Number(pendingResumePosition), player.duration))
        if (target >= player.duration - 60000 || target / player.duration >= 0.95) {
            clearResumeForCurrent()
            resumeRestoreTimer.stop()
            return true
        }

        const closeEnoughBeforeSeek = Math.abs(Number(player.position) - target) <= 2200
        if (!closeEnoughBeforeSeek)
            player.position = target

        // A QMediaPlayer.position assignment may update the QML property
        // immediately even though the FFmpeg pipeline later snaps back to 0.
        // Require the target to remain stable for several timer ticks before
        // considering the first-start restore complete.
        const closeEnough = Math.abs(Number(player.position) - target) <= 2200
        resumeConfirmTicks = closeEnough ? resumeConfirmTicks + 1 : 0
        if (resumeConfirmTicks >= 3) {
            resumeAppliedForSource = true
            pendingResumePosition = -1
            resumeRestoreTimer.stop()
            return true
        }
        return false
    }
    function handlePointer(sourceItem, x, y) {
        if (!sourceItem) return
        const p = sourceItem.mapToItem(viewer.contentItem, x, y)
        if (lastPointerSceneX < 0 || lastPointerSceneY < 0) {
            lastPointerSceneX = p.x
            lastPointerSceneY = p.y
            return
        }
        const moved = Math.abs(p.x - lastPointerSceneX) >= 2 || Math.abs(p.y - lastPointerSceneY) >= 2
        if (moved) {
            revealControls()
            notePauseInteraction()
        }
        lastPointerSceneX = p.x
        lastPointerSceneY = p.y
    }
    function toggleHostFullScreen() {
        if (movingToWindow) return
        if (!normalOverlayParent)
            normalOverlayParent = parent
        // Detach video capture textures before moving between scene graphs.
        // A live ShaderEffectSource can otherwise retain the old window's
        // render context and try to draw without a valid command buffer.
        movingToWindow = true
        audioTrackPopup.close()
        subtitleTrackPopup.close()
        subtitleAiSubtitlePopup.close()
        subtitleStylePopup.close()
        subtitleStyleFontPicker.close()
        subtitleStyleColorDialog.close()
        Qt.callLater(function() {
            if (!viewer.visible) { viewer.movingToWindow = false; return }
            viewer.viewerFullScreen = !viewer.viewerFullScreen
            if (viewer.viewerFullScreen)
                viewerFullScreenWindow.showFullScreen()
            else
                viewerFullScreenWindow.hide()
            revealControls()
            Qt.callLater(function() {
                viewer.movingToWindow = false
                if (viewer.viewerFullScreen)
                    viewerFullScreenWindow.requestActivate()
            })
        })
    }
    function openAt(index) {
        if (index < 0 || index >= files.length) return
        if (visible && currentIndex !== index) {
            saveResumePosition()
            saveTrackPreferences()
        }
        currentIndex = index
        prepareResumeForCurrent()
        open()
        revealControls()
        forceActiveFocus()
        Qt.callLater(function() { player.play(); filmstrip.positionViewAtIndex(currentIndex, ListView.Center) })
    }
    function setIndex(index, autoplay) {
        if (!files.length) return
        const nextIndex = Math.max(0, Math.min(files.length - 1, index))
        if (currentIndex !== nextIndex) {
            saveResumePosition()
            saveTrackPreferences()
        }
        currentIndex = nextIndex
        prepareResumeForCurrent()
        revealControls()
        Qt.callLater(function() {
            filmstrip.positionViewAtIndex(currentIndex, ListView.Center)
            if (autoplay !== false) player.play()
        })
    }
    function previousVideo() { if (files.length) setIndex((currentIndex - 1 + files.length) % files.length, true) }
    function nextVideo() { if (files.length) setIndex((currentIndex + 1) % files.length, true) }
    function togglePlayback() { if (playing) player.pause(); else player.play() }
    function seekBy(deltaMs) { player.position = Math.max(0, Math.min(player.duration, player.position + deltaMs)) }
    function handleGlobalAction(action) {
        revealControls()
        if (action === "escape") {
            if (subtitleStylePopup.visible) { subtitleStylePopup.close(); return }
            if (subtitleAiSubtitlePopup.visible) {
                if (!subtitleAiManager.busy)
                    subtitleAiSubtitlePopup.close()
                return
            }
            if (audioTrackPopup.visible) { audioTrackPopup.close(); return }
            if (subtitleTrackPopup.visible) { subtitleTrackPopup.close(); return }
            if (hostFullScreen) toggleHostFullScreen()
            else close()
            return
        }
        if (action === "toggle") { togglePlayback(); return }
        if (action === "left") { seekBy(-5000); return }
        if (action === "right") { seekBy(5000); return }
        if (action === "up") { audioOutput.volume = Math.min(1, audioOutput.volume + 0.05); return }
        if (action === "down") { audioOutput.volume = Math.max(0, audioOutput.volume - 0.05); return }
        if (action === "mute") { audioOutput.muted = !audioOutput.muted; return }
        if (action === "fullscreen") { toggleHostFullScreen(); return }
    }

    SubtitleAiManager {
        id: subtitleAiManager
        objectName: "videoSubtitleAiManager"
    }
    SubtitleStyleManager {
        id: subtitleStyleManager
    }

    Connections {
        target: subtitleAiManager
        function onCancelled() { Qt.callLater(viewer.refreshSubtitleAiCacheState) }
        function onCompleted(outputPath) {
            Qt.callLater(function() {
                viewer.refreshSubtitleAiCacheState()
                viewer.selectExternalSubtitleByPath(outputPath)
            })
        }
        function onFailed(_message) { Qt.callLater(viewer.refreshSubtitleAiCacheState) }
    }

    component ViewerIconButton: GToolButton { colorTheme: videoTheme;
        id: control
        property string iconName: ""
        property string toolTipText: ""
        property bool emphasized: false
        implicitWidth: 38
        implicitHeight: 38
        padding: 0
        HoverHandler { cursorShape: Qt.PointingHandCursor; onHoveredChanged: if (hovered) viewer.revealControls() }
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
                tintColor: videoTheme.viewerText
                iconOpacity: control.enabled ? 0.96 : 0.42
            }
        }
        background: GGlassPanel {
            sourceItem: videoScene
            radius: 9
            tintColor: control.down ? videoTheme.viewerGlassPressed
                                     : (control.hovered ? videoTheme.viewerGlassHover
                                                        : (control.emphasized ? videoTheme.viewerGlassEmphasis : videoTheme.viewerGlassSoft))
            borderColor: control.hovered || control.emphasized ? videoTheme.accentBorder : videoTheme.viewerGlassBorderSoft
            blurAmount: 0.46
        }
        GToolTip { colorTheme: videoTheme; text: control.toolTipText }
    }

    component ViewerTextButton: GToolButton { colorTheme: videoTheme;
        id: textControl
        property string label: ""
        property string toolTipText: ""
        property bool emphasized: false
        implicitWidth: 44
        implicitHeight: 38
        padding: 0
        HoverHandler { cursorShape: Qt.PointingHandCursor; onHoveredChanged: if (hovered) viewer.revealControls() }
        Connections {
            target: textControl
            function onPressedChanged() { if (textControl.pressed) viewer.revealControls() }
            function onClicked() { viewer.revealControls() }
        }
        contentItem: Text {
            text: textControl.label
            color: textControl.enabled ? videoTheme.viewerText : videoTheme.viewerTextMuted
            font.pixelSize: 10
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: GGlassPanel {
            sourceItem: videoScene
            radius: 9
            tintColor: textControl.down ? videoTheme.viewerGlassPressed
                                         : (textControl.hovered ? videoTheme.viewerGlassHover
                                                                : (textControl.emphasized ? videoTheme.viewerGlassEmphasis : videoTheme.viewerGlassSoft))
            borderColor: textControl.hovered || textControl.emphasized ? videoTheme.accentBorder : videoTheme.viewerGlassBorderSoft
            blurAmount: 0.46
        }
        GToolTip { colorTheme: videoTheme; text: textControl.toolTipText }
    }

    ApplicationWindow {
        id: viewerFullScreenWindow
        transientParent: viewer.normalOverlayParent ? viewer.normalOverlayParent.Window.window : null
        visible: false
        color: videoTheme.viewerBackground
        title: viewer.lang.language === "tr" ? "G-File Video Oynatıcı" : "G-File Video Player"
        flags: Qt.Window | Qt.FramelessWindowHint
        onClosing: function(close) {
            if (viewer.viewerFullScreen) {
                close.accepted = false
                viewer.toggleHostFullScreen()
            }
        }

    }

    background: Rectangle { color: videoTheme.viewerBackground }

    FrameRateMatchClient {
        player: viewer.frameMatchMediaPlayer
        active: viewer.playbackSessionOpen
        output: viewer.Window.window && viewer.Window.window.screen
                ? viewer.Window.window.screen.name : ""
    }

    MediaPlayer {
        id: player
        objectName: "videoMediaPlayer"
        source: viewer.currentItem ? viewer.currentItem.url : ""
        activeSubtitleTrack: -1
        function disableNativeSubtitleTrack() {
            if (activeSubtitleTrack !== -1)
                activeSubtitleTrack = -1
        }
        // A backend can activate its default track after loading or seeking.
        // All CC text is rendered by subtitleOverlay, never by VideoOutput.
        onActiveTracksChanged: {
            // Qt emits the shared track notification during a backend track
            // transition. Disabling it reentrantly can be overwritten by the
            // transition itself, so apply this after that transition returns.
            Qt.callLater(player.disableNativeSubtitleTrack)
        }
        audioOutput: AudioOutput { id: audioOutput; volume: 0.82 }
        videoOutput: videoOutput
        onMediaStatusChanged: {
            if (mediaStatus === MediaPlayer.LoadedMedia || mediaStatus === MediaPlayer.BufferedMedia) {
                // G-File renders every subtitle through its own styled overlay.
                // Keep Qt Multimedia's native subtitle track disabled to avoid
                // the same cue being rendered twice.
                if (player.activeSubtitleTrack >= 0)
                    player.activeSubtitleTrack = -1
                if (!viewer.tryApplyResume() && viewer.pendingResumePosition >= 3000)
                    resumeRestoreTimer.restart()
                if (!viewer.trackPreferencesAppliedForSource
                        && Object.keys(viewer.pendingTrackPreferences || {}).length)
                    trackPreferenceRestoreTimer.restart()
            }
            if (mediaStatus === MediaPlayer.EndOfMedia) {
                viewer.clearResumeForCurrent()
                if (viewer.files.length > 1)
                    viewer.nextVideo()
            }
        }
        onDurationChanged: {
            if (!viewer.tryApplyResume() && viewer.pendingResumePosition >= 3000)
                resumeRestoreTimer.restart()
        }
        onSeekableChanged: {
            if (!viewer.tryApplyResume() && viewer.pendingResumePosition >= 3000)
                resumeRestoreTimer.restart()
        }
        onPlaybackStateChanged: {
            if (playbackState === MediaPlayer.PausedState) {
                viewer.saveResumePosition()
                viewer.pauseScreenVisible = false
                pauseScreenTimer.restart()
            } else {
                pauseScreenTimer.stop()
                viewer.pauseScreenVisible = false
            }
        }
    }

    Connections {
        target: Application
        function onAboutToQuit() {
            // Popup.onClosed is not guaranteed when the whole application is
            // torn down with its parent window. Persist the live playback
            // position before the event loop exits.
            viewer.saveResumePosition()
            viewer.saveTrackPreferences()
        }
    }

    Connections {
        target: VideoPlayerInputManager
        enabled: viewer.visible
        function onEscapePressed() { viewer.handleGlobalAction("escape") }
        function onTogglePlaybackPressed() { viewer.handleGlobalAction("toggle") }
        function onSeekBackwardPressed() { viewer.handleGlobalAction("left") }
        function onSeekForwardPressed() { viewer.handleGlobalAction("right") }
        function onVolumeUpPressed() { viewer.handleGlobalAction("up") }
        function onVolumeDownPressed() { viewer.handleGlobalAction("down") }
        function onMutePressed() { viewer.handleGlobalAction("mute") }
        function onFullscreenPressed() { viewer.handleGlobalAction("fullscreen") }
    }

    onOpened: {
        if (!normalOverlayParent)
            normalOverlayParent = parent
        // Popup emits opened again when its parent moves to another window.
        // Playback and the live AI stream already belong to this session.
        if (playbackSessionOpen) return
        playbackSessionOpen = true
        prepareResumeForCurrent()
        refreshPauseMetadata()
        if (hostWindow) {
            if (hostWindow.mediaViewerConsumesF11 !== undefined)
                hostWindow.mediaViewerConsumesF11 = true
            if (hostWindow.activeVideoViewer !== undefined)
                hostWindow.activeVideoViewer = viewer
        }
        if (pendingResumePosition >= 3000)
            resumeRestoreTimer.restart()
        if (Object.keys(pendingTrackPreferences || {}).length)
            trackPreferenceRestoreTimer.restart()
    }
    onClosed: {
        playbackSessionOpen = false
        saveResumePosition()
        saveTrackPreferences()
        player.stop()
        viewerFullScreen = false
        viewerFullScreenWindow.hide()
        audioTrackPopup.close()
        subtitleTrackPopup.close()
        subtitleAiSubtitlePopup.close()
        if (hostWindow) {
            if (hostWindow.mediaViewerConsumesF11 !== undefined)
                hostWindow.mediaViewerConsumesF11 = false
            if (hostWindow.activeVideoViewer === viewer)
                hostWindow.activeVideoViewer = null
        }
        controlsHideTimer.stop()
        pauseScreenTimer.stop()
        resumeRestoreTimer.stop()
        trackPreferenceRestoreTimer.stop()
        pauseScreenVisible = false
        controlsVisible = true
        lastPointerSceneX = -1
        lastPointerSceneY = -1
        if (resetMediaOnClose) {
            currentIndex = -1
            files = []
        }
    }
    onCurrentIndexChanged: {
        subtitleAiManager.clearSelectedSubtitle()
        selectedSubtitleSource = null
        prepareResumeForCurrent()
        refreshPauseMetadata()
        if (visible)
            Qt.callLater(function() { player.play(); filmstrip.positionViewAtIndex(currentIndex, ListView.Center) })
    }
    onHostFullScreenChanged: {
        lastPointerSceneX = -1
        lastPointerSceneY = -1
        revealControls()
    }

    Connections {
        target: viewer.metadataManager
        enabled: viewer.metadataManager !== null
        function onMetadataReady(filePath, metadata) {
            if (!viewer.currentItem) return
            if (String(filePath || "") === String(viewer.pauseLookupSource || ""))
                viewer.pauseMetadata = metadata
        }
        function onMetadataFailed(filePath, message) {
            if (!viewer.currentItem) return
            if (String(filePath || "") === String(viewer.pauseLookupSource || ""))
                viewer.pauseMetadata = ({ state: "error", parsedTitle: String(viewer.currentItem.name || ""), message: message })
        }
    }

    Timer {
        id: controlsHideTimer
        interval: 1800
        onTriggered: {
            if (topHover.hovered || bottomHover.hovered || stripHover.hovered || infoHover.hovered
                    || audioTrackPopup.visible || subtitleTrackPopup.visible) { restart(); return }
            viewer.controlsSettling = true
            viewer.controlsVisible = false
            settleTimer.restart()
        }
    }
    Timer { id: settleTimer; interval: 220; onTriggered: viewer.controlsSettling = false }
    Timer {
        id: pauseScreenTimer
        interval: 2000
        repeat: false
        onTriggered: {
            if (viewer.visible && viewer.paused) {
                viewer.refreshPauseMetadata()
                viewer.pauseScreenVisible = true
            }
        }
    }

    Timer {
        id: resumeRestoreTimer
        interval: 150
        repeat: true
        onTriggered: {
            if (!viewer.visible || viewer.resumeAppliedForSource || viewer.pendingResumePosition < 3000) {
                stop()
                return
            }
            viewer.resumeRestoreAttempts += 1
            if (viewer.tryApplyResume()) {
                stop()
                return
            }
            // Give slow first-start FFmpeg/AV1 pipelines several seconds to
            // become seekable before abandoning this open attempt.
            if (viewer.resumeRestoreAttempts >= 40)
                stop()
        }
    }

    Timer {
        id: trackPreferenceRestoreTimer
        interval: 150
        repeat: true
        onTriggered: {
            if (!viewer.visible || viewer.trackPreferencesAppliedForSource) {
                stop()
                return
            }
            viewer.trackPreferenceRestoreAttempts += 1
            if (viewer.tryApplyTrackPreferences()) {
                stop()
                return
            }
            // Some FFmpeg backends populate track metadata slightly after
            // LoadedMedia. Wait up to ~6 seconds, then keep Qt's defaults.
            if (viewer.trackPreferenceRestoreAttempts >= 40) {
                viewer.trackPreferencesAppliedForSource = true
                stop()
            }
        }
    }

    Timer {
        id: resumeSaveTimer
        interval: 2000
        repeat: true
        running: viewer.visible && viewer.playing
        onTriggered: viewer.saveResumePosition()
    }

    Component.onCompleted: VideoPlayerInputManager.registerViewer(viewer)
    Component.onDestruction: {
        VideoPlayerInputManager.unregisterViewer(viewer)
        saveResumePosition()
        saveTrackPreferences()
    }

    Item {
        anchors.fill: parent

        Item {
            id: videoScene
            anchors.fill: parent

            Rectangle { anchors.fill: parent; color: videoTheme.viewerBackground }

        Item {
            id: videoViewport
            anchors.centerIn: parent

            readonly property real nativeWidth: Math.max(1, viewer.sourceVideoWidth)
            readonly property real nativeHeight: Math.max(1, viewer.sourceVideoHeight)
            readonly property bool nativeKnown: viewer.sourceVideoWidth > 0 && viewer.sourceVideoHeight > 0
            readonly property real fitScale: nativeKnown
                                               ? Math.min(parent.width / nativeWidth, parent.height / nativeHeight)
                                               : 1.0
            readonly property real renderScale: nativeKnown
                                                  ? (viewer.allowVideoUpscale ? fitScale : Math.min(1.0, fitScale))
                                                  : 1.0

            width: nativeKnown ? Math.max(1, nativeWidth * renderScale) : parent.width
            height: nativeKnown ? Math.max(1, nativeHeight * renderScale) : parent.height

            VideoOutput {
                id: videoOutput
                objectName: "videoOutput"
                anchors.fill: parent
                // The viewport already has the exact source aspect ratio, so
                // no secondary stretch pass is needed here.
                fillMode: VideoOutput.Stretch
            }

            Connections {
                target: videoOutput.videoSink
                function onSubtitleTextChanged(text) {
                    // Flush late native cues as well as disabling the track;
                    // otherwise a buffered English cue can sit behind CC.
                    if (text.length) {
                        videoOutput.videoSink.subtitleText = ""
                        // Also handle text arriving after a track notification.
                        Qt.callLater(player.disableNativeSubtitleTrack)
                    }
                }
            }

            Item {
                id: subtitleOverlay
                objectName: "videoSubtitleOverlay"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: Math.max(18, parent.width * 0.06)
                anchors.rightMargin: Math.max(18, parent.width * 0.06)

                // Keep subtitles in their normal low position while the player
                // chrome is hidden.  When the bottom controls are visible, lift
                // the subtitle just above the control panel so short and long
                // cues never sit underneath the seek/buttons area.  mapToItem()
                // makes this work even when the video viewport is letterboxed or
                // kept at native size instead of filling the whole viewer.
                readonly property real restingBottomMargin: Math.max(18, parent.height * 0.045)
                readonly property real controlsSafeBottomMargin: {
                    if (!viewer.controlsVisible || viewer.controlsOpacity < 0.5 || !bottomPanel.visible)
                        return restingBottomMargin
                    // Explicitly touch geometry properties so this binding is
                    // reevaluated when the viewer/control layout changes.
                    const _geometryRevision = bottomPanel.x + bottomPanel.y + bottomPanel.width + bottomPanel.height
                                              + videoViewport.x + videoViewport.y + videoViewport.width + videoViewport.height
                    const panelTop = bottomPanel.mapToItem(videoViewport, 0, 0).y
                    const required = videoViewport.height - panelTop + 14
                    return Math.max(restingBottomMargin, required)
                }
                property real animatedBottomMargin: controlsSafeBottomMargin
                Behavior on animatedBottomMargin {
                    NumberAnimation { duration: 150; easing.type: Easing.OutCubic }
                }
                anchors.bottomMargin: animatedBottomMargin
                height: subtitleText.implicitHeight
                z: 5

                readonly property bool liveJobActive: viewer.subtitleAiJobPath.length > 0
                    && viewer.currentLocalPath() === viewer.subtitleAiJobPath
                    && (subtitleAiManager.busy || subtitleAiManager.outputPath.length > 0)
                readonly property string liveText: {
                    const _revision = subtitleAiManager.liveRevision
                    if (!liveJobActive)
                        return ""
                    return subtitleAiManager.liveSubtitleAt(player.position)
                }
                readonly property string selectedFileText: {
                    const _revision = subtitleAiManager.liveRevision
                    return subtitleAiManager.selectedSubtitleAt(player.position)
                }
                // Keep translation ownership until the finished sidecar is
                // selected, including the deferred completion handoff.
                readonly property string displayText: liveJobActive && (subtitleAiManager.busy || subtitleAiManager.liveTranslationActive)
                                                      ? liveText : (liveText.length > 0 ? liveText : selectedFileText)
                readonly property string semanticKind: viewer.subtitleSemanticKind(displayText)
                visible: displayText.length > 0 && (liveText.length > 0 || selectedFileText.length > 0)

                Text {
                    id: subtitleNaturalText
                    visible: false
                    text: subtitleOverlay.displayText
                    textFormat: Text.StyledText
                    font.family: subtitleOverlay.semanticKind === "forced" ? subtitleStyleManager.forcedFontFamily
                                 : (subtitleOverlay.semanticKind === "sdh" ? subtitleStyleManager.sdhFontFamily : subtitleStyleManager.subtitleFontFamily)
                    font.pixelSize: subtitleOverlay.semanticKind === "forced" ? subtitleStyleManager.forcedFontSize
                                    : (subtitleOverlay.semanticKind === "sdh" ? subtitleStyleManager.sdhFontSize : subtitleStyleManager.subtitleFontSize)
                    font.weight: Font.Medium
                    wrapMode: Text.NoWrap
                }

                // Capture the video only as an off-screen texture.  Drawing the
                // ShaderEffectSource itself used to leave a second rectangular
                // patch behind short subtitle backgrounds.
                Loader {
                    anchors.fill: subtitleBackground
                    z: 0
                    active: viewer.visible && !viewer.movingToWindow && subtitleStyleManager.backgroundBlur > 0
                    sourceComponent: Item {
                        ShaderEffectSource {
                            id: subtitleBlurCapture
                            anchors.fill: parent
                            sourceItem: viewer.movingToWindow ? null : videoOutput
                            sourceRect: {
                                const point = subtitleBackground.mapToItem(videoOutput, 0, 0)
                                return Qt.rect(point.x, point.y, Math.max(1, subtitleBackground.width),
                                               Math.max(1, subtitleBackground.height))
                            }
                            live: subtitleOverlay.visible
                            recursive: false
                            hideSource: false
                            visible: false
                        }
                        Rectangle {
                            id: subtitleBlurMask
                            anchors.fill: parent
                            radius: subtitleStyleManager.backgroundRadius
                            color: videoTheme.viewerText
                            visible: false
                            antialiasing: true
                            layer.enabled: true
                        }
                        MultiEffect {
                            anchors.fill: parent
                            source: subtitleBlurCapture
                            blurEnabled: true
                            blur: Math.min(1.0, subtitleStyleManager.backgroundBlur / 48.0)
                            blurMax: 48
                            autoPaddingEnabled: false
                            maskEnabled: true
                            maskSource: subtitleBlurMask
                            maskThresholdMin: 0.01
                            maskThresholdMax: 1.0
                            maskSpreadAtMin: 0.0
                            maskSpreadAtMax: 0.0
                        }
                    }
                }

                Rectangle {
                    id: subtitleBackground
                    anchors.fill: subtitleText
                    anchors.leftMargin: -12
                    anchors.rightMargin: -12
                    anchors.topMargin: -6
                    anchors.bottomMargin: -6
                    z: 1
                    radius: subtitleStyleManager.backgroundRadius
                    antialiasing: true
                    color: Qt.rgba(subtitleStyleManager.backgroundColor.r,
                                   subtitleStyleManager.backgroundColor.g,
                                   subtitleStyleManager.backgroundColor.b,
                                   subtitleStyleManager.backgroundOpacity)
                }

                Text {
                    id: subtitleText
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    width: Math.min(parent.width, Math.max(1, subtitleNaturalText.implicitWidth))
                    z: 2
                    text: subtitleOverlay.displayText
                    textFormat: Text.StyledText
                    color: subtitleOverlay.semanticKind === "forced" ? subtitleStyleManager.forcedColor
                           : (subtitleOverlay.semanticKind === "sdh" ? subtitleStyleManager.sdhColor : subtitleStyleManager.subtitleColor)
                    font.family: subtitleOverlay.semanticKind === "forced" ? subtitleStyleManager.forcedFontFamily
                                 : (subtitleOverlay.semanticKind === "sdh" ? subtitleStyleManager.sdhFontFamily : subtitleStyleManager.subtitleFontFamily)
                    font.pixelSize: subtitleOverlay.semanticKind === "forced" ? subtitleStyleManager.forcedFontSize
                                    : (subtitleOverlay.semanticKind === "sdh" ? subtitleStyleManager.sdhFontSize : subtitleStyleManager.subtitleFontSize)
                    font.weight: Font.Medium
                    style: Text.Outline
                    styleColor: "#D9000000"
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }
        }

        MouseArea {
            id: stageMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: viewer.hostFullScreen && !viewer.controlsVisible ? Qt.BlankCursor : Qt.ArrowCursor
            onPositionChanged: function(mouse) { viewer.handlePointer(stageMouse, mouse.x, mouse.y) }
            onClicked: viewer.togglePlayback()
        }

        Item {
            id: pauseScreen
            anchors.fill: parent
            z: 20
            visible: viewer.pauseScreenVisible && viewer.paused && player.error === MediaPlayer.NoError
            opacity: visible ? 1.0 : 0.0

            // The paused video frame remains fully visible. These masks only
            // add local contrast for metadata and the progress/status row.
            Rectangle {
                anchors.fill: parent
                color: "#16000000"
            }

            Rectangle {
                anchors.fill: parent
                color: "transparent"
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: "#22000000" }
                    GradientStop { position: 0.42; color: "#12000000" }
                    GradientStop { position: 0.68; color: "#62000000" }
                    GradientStop { position: 1.0; color: "#A8000000" }
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: Math.min(300, parent.height * 0.34)
                color: "transparent"
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#00000000" }
                    GradientStop { position: 1.0; color: "#9C000000" }
                }
            }

            MouseArea {
                id: pauseScreenMouse
                anchors.fill: parent
                z: 3
                hoverEnabled: true
                cursorShape: viewer.hostFullScreen && !viewer.controlsVisible ? Qt.BlankCursor : Qt.ArrowCursor
                property real previousX: -1
                property real previousY: -1
                onEntered: {
                    previousX = mouseX
                    previousY = mouseY
                }
                onPositionChanged: function(mouse) {
                    const moved = previousX >= 0 && previousY >= 0
                                  && (Math.abs(mouse.x - previousX) >= 2
                                      || Math.abs(mouse.y - previousY) >= 2)
                    previousX = mouse.x
                    previousY = mouse.y
                    if (moved) {
                        viewer.notePauseInteraction()
                        viewer.revealControls()
                    }
                }
                onClicked: viewer.togglePlayback()
            }

            TmdbPauseOverlay {
                id: pauseTmdbCard
                anchors.fill: parent
                z: 2
                metadata: viewer.pauseMetadata
                itemData: viewer.currentItem
                lang: viewer.lang
                metadataLanguage: viewer.metadataManager
                                  ? String(viewer.metadataManager.preferredLanguage || viewer.lang.language || "en")
                                  : String(viewer.lang.language || "en")
                shown: pauseScreen.visible
                animationsEnabled: true
                showBackdropArtwork: false
                frameless: true
                showFooter: false
            }

            RowLayout {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: Math.max(42, Math.min(72, parent.width * 0.035))
                anchors.rightMargin: Math.max(42, Math.min(72, parent.width * 0.035))
                anchors.bottomMargin: 38
                spacing: 10
                z: 4

                Text {
                    text: viewer.lang.language === "tr" ? "DURAKLATILDI" : "PAUSED"
                    color: "#F7FAFF"
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                    font.letterSpacing: 2.0
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: viewer.formatTime(player.position) + " / " + viewer.formatTime(player.duration)
                    color: "#C7FFFFFF"
                    font.pixelSize: 10
                    font.weight: Font.Medium
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: Math.max(42, Math.min(72, parent.width * 0.035))
                anchors.rightMargin: Math.max(42, Math.min(72, parent.width * 0.035))
                anchors.bottomMargin: 26
                height: 3
                radius: 2
                color: "#36FFFFFF"
                z: 4
                Rectangle {
                    width: player.duration > 0 ? parent.width * Math.max(0, Math.min(1, player.position / player.duration)) : 0
                    height: parent.height
                    radius: parent.radius
                    color: "#F7FAFF"
                }
            }
        }

        Text {
            anchors.centerIn: parent
            visible: player.error !== MediaPlayer.NoError
            text: player.errorString
            color: videoTheme.viewerText
            font.pixelSize: 13
            wrapMode: Text.Wrap
            width: Math.min(parent.width * 0.7, 620)
            horizontalAlignment: Text.AlignHCenter
        }

        Item {
            id: controlsLayer
            anchors.fill: parent
            opacity: viewer.controlsOpacity
            visible: opacity > 0.01
            Behavior on opacity { NumberAnimation { duration: 150 } }

            GGlassPanel {
                id: infoCard
                sourceItem: videoScene
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.leftMargin: 18
                anchors.topMargin: 16
                width: Math.min(430, Math.max(230, titleText.implicitWidth + 82))
                height: Math.max(54, infoRow.implicitHeight + 12)
                radius: 12
                tintColor: videoTheme.viewerGlass
                borderColor: videoTheme.viewerGlassBorder
                HoverHandler { id: infoHover; onHoveredChanged: if (hovered) viewer.revealControls() }
                RowLayout {
                    id: infoRow
                    anchors.fill: parent
                    anchors.leftMargin: 7
                    anchors.rightMargin: 12
                    spacing: 9
                    ViewerIconButton { Layout.preferredWidth: 36; Layout.preferredHeight: 36; iconName: "viewer-close.svg"; toolTipText: viewer.lang.language === "tr" ? "Kapat (Esc)" : "Close (Esc)"; onClicked: viewer.close() }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text { id: titleText; Layout.fillWidth: true; text: viewer.currentItem ? viewer.currentItem.name : ""; color: videoTheme.viewerText; font.pixelSize: 12; font.weight: Font.DemiBold; elide: Text.ElideMiddle }
                        Text {
                            Layout.fillWidth: true
                            text: viewer.currentItem
                                  ? viewer.formatSize(viewer.currentItem.size)
                                    + (viewer.sourceVideoResolution.length ? "  ·  " + viewer.sourceVideoResolution : "")
                                  : ""
                            color: videoTheme.viewerTextMuted
                            font.pixelSize: 9
                            elide: Text.ElideRight
                        }
                    }
                }
            }

            GGlassPanel {
                sourceItem: videoScene
                anchors.top: parent.top
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.topMargin: 18
                height: Math.max(52, topTools.implicitHeight + 12)
                width: topTools.implicitWidth + 18
                radius: 12
                tintColor: videoTheme.viewerGlass
                borderColor: videoTheme.viewerGlassBorder
                HoverHandler { id: topHover; onHoveredChanged: if (hovered) viewer.revealControls() }
                Row {
                    id: topTools
                    anchors.centerIn: parent
                    spacing: 6
                    ViewerIconButton {
                        iconName: "viewer-fit.svg"
                        emphasized: viewer.allowVideoUpscale
                        toolTipText: viewer.lang.language === "tr"
                                     ? (viewer.allowVideoUpscale ? "Doğal boyut / kalite" : "Ekrana büyüt")
                                     : (viewer.allowVideoUpscale ? "Native size / quality" : "Scale to screen")
                        onClicked: viewer.allowVideoUpscale = !viewer.allowVideoUpscale
                    }
                    ViewerIconButton { visible: viewer.currentItem && String(viewer.currentItem.parentPath || "").length > 0; iconName: "viewer-folder.svg"; toolTipText: viewer.lang.language === "tr" ? "Konumu aç" : "Open location"; onClicked: if (viewer.currentItem) { const p = viewer.currentItem.parentPath; viewer.close(); viewer.browseRequested(p) } }
                    ViewerIconButton { iconName: "viewer-external.svg"; toolTipText: viewer.lang.language === "tr" ? "Harici aç" : "Open externally"; onClicked: if (viewer.currentItem) Qt.openUrlExternally(viewer.currentItem.url) }
                    ViewerIconButton { iconName: viewer.hostFullScreen ? "viewer-fullscreen-exit.svg" : "viewer-fullscreen.svg"; toolTipText: viewer.lang.language === "tr" ? "Tam ekran (F11)" : "Full screen (F11)"; emphasized: viewer.hostFullScreen; onClicked: viewer.toggleHostFullScreen() }
                }
            }

            GGlassPanel {
                id: bottomPanel
                sourceItem: videoScene
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: stripPanel.top
                anchors.leftMargin: Math.max(18, parent.width * 0.12)
                anchors.rightMargin: Math.max(18, parent.width * 0.12)
                anchors.bottomMargin: 12
                height: Math.max(94, bottomPanelContent.implicitHeight + 16)
                radius: 14
                tintColor: videoTheme.viewerGlassStrong
                borderColor: videoTheme.viewerGlassBorder
                HoverHandler { id: bottomHover; onHoveredChanged: if (hovered) viewer.revealControls() }

                ColumnLayout {
                    id: bottomPanelContent
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    anchors.topMargin: 8
                    anchors.bottomMargin: 8
                    spacing: 5

                    GSlider { colorTheme: videoTheme;
                        id: seekSlider
                        Layout.fillWidth: true
                        Layout.preferredHeight: 22
                        Layout.maximumHeight: 22
                        from: 0
                        to: Math.max(1, player.duration)
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        onPressedChanged: { viewer.revealControls(); if (!pressed) player.position = value }
                        onMoved: viewer.revealControls()
                        Binding { target: seekSlider; property: "value"; value: player.position; when: !seekSlider.pressed }
                        background: Rectangle {
                            x: seekSlider.leftPadding
                            y: seekSlider.topPadding + seekSlider.availableHeight / 2 - height / 2
                            width: seekSlider.availableWidth
                            height: 4
                            radius: 2
                            color: videoTheme.viewerTrack
                            Rectangle { width: seekSlider.visualPosition * parent.width; height: parent.height; radius: 2; color: videoTheme.accent }
                        }
                        handle: Rectangle {
                            x: seekSlider.leftPadding + seekSlider.visualPosition * (seekSlider.availableWidth - width)
                            y: seekSlider.topPadding + seekSlider.availableHeight / 2 - height / 2
                            width: 13; height: 13; radius: 7
                            color: videoTheme.viewerText
                            border.width: 2
                            border.color: videoTheme.accent
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Text { text: viewer.formatTime(player.position); color: videoTheme.viewerTextMuted; font.pixelSize: 9; Layout.preferredWidth: 48 }
                        Item { Layout.fillWidth: true }
                        ViewerIconButton { iconName: "viewer-prev.svg"; toolTipText: viewer.lang.language === "tr" ? "Önceki video" : "Previous video"; onClicked: viewer.previousVideo() }
                        ViewerIconButton { iconName: "viewer-rewind.svg"; toolTipText: viewer.lang.language === "tr" ? "10 sn geri" : "Back 10 sec"; onClicked: viewer.seekBy(-10000) }
                        ViewerIconButton { implicitWidth: 44; implicitHeight: 40; iconName: viewer.playing ? "viewer-pause.svg" : "viewer-play.svg"; emphasized: true; toolTipText: viewer.lang.language === "tr" ? "Oynat / duraklat (Space)" : "Play / pause (Space)"; onClicked: viewer.togglePlayback() }
                        ViewerIconButton { iconName: "viewer-forward.svg"; toolTipText: viewer.lang.language === "tr" ? "10 sn ileri" : "Forward 10 sec"; onClicked: viewer.seekBy(10000) }
                        ViewerIconButton { iconName: "viewer-next.svg"; toolTipText: viewer.lang.language === "tr" ? "Sonraki video" : "Next video"; onClicked: viewer.nextVideo() }
                        Item { Layout.fillWidth: true }
                        ViewerTextButton {
                            visible: viewer.subtitleAiPanelMinimized && (subtitleAiManager.busy || subtitleAiManager.outputPath.length > 0 || subtitleAiManager.error.length > 0)
                            implicitWidth: 92
                            label: subtitleAiManager.busy
                                   ? "AI  " + subtitleAiManager.progress + "%"
                                   : (subtitleAiManager.error.length > 0 ? "AI  !" : "AI  ✓")
                            emphasized: true
                            toolTipText: viewer.lang.language === "tr"
                                         ? "Altyazı AI kontrol panelini aç"
                                         : "Open subtitle AI control panel"
                            onClicked: viewer.restoreSubtitleAiPanel()
                        }
                        ViewerTextButton {
                            label: viewer.lang.language === "tr" ? "SES" : "AUDIO"
                            implicitWidth: viewer.lang.language === "tr" ? 42 : 50
                            enabled: player.audioTracks.length > 0
                            emphasized: audioTrackPopup.visible
                            toolTipText: player.audioTracks.length > 0
                                         ? (viewer.lang.language === "tr" ? "Ses parçası: " : "Audio track: ") + viewer.audioTrackLabel(player.activeAudioTrack)
                                         : (viewer.lang.language === "tr" ? "Ses parçası bulunamadı" : "No audio tracks")
                            onClicked: { subtitleTrackPopup.close(); audioTrackPopup.open() }
                        }
                        ViewerTextButton {
                            label: "CC"
                            enabled: viewer.currentLocalPath().length > 0 || player.subtitleTracks.length > 0
                            emphasized: subtitleTrackPopup.visible || player.activeSubtitleTrack >= 0 || viewer.selectedSubtitleSource !== null
                            toolTipText: (player.activeSubtitleTrack >= 0 || viewer.selectedSubtitleSource !== null)
                                         ? (viewer.lang.language === "tr" ? "Altyazı: " : "Subtitle: ") + viewer.currentSubtitleLabel()
                                         : (viewer.lang.language === "tr" ? "Altyazı seçenekleri" : "Subtitle options")
                            onClicked: { audioTrackPopup.close(); subtitleTrackPopup.open() }
                        }
                        ViewerIconButton { iconName: audioOutput.muted || audioOutput.volume <= 0.001 ? "viewer-volume-muted.svg" : "viewer-volume.svg"; toolTipText: viewer.lang.language === "tr" ? "Sesi aç/kapat (M)" : "Mute / unmute (M)"; onClicked: audioOutput.muted = !audioOutput.muted }
                        GSlider { colorTheme: videoTheme;
                            id: volumeSlider
                            Layout.preferredWidth: 88
                            from: 0; to: 1; value: audioOutput.volume
                            HoverHandler { cursorShape: Qt.PointingHandCursor }
                            onMoved: { viewer.revealControls(); audioOutput.volume = value }
                            background: Rectangle {
                                x: volumeSlider.leftPadding
                                y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - height / 2
                                width: volumeSlider.availableWidth; height: 3; radius: 2; color: videoTheme.viewerTrack
                                Rectangle { width: volumeSlider.visualPosition * parent.width; height: parent.height; radius: 2; color: videoTheme.viewerText }
                            }
                            handle: Rectangle {
                                x: volumeSlider.leftPadding + volumeSlider.visualPosition * (volumeSlider.availableWidth - width)
                                y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - height / 2
                                width: 10; height: 10; radius: 5; color: videoTheme.viewerText
                            }
                        }
                        Text { text: viewer.formatTime(player.duration); color: videoTheme.viewerTextMuted; font.pixelSize: 9; Layout.preferredWidth: 48; horizontalAlignment: Text.AlignRight }
                    }
                }
            }

            GGlassPanel {
                id: stripPanel
                sourceItem: videoScene
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: 18
                anchors.rightMargin: 18
                anchors.bottomMargin: 14
                height: 76
                radius: 12
                tintColor: videoTheme.viewerGlassStrong
                borderColor: videoTheme.viewerGlassBorder
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
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        width: 96; height: 60; radius: 8
                        color: "#16000000"
                        border.width: index === viewer.currentIndex ? 2 : 1
                        border.color: index === viewer.currentIndex ? videoTheme.accent : videoTheme.viewerGlassBorderSoft
                        clip: true
                        Image { anchors.fill: parent; anchors.margins: 2; source: viewer.thumbSource(modelData); fillMode: Image.PreserveAspectCrop; asynchronous: true; cache: true; sourceSize.width: 192; sourceSize.height: 128 }
                        Rectangle {
                            anchors.centerIn: parent
                            width: 25; height: 25; radius: 13
                            color: "#B0000000"
                            GViewerIcon { anchors.centerIn: parent; width: 12; height: 12; source: viewer.viewerIcon("viewer-play.svg"); tintColor: videoTheme.viewerText }
                        }
                        MouseArea { anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: viewer.setIndex(index, true) }
                    }
                }
            }
        }
    }

    Popup {
        palette: viewer.palette
        id: audioTrackPopup
        parent: viewer.contentItem
        modal: false
        focus: true
        width: Math.min(360, Math.max(240, viewer.width - 32))
        height: Math.min(420, Math.max(80, viewer.height - 192), audioTrackColumn.implicitHeight + 20)
        x: Math.max(16, viewer.width - width - 18)
        y: Math.max(16, viewer.height - height - 176)
        padding: 10
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onOpened: { viewer.refreshSubtitleAiExistingSources(); viewer.revealControls() }
        background: GGlassPanel {
            sourceItem: videoScene
            radius: 14
            tintColor: videoTheme.viewerGlassStrong
            borderColor: videoTheme.viewerGlassBorder
            blurAmount: 0.66
        }
        contentItem: ScrollView {
            id: audioTrackScroll
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical.policy: ScrollBar.AsNeeded
            contentWidth: availableWidth
            contentHeight: audioTrackColumn.implicitHeight

            Column {
                id: audioTrackColumn
                width: audioTrackScroll.availableWidth
                spacing: 4
                Text {
                    width: parent.width
                    height: 34
                    text: viewer.lang.language === "tr" ? "Ses Parçası" : "Audio Track"
                    color: videoTheme.viewerText
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: 10
                }
                Rectangle { width: parent.width; height: 1; color: videoTheme.viewerGlassBorderSoft }
                Repeater {
                    model: player.audioTracks.length
                    delegate: ItemDelegate {
                        required property int index
                        width: audioTrackColumn.width
                        height: 42
                        hoverEnabled: true
                        text: viewer.audioTrackLabel(index)
                        highlighted: player.activeAudioTrack === index
                        contentItem: Text {
                            text: parent.text
                            color: videoTheme.viewerText
                            font.pixelSize: 11
                            font.weight: parent.highlighted ? Font.DemiBold : Font.Normal
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: 10
                        }
                        background: Rectangle {
                            radius: 8
                            color: parent.down ? videoTheme.viewerGlassPressed : (parent.hovered ? videoTheme.viewerGlassHover : (parent.highlighted ? videoTheme.viewerGlassHover : "transparent"))
                            border.width: parent.highlighted ? 1 : 0
                            border.color: videoTheme.accent
                        }
                        onClicked: {
                            player.activeAudioTrack = index
                            viewer.saveTrackPreferences()
                            audioTrackPopup.close()
                        }
                    }
                }
            }
        }
    }

    Popup {
        palette: viewer.palette
        id: subtitleTrackPopup
        parent: viewer.contentItem
        modal: false
        focus: true
        width: Math.min(360, Math.max(240, viewer.width - 32))
        height: Math.min(460, Math.max(80, viewer.height - 192), subtitleTrackColumn.implicitHeight + 20)
        x: Math.max(16, viewer.width - width - 18)
        y: Math.max(16, viewer.height - height - 176)
        padding: 10
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onOpened: {
            viewer.refreshSubtitleAiExistingSources()
            viewer.revealControls()
        }
        background: GGlassPanel {
            sourceItem: videoScene
            radius: 14
            tintColor: videoTheme.viewerGlassStrong
            borderColor: videoTheme.viewerGlassBorder
            blurAmount: 0.66
        }
        contentItem: ScrollView {
            id: subtitleTrackScroll
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical.policy: ScrollBar.AsNeeded
            contentWidth: availableWidth
            contentHeight: subtitleTrackColumn.implicitHeight

            Column {
                id: subtitleTrackColumn
                width: subtitleTrackScroll.availableWidth
                spacing: 4
                Text {
                    width: parent.width
                    height: 34
                    text: viewer.lang.language === "tr" ? "Altyazı" : "Subtitles"
                    color: videoTheme.viewerText
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: 10
                }
                Rectangle { width: parent.width; height: 1; color: videoTheme.viewerGlassBorderSoft }
                ItemDelegate {
                    width: subtitleTrackColumn.width
                    height: 42
                    hoverEnabled: true
                    text: viewer.lang.language === "tr" ? "Kapalı" : "Off"
                    highlighted: player.activeSubtitleTrack < 0 && !viewer.selectedSubtitleSource
                    contentItem: Text {
                        text: parent.text
                        color: videoTheme.viewerText
                        font.pixelSize: 11
                        font.weight: parent.highlighted ? Font.DemiBold : Font.Normal
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: 10
                    }
                    background: Rectangle {
                        radius: 8
                        color: parent.down ? videoTheme.viewerGlassPressed : (parent.hovered ? videoTheme.viewerGlassHover : (parent.highlighted ? videoTheme.viewerGlassHover : "transparent"))
                        border.width: parent.highlighted ? 1 : 0
                        border.color: videoTheme.accent
                    }
                    onClicked: {
                        viewer.clearSubtitleSelection()
                        subtitleTrackPopup.close()
                    }
                }
                Repeater {
                    model: player.subtitleTracks.length
                    delegate: ItemDelegate {
                        required property int index
                        width: subtitleTrackColumn.width
                        height: 42
                        hoverEnabled: true
                        text: viewer.subtitleTrackLabel(index)
                        highlighted: viewer.selectedSubtitleSource !== null
                                     && String(viewer.selectedSubtitleSource.kind || "") === "embedded"
                                     && Number(viewer.selectedSubtitleSource.trackIndex) === index
                        contentItem: Text {
                            text: parent.text
                            color: videoTheme.viewerText
                            font.pixelSize: 11
                            font.weight: parent.highlighted ? Font.DemiBold : Font.Normal
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: 10
                        }
                        background: Rectangle {
                            radius: 8
                            color: parent.down ? videoTheme.viewerGlassPressed : (parent.hovered ? videoTheme.viewerGlassHover : (parent.highlighted ? videoTheme.viewerGlassHover : "transparent"))
                            border.width: parent.highlighted ? 1 : 0
                            border.color: videoTheme.accent
                        }
                        onClicked: {
                            viewer.selectEmbeddedSubtitle(index)
                            subtitleTrackPopup.close()
                        }
                    }
                }
                Repeater {
                    model: viewer.subtitleExternalSources()
                    delegate: ItemDelegate {
                        required property var modelData
                        width: subtitleTrackColumn.width
                        height: 42
                        hoverEnabled: true
                        text: viewer.externalSubtitleLabel(modelData)
                        highlighted: viewer.selectedSubtitleSource !== null
                                     && String(viewer.selectedSubtitleSource.path || "") === String(modelData.path || "")
                        contentItem: Text {
                            text: parent.text
                            color: videoTheme.viewerText
                            font.pixelSize: 11
                            font.weight: parent.highlighted ? Font.DemiBold : Font.Normal
                            elide: Text.ElideMiddle
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: 10
                        }
                        background: Rectangle {
                            radius: 8
                            color: parent.down ? videoTheme.viewerGlassPressed : (parent.hovered ? videoTheme.viewerGlassHover : (parent.highlighted ? videoTheme.viewerGlassHover : "transparent"))
                            border.width: parent.highlighted ? 1 : 0
                            border.color: videoTheme.accent
                        }
                        onClicked: {
                            viewer.selectExternalSubtitle(modelData)
                            subtitleTrackPopup.close()
                        }
                    }
                }
                Rectangle { width: subtitleTrackColumn.width; height: 1; color: videoTheme.viewerGlassBorderSoft }
                ItemDelegate {
                    width: subtitleTrackColumn.width
                    height: 44
                    hoverEnabled: true
                    text: viewer.lang.language === "tr" ? "Aa  Altyazı görünümünü özelleştir…" : "Aa  Customize subtitle appearance…"
                    contentItem: Text {
                        text: parent.text
                        color: videoTheme.viewerText
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: 10
                        elide: Text.ElideRight
                    }
                    background: Rectangle {
                        radius: 8
                        color: parent.down ? videoTheme.viewerGlassPressed : (parent.hovered ? videoTheme.viewerGlassHover : "transparent")
                        border.width: parent.hovered ? 1 : 0
                        border.color: videoTheme.accent
                    }
                    onClicked: { subtitleTrackPopup.close(); subtitleStylePopup.open() }
                }
                Rectangle {
                    width: subtitleTrackColumn.width
                    height: 1
                    color: videoTheme.viewerGlassBorderSoft
                    visible: viewer.currentLocalPath().length > 0
                }
                ItemDelegate {
                    width: subtitleTrackColumn.width
                    height: 44
                    visible: viewer.currentLocalPath().length > 0
                    enabled: visible
                    hoverEnabled: true
                    text: viewer.lang.language === "tr" ? "+  Altyazı oluştur…" : "+  Generate subtitles…"
                    contentItem: Text {
                        text: parent.text
                        color: videoTheme.viewerText
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: 10
                    }
                    background: Rectangle {
                        radius: 8
                        color: parent.down ? videoTheme.viewerGlassPressed : (parent.hovered ? videoTheme.viewerGlassHover : videoTheme.viewerGlassHover)
                        border.width: 1
                        border.color: parent.hovered ? videoTheme.accent : videoTheme.viewerGlassHover
                    }
                    onClicked: viewer.openSubtitleAiGenerateDialog()
                }
                ItemDelegate {
                    width: subtitleTrackColumn.width
                    height: 44
                    visible: viewer.currentLocalPath().length > 0 && viewer.subtitleAiExistingSources.length > 0
                    enabled: visible
                    hoverEnabled: true
                    text: viewer.lang.language === "tr" ? "+  Mevcut altyazıyı AI ile çevir…" : "+  Translate existing subtitle with AI…"
                    contentItem: Text {
                        text: parent.text
                        color: videoTheme.viewerText
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: 10
                        elide: Text.ElideRight
                    }
                    background: Rectangle {
                        radius: 8
                        color: parent.down ? videoTheme.viewerGlassPressed : (parent.hovered ? videoTheme.viewerGlassHover : videoTheme.viewerGlassHover)
                        border.width: 1
                        border.color: parent.hovered ? videoTheme.accent : videoTheme.viewerGlassHover
                    }
                    onClicked: viewer.openSubtitleAiTranslateDialog()
                }
            }
        }
    }

    ColorDialog {
        id: subtitleStyleColorDialog
        title: viewer.lang.language === "tr" ? "Altyazı rengini seç" : "Choose subtitle color"
        onAccepted: viewer.applySubtitleStyleColor(viewer.subtitleStyleColorTarget, selectedColor)
    }

    Popup {
        palette: viewer.palette
        id: subtitleStyleFontPicker
        parent: viewer.contentItem
        modal: true
        focus: true
        width: Math.min(520, Math.max(360, viewer.width - 56))
        height: Math.min(620, Math.max(420, viewer.height - 64))
        x: Math.round((viewer.width - width) / 2)
        y: Math.round((viewer.height - height) / 2)
        padding: 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        Overlay.modal: Rectangle { color: "#A0000000" }
        background: GGlassPanel {
            sourceItem: videoScene
            radius: 16
            tintColor: videoTheme.viewerGlassStrong
            borderColor: videoTheme.viewerGlassBorder
            blurAmount: 0.72
        }

        contentItem: ColumnLayout {
            spacing: 0
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 20; Layout.rightMargin: 14
                Layout.topMargin: 16; Layout.bottomMargin: 12
                Text {
                    Layout.fillWidth: true
                    text: viewer.lang.language === "tr" ? "Altyazı Fontu" : "Subtitle Font"
                    color: videoTheme.viewerText; font.pixelSize: 17; font.weight: Font.Bold
                }
                GButton { colorTheme: videoTheme;
                    implicitWidth: 34; implicitHeight: 30; padding: 0; text: "×"; flat: true
                    contentItem: Text {
                        text: parent.text; color: parent.hovered ? videoTheme.viewerText : videoTheme.viewerTextMuted
                        font.pixelSize: 18; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 8; color: parent.down ? videoTheme.viewerGlassBorderSoft : (parent.hovered ? videoTheme.viewerGlassHover : videoTheme.viewerGlassSoft)
                        border.width: 1; border.color: parent.hovered ? videoTheme.viewerGlassBorder : videoTheme.viewerGlassBorderSoft
                    }
                    onClicked: subtitleStyleFontPicker.close()
                }
            }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: videoTheme.viewerGlassBorderSoft }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 18; Layout.rightMargin: 18
                Layout.topMargin: 14; Layout.bottomMargin: 12
                spacing: 10
                GTextField { colorTheme: videoTheme;
                    id: subtitleFontSearchField
                    Layout.fillWidth: true
                    implicitHeight: 40
                    text: viewer.subtitleStyleFontSearch
                    placeholderText: viewer.lang.language === "tr" ? "Font ara…" : "Search fonts…"
                    color: videoTheme.viewerText
                    placeholderTextColor: videoTheme.viewerTextMuted
                    selectionColor: videoTheme.accent
                    selectedTextColor: "white"
                    leftPadding: 12; rightPadding: 12
                    onTextChanged: viewer.subtitleStyleFontSearch = text
                    background: Rectangle {
                        radius: 9
                        color: videoTheme.viewerGlassSoft
                        border.width: 1
                        border.color: subtitleFontSearchField.activeFocus ? videoTheme.accent : videoTheme.viewerGlassBorder
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 88
                    radius: 10
                    color: videoTheme.viewerGlassSoft
                    border.width: 1
                    border.color: videoTheme.viewerGlassBorderSoft
                    Text {
                        anchors.centerIn: parent
                        width: parent.width - 24
                        text: viewer.lang.language === "tr" ? "Seçili font · Altyazı Önizleme 123" : "Selected font · Subtitle Preview 123"
                        color: videoTheme.viewerText
                        font.family: viewer.subtitleStyleFontCurrent
                        font.pixelSize: viewer.subtitleStyleFontSizeFor(viewer.subtitleStyleFontTarget)
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Text {
                        text: viewer.lang.language === "tr" ? "Boyut" : "Size"
                        color: videoTheme.viewerTextMuted
                        font.pixelSize: 11
                        Layout.preferredWidth: 48
                    }
                    GSlider { colorTheme: videoTheme;
                        Layout.fillWidth: true
                        from: 12; to: 72; stepSize: 1
                        value: viewer.subtitleStyleFontSizeFor(viewer.subtitleStyleFontTarget)
                        onMoved: viewer.applySubtitleStyleFontSize(viewer.subtitleStyleFontTarget, value)
                    }
                    Text {
                        text: viewer.subtitleStyleFontSizeFor(viewer.subtitleStyleFontTarget) + " px"
                        color: videoTheme.viewerText
                        font.pixelSize: 11
                        Layout.preferredWidth: 46
                        horizontalAlignment: Text.AlignRight
                    }
                }
            }

            ListView {
                id: subtitleFontList
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: 14; Layout.rightMargin: 14
                Layout.bottomMargin: 14
                clip: true
                spacing: 4
                model: viewer.subtitleStyleFilteredFonts
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                delegate: ItemDelegate {
                    required property var modelData
                    width: ListView.view.width
                    height: 42
                    hoverEnabled: true
                    highlighted: String(modelData) === viewer.subtitleStyleFontCurrent
                    contentItem: Text {
                        text: String(parent.modelData)
                        color: videoTheme.viewerText
                        font.family: String(parent.modelData)
                        font.pixelSize: 13
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: 10
                        elide: Text.ElideRight
                    }
                    background: Rectangle {
                        radius: 8
                        color: parent.highlighted ? videoTheme.viewerGlassEmphasis : (parent.down ? videoTheme.viewerGlassPressed : (parent.hovered ? videoTheme.viewerGlassHover : videoTheme.viewerGlassSoft))
                        border.width: 1
                        border.color: parent.highlighted ? videoTheme.accent : (parent.hovered ? videoTheme.viewerGlassBorder : videoTheme.viewerGlassBorderSoft)
                    }
                    onClicked: {
                        viewer.subtitleStyleFontCurrent = String(modelData)
                        viewer.applySubtitleStyleFont(viewer.subtitleStyleFontTarget, viewer.subtitleStyleFontCurrent)
                        subtitleStyleFontPicker.close()
                    }
                }
            }
        }
    }

    Popup {
        palette: viewer.palette
        id: subtitleStylePopup
        parent: viewer.contentItem
        modal: true
        focus: true
        width: Math.min(560, Math.max(380, viewer.width - 48))
        height: Math.min(680, Math.max(420, viewer.height - 48))
        x: Math.round((viewer.width - width) / 2)
        y: Math.round((viewer.height - height) / 2)
        padding: 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        onOpened: viewer.revealControls()
        Overlay.modal: Rectangle { color: "#92000000" }
        background: GGlassPanel { sourceItem: videoScene; radius: 16; tintColor: videoTheme.viewerGlassStrong; borderColor: videoTheme.viewerGlassBorder; blurAmount: 0.72 }

        contentItem: ColumnLayout {
            spacing: 0
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 24; Layout.rightMargin: 16; Layout.topMargin: 16; Layout.bottomMargin: 14
                Text {
                    Layout.fillWidth: true
                    text: viewer.lang.language === "tr" ? "Altyazı Görünümü" : "Subtitle Appearance"
                    color: videoTheme.viewerText; font.pixelSize: 18; font.weight: Font.Bold
                }
                GButton { colorTheme: videoTheme;
                    implicitWidth: 34; implicitHeight: 30; padding: 0; text: "×"; flat: true
                    contentItem: Text { text: parent.text; color: parent.hovered ? videoTheme.viewerText : videoTheme.viewerTextMuted; font.pixelSize: 18; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    background: Rectangle { radius: 8; color: parent.down ? videoTheme.viewerGlassBorderSoft : (parent.hovered ? videoTheme.viewerGlassHover : videoTheme.viewerGlassSoft); border.width: 1; border.color: parent.hovered ? videoTheme.viewerGlassBorder : videoTheme.viewerGlassBorderSoft }
                    onClicked: subtitleStylePopup.close()
                }
            }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: videoTheme.viewerGlassBorderSoft }

            ScrollView {
                Layout.fillWidth: true; Layout.fillHeight: true
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                contentWidth: availableWidth

                ColumnLayout {
                    width: Math.max(0, parent.width - 40)
                    x: 20
                    spacing: 16

                    Rectangle {
                        Layout.fillWidth: true; Layout.preferredHeight: 132; radius: 12
                        color: "#12000000"; border.width: 1; border.color: videoTheme.viewerGlassHover
                        Column {
                            anchors.centerIn: parent
                            width: parent.width - 36
                            spacing: 8
                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: Math.min(parent.width, normalPreview.implicitWidth + 24)
                                height: normalPreview.implicitHeight + 10
                                radius: subtitleStyleManager.backgroundRadius
                                color: Qt.rgba(subtitleStyleManager.backgroundColor.r, subtitleStyleManager.backgroundColor.g, subtitleStyleManager.backgroundColor.b, subtitleStyleManager.backgroundOpacity)
                                Text { id: normalPreview; anchors.centerIn: parent; text: viewer.lang.language === "tr" ? "Normal altyazı" : "Normal subtitle"; color: subtitleStyleManager.subtitleColor; font.family: subtitleStyleManager.subtitleFontFamily; font.pixelSize: Math.min(28, subtitleStyleManager.subtitleFontSize); style: Text.Outline; styleColor: "#D9000000" }
                            }
                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: "[KAPI ÇALAR]  ·  SDH"; color: subtitleStyleManager.sdhColor; font.family: subtitleStyleManager.sdhFontFamily; font.pixelSize: Math.min(26, subtitleStyleManager.sdhFontSize); style: Text.Outline; styleColor: "#D9000000" }
                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: "TEST 123  ·  FORCED"; color: subtitleStyleManager.forcedColor; font.family: subtitleStyleManager.forcedFontFamily; font.pixelSize: Math.min(26, subtitleStyleManager.forcedFontSize); style: Text.Outline; styleColor: "#D9000000" }
                        }
                    }

                    Text { text: viewer.lang.language === "tr" ? "GENEL ALTYAZI" : "GENERAL SUBTITLE"; color: videoTheme.viewerTextMuted; font.pixelSize: 10; font.weight: Font.Bold }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 10
                        GButton { colorTheme: videoTheme;
                            Layout.fillWidth: true; implicitHeight: 42
                            text: viewer.lang.language === "tr" ? "Renk" : "Color"
                            contentItem: RowLayout {
                                spacing: 9
                                Rectangle { Layout.preferredWidth: 20; Layout.preferredHeight: 20; radius: 5; color: subtitleStyleManager.subtitleColor; border.width: 1; border.color: videoTheme.viewerGlassBorder }
                                Text { Layout.fillWidth: true; text: parent.parent.text; color: videoTheme.viewerText; font.pixelSize: 11; verticalAlignment: Text.AlignVCenter }
                            }
                            background: Rectangle { radius: 9; color: parent.down ? videoTheme.viewerGlassPressed : (parent.hovered ? videoTheme.viewerGlassHover : videoTheme.viewerGlassSoft); border.width: 1; border.color: parent.hovered ? videoTheme.accent : videoTheme.viewerGlassBorder }
                            onClicked: viewer.openSubtitleStyleColor("subtitle", subtitleStyleManager.subtitleColor)
                        }
                        GButton { colorTheme: videoTheme;
                            Layout.fillWidth: true; implicitHeight: 42
                            text: subtitleStyleManager.subtitleFontFamily + " · " + subtitleStyleManager.subtitleFontSize + " px"
                            contentItem: Text { text: parent.text; color: videoTheme.viewerText; font.family: subtitleStyleManager.subtitleFontFamily; font.pixelSize: 11; verticalAlignment: Text.AlignVCenter; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight }
                            background: Rectangle { radius: 9; color: parent.down ? videoTheme.viewerGlassPressed : (parent.hovered ? videoTheme.viewerGlassHover : videoTheme.viewerGlassSoft); border.width: 1; border.color: parent.hovered ? videoTheme.accent : videoTheme.viewerGlassBorder }
                            onClicked: viewer.openSubtitleStyleFont("subtitle", subtitleStyleManager.subtitleFontFamily)
                        }
                    }

                    Text { text: viewer.lang.language === "tr" ? "ARKA PLAN" : "BACKGROUND"; color: videoTheme.viewerTextMuted; font.pixelSize: 10; font.weight: Font.Bold }
                    GButton { colorTheme: videoTheme;
                        Layout.fillWidth: true; implicitHeight: 42
                        text: viewer.lang.language === "tr" ? "Arka plan rengi" : "Background color"
                        contentItem: RowLayout {
                            spacing: 9
                            Rectangle { Layout.preferredWidth: 20; Layout.preferredHeight: 20; radius: 5; color: subtitleStyleManager.backgroundColor; border.width: 1; border.color: videoTheme.viewerGlassBorder }
                            Text { Layout.fillWidth: true; text: parent.parent.text; color: videoTheme.viewerText; font.pixelSize: 11; verticalAlignment: Text.AlignVCenter }
                        }
                        background: Rectangle { radius: 9; color: parent.down ? videoTheme.viewerGlassPressed : (parent.hovered ? videoTheme.viewerGlassHover : videoTheme.viewerGlassSoft); border.width: 1; border.color: parent.hovered ? videoTheme.accent : videoTheme.viewerGlassBorder }
                        onClicked: viewer.openSubtitleStyleColor("background", subtitleStyleManager.backgroundColor)
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 10
                        Text { text: viewer.lang.language === "tr" ? "Radius" : "Radius"; color: videoTheme.viewerTextMuted; font.pixelSize: 10; Layout.preferredWidth: 78 }
                        GSlider { colorTheme: videoTheme; Layout.fillWidth: true; from: 0; to: 32; stepSize: 1; value: subtitleStyleManager.backgroundRadius; onMoved: subtitleStyleManager.backgroundRadius = Math.round(value) }
                        Text { text: subtitleStyleManager.backgroundRadius + " px"; color: videoTheme.viewerText; font.pixelSize: 10; Layout.preferredWidth: 48; horizontalAlignment: Text.AlignRight }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 10
                        Text { text: viewer.lang.language === "tr" ? "Saydamlık" : "Opacity"; color: videoTheme.viewerTextMuted; font.pixelSize: 10; Layout.preferredWidth: 78 }
                        GSlider { colorTheme: videoTheme; Layout.fillWidth: true; from: 0; to: 1; stepSize: 0.01; value: 1.0 - subtitleStyleManager.backgroundOpacity; onMoved: subtitleStyleManager.backgroundOpacity = 1.0 - value }
                        Text { text: Math.round((1.0 - subtitleStyleManager.backgroundOpacity) * 100) + "%"; color: videoTheme.viewerText; font.pixelSize: 10; Layout.preferredWidth: 48; horizontalAlignment: Text.AlignRight }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 10
                        Text { text: "Blur"; color: videoTheme.viewerTextMuted; font.pixelSize: 10; Layout.preferredWidth: 78 }
                        GSlider { colorTheme: videoTheme; Layout.fillWidth: true; from: 0; to: 48; stepSize: 1; value: subtitleStyleManager.backgroundBlur; onMoved: subtitleStyleManager.backgroundBlur = Math.round(value) }
                        Text { text: subtitleStyleManager.backgroundBlur + " px"; color: videoTheme.viewerText; font.pixelSize: 10; Layout.preferredWidth: 48; horizontalAlignment: Text.AlignRight }
                    }

                    Text { text: "SDH"; color: videoTheme.viewerTextMuted; font.pixelSize: 10; font.weight: Font.Bold }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 10
                        GButton { colorTheme: videoTheme;
                            Layout.fillWidth: true; implicitHeight: 42; text: viewer.lang.language === "tr" ? "SDH rengi" : "SDH color"
                            contentItem: RowLayout {
                                spacing: 9
                                Rectangle { Layout.preferredWidth: 20; Layout.preferredHeight: 20; radius: 5; color: subtitleStyleManager.sdhColor; border.width: 1; border.color: videoTheme.viewerGlassBorder }
                                Text { Layout.fillWidth: true; text: parent.parent.text; color: videoTheme.viewerText; font.pixelSize: 11; verticalAlignment: Text.AlignVCenter }
                            }
                            background: Rectangle { radius: 9; color: parent.down ? videoTheme.viewerGlassPressed : (parent.hovered ? videoTheme.viewerGlassHover : videoTheme.viewerGlassSoft); border.width: 1; border.color: parent.hovered ? videoTheme.accent : videoTheme.viewerGlassBorder }
                            onClicked: viewer.openSubtitleStyleColor("sdh", subtitleStyleManager.sdhColor)
                        }
                        GButton { colorTheme: videoTheme;
                            Layout.fillWidth: true; implicitHeight: 42; text: subtitleStyleManager.sdhFontFamily + " · " + subtitleStyleManager.sdhFontSize + " px"
                            contentItem: Text { text: parent.text; color: videoTheme.viewerText; font.family: subtitleStyleManager.sdhFontFamily; font.pixelSize: 11; verticalAlignment: Text.AlignVCenter; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight }
                            background: Rectangle { radius: 9; color: parent.down ? videoTheme.viewerGlassPressed : (parent.hovered ? videoTheme.viewerGlassHover : videoTheme.viewerGlassSoft); border.width: 1; border.color: parent.hovered ? videoTheme.accent : videoTheme.viewerGlassBorder }
                            onClicked: viewer.openSubtitleStyleFont("sdh", subtitleStyleManager.sdhFontFamily)
                        }
                    }

                    Text { text: viewer.lang.language === "tr" ? "ZORUNLU / FORCED" : "FORCED"; color: videoTheme.viewerTextMuted; font.pixelSize: 10; font.weight: Font.Bold }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 10
                        GButton { colorTheme: videoTheme;
                            Layout.fillWidth: true; implicitHeight: 42; text: viewer.lang.language === "tr" ? "Forced rengi" : "Forced color"
                            contentItem: RowLayout {
                                spacing: 9
                                Rectangle { Layout.preferredWidth: 20; Layout.preferredHeight: 20; radius: 5; color: subtitleStyleManager.forcedColor; border.width: 1; border.color: videoTheme.viewerGlassBorder }
                                Text { Layout.fillWidth: true; text: parent.parent.text; color: videoTheme.viewerText; font.pixelSize: 11; verticalAlignment: Text.AlignVCenter }
                            }
                            background: Rectangle { radius: 9; color: parent.down ? videoTheme.viewerGlassPressed : (parent.hovered ? videoTheme.viewerGlassHover : videoTheme.viewerGlassSoft); border.width: 1; border.color: parent.hovered ? videoTheme.accent : videoTheme.viewerGlassBorder }
                            onClicked: viewer.openSubtitleStyleColor("forced", subtitleStyleManager.forcedColor)
                        }
                        GButton { colorTheme: videoTheme;
                            Layout.fillWidth: true; implicitHeight: 42; text: subtitleStyleManager.forcedFontFamily + " · " + subtitleStyleManager.forcedFontSize + " px"
                            contentItem: Text { text: parent.text; color: videoTheme.viewerText; font.family: subtitleStyleManager.forcedFontFamily; font.pixelSize: 11; verticalAlignment: Text.AlignVCenter; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight }
                            background: Rectangle { radius: 9; color: parent.down ? videoTheme.viewerGlassPressed : (parent.hovered ? videoTheme.viewerGlassHover : videoTheme.viewerGlassSoft); border.width: 1; border.color: parent.hovered ? videoTheme.accent : videoTheme.viewerGlassBorder }
                            onClicked: viewer.openSubtitleStyleFont("forced", subtitleStyleManager.forcedFontFamily)
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: viewer.lang.language === "tr"
                              ? "Forced: track zorunlu işaretliyse tamamı; değilse yalnız harf içeren ve tüm harfleri BÜYÜK cue'lar. 123 hariç, TEST 123 dahil, test 123 hariç."
                              : "Forced: the whole track when marked forced; otherwise only cues containing letters whose letters are ALL CAPS. 123 is excluded, TEST 123 included, test 123 excluded."
                        color: videoTheme.viewerTextMuted; font.pixelSize: 9; wrapMode: Text.Wrap
                    }

                    RowLayout {
                        Layout.fillWidth: true; Layout.bottomMargin: 20
                        Item { Layout.fillWidth: true }
                        GButton { colorTheme: videoTheme;
                            text: viewer.lang.language === "tr" ? "Varsayılana dön" : "Reset defaults"
                            contentItem: Text { text: parent.text; color: videoTheme.viewerTextMuted; font.pixelSize: 10; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                            background: Rectangle { radius: 9; color: parent.down ? videoTheme.viewerGlassPressed : (parent.hovered ? videoTheme.viewerGlassHover : videoTheme.viewerGlassSoft); border.width: 1; border.color: videoTheme.viewerGlassBorderSoft }
                            onClicked: subtitleStyleManager.resetDefaults()
                        }
                    }
                }
            }
        }
    }

    Popup {
        palette: viewer.palette
        id: subtitleAiSubtitlePopup
        parent: viewer.contentItem
        modal: true
        focus: true
        width: Math.min(480, Math.max(360, viewer.width - 48))
        height: Math.min(viewer.height - 40, subtitleAiManager.busy ? 350 : (subtitleAiManager.outputPath.length || subtitleAiManager.error.length ? 390 : 540))
        x: Math.round((viewer.width - width) / 2)
        y: Math.round((viewer.height - height) / 2)
        padding: 0
        closePolicy: subtitleAiManager.busy ? Popup.NoAutoClose : (Popup.CloseOnEscape | Popup.CloseOnPressOutside)
        onOpened: viewer.revealControls()

        Overlay.modal: Rectangle { color: "#92000000" }
        background: GGlassPanel {
            sourceItem: videoScene
            radius: 16
            tintColor: videoTheme.viewerGlassStrong
            borderColor: videoTheme.viewerGlassBorder
            blurAmount: 0.72
        }

        contentItem: ColumnLayout {
            spacing: 0

            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 16
                Layout.topMargin: 16
                Layout.bottomMargin: 14
                spacing: 4
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text {
                        Layout.fillWidth: true
                        text: viewer.subtitleAiMode === "subtitle"
                              ? (viewer.lang.language === "tr" ? "G-File AI · Altyazı Çevir" : "G-File AI · Translate Subtitle")
                              : (viewer.lang.language === "tr" ? "G-File AI · Altyazı Oluştur" : "G-File AI · Generate Subtitles")
                        color: videoTheme.viewerText
                        font.pixelSize: 18
                        font.weight: Font.Bold
                    }
                    GButton { colorTheme: videoTheme;
                        id: subtitleAiMinimizeButton
                        visible: subtitleAiManager.busy
                        implicitWidth: 34
                        implicitHeight: 30
                        padding: 0
                        text: "—"
                        font.pixelSize: 16
                        flat: true
                        contentItem: Text {
                            text: subtitleAiMinimizeButton.text
                            color: subtitleAiMinimizeButton.hovered ? videoTheme.viewerText : videoTheme.viewerTextMuted
                            font.pixelSize: subtitleAiMinimizeButton.font.pixelSize
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            radius: 8
                            color: subtitleAiMinimizeButton.down ? videoTheme.viewerGlassBorderSoft : (subtitleAiMinimizeButton.hovered ? videoTheme.viewerGlassHover : videoTheme.viewerGlassSoft)
                            border.width: 1
                            border.color: subtitleAiMinimizeButton.hovered ? videoTheme.viewerGlassBorder : videoTheme.viewerGlassBorderSoft
                        }
                        onClicked: viewer.minimizeSubtitleAiPanel()
                        ToolTip.visible: hovered
                        ToolTip.text: viewer.lang.language === "tr" ? "Kontrol paneline küçült" : "Minimize to controls"
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: viewer.currentItem ? String(viewer.currentItem.name || "") : ""
                    color: videoTheme.viewerTextMuted
                    font.pixelSize: 10
                    elide: Text.ElideMiddle
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: videoTheme.viewerGlassBorderSoft }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.topMargin: 18
                Layout.bottomMargin: 18
                spacing: 14

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    visible: viewer.subtitleAiMode === "subtitle" && !subtitleAiManager.busy && !subtitleAiManager.outputPath.length && !subtitleAiManager.error.length
                    Text { text: viewer.lang.language === "tr" ? "Kaynak altyazı" : "Source subtitle"; color: videoTheme.viewerTextMuted; font.pixelSize: 10; font.weight: Font.DemiBold }
                    GComboBox { colorTheme: videoTheme;
                        id: subtitleAiExistingCombo
                        Layout.fillWidth: true
                        model: viewer.subtitleAiExistingSources.map(function(item) { return viewer.existingSubtitleLabel(item) })
                        currentIndex: Math.max(0, Math.min(viewer.subtitleAiExistingSourceIndex, count - 1))
                        onActivated: {
                            viewer.subtitleAiExistingSourceIndex = currentIndex
                            const source = viewer.selectedExistingSubtitle()
                            if (source) viewer.ensureSubtitleAiTargetValid(String(source.language || "und"))
                            viewer.refreshSubtitleAiCacheState()
                        }
                        indicator: Text { x: subtitleAiExistingCombo.width - width - 12; y: Math.round((subtitleAiExistingCombo.height - height) / 2) - 1; text: "⌄"; color: videoTheme.viewerTextMuted; font.pixelSize: 15 }
                        contentItem: Text { leftPadding: 12; rightPadding: 32; text: subtitleAiExistingCombo.displayText; color: videoTheme.viewerText; font.pixelSize: 11; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                        background: Rectangle { radius: 9; color: subtitleAiExistingCombo.down ? videoTheme.viewerGlassPressed : videoTheme.viewerGlassSoft; border.width: 1; border.color: subtitleAiExistingCombo.activeFocus ? videoTheme.accent : videoTheme.viewerGlassBorder }
                        popup: Popup {
                            palette: viewer.palette
                            y: subtitleAiExistingCombo.height + 4; width: subtitleAiExistingCombo.width; padding: 5
                            implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
                            background: GGlassPanel { sourceItem: videoScene; radius: 10; tintColor: videoTheme.viewerGlassStrong; borderColor: videoTheme.viewerGlassBorder; blurAmount: 0.64 }
                            contentItem: ListView { clip: true; implicitHeight: Math.min(contentHeight, 260); model: subtitleAiExistingCombo.popup.visible ? subtitleAiExistingCombo.delegateModel : null; currentIndex: subtitleAiExistingCombo.highlightedIndex; ScrollIndicator.vertical: ScrollIndicator {} }
                        }
                        delegate: ItemDelegate {
                            required property var modelData
                            required property int index
                            width: subtitleAiExistingCombo.width - 10; height: 40; hoverEnabled: true
                            contentItem: Text { text: modelData; color: videoTheme.viewerText; font.pixelSize: 10; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                            background: Rectangle { radius: 7; color: parent.down ? videoTheme.viewerGlassPressed : (parent.hovered ? videoTheme.viewerGlassHover : (subtitleAiExistingCombo.currentIndex === index ? videoTheme.viewerGlassBorderSoft : "transparent")); border.width: subtitleAiExistingCombo.currentIndex === index ? 1 : 0; border.color: videoTheme.accent }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: {
                            const source = viewer.selectedExistingSubtitle()
                            if (!source) return viewer.lang.language === "tr" ? "Çevrilebilir altyazı bulunamadı." : "No translatable subtitle found."
                            const cached = viewer.subtitleAiCacheState || ({})
                            if (cached.translationReady) return viewer.lang.language === "tr" ? "✓ Bu çeviri hazır" : "✓ Translation is ready"
                            if (Number(cached.translationProgress || 0) > 0) return viewer.lang.language === "tr" ? "Önceki çeviri kaldığı yerden devam edebilir." : "Previous translation can be resumed."
                            return viewer.lang.language === "tr" ? "Whisper kullanılmaz; seçili altyazı doğrudan yerel AI modeline verilir." : "Whisper is skipped; the selected subtitle is sent directly to the local AI model."
                        }
                        color: viewer.subtitleAiCacheState && viewer.subtitleAiCacheState.translationReady ? "#CFFFD8" : "#AFFFFFFF"
                        font.pixelSize: 9
                        wrapMode: Text.Wrap
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    visible: viewer.subtitleAiMode === "transcribe" && !subtitleAiManager.busy && !subtitleAiManager.outputPath.length && !subtitleAiManager.error.length
                    Text { text: viewer.lang.language === "tr" ? "Konuşma dili / ses parçası" : "Spoken language / audio track"; color: videoTheme.viewerTextMuted; font.pixelSize: 10; font.weight: Font.DemiBold }
                    GComboBox { colorTheme: videoTheme;
                        id: subtitleAiLanguageCombo
                        Layout.fillWidth: true
                        model: viewer.subtitleAiSourceOptions().map(function(item) { return item.label })
                        currentIndex: Math.max(0, Math.min(viewer.subtitleAiSourceIndex, count - 1))
                        onActivated: {
                            viewer.subtitleAiSourceIndex = currentIndex
                            const source = viewer.subtitleAiSourceOptions()[currentIndex]
                            if (source) viewer.ensureSubtitleAiTargetValid(source.code)
                            viewer.refreshSubtitleAiCacheState()
                        }
                        indicator: Text { x: subtitleAiLanguageCombo.width - width - 12; y: Math.round((subtitleAiLanguageCombo.height - height) / 2) - 1; text: "⌄"; color: videoTheme.viewerTextMuted; font.pixelSize: 15 }
                        contentItem: Text { leftPadding: 12; rightPadding: 32; text: subtitleAiLanguageCombo.displayText; color: videoTheme.viewerText; font.pixelSize: 11; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                        background: Rectangle { radius: 9; color: subtitleAiLanguageCombo.down ? videoTheme.viewerGlassPressed : videoTheme.viewerGlassSoft; border.width: 1; border.color: subtitleAiLanguageCombo.activeFocus ? videoTheme.accent : videoTheme.viewerGlassBorder }
                        popup: Popup {
                            palette: viewer.palette
                            y: subtitleAiLanguageCombo.height + 4; width: subtitleAiLanguageCombo.width; padding: 5
                            implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
                            background: GGlassPanel { sourceItem: videoScene; radius: 10; tintColor: videoTheme.viewerGlassStrong; borderColor: videoTheme.viewerGlassBorder; blurAmount: 0.64 }
                            contentItem: ListView { clip: true; implicitHeight: Math.min(contentHeight, 260); model: subtitleAiLanguageCombo.popup.visible ? subtitleAiLanguageCombo.delegateModel : null; currentIndex: subtitleAiLanguageCombo.highlightedIndex; ScrollIndicator.vertical: ScrollIndicator {} }
                        }
                        delegate: ItemDelegate {
                            required property var modelData
                            required property int index
                            width: subtitleAiLanguageCombo.width - 10; height: 38; hoverEnabled: true
                            contentItem: Text { text: modelData; color: videoTheme.viewerText; font.pixelSize: 10; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                            background: Rectangle { radius: 7; color: parent.down ? videoTheme.viewerGlassPressed : (parent.hovered ? videoTheme.viewerGlassHover : (subtitleAiLanguageCombo.currentIndex === index ? videoTheme.viewerGlassBorderSoft : "transparent")); border.width: subtitleAiLanguageCombo.currentIndex === index ? 1 : 0; border.color: videoTheme.accent }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: !subtitleAiManager.busy
                        text: {
                            const cached = viewer.subtitleAiCacheState || ({})
                            const sources = viewer.subtitleAiSourceOptions()
                            const source = sources.length ? sources[Math.max(0, Math.min(viewer.subtitleAiSourceIndex, sources.length - 1))] : ({ code: "und" })
                            const actual = String(cached.detectedLanguage || source.code || "und")
                            if (cached.sourceReady)
                                return viewer.lang.language === "tr" ? "✓ " + actual + " kaynak altyazı hazır · Whisper tekrar çalışmayacak" : "✓ " + actual + " source subtitles cached · Whisper will be skipped"
                            return viewer.lang.language === "tr" ? "Kaynak altyazı henüz hazır değil" : "Source subtitles are not cached yet"
                        }
                        color: viewer.subtitleAiCacheState && viewer.subtitleAiCacheState.sourceReady ? "#CFFFD8" : "#8FFFFFFF"
                        font.pixelSize: 9
                        wrapMode: Text.Wrap
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    visible: viewer.subtitleAiMode === "transcribe" && !subtitleAiManager.busy && !subtitleAiManager.outputPath.length && !subtitleAiManager.error.length
                    Text { text: viewer.lang.language === "tr" ? "Whisper modeli" : "Whisper model"; color: videoTheme.viewerTextMuted; font.pixelSize: 10; font.weight: Font.DemiBold }
                    GComboBox { colorTheme: videoTheme;
                        id: subtitleAiProfileCombo
                        Layout.fillWidth: true
                        model: viewer.subtitleAiProfiles.map(function(item) { return item.label })
                        currentIndex: viewer.subtitleAiProfileIndex
                        onActivated: { viewer.subtitleAiProfileIndex = currentIndex; viewer.refreshSubtitleAiCacheState() }
                        indicator: Text { x: subtitleAiProfileCombo.width - width - 12; y: Math.round((subtitleAiProfileCombo.height - height) / 2) - 1; text: "⌄"; color: videoTheme.viewerTextMuted; font.pixelSize: 15 }
                        contentItem: Text { leftPadding: 12; rightPadding: 32; text: subtitleAiProfileCombo.displayText; color: videoTheme.viewerText; font.pixelSize: 11; verticalAlignment: Text.AlignVCenter }
                        background: Rectangle { radius: 9; color: subtitleAiProfileCombo.down ? videoTheme.viewerGlassPressed : videoTheme.viewerGlassSoft; border.width: 1; border.color: subtitleAiProfileCombo.activeFocus ? videoTheme.accent : videoTheme.viewerGlassBorder }
                        popup: Popup {
                            palette: viewer.palette
                            y: subtitleAiProfileCombo.height + 4; width: subtitleAiProfileCombo.width; padding: 5
                            implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
                            background: GGlassPanel { sourceItem: videoScene; radius: 10; tintColor: videoTheme.viewerGlassStrong; borderColor: videoTheme.viewerGlassBorder; blurAmount: 0.64 }
                            contentItem: ListView { clip: true; implicitHeight: Math.min(contentHeight, 220); model: subtitleAiProfileCombo.popup.visible ? subtitleAiProfileCombo.delegateModel : null; currentIndex: subtitleAiProfileCombo.highlightedIndex }
                        }
                        delegate: ItemDelegate {
                            required property var modelData
                            required property int index
                            width: subtitleAiProfileCombo.width - 10; height: 38; hoverEnabled: true
                            contentItem: Text { text: modelData; color: videoTheme.viewerText; font.pixelSize: 10; verticalAlignment: Text.AlignVCenter }
                            background: Rectangle { radius: 7; color: parent.down ? videoTheme.viewerGlassPressed : (parent.hovered ? videoTheme.viewerGlassHover : (subtitleAiProfileCombo.currentIndex === index ? videoTheme.viewerGlassBorderSoft : "transparent")); border.width: subtitleAiProfileCombo.currentIndex === index ? 1 : 0; border.color: videoTheme.accent }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    visible: !subtitleAiManager.busy && !subtitleAiManager.outputPath.length && !subtitleAiManager.error.length
                    Text { text: viewer.lang.language === "tr" ? "AI çeviri dili" : "AI translation language"; color: videoTheme.viewerTextMuted; font.pixelSize: 10; font.weight: Font.DemiBold }
                    GComboBox { colorTheme: videoTheme;
                        id: subtitleAiTargetCombo
                        Layout.fillWidth: true
                        model: viewer.subtitleAiTargetOptions().map(function(item) { return item.code.length ? item.label + "  (" + item.code + ")" : item.label })
                        currentIndex: viewer.subtitleAiTargetIndex()
                        onActivated: { const options = viewer.subtitleAiTargetOptions(); viewer.subtitleAiTargetCode = options[currentIndex].code; viewer.refreshSubtitleAiCacheState() }
                        indicator: Text { x: subtitleAiTargetCombo.width - width - 12; y: Math.round((subtitleAiTargetCombo.height - height) / 2) - 1; text: "⌄"; color: videoTheme.viewerTextMuted; font.pixelSize: 15 }
                        contentItem: Text { leftPadding: 12; rightPadding: 32; text: subtitleAiTargetCombo.displayText; color: videoTheme.viewerText; font.pixelSize: 11; verticalAlignment: Text.AlignVCenter }
                        background: Rectangle { radius: 9; color: subtitleAiTargetCombo.down ? videoTheme.viewerGlassPressed : videoTheme.viewerGlassSoft; border.width: 1; border.color: subtitleAiTargetCombo.activeFocus ? videoTheme.accent : videoTheme.viewerGlassBorder }
                        popup: Popup {
                            palette: viewer.palette
                            y: subtitleAiTargetCombo.height + 4; width: subtitleAiTargetCombo.width; padding: 5
                            implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
                            background: GGlassPanel { sourceItem: videoScene; radius: 10; tintColor: videoTheme.viewerGlassStrong; borderColor: videoTheme.viewerGlassBorder; blurAmount: 0.64 }
                            contentItem: ListView { clip: true; implicitHeight: Math.min(contentHeight, 260); model: subtitleAiTargetCombo.popup.visible ? subtitleAiTargetCombo.delegateModel : null; currentIndex: subtitleAiTargetCombo.highlightedIndex; ScrollIndicator.vertical: ScrollIndicator {} }
                        }
                        delegate: ItemDelegate {
                            required property var modelData
                            required property int index
                            width: subtitleAiTargetCombo.width - 10; height: 38; hoverEnabled: true
                            contentItem: Text { text: modelData; color: videoTheme.viewerText; font.pixelSize: 10; verticalAlignment: Text.AlignVCenter }
                            background: Rectangle { radius: 7; color: parent.down ? videoTheme.viewerGlassPressed : (parent.hovered ? videoTheme.viewerGlassHover : (subtitleAiTargetCombo.currentIndex === index ? videoTheme.viewerGlassBorderSoft : "transparent")); border.width: subtitleAiTargetCombo.currentIndex === index ? 1 : 0; border.color: videoTheme.accent }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: viewer.subtitleAiTargetCode.length > 0 && viewer.subtitleAiCacheState && viewer.subtitleAiCacheState.sourceReady
                        text: {
                            const cached = viewer.subtitleAiCacheState || ({})
                            const label = viewer.subtitleAiTargetLabel(viewer.subtitleAiTargetCode)
                            if (cached.translationReady)
                                return viewer.lang.language === "tr" ? "✓ " + label + " çevirisi hazır" : "✓ " + label + " translation cached"
                            const progress = Number(cached.translationProgress || 0)
                            if (progress > 0) {
                                const units = Number(cached.translationTotalUnits || 0) > 0
                                              ? " · " + Number(cached.translationCompletedUnits || 0) + "/" + Number(cached.translationTotalUnits || 0)
                                              : ""
                                return viewer.lang.language === "tr" ? label + " %" + progress + units + " · Devam edilebilir" : label + " " + progress + "%" + units + " · Resumable"
                            }
                            return viewer.lang.language === "tr" ? label + " çevirisi henüz başlamadı" : label + " translation has not started yet"
                        }
                        color: viewer.subtitleAiCacheState && viewer.subtitleAiCacheState.translationReady ? "#CFFFD8" : (Number(viewer.subtitleAiCacheState ? viewer.subtitleAiCacheState.translationProgress : 0) > 0 ? "#FFE7A8" : "#8FFFFFFF")
                        font.pixelSize: 9
                        font.weight: Number(viewer.subtitleAiCacheState ? viewer.subtitleAiCacheState.translationProgress : 0) > 0 ? Font.DemiBold : Font.Normal
                        wrapMode: Text.Wrap
                    }
                    Text {
                        Layout.fillWidth: true
                        text: viewer.subtitleAiMode === "subtitle"
                              ? (viewer.lang.language === "tr"
                                 ? "Mevcut altyazı zaman kodları korunarak çevrilir; tamamlanan parçalar gerçek zamanlı olarak film üzerinde görünür."
                                 : "Existing subtitle timings are preserved; translated chunks appear on the video in real time.")
                              : (viewer.lang.language === "tr"
                                 ? "Whisper konuşmayı çözer; hedef dil seçiliyse çıkan altyazı yerel AI modeliyle çevrilir. und seslerde dil otomatik algılanır."
                                 : "Whisper transcribes speech; when a target is selected, the local AI model translates it. und audio uses automatic language detection.")
                        color: videoTheme.viewerTextMuted; font.pixelSize: 9; wrapMode: Text.Wrap
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    visible: subtitleAiManager.busy
                    Text {
                        Layout.fillWidth: true
                        text: subtitleAiManager.status.length ? subtitleAiManager.status : (viewer.lang.language === "tr" ? "Altyazı işleniyor…" : "Processing subtitles…")
                        color: videoTheme.viewerText
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                        wrapMode: Text.Wrap
                    }
                    GProgressBar { colorTheme: videoTheme;
                        Layout.fillWidth: true
                        from: 0
                        to: 100
                        value: subtitleAiManager.progress
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "%" + subtitleAiManager.progress
                        color: videoTheme.viewerTextMuted
                        font.pixelSize: 10
                        horizontalAlignment: Text.AlignRight
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    visible: !subtitleAiManager.busy && subtitleAiManager.outputPath.length > 0
                    Text {
                        Layout.fillWidth: true
                        text: viewer.lang.language === "tr" ? "✓ Altyazı hazır" : "✓ Subtitle ready"
                        color: "#D7FFE3"
                        font.pixelSize: 13
                        font.weight: Font.Bold
                    }
                    Text {
                        Layout.fillWidth: true
                        text: subtitleAiManager.outputPath
                        color: videoTheme.viewerTextMuted
                        font.pixelSize: 9
                        wrapMode: Text.WrapAnywhere
                    }
                    Text {
                        Layout.fillWidth: true
                        text: viewer.lang.language === "tr"
                              ? "Dosya videonun yanına yazıldı. G-File klasör görünümünde otomatik olarak görünecek."
                              : "The file was written next to the video and will appear in the G-File folder view."
                        color: videoTheme.viewerTextMuted
                        font.pixelSize: 9
                        wrapMode: Text.Wrap
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    visible: !subtitleAiManager.busy && subtitleAiManager.error.length > 0
                    Text {
                        Layout.fillWidth: true
                        text: viewer.lang.language === "tr" ? "Altyazı işlemi başarısız" : "Subtitle operation failed"
                        color: "#FFD3D3"
                        font.pixelSize: 13
                        font.weight: Font.Bold
                    }
                    Text {
                        Layout.fillWidth: true
                        text: subtitleAiManager.error
                        color: videoTheme.viewerTextMuted
                        font.pixelSize: 9
                        wrapMode: Text.Wrap
                    }
                }

                Item { Layout.fillHeight: true }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Item { Layout.fillWidth: true }
                    GButton { colorTheme: videoTheme;
                        visible: subtitleAiManager.busy
                        text: viewer.lang.language === "tr" ? "İptal" : "Cancel"
                        onClicked: subtitleAiManager.cancel()
                    }
                    GButton { colorTheme: videoTheme;
                        visible: !subtitleAiManager.busy && !subtitleAiManager.outputPath.length && !subtitleAiManager.error.length
                        text: viewer.lang.language === "tr" ? "Vazgeç" : "Cancel"
                        onClicked: { viewer.subtitleAiPanelMinimized = false; subtitleAiSubtitlePopup.close() }
                    }
                    GButton { colorTheme: videoTheme;
                        visible: !subtitleAiManager.busy && !subtitleAiManager.outputPath.length && !subtitleAiManager.error.length
                        text: viewer.subtitleAiActionLabel()
                        highlighted: true
                        onClicked: viewer.startSubtitleAi()
                    }
                    GButton { colorTheme: videoTheme;
                        visible: !subtitleAiManager.busy && (subtitleAiManager.outputPath.length > 0 || subtitleAiManager.error.length > 0)
                        text: viewer.lang.language === "tr" ? "Kapat" : "Close"
                        onClicked: { viewer.subtitleAiPanelMinimized = false; subtitleAiSubtitlePopup.close() }
                    }
                    GButton { colorTheme: videoTheme;
                        visible: !subtitleAiManager.busy && subtitleAiManager.error.length > 0
                        text: viewer.lang.language === "tr" ? "Tekrar dene" : "Try again"
                        highlighted: true
                        onClicked: {
                            subtitleAiManager.resetResult()
                            viewer.refreshSubtitleAiCacheState()
                        }
                    }
                }
            }
        }
    }

}
