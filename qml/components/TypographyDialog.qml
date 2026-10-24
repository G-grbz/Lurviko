import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lurviko.App

GModalPopup {
    id: dialog
    required property var lang
    readonly property bool turkish: lang.language === "tr"
    readonly property var roleNames: turkish
        ? ["Ana başlık", "Alt başlık", "Gövde metni", "Sol menü", "Açıklama", "Menüler ve düğmeler"]
        : ["Headings", "Subheadings", "Body text", "Sidebar", "Descriptions", "Menus and buttons"]
    // Real interface strings, with the same automatic size/weight as their
    // source controls; each role can be recognized without a generic slogan.
    readonly property var sampleTexts: turkish
        ? ["Kategoriler", "İkon boyutları", "Sana Özel", "İndirilenler",
           "Her kategori kendi kaynaklarını ve indeks ayarlarını yönetir", "Kopyala"]
        : ["Categories", "Icon sizes", "Private", "Downloads",
           "Each category manages its own sources and indexing settings", "Copy"]
    readonly property var sampleContexts: turkish
        ? ["Keşfet bölüm başlıkları", "Keşfet → İkon boyutları",
           "Dosya/klasör adları, kategori kartları ve hızlı erişim",
           "Konumlar, Favoriler, Ağ, Diskler ve alt öğeleri", "Başlıkların altındaki açıklamalar",
           "Sağ tık menüsü ve düğmeler"]
        : ["Explore section headings", "Explore → Icon sizes",
           "File/folder names, category cards and quick access",
           "Places, Favorites, Network, Devices and their entries", "Descriptions below headings",
           "Context menus and buttons"]
    readonly property var samplePixels: [17, 13, 13, 12, 10, 12]
    readonly property var sampleWeights: [Font.Normal, Font.Normal, Font.Normal, Font.Normal, Font.Normal, Font.Normal]
    property var fontFamilies: []
    objectName: "typographyDialog"
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(760, parent ? parent.width - 24 : 760)
    height: Math.min(690, parent ? parent.height - 24 : 690)
    padding: 20
    onAboutToShow: fontFamilies = [turkish ? "Varsayılan" : "Default"].concat(AppTheme.availableFontFamilies)

    contentItem: ColumnLayout {
        spacing: 14
        GText {
            text: dialog.turkish ? "Yazı tipleri ve renkler" : "Fonts and colors"
            basePixelSize: 20
            baseBold: true
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }
        GText {
            text: dialog.turkish
                ? "Değişiklikler anında kaydedilir. Otomatik renk ve boyut temayı ve mevcut yerleşimi izler. Varsayılan yazı tipi Roboto; bulunamazsa sistem yazı tipi kullanılır. İşaretli stiller uygulanır; boş kutular kapalıdır."
                : "Changes are saved immediately. Automatic colors and sizes follow the theme and existing layout. The default font is Roboto, with the system font as fallback. Checked styles are enabled; unchecked styles are off."
            baseColor: AppTheme.textMuted
            basePixelSize: 10
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }
        ScrollView {
            id: scroll
            objectName: "typographyScrollView"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            contentHeight: roleList.implicitHeight
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                id: roleList
                objectName: "typographyRoleList"
                width: scroll.availableWidth
                spacing: 12
                Repeater {
                    model: AppTheme.typographyRoles
                    delegate: Rectangle {
                        id: roleCard
                        required property string modelData
                        required property int index
                        readonly property var setting: AppTheme.typographyValue(modelData)
                        Layout.fillWidth: true
                        implicitHeight: fields.implicitHeight + 24
                        color: AppTheme.surfaceRaised
                        radius: AppTheme.radiusSmall
                        border.color: AppTheme.border
                        ColumnLayout {
                            id: fields
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 8
                            GText {
                                text: dialog.roleNames[roleCard.index]
                                typographyRole: "menu"
                                basePixelSize: 13
                                baseFontWeight: Font.DemiBold
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                            GText {
                                objectName: "typographyPreview_" + roleCard.modelData
                                Layout.fillWidth: true
                                text: dialog.sampleTexts[roleCard.index]
                                typographyRole: roleCard.modelData
                                basePixelSize: dialog.samplePixels[roleCard.index]
                                baseFontWeight: dialog.sampleWeights[roleCard.index]
                                baseColor: roleCard.modelData === "description" ? AppTheme.textMuted : AppTheme.text
                                elide: Text.ElideRight
                            }
                            GText {
                                objectName: "typographyContext_" + roleCard.modelData
                                Layout.fillWidth: true
                                text: dialog.sampleContexts[roleCard.index]
                                typographyRole: "description"
                                basePixelSize: 10
                                baseColor: AppTheme.textMuted
                                wrapMode: Text.Wrap
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                GAccountPicker {
                                    objectName: "typographyFont_" + roleCard.modelData
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 80
                                    model: dialog.fontFamilies
                                    allowKeep: true
                                    keepText: AppTheme.defaultFontFamily
                                    value: roleCard.setting.family || ""
                                    Accessible.name: dialog.roleNames[roleCard.index]
                                    onEdited: function(value) {
                                        if (!value || AppTheme.availableFontFamilies.indexOf(value) >= 0)
                                            AppTheme.setTypography(roleCard.modelData, "family", value)
                                    }
                                }
                                GComboBox {
                                    objectName: "typographySize_" + roleCard.modelData
                                    Layout.preferredWidth: 105
                                    model: [dialog.turkish ? "Otomatik" : "Automatic"].concat(
                                        Array.from({length: 41}, function(_, i) { return (i + 8) + " px" }))
                                    currentIndex: roleCard.setting.pixels ? roleCard.setting.pixels - 7 : 0
                                    onActivated: function(index) {
                                        AppTheme.setTypography(roleCard.modelData, "pixels", index ? index + 7 : 0)
                                    }
                                }
                                GButton {
                                    objectName: "typographyColor_" + roleCard.modelData
                                    Layout.preferredWidth: 46
                                    text: ""
                                    Accessible.name: dialog.turkish ? "Metin rengi" : "Text color"
                                    onClicked: {
                                        colorEditor.role = roleCard.modelData
                                        colorEditor.open()
                                    }
                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: 22; height: 22; radius: 5
                                        color: AppTheme.typographyColor(roleCard.modelData, AppTheme.text)
                                        border.color: AppTheme.borderStrong
                                    }
                                }
                            }
                            Flow {
                                Layout.fillWidth: true
                                spacing: 12
                                Repeater {
                                    model: ["bold", "italic", "underline", "overline", "strikeout"]
                                    delegate: GCheckBox {
                                        required property string modelData
                                        required property int index
                                        objectName: "typographyStyle_" + roleCard.modelData + "_" + modelData
                                        text: (dialog.turkish
                                            ? ["Kalın", "İtalik", "Altı çizili", "Üst çizgi", "Üstü çizili"]
                                            : ["Bold", "Italic", "Underline", "Overline", "Strikethrough"])[index]
                                        tristate: false
                                        checked: roleCard.setting[modelData] === true
                                        onClicked: AppTheme.setTypography(roleCard.modelData, modelData, checked)
                                    }
                                }
                            }
                        }
                    }
                }
            }
            GPopupWheelScroll {
                parent: scroll
                anchors.fill: parent
                view: scroll.contentItem
            }
        }
        RowLayout {
            Layout.fillWidth: true
            GButton {
                text: dialog.turkish ? "Varsayılanlara dön" : "Restore defaults"
                onClicked: AppTheme.resetTypography()
            }
            Item { Layout.fillWidth: true }
            GButton {
                text: dialog.turkish ? "Kapat" : "Close"
                onClicked: dialog.close()
            }
        }
    }
    GModalPopup {
        id: colorEditor
        objectName: "typographyColorDialog"
        property string role: "body"
        property color draft: AppTheme.text
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(360, parent ? parent.width - 24 : 360)
        height: Math.min(420, parent ? parent.height - 24 : 420)
        onAboutToShow: {
            draft = AppTheme.typographyColor(role, AppTheme.text)
            hex.text = String(draft)
        }
        contentItem: ColumnLayout {
            spacing: 12
            GText { text: dialog.turkish ? "Metin rengi" : "Text color"; basePixelSize: 16; baseBold: true }
            Rectangle {
                Layout.fillWidth: true; Layout.preferredHeight: 42
                radius: 6; color: colorEditor.draft; border.color: AppTheme.borderStrong
            }
            Repeater {
                model: ["R", "G", "B"]
                delegate: RowLayout {
                    required property string modelData
                    required property int index
                    Layout.fillWidth: true
                    GText { text: modelData; Layout.preferredWidth: 15 }
                    GSlider {
                        Layout.fillWidth: true
                        from: 0; to: 255; stepSize: 1
                        value: [colorEditor.draft.r, colorEditor.draft.g, colorEditor.draft.b][index] * 255
                        onMoved: {
                            const c = colorEditor.draft
                            colorEditor.draft = Qt.rgba(index === 0 ? value / 255 : c.r,
                                index === 1 ? value / 255 : c.g, index === 2 ? value / 255 : c.b, 1)
                            hex.text = String(colorEditor.draft)
                        }
                    }
                }
            }
            GTextField {
                id: hex
                objectName: "typographyColorHex"
                Layout.fillWidth: true
                maximumLength: 7
                placeholderText: "#RRGGBB"
                validator: RegularExpressionValidator { regularExpression: /#[0-9a-fA-F]{6}/ }
                onTextEdited: if (acceptableInput) colorEditor.draft = text
            }
            Item { Layout.fillHeight: true }
            GButton {
                Layout.fillWidth: true
                text: dialog.turkish ? "Otomatik (tema rengi)" : "Automatic (theme color)"
                onClicked: { AppTheme.setTypography(colorEditor.role, "color", ""); colorEditor.close() }
            }
            RowLayout {
                GButton { text: dialog.turkish ? "İptal" : "Cancel"; onClicked: colorEditor.close() }
                Item { Layout.fillWidth: true }
                GButton {
                    text: dialog.turkish ? "Uygula" : "Apply"
                    enabled: hex.acceptableInput
                    onClicked: {
                        AppTheme.setTypography(colorEditor.role, "color", String(colorEditor.draft))
                        colorEditor.close()
                    }
                }
            }
        }
    }
}
