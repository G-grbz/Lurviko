import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Lurviko.App

// Live Discover navigation watermark editor. The preview and the real sidebar
// read the same AppTheme draft values. Cancel discards changes; Save persists.
GModalPopup {
    id: editor
    objectName: "exploreWatermarkEditor"
    required property var lang
    readonly property bool turkish: lang.language === "tr"
    readonly property var bundledIcons: [
        {icon: "watermark.png", tr: "Varsayılan", en: "Default"},
        {icon: "home.svg", tr: "Ev", en: "Home"},
        {icon: "favorite.svg", tr: "Favori", en: "Favorite"},
        {icon: "settings.svg", tr: "Ayarlar", en: "Settings"},
        {icon: "sun.svg", tr: "Güneş", en: "Sun"},
        {icon: "moon.svg", tr: "Ay", en: "Moon"},
        {icon: "lock.svg", tr: "Kilit", en: "Lock"},
        {icon: "music-note.svg", tr: "Müzik", en: "Music"},
        {icon: "eye.svg", tr: "Göz", en: "Eye"},
        {icon: "history.svg", tr: "Geçmiş", en: "History"},
        {icon: "keyboard.svg", tr: "Klavye", en: "Keyboard"},
        {icon: "info.svg", tr: "Bilgi", en: "Info"},
        {icon: "view-layout.svg", tr: "Düzen", en: "Layout"}
    ]
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(640, parent ? parent.width - 32 : 640)
    height: Math.min(760, parent ? parent.height - 32 : 760)
    padding: 18
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    function label(tr, en) { return turkish ? tr : en }
    function selectedBundled(name) { return AppTheme.discoverWatermarkIcon === name }
    function saveAndClose() {
        AppTheme.saveDiscoverWatermark()
        close()
    }
    onAboutToShow: AppTheme.restoreDiscoverWatermark()
    onClosed: AppTheme.restoreDiscoverWatermark()

    FileDialog {
        id: iconPicker
        title: editor.label("Filigran ikonu seç", "Select watermark icon")
        fileMode: FileDialog.OpenFile
        nameFilters: ["Images (*.svg *.png *.jpg *.jpeg *.webp)", "All files (*)"]
        onAccepted: {
            const source = selectedFile.toString()
            if (AppTheme.localIconSource(source).length)
                AppTheme.discoverWatermarkIcon = source
        }
    }

    component WatermarkSlider: ColumnLayout {
        id: control
        property string title: ""
        property real minimum: 0
        property real maximum: 100
        property real step: 1
        property real currentValue: 0
        property string unit: ""
        property int decimals: 0
        signal edited(real value)
        spacing: 2
        RowLayout {
            Layout.fillWidth: true
            GText { Layout.fillWidth: true; text: control.title; basePixelSize: 12; baseColor: AppTheme.text }
            GText {
                text: Number(control.currentValue).toFixed(control.decimals) + control.unit
                basePixelSize: 11
                baseColor: AppTheme.accent
                baseFontWeight: Font.DemiBold
            }
        }
        GSlider {
            Layout.fillWidth: true
            from: control.minimum
            to: control.maximum
            stepSize: control.step
            value: control.currentValue
            Accessible.name: control.title
            onMoved: control.edited(value)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                GText {
                    text: editor.label("Keşfet filigranı", "Discover watermark")
                    basePixelSize: 19
                    baseFontWeight: Font.DemiBold
                    baseColor: AppTheme.text
                }
                GText {
                    Layout.fillWidth: true
                    text: editor.label("İkonu seç; boyutunu, açısını ve konumunu canlı düzenle.",
                                       "Choose an icon and adjust its size, angle and position live.")
                    basePixelSize: 11
                    baseColor: AppTheme.textMuted
                    wrapMode: Text.WordWrap
                }
            }
            GToolButton {
                Layout.preferredWidth: 36
                Layout.preferredHeight: 36
                icon.source: AppTheme.icon("close-ui.svg")
                onClicked: editor.close()
                GToolTip { text: editor.label("Kapat", "Close") }
            }
        }

        // Preview uses the real card implementation, not an approximation.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 108
            radius: 13
            color: AppTheme.surfaceRaised
            border.color: AppTheme.border
            ExploreNavigationCard {
                objectName: "discoverWatermarkLivePreview"
                anchors.centerIn: parent
                width: Math.min(300, parent.width - 30)
                height: 66
                title: editor.label("Keşfet", "Discover")
                subtitle: editor.label("Ana sayfa", "Home overview")
                iconSource: AppTheme.icon("dashboard.svg")
                active: true
                watermarkEditText: editor.label("Filigranı düzenle", "Edit watermark")
            }
        }

        ScrollView {
            id: optionsScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                width: optionsScroll.availableWidth
                spacing: 8
                GText {
                    Layout.fillWidth: true
                    text: editor.label("İkon seçimi", "Icon selection")
                    basePixelSize: 13
                    baseFontWeight: Font.DemiBold
                    baseColor: AppTheme.text
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 4
                    columnSpacing: 6
                    rowSpacing: 6
                    Repeater {
                        model: editor.bundledIcons
                        delegate: GButton {
                            required property var modelData
                            objectName: "watermarkIconOption_" + modelData.icon.replace(/\.[^.]+$/, "")
                            Layout.fillWidth: true
                            Layout.preferredHeight: 36
                            text: editor.turkish ? modelData.tr : modelData.en
                            icon.source: AppTheme.icon(modelData.icon, AppTheme.accent)
                            icon.width: 16
                            icon.height: 16
                            onClicked: AppTheme.discoverWatermarkIcon = modelData.icon
                            background: Rectangle {
                                radius: 9
                                color: editor.selectedBundled(modelData.icon)
                                    ? AppTheme.accentSoft : (parent.hovered ? AppTheme.surfaceHover : AppTheme.surface)
                                border.color: editor.selectedBundled(modelData.icon)
                                    ? AppTheme.accentBorder : AppTheme.border
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    GButton {
                        text: editor.label("Dosyadan ikon seç…", "Choose icon file…")
                        icon.source: AppTheme.icon("folder.svg")
                        onClicked: iconPicker.open()
                    }
                    GText {
                        Layout.fillWidth: true
                        text: AppTheme.discoverWatermarkIcon.indexOf("file:") === 0
                              ? editor.label("Özel ikon", "Custom icon") : AppTheme.discoverWatermarkIcon
                        baseColor: AppTheme.textMuted
                        basePixelSize: 10
                        elide: Text.ElideMiddle
                        horizontalAlignment: Text.AlignRight
                    }
                }
                GText {
                    Layout.fillWidth: true
                    text: editor.label("Özel ikonlar SVG, PNG, JPG veya WEBP olabilir.",
                                       "Custom icons may be SVG, PNG, JPG or WEBP.")
                    basePixelSize: 10
                    baseColor: AppTheme.textMuted
                    wrapMode: Text.WordWrap
                }
                WatermarkSlider {
                    Layout.fillWidth: true
                    title: editor.label("Boyut", "Size")
                    minimum: 0.5; maximum: 2.5; step: 0.05
                    currentValue: AppTheme.discoverWatermarkScale
                    decimals: 2; unit: "×"
                    onEdited: function(value) { AppTheme.discoverWatermarkScale = value }
                }
                WatermarkSlider {
                    Layout.fillWidth: true
                    title: editor.label("Eğim", "Rotation")
                    minimum: -45; maximum: 45; step: 1
                    currentValue: AppTheme.discoverWatermarkAngle
                    unit: "°"
                    onEdited: function(value) { AppTheme.discoverWatermarkAngle = value }
                }
                WatermarkSlider {
                    Layout.fillWidth: true
                    title: editor.label("Opaklık", "Opacity")
                    minimum: 1; maximum: 40; step: 1
                    currentValue: AppTheme.discoverWatermarkOpacity * 100
                    unit: "%"
                    onEdited: function(value) { AppTheme.discoverWatermarkOpacity = value / 100 }
                }
                WatermarkSlider {
                    Layout.fillWidth: true
                    title: editor.label("Yatay konum", "Horizontal position")
                    minimum: -90; maximum: 90; step: 1
                    currentValue: AppTheme.discoverWatermarkOffsetX
                    unit: " px"
                    onEdited: function(value) { AppTheme.discoverWatermarkOffsetX = value }
                }
                WatermarkSlider {
                    Layout.fillWidth: true
                    title: editor.label("Dikey konum", "Vertical position")
                    minimum: -35; maximum: 35; step: 1
                    currentValue: AppTheme.discoverWatermarkOffsetY
                    unit: " px"
                    onEdited: function(value) { AppTheme.discoverWatermarkOffsetY = value }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            GButton {
                text: editor.label("Varsayılana dön", "Reset to default")
                onClicked: AppTheme.resetDiscoverWatermarkDraft()
            }
            Item { Layout.fillWidth: true }
            GButton { text: editor.label("İptal", "Cancel"); onClicked: editor.close() }
            GButton {
                objectName: "saveExploreWatermarkButton"
                text: editor.label("Kaydet", "Save")
                onClicked: editor.saveAndClose()
            }
        }
    }
}
