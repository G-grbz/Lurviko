import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lurviko.App
import Lurviko.Backend

// Application-wide preferences. The View menu keeps only the settings that
// change the current folder view; everything that is set once lives here.
GModalPopup {
    id: dialog
    required property var lang
    readonly property bool turkish: lang.language === "tr"
    signal watermarkEditorRequested()

    objectName: "settingsDialog"
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(980, parent ? parent.width - 48 : 980)
    height: Math.min(740, parent ? parent.height - 48 : 740)
    padding: 0

    property string currentSection: "appearance"
    property string searchQuery: ""
    property bool noResults: false
    property int searchRevision: 0
    onSearchQueryChanged: applySearch()
    readonly property color panelColor: AppTheme.dark
        ? Qt.rgba(1, 1, 1, 0.035)
        : Qt.rgba(AppTheme.accent.r, AppTheme.accent.g, AppTheme.accent.b, 0.045)
    readonly property var sections: [
        {id: "appearance", tr: "Görünüm", en: "Appearance", trDesc: "Tema, sol menü, yazı tipleri ve ikonlar", enDesc: "Theme, sidebar, fonts and icons", icon: "details.svg"},
        {id: "behavior", tr: "Davranış", en: "Behavior", trDesc: "Tıklama ve kaydırma davranışı", enDesc: "Clicking and scrolling", icon: "behavior.svg"},
        {id: "previews", tr: "Dosya önizlemeleri", en: "File previews", trDesc: "Önizlenecek türler ve boyut sınırları", enDesc: "Preview types and size limits", icon: "preview.svg"},
        {id: "tabs", tr: "Sekmeler ve oturum", en: "Tabs and session", trDesc: "Sekme açma ve oturumu sürdürme", enDesc: "Opening tabs and restoring the session", icon: "tab.svg"},
        {id: "operations", tr: "İşlemler", en: "Operations", trDesc: "İlerleme ve medya açma", enDesc: "Progress and opening media", icon: "actions.svg"},
        {id: "confirmations", tr: "Onaylar", en: "Confirmations", trDesc: "Hangi işlemlerde onay istenecek", enDesc: "Which actions ask first", icon: "dialog.svg"},
        {id: "modules", tr: "Modüller", en: "Modules", trDesc: "İsteğe bağlı özellikler", enDesc: "Optional features", icon: "modules.svg"}
    ]
    readonly property var previewKinds: [
        {id: "images", tr: "Resimler", en: "Images",
         hint: "PNG, JPEG, WebP, GIF, HEIC, AVIF, JXL, TIFF, PSD, KRA, XCF, ORA, RAW…"},
        {id: "svg", tr: "Vektör çizimler", en: "Vector drawings", hint: "SVG, SVGZ"},
        {id: "video", tr: "Videolar", en: "Videos", hint: "MP4, MKV, WebM, AVI, MOV, WMV, FLV, M2TS…"},
        {id: "audio", tr: "Müzik kapakları", en: "Album art", hint: "MP3, FLAC, M4A, OGG, Opus, WAV, APE, WV…"},
        {id: "pdf", tr: "PDF belgeleri", en: "PDF documents", hint: "PDF"},
        {id: "print", tr: "PostScript ve DjVu", en: "PostScript and DjVu", hint: "PS, EPS, DjVu"},
        {id: "office", tr: "Ofis belgeleri", en: "Office documents",
         hint: "ODT, ODS, ODP, ODG, DOCX, XLSX, PPTX"},
        {id: "ebooks", tr: "E-kitaplar", en: "E-books", hint: "EPUB"},
        {id: "comics", tr: "Çizgi romanlar", en: "Comic books", hint: "CBZ"},
        {id: "fonts", tr: "Yazı tipleri", en: "Fonts", hint: "TTF, OTF, TTC, WOFF, WOFF2"},
        {id: "text", tr: "Metin ve kaynak kodu", en: "Text and source code",
         trHint: "İçerikten algılanır: .gitignore, README, Makefile ve uzantısız metin dosyaları dahil",
         enHint: "Detected from content, including .gitignore, README, Makefile and extension-less text files"},
        {id: "appimage", tr: "AppImage simgeleri", en: "AppImage icons", hint: "AppImage"}
    ]

    function t(tr, en) { return turkish ? tr : en }
    function sectionInfo(id) {
        for (let i = 0; i < sections.length; ++i)
            if (sections[i].id === id) return sections[i]
        return sections[0]
    }
    function sectionMatches(id) {
        const item = sectionItem(id)
        return !item || !item.searchHidden
    }
    function normalized(value) { return String(value || "").toLocaleLowerCase() }
    // Filters every row by its title, description and keywords. A matching
    // section title keeps the whole section visible.
    function applySearch() {
        const query = normalized(searchQuery).trim()
        let first = ""
        for (let i = 0; i < sectionColumn.children.length; ++i) {
            const section = sectionColumn.children[i]
            if (!section.sectionId) continue
            const sectionHit = query.length > 0
                && normalized(section.title + " " + section.subtitle).indexOf(query) >= 0
            let any = false
            const rows = section.rows
            for (let j = 0; j < rows.length; ++j) {
                const row = rows[j]
                if (row.searchHidden === undefined) continue
                const haystack = normalized((row.text || "") + " " + (row.description || "")
                                            + " " + (row.searchKeywords || ""))
                const hit = query.length === 0 || sectionHit
                        || (haystack.trim().length > 0 && haystack.indexOf(query) >= 0)
                row.searchHidden = !hit
                if (hit && row.shown !== false) any = true
            }
            section.searchHidden = query.length > 0 && !any && !sectionHit
            if (!section.searchHidden && !first) first = section.sectionId
        }
        noResults = query.length > 0 && first.length === 0
        searchRevision += 1
        sectionGlide.stop()
        settingsScroll.contentY = 0
        if (first.length) currentSection = first
    }
    function sectionItem(id) {
        for (let i = 0; i < sectionColumn.children.length; ++i) {
            const child = sectionColumn.children[i]
            if (child.sectionId === id) return child
        }
        return null
    }
    function showSection(id) {
        const item = sectionItem(id)
        if (!item) return
        currentSection = id
        const bottom = Math.max(0, settingsScroll.contentHeight - settingsScroll.height)
        sectionGlide.stop()
        sectionGlide.from = settingsScroll.contentY
        sectionGlide.to = Math.min(bottom, Math.max(0, item.y))
        sectionGlide.start()
    }
    function openSection(id) {
        open()
        Qt.callLater(function() { dialog.showSection(id) })
    }
    function syncCurrentSection() {
        if (sectionGlide.running) return
        let current = sections[0].id
        for (let i = 0; i < sectionColumn.children.length; ++i) {
            const child = sectionColumn.children[i]
            if (child.sectionId && child.y <= settingsScroll.contentY + 48) current = child.sectionId
        }
        if (settingsScroll.contentY >= settingsScroll.contentHeight - settingsScroll.height - 1)
            current = sections[sections.length - 1].id
        currentSection = current
    }
    function formatKiB(kib) {
        const value = Number(kib)
        if (!(value > 0)) return t("Sınırsız", "No limit")
        if (value < 1024) return value + " KiB"
        if (value < 1024 * 1024) return (value / 1024).toFixed(value % 1024 === 0 ? 0 : 1) + " MiB"
        return (value / (1024 * 1024)).toFixed(2) + " GiB"
    }
    function applyWheelSpeed() {
        const entered = Number(wheelSpeedInput.text)
        if (wheelSpeedInput.text.trim().length > 0 && Number.isFinite(entered))
            AppTheme.setWheelScrollStep(entered)
        wheelSpeedInput.text = String(AppTheme.wheelScrollStep)
    }

    onAboutToShow: {
        settingsSearch.text = ""
        settingsScroll.contentY = 0
        currentSection = sections[0].id
        wheelSpeedInput.text = String(AppTheme.wheelScrollStep)
    }

    NumberAnimation {
        id: sectionGlide
        target: settingsScroll
        property: "contentY"
        duration: 260
        easing.type: Easing.OutCubic
        onStopped: dialog.syncCurrentSection()
    }

    // ---- Reusable rows -----------------------------------------------------

    component SettingsSection: ColumnLayout {
        id: section
        property string sectionId
        property string title
        property string subtitle
        default property alias content: body.data
        readonly property alias rows: body.children
        property bool searchHidden: false
        visible: !searchHidden
        Layout.fillWidth: true
        spacing: 8
        GText {
            Layout.fillWidth: true
            Layout.topMargin: 6
            Layout.leftMargin: 6
            // Group label; the large title lives in the dialog header.
            text: section.title
            baseColor: AppTheme.textMuted
            basePixelSize: 11
            baseFontWeight: Font.DemiBold
            baseCapitalization: Font.AllUppercase
            baseLetterSpacing: 0.9
        }
        GText {
            Layout.fillWidth: true
            visible: section.subtitle.length > 0
            Layout.leftMargin: 4
            text: section.subtitle
            baseColor: AppTheme.textMuted
            basePixelSize: 11
            wrapMode: Text.WordWrap
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: body.implicitHeight + 12
            radius: 16
            color: AppTheme.dark ? Qt.rgba(1, 1, 1, 0.035) : Qt.rgba(1, 1, 1, 0.78)
            border.width: 1
            border.color: AppTheme.dark ? Qt.rgba(1, 1, 1, 0.07) : Qt.rgba(0.05, 0.08, 0.19, 0.08)
            ColumnLayout {
                id: body
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 6
                spacing: 0
            }
        }
    }

    // Behaves like the old checkable menu entries (text/checked/triggered())
    // and also exposes its switch for pointer interaction.
    component ToggleRow: Item {
        id: row
        property string text
        property string description
        property bool checked
        readonly property bool checkable: true
        property alias switchName: rowSwitch.objectName
        signal triggered()
        signal toggledTo(bool value)
        onTriggered: toggledTo(!checked)
        property bool shown: true
        property bool searchHidden: false
        property string searchKeywords
        visible: shown && !searchHidden
        Layout.fillWidth: true
        implicitHeight: Math.max(46, labels.implicitHeight + 18)
        opacity: enabled ? 1 : 0.5
        Rectangle {
            anchors.fill: parent
            radius: 10
            color: rowMouse.containsMouse && row.enabled ? AppTheme.surfaceHover : "transparent"
        }
        MouseArea {
            id: rowMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: row.triggered()
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 10
            spacing: 12
            ColumnLayout {
                id: labels
                Layout.fillWidth: true
                spacing: 2
                GText {
                    Layout.fillWidth: true
                    text: row.text
                    baseColor: AppTheme.text
                    basePixelSize: 13
                    wrapMode: Text.WordWrap
                }
                GText {
                    Layout.fillWidth: true
                    visible: row.description.length > 0
                    text: row.description
                    baseColor: AppTheme.textMuted
                    basePixelSize: 11
                    wrapMode: Text.WordWrap
                }
            }
            GSwitch {
                id: rowSwitch
                checked: row.checked
                onToggled: {
                    row.toggledTo(checked)
                    checked = Qt.binding(function() { return row.checked })
                }
            }
        }
    }

    component ActionRow: Item {
        id: action
        property string text
        property string description
        signal triggered()
        property bool shown: true
        property bool searchHidden: false
        property string searchKeywords
        visible: shown && !searchHidden
        Layout.fillWidth: true
        implicitHeight: Math.max(46, actionLabels.implicitHeight + 18)
        Rectangle {
            anchors.fill: parent
            radius: 10
            color: actionMouse.containsMouse ? AppTheme.surfaceHover : "transparent"
        }
        MouseArea {
            id: actionMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: action.triggered()
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 14
            spacing: 12
            ColumnLayout {
                id: actionLabels
                Layout.fillWidth: true
                spacing: 2
                GText { Layout.fillWidth: true; text: action.text; baseColor: AppTheme.text; basePixelSize: 13; wrapMode: Text.WordWrap }
                GText {
                    Layout.fillWidth: true
                    visible: action.description.length > 0
                    text: action.description
                    baseColor: AppTheme.textMuted
                    basePixelSize: 11
                    wrapMode: Text.WordWrap
                }
            }
            GText { text: "›"; baseColor: AppTheme.textMuted; basePixelSize: 20 }
        }
    }

    component SizeLimitRow: Item {
        id: limitRow
        property string text
        property string description
        property int value
        property string summary
        property alias fieldName: limitField.objectName
        signal committed(int kib)
        property bool shown: true
        property bool searchHidden: false
        property string searchKeywords
        visible: shown && !searchHidden
        Layout.fillWidth: true
        implicitHeight: Math.max(56, limitLabels.implicitHeight + 18)
        opacity: enabled ? 1 : 0.5
        function commit() {
            const entered = Number(limitField.text)
            if (limitField.text.trim().length === 0)
                limitRow.committed(0)
            else if (Number.isFinite(entered) && entered >= 0)
                limitRow.committed(Math.round(entered))
            limitField.text = Qt.binding(function() { return String(limitRow.value) })
        }
        // Arrow steps grow while held: 1 MiB, then 10 MiB, then 100 MiB.
        // Only the field text changes during a hold; the value is saved once
        // on release, as every save refreshes all visible previews.
        property int stepDirection: 0
        property int stepCount: 0
        function step(direction) {
            const current = Math.max(0, Math.round(Number(limitField.text) || 0))
            const size = stepCount < 10 ? 1024 : stepCount < 30 ? 10240 : 102400
            const next = direction > 0 ? Math.floor(current / size) * size + size
                                       : Math.ceil(current / size) * size - size
            limitField.text = String(Math.max(0, Math.min(2147483647, next)))
            ++stepCount
        }
        function beginStep(direction) {
            stepDirection = direction
            stepCount = 0
            step(direction)
            stepRepeat.interval = 400
            stepRepeat.restart()
        }
        function endStep() {
            if (stepDirection === 0)
                return
            stepDirection = 0
            stepRepeat.stop()
            commit()
        }
        Timer {
            id: stepRepeat
            repeat: true
            onTriggered: {
                interval = 70
                limitRow.step(limitRow.stepDirection)
            }
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 10
            spacing: 10
            ColumnLayout {
                id: limitLabels
                Layout.fillWidth: true
                spacing: 2
                GText { Layout.fillWidth: true; text: limitRow.text; baseColor: AppTheme.text; basePixelSize: 13; wrapMode: Text.WordWrap }
                GText {
                    Layout.fillWidth: true
                    text: limitRow.description
                    baseColor: AppTheme.textMuted
                    basePixelSize: 11
                    wrapMode: Text.WordWrap
                }
            }
            GTextField {
                id: limitField
                Layout.preferredWidth: 110
                Layout.preferredHeight: 34
                horizontalAlignment: Text.AlignRight
                inputMethodHints: Qt.ImhDigitsOnly
                validator: IntValidator { bottom: 0; top: 2147483647 }
                text: String(limitRow.value)
                Accessible.name: limitRow.text
                onAccepted: limitRow.commit()
                onEditingFinished: limitRow.commit()
                Keys.onUpPressed: function(event) { limitRow.stepCount = 0; limitRow.step(1); limitRow.commit() }
                Keys.onDownPressed: function(event) { limitRow.stepCount = 0; limitRow.step(-1); limitRow.commit() }
            }
            ColumnLayout {
                Layout.preferredHeight: 34
                spacing: 2
                Repeater {
                    model: [1, -1]
                    delegate: GButton {
                        required property int modelData
                        objectName: limitField.objectName + (modelData > 0 ? "Up" : "Down")
                        Layout.preferredWidth: 22
                        Layout.preferredHeight: 16
                        implicitWidth: 22; implicitHeight: 16
                        // GButton gives icon-only buttons 8 px padding on every
                        // side, which left no room for the icon at 22x16.
                        padding: 0
                        leftPadding: 0; rightPadding: 0; topPadding: 0; bottomPadding: 0
                        focusPolicy: Qt.NoFocus
                        text: ""
                        display: AbstractButton.IconOnly
                        icon.source: AppTheme.icon(modelData > 0 ? "up.svg" : "down.svg")
                        icon.width: 12; icon.height: 12
                        enabled: limitRow.enabled && (modelData > 0 || Number(limitField.text) > 0)
                        Accessible.name: modelData > 0 ? dialog.t("Artır", "Increase") : dialog.t("Azalt", "Decrease")
                        onPressedChanged: {
                            if (pressed)
                                limitRow.beginStep(modelData)
                            else
                                limitRow.endStep()
                        }
                        // Disabled at 0 mid-hold: still save what was reached.
                        onEnabledChanged: if (!enabled) limitRow.endStep()
                        background: Rectangle {
                            radius: 5
                            color: parent.pressed ? AppTheme.surfaceHover
                                 : parent.hovered && parent.enabled ? AppTheme.surfaceRaised : "transparent"
                            border.color: parent.enabled ? AppTheme.border : "transparent"
                        }
                    }
                }
            }
            GText { text: "KiB"; baseColor: AppTheme.textMuted; basePixelSize: 12 }
            GText {
                Layout.preferredWidth: 76
                text: limitRow.summary
                baseColor: AppTheme.accent
                basePixelSize: 11
                elide: Text.ElideRight
            }
        }
    }

    // ---- Layout ------------------------------------------------------------

    contentItem: Item {
        id: frame

        // Full-height navigation panel, flush with the rounded modal frame.
        Item {
            id: sidePanel
            objectName: "settingsSidePanel"
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: Math.min(256, Math.max(200, frame.width * 0.28))
            // One translucent shape: rounded on the left, square on the right.
            // Two overlapping rectangles would double the tint along the seam.
            Item {
                anchors.fill: parent
                clip: true
                Rectangle {
                    x: 0
                    width: parent.width + 30
                    height: parent.height
                    radius: 26
                    color: dialog.panelColor
                }
            }
            Rectangle {
                anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.right: parent.right
                width: 1
                color: AppTheme.dark ? Qt.rgba(1, 1, 1, 0.07) : Qt.rgba(0.05, 0.08, 0.19, 0.08)
            }

            ColumnLayout {
                objectName: "settingsSectionList"
                anchors.fill: parent
                anchors.margins: 16
                anchors.topMargin: 22
                spacing: 3

                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 4
                    spacing: 12
                    Rectangle {
                        Layout.preferredWidth: 40; Layout.preferredHeight: 40
                        radius: 13
                        border.width: 1
                        border.color: AppTheme.accentBorder
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: AppTheme.accentSoft }
                            GradientStop { position: 1.0; color: Qt.rgba(AppTheme.accent.r, AppTheme.accent.g, AppTheme.accent.b, AppTheme.dark ? 0.10 : 0.05) }
                        }
                        CrispIcon { anchors.centerIn: parent; width: 21; height: 21; source: AppTheme.icon("settings.svg", AppTheme.accent) }
                    }
                    GText {
                        Layout.fillWidth: true
                        text: dialog.t("Ayarlar", "Settings")
                        baseColor: AppTheme.text
                        basePixelSize: 19
                        baseFontWeight: Font.DemiBold
                    }
                }

                GTextField {
                    id: settingsSearch
                    objectName: "settingsSearchField"
                    Layout.fillWidth: true
                    Layout.topMargin: 16
                    Layout.bottomMargin: 10
                    Layout.preferredHeight: 36
                    placeholderText: dialog.t("Ayar ara…", "Search settings…")
                    onTextChanged: dialog.searchQuery = text
                    Keys.onEscapePressed: function(event) {
                        if (text.length) { text = ""; event.accepted = true }
                        else event.accepted = false
                    }
                }

                Repeater {
                    model: dialog.sections
                    delegate: Rectangle {
                        id: navEntry
                        required property var modelData
                        readonly property bool current: dialog.currentSection === modelData.id
                        readonly property bool matches: dialog.searchRevision >= 0 && dialog.sectionMatches(modelData.id)
                        objectName: "settingsNav_" + modelData.id
                        Layout.fillWidth: true
                        implicitHeight: 38
                        radius: 11
                        opacity: matches ? 1 : 0.38
                        color: current ? (AppTheme.dark ? Qt.rgba(1, 1, 1, 0.07) : Qt.rgba(1, 1, 1, 0.9))
                                       : (navMouse.containsMouse ? AppTheme.surfaceHover : "transparent")
                        border.width: current ? 1 : 0
                        border.color: AppTheme.dark ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(0.05, 0.08, 0.19, 0.08)
                        // No color animation: interpolating to/from "transparent"
                        // (transparent black) flashed gray on hover and on the
                        // previously selected entry.
                        Rectangle {
                            visible: navEntry.current
                            anchors.left: parent.left
                            anchors.leftMargin: 4
                            anchors.verticalCenter: parent.verticalCenter
                            width: 3; height: 16; radius: 2
                            color: AppTheme.accent
                        }
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 14
                            anchors.rightMargin: 10
                            spacing: 10
                            CrispIcon {
                                Layout.preferredWidth: 18; Layout.preferredHeight: 18
                                source: AppTheme.icon(navEntry.modelData.icon, navEntry.current ? AppTheme.accent : AppTheme.textMuted)
                            }
                            GText {
                                Layout.fillWidth: true
                                text: dialog.turkish ? navEntry.modelData.tr : navEntry.modelData.en
                                baseColor: navEntry.current ? AppTheme.text : AppTheme.textMuted
                                baseFontWeight: navEntry.current ? Font.DemiBold : Font.Normal
                                basePixelSize: 13
                                elide: Text.ElideRight
                            }
                        }
                        MouseArea {
                            id: navMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            enabled: navEntry.matches
                            onClicked: dialog.showSection(navEntry.modelData.id)
                        }
                    }
                }
                Item { Layout.fillHeight: true }
                GText {
                    Layout.fillWidth: true
                    Layout.leftMargin: 4
                    text: dialog.t("Değişiklikler anında uygulanır ve kaydedilir.", "Changes apply and save immediately.")
                    baseColor: AppTheme.textFaint
                    basePixelSize: 10
                    wrapMode: Text.WordWrap
                }
            }
        }

        ColumnLayout {
            anchors.left: sidePanel.right
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.leftMargin: 26
            anchors.rightMargin: 18
            anchors.topMargin: 22
            anchors.bottomMargin: 18
            spacing: 14

            GModalHeader {
                title: {
                    const info = dialog.sectionInfo(dialog.currentSection)
                    return dialog.turkish ? info.tr : info.en
                }
                subtitle: {
                    const info = dialog.sectionInfo(dialog.currentSection)
                    return dialog.turkish ? info.trDesc : info.enDesc
                }
                closeButtonName: "closeSettingsButton"
                closeToolTip: dialog.t("Kapat", "Close")
                onCloseRequested: dialog.close()
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                Flickable {
                    id: settingsScroll
                    objectName: "settingsScroll"
                    anchors.fill: parent
                    anchors.rightMargin: 14
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    contentWidth: width
                    contentHeight: sectionColumn.implicitHeight + 24
                    onContentYChanged: dialog.syncCurrentSection()

                    GPopupWheelScroll {
                        parent: settingsScroll
                        anchors.fill: parent
                        view: settingsScroll
                        scrollbar: settingsScrollBar
                        enabled: dialog.visible
                    }

                    ColumnLayout {
                        id: sectionColumn
                        width: settingsScroll.width
                        spacing: 22

                        // -- Appearance --
                        SettingsSection {
                            sectionId: "appearance"
                            title: dialog.t("Görünüm", "Appearance")
                            ToggleRow {
                                objectName: "sidebarAutoHideSetting"
                                switchName: "sidebarAutoHideSwitch"
                                text: dialog.t("Kompakt sol menü", "Compact sidebar")
                                description: dialog.t("Sol menü dar bir şeride dönüşür ve üzerine gelince açılır.",
                                                      "The sidebar collapses to a rail and expands on hover.")
                                checked: AppTheme.sidebarAutoHide
                                onToggledTo: function(value) { AppTheme.setSidebarAutoHide(value) }
                            }
                            Item {
                                objectName: "sidebarRailModeSetting"
                                property bool searchHidden: false
                                readonly property bool shown: AppTheme.sidebarAutoHide
                                readonly property string searchKeywords: dialog.t("Kompakt şerit içeriği başlık ikon", "Compact rail content title icon")
                                visible: shown && !searchHidden
                                Layout.fillWidth: true
                                implicitHeight: visible ? 52 : 0
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 12
                                    anchors.rightMargin: 10
                                    GText {
                                        Layout.fillWidth: true
                                        text: dialog.t("Kompakt şerit içeriği", "Compact rail content")
                                        baseColor: AppTheme.text
                                        basePixelSize: 13
                                    }
                                    GComboBox {
                                        objectName: "sidebarRailModePicker"
                                        Layout.preferredWidth: 200
                                        model: dialog.turkish ? ["Başlık ve ikon", "Yalnız başlık", "Yalnız ikon"]
                                                              : ["Title and icon", "Title only", "Icon only"]
                                        currentIndex: ["both", "title", "icon"].indexOf(AppTheme.sidebarRailMode)
                                        onActivated: function(index) { AppTheme.setSidebarRailMode(["both", "title", "icon"][index]) }
                                    }
                                }
                            }
                            ToggleRow {
                                objectName: "qtThemeMenuItem"
                                text: dialog.t("Qt temasını kullan", "Use Qt theme")
                                description: dialog.t("Renkleri ve menü yüzeylerini masaüstü temasından alır.",
                                                      "Takes colors and menu surfaces from the desktop theme.")
                                checked: AppTheme.useQtTheme
                                onToggledTo: function(value) { AppTheme.setQtTheme(value) }
                            }
                            ActionRow {
                                objectName: "typographyMenuItem"
                                text: dialog.t("Yazı tipleri ve renkler…", "Fonts and colors…")
                                description: dialog.t("Başlık, gövde, menü ve sol menü yazılarını düzenle.",
                                                      "Edit heading, body, menu and sidebar text styles.")
                                onTriggered: typographyDialogLoader.active = true
                            }
                            ActionRow {
                                objectName: "exploreWatermarkViewSetting"
                                text: dialog.t("Keşfet filigranını düzenle…", "Edit Discover watermark…")
                                onTriggered: {
                                    dialog.close()
                                    dialog.watermarkEditorRequested()
                                }
                            }
                            Item {
                                objectName: "iconThemeSetting"
                                property bool searchHidden: false
                                readonly property string searchKeywords: dialog.t("Dosya ve klasör ikonları sistem simge", "File and folder icons system")
                                visible: !searchHidden
                                Layout.fillWidth: true
                                implicitHeight: 52
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 12
                                    anchors.rightMargin: 10
                                    GText {
                                        Layout.fillWidth: true
                                        text: dialog.t("Dosya ve klasör ikonları", "File and folder icons")
                                        baseColor: AppTheme.text
                                        basePixelSize: 13
                                    }
                                    GComboBox {
                                        objectName: "iconThemePicker"
                                        Layout.preferredWidth: 200
                                        model: [dialog.lang.t("default_icons"), dialog.lang.t("system_icons")]
                                        currentIndex: AppTheme.useSystemIcons ? 1 : 0
                                        onActivated: function(index) { AppTheme.setSystemIcons(index === 1) }
                                    }
                                }
                            }
                            ToggleRow {
                                objectName: "coloredFolderIconsSetting"
                                switchName: "coloredFolderIconsSwitch"
                                shown: !AppTheme.useSystemIcons
                                text: dialog.lang.t("colored_folder_icons")
                                checked: AppTheme.useColoredFolderIcons
                                onToggledTo: function(value) { AppTheme.setColoredFolderIcons(value) }
                            }
                        }

                        // -- Behavior --
                        SettingsSection {
                            sectionId: "behavior"
                            title: dialog.t("Davranış", "Behavior")
                            ToggleRow {
                                objectName: "singleClickOpenSetting"
                                text: dialog.t("Tek tıkla aç", "Open with single click")
                                description: dialog.t("Dosya ve klasörler çift tık yerine tek tıkla açılır.",
                                                      "Files and folders open with a single click instead of a double click.")
                                checked: AppTheme.singleClickOpen
                                onToggledTo: function(value) { AppTheme.setSingleClickOpen(value) }
                            }
                            Item {
                                objectName: "wheelSpeedSetting"
                                property bool searchHidden: false
                                readonly property string searchKeywords: dialog.t("Tekerlek hızı kaydırma scroll", "Wheel speed scrolling scroll")
                                visible: !searchHidden
                                Layout.fillWidth: true
                                implicitHeight: 84
                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 12
                                    anchors.rightMargin: 10
                                    anchors.topMargin: 8
                                    anchors.bottomMargin: 8
                                    spacing: 4
                                    RowLayout {
                                        Layout.fillWidth: true
                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 2
                                            GText { Layout.fillWidth: true; text: dialog.lang.t("wheel_speed"); baseColor: AppTheme.text; basePixelSize: 13 }
                                            GText {
                                                Layout.fillWidth: true
                                                text: dialog.t("Bir tekerlek adımında kaydırılan piksel. Tüm listeler ve menüler bunu kullanır.",
                                                               "Pixels scrolled per wheel notch. Used by every list and menu.")
                                                baseColor: AppTheme.textMuted; basePixelSize: 11; wrapMode: Text.WordWrap
                                            }
                                        }
                                        GTextField {
                                            id: wheelSpeedInput
                                            objectName: "settingsWheelSpeedField"
                                            Layout.preferredWidth: 72
                                            Layout.preferredHeight: 32
                                            horizontalAlignment: TextInput.AlignHCenter
                                            inputMethodHints: Qt.ImhDigitsOnly
                                            validator: IntValidator { bottom: 10; top: 1000 }
                                            onEditingFinished: dialog.applyWheelSpeed()
                                        }
                                    }
                                    GSlider {
                                        objectName: "settingsWheelSpeedSlider"
                                        Layout.fillWidth: true
                                        from: 10; to: 1000; stepSize: 1
                                        value: AppTheme.wheelScrollStep
                                        Accessible.name: dialog.lang.t("wheel_speed")
                                        onMoved: {
                                            AppTheme.setWheelScrollStep(value)
                                            wheelSpeedInput.text = String(AppTheme.wheelScrollStep)
                                        }
                                    }
                                }
                            }
                        }

                        // -- File previews --
                        SettingsSection {
                            sectionId: "previews"
                            title: dialog.t("Dosya önizlemeleri", "File previews")
                            subtitle: dialog.t("Dolphin'deki gibi: türleri tek tek aç/kapat ve büyük dosyalarda önizlemeyi atlayarak sistemi yorma. 0 KiB sınırsız demektir.",
                                               "Like Dolphin: toggle each type and skip large files so previews never load the system. 0 KiB means no limit.")
                            ToggleRow {
                                objectName: "filePreviewsSetting"
                                switchName: "filePreviewsSwitch"
                                text: dialog.t("Dosya önizlemelerini göster", "Show file previews")
                                description: dialog.t("Kapalıyken dosyalar yalnız ikonlarıyla gösterilir.",
                                                      "When off, files are shown with their icons only.")
                                checked: PreviewSettings.enabled
                                onToggledTo: function(value) { PreviewSettings.enabled = value }
                            }
                            SizeLimitRow {
                                objectName: "systemPreviewLimitSetting"
                                fieldName: "systemPreviewLimitField"
                                enabled: PreviewSettings.enabled
                                text: dialog.t("Sistem diski (/) için en büyük boyut", "Largest file on the system disk (/)")
                                description: dialog.t("/ ve /home gibi iç sistem bölümlerindeki dosyalar.",
                                                      "Files on / and internal system mounts such as /home.")
                                value: PreviewSettings.systemMaxKiB
                                summary: "= " + dialog.formatKiB(PreviewSettings.systemMaxKiB)
                                onCommitted: function(kib) { PreviewSettings.systemMaxKiB = kib }
                            }
                            SizeLimitRow {
                                objectName: "externalPreviewLimitSetting"
                                fieldName: "externalPreviewLimitField"
                                enabled: PreviewSettings.enabled
                                text: dialog.t("Diğer diskler için en büyük boyut", "Largest file on other disks")
                                description: dialog.t("USB bellekler, harici diskler, /run/media, /mnt ve ağ bağlantıları.",
                                                      "USB sticks, external disks, /run/media, /mnt and network mounts.")
                                value: PreviewSettings.externalMaxKiB
                                summary: "= " + dialog.formatKiB(PreviewSettings.externalMaxKiB)
                                onCommitted: function(kib) { PreviewSettings.externalMaxKiB = kib }
                            }
                            Rectangle {
                                property bool searchHidden: false
                                visible: !searchHidden
                                Layout.fillWidth: true
                                Layout.leftMargin: 12
                                Layout.rightMargin: 12
                                Layout.topMargin: 4
                                Layout.bottomMargin: 4
                                implicitHeight: 1
                                color: AppTheme.border
                            }
                            Repeater {
                                model: dialog.previewKinds
                                delegate: ToggleRow {
                                    required property var modelData
                                    objectName: "previewKind_" + modelData.id
                                    switchName: "previewKindSwitch_" + modelData.id
                                    enabled: PreviewSettings.enabled
                                    text: dialog.turkish ? modelData.tr : modelData.en
                                    description: modelData.hint || (dialog.turkish ? modelData.trHint : modelData.enHint)
                                    checked: PreviewSettings.revision >= 0 && PreviewSettings.kindEnabled(modelData.id)
                                    onToggledTo: function(value) { PreviewSettings.setKindEnabled(modelData.id, value) }
                                }
                            }
                        }

                        // -- Tabs and session --
                        SettingsSection {
                            sectionId: "tabs"
                            title: dialog.t("Sekmeler ve oturum", "Tabs and session")
                            ToggleRow {
                                objectName: "activateNewTabsSetting"
                                text: dialog.t("Yeni sekmeye otomatik geç", "Switch to newly opened tab")
                                checked: BrowserSession.activateNewTabs
                                onToggledTo: function(value) { BrowserSession.setActivateNewTabs(value) }
                            }
                            ToggleRow {
                                objectName: "resumeSessionMenuItem"
                                text: dialog.lang.t("resume_previous_session")
                                checked: BrowserSession.resumeSession
                                onToggledTo: function(value) { BrowserSession.setResumeSession(value) }
                            }
                            ToggleRow {
                                objectName: "warnOnMultipleTabsMenuItem"
                                text: dialog.lang.t("warn_closing_multiple_tabs")
                                checked: BrowserSession.warnOnMultipleTabs
                                onToggledTo: function(value) { BrowserSession.setWarnOnMultipleTabs(value) }
                            }
                        }

                        // -- Operations --
                        SettingsSection {
                            sectionId: "operations"
                            title: dialog.t("İşlemler", "Operations")
                            ToggleRow {
                                objectName: "kioProgressSetting"
                                text: dialog.lang.t("kio_progress")
                                checked: AppTheme.kioProgressEnabled
                                onToggledTo: function(value) { AppTheme.setKioProgressEnabled(value) }
                            }
                            Item {
                                objectName: "operationAutoHideSettings"
                                property bool searchHidden: false
                                readonly property bool shown: !AppTheme.kioProgressEnabled
                                readonly property string searchKeywords: dialog.t("İşlem otomatik gizle saniye tamamlanınca", "Operation auto hide seconds completion")
                                visible: shown && !searchHidden
                                Layout.fillWidth: true
                                implicitHeight: visible ? autoHideColumn.implicitHeight + 16 : 0
                                ColumnLayout {
                                    id: autoHideColumn
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    anchors.margins: 8
                                    anchors.leftMargin: 12
                                    anchors.rightMargin: 10
                                    spacing: 8
                                    GSwitch {
                                        objectName: "operationAutoHideSwitch"
                                        Layout.fillWidth: true
                                        text: dialog.lang.t("operation_auto_hide")
                                        checked: AppTheme.operationAutoHide
                                        onToggled: AppTheme.setOperationAutoHide(checked)
                                    }
                                    RowLayout {
                                        visible: AppTheme.operationAutoHide
                                        GText {
                                            Layout.fillWidth: true
                                            text: dialog.lang.t("operation_hide_seconds")
                                            baseColor: AppTheme.textMuted
                                            basePixelSize: 12
                                        }
                                        GTextField {
                                            objectName: "operationAutoHideDelay"
                                            Layout.preferredWidth: 90
                                            Layout.minimumWidth: 70
                                            Layout.preferredHeight: 34
                                            horizontalAlignment: Text.AlignHCenter
                                            inputMethodHints: Qt.ImhDigitsOnly
                                            validator: IntValidator { bottom: 1; top: 300 }
                                            text: String(AppTheme.operationAutoHideSeconds)
                                            Accessible.name: dialog.lang.t("operation_hide_seconds")
                                            function saveDelay() {
                                                if (acceptableInput) AppTheme.setOperationAutoHideSeconds(Number(text))
                                                text = Qt.binding(function() { return String(AppTheme.operationAutoHideSeconds) })
                                            }
                                            onAccepted: saveDelay()
                                            onEditingFinished: saveDelay()
                                        }
                                    }
                                    GSwitch {
                                        objectName: "operationShowOnCompletionSwitch"
                                        visible: AppTheme.operationAutoHide
                                        Layout.fillWidth: true
                                        text: dialog.lang.t("operation_show_on_completion")
                                        checked: AppTheme.operationShowOnCompletion
                                        onToggled: AppTheme.setOperationShowOnCompletion(checked)
                                    }
                                }
                            }
                            ToggleRow {
                                objectName: "openImagesInternallySetting"
                                switchName: "openImagesInternallySwitch"
                                text: dialog.t("Resimleri Lurviko görüntüleyicide aç", "Open images in Lurviko's viewer")
                                description: dialog.t("Klasördeki diğer resimlerle birlikte açılır. Kapalıyken varsayılan uygulama kullanılır.",
                                                      "Opens with the folder's other images. When off, the default application is used.")
                                checked: AppTheme.openImagesInternally
                                onToggledTo: function(value) { AppTheme.setOpenImagesInternally(value) }
                            }
                            ToggleRow {
                                objectName: "openVideosInternallySetting"
                                switchName: "openVideosInternallySwitch"
                                text: dialog.t("Videoları Lurviko oynatıcıda aç", "Play videos in Lurviko's player")
                                description: dialog.t("Altyazı, ses kanalı ve kaldığı yerden devam desteğiyle. Kapalıyken varsayılan uygulama kullanılır.",
                                                      "With subtitles, audio tracks and resume. When off, the default application is used.")
                                checked: AppTheme.openVideosInternally
                                onToggledTo: function(value) { AppTheme.setOpenVideosInternally(value) }
                            }
                            ToggleRow {
                                objectName: "openMusicInternallySetting"
                                switchName: "openMusicInternallySwitch"
                                text: dialog.t("Müzikleri Lurviko mini oynatıcıda çal", "Play music in Lurviko's mini player")
                                description: dialog.t("Klasördeki diğer şarkılar sıraya eklenir. Kapalıyken varsayılan uygulama kullanılır.",
                                                      "The folder's other songs are queued. When off, the default application is used.")
                                checked: AppTheme.openMusicInternally
                                onToggledTo: function(value) { AppTheme.setOpenMusicInternally(value) }
                            }
                            Item {
                                objectName: "clipboardHistoryLimitSetting"
                                property bool searchHidden: false
                                property string searchKeywords: "pano clipboard geçmiş history kopyala copy kes cut"
                                visible: !searchHidden
                                Layout.fillWidth: true
                                implicitHeight: Math.max(56, historyLabels.implicitHeight + 18)
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 12
                                    anchors.rightMargin: 10
                                    spacing: 12
                                    ColumnLayout {
                                        id: historyLabels
                                        Layout.fillWidth: true
                                        spacing: 2
                                        GText {
                                            Layout.fillWidth: true
                                            text: dialog.t("Pano geçmişinde tutulacak kayıt", "Clipboard history entries")
                                            baseColor: AppTheme.text
                                            basePixelSize: 13
                                            wrapMode: Text.WordWrap
                                        }
                                        GText {
                                            Layout.fillWidth: true
                                            text: dialog.t("Her kopyala/kes tek kayıttır. Silinen dosyalar kayıttan çıkarılır.",
                                                           "Each Copy/Cut is one entry. Deleted files drop out.")
                                            baseColor: AppTheme.textMuted
                                            basePixelSize: 11
                                            wrapMode: Text.WordWrap
                                        }
                                    }
                                    GSpinBox {
                                        objectName: "clipboardHistoryLimitField"
                                        from: 5
                                        to: 100
                                        stepSize: 5
                                        editable: true
                                        value: ClipboardHistory.maxEntries
                                        onValueModified: ClipboardHistory.maxEntries = value
                                        Layout.preferredWidth: 112
                                        Accessible.name: dialog.t("Pano geçmişinde tutulacak kayıt", "Clipboard history entries")
                                    }
                                }
                            }
                            ToggleRow {
                                objectName: "clipboardHistoryClearOnExitSetting"
                                switchName: "clipboardHistoryClearOnExitSwitch"
                                searchKeywords: "pano clipboard geçmiş history"
                                text: dialog.t("Lurviko kapanınca pano geçmişini sil", "Clear clipboard history when Lurviko exits")
                                description: dialog.t("Kapalıyken geçmiş, kopyalanan dosyaların yollarıyla birlikte diske kaydedilir ve sonraki açılışta geri gelir.",
                                                      "When off, the history (with the paths of copied files) is saved to disk and restored next time.")
                                checked: ClipboardHistory.clearOnExit
                                onToggledTo: function(value) { ClipboardHistory.clearOnExit = value }
                            }
                        }

                        // -- Confirmations --
                        SettingsSection {
                            sectionId: "confirmations"
                            title: dialog.t("Onaylar", "Confirmations")
                            ToggleRow {
                                objectName: "skipHiddenNameConfirmationItem"
                                text: dialog.t("Gizli ad onayını atla", "Skip hidden name confirmation")
                                description: dialog.t("Nokta ile başlayan adlar için soru sorulmaz.",
                                                      "No prompt for names starting with a dot.")
                                checked: AppTheme.skipHiddenNameConfirmation
                                onToggledTo: function(value) { AppTheme.setSkipHiddenNameConfirmation(value) }
                            }
                            ToggleRow {
                                objectName: "skipPermanentDeleteConfirmationItem"
                                text: dialog.t("Kalıcı silme onayını atla", "Skip permanent delete confirmation")
                                description: dialog.t("Dikkat: dosyalar sorulmadan kalıcı olarak silinir.",
                                                      "Careful: files are deleted permanently without asking.")
                                checked: AppTheme.skipPermanentDeleteConfirmation
                                onToggledTo: function(value) { AppTheme.setSkipPermanentDeleteConfirmation(value) }
                            }
                        }

                        // -- Modules --
                        SettingsSection {
                            sectionId: "modules"
                            title: dialog.t("Modüller", "Modules")
                            ToggleRow {
                                objectName: "gitModuleViewSetting"
                                switchName: "gitModuleViewSwitch"
                                text: dialog.t("Git modülünü etkinleştir", "Enable Git module")
                                description: dialog.t("Depolarda durum rozetleri ve Git işlemleri gösterilir.",
                                                      "Shows status badges and Git actions inside repositories.")
                                checked: AppTheme.gitEnabled
                                onToggledTo: function(value) { AppTheme.setGitEnabled(value) }
                            }
                        }
                    }
                }

                ScrollBar {
                    id: settingsScrollBar
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 7
                    orientation: Qt.Vertical
                    size: settingsScroll.visibleArea.heightRatio
                    position: settingsScroll.visibleArea.yPosition
                    visible: settingsScroll.contentHeight > settingsScroll.height
                    active: hovered || pressed || settingsScroll.moving
                    onPositionChanged: if (pressed) settingsScroll.contentY = position * settingsScroll.contentHeight
                    contentItem: Rectangle { radius: 3; color: AppTheme.textMuted; opacity: parent.active ? 0.75 : 0.35 }
                }

                GText {
                    objectName: "settingsNoResults"
                    anchors.centerIn: parent
                    visible: dialog.noResults
                    text: dialog.t("Eşleşen ayar bulunamadı.", "No matching settings.")
                    baseColor: AppTheme.textMuted
                    basePixelSize: 13
                }
            }
        }

        Loader {
            id: typographyDialogLoader
            objectName: "typographyDialogLoader"
            visible: false
            active: false
            sourceComponent: TypographyDialog {
                // Before the settings popup is first shown its content has no
                // window, so Overlay.overlay cannot be resolved from here.
                parent: dialog.parent
                lang: dialog.lang
                onClosed: Qt.callLater(function() { typographyDialogLoader.active = false })
            }
            onLoaded: item.open()
        }
    }

}
