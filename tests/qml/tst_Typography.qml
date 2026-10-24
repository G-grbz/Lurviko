import QtQuick
import QtQuick.Controls
import QtTest
import Lurviko.App

Item {
    width: 800; height: 740
    QtObject { id: language; property string language: "en" }
    GText { id: heading; text: "Heading"; basePixelSize: 20 }
    GText { id: body; text: "Body"; basePixelSize: 12 }
    GText { id: description; typographyRole: "description"; text: "Description"; baseColor: AppTheme.textMuted; basePixelSize: 11 }
    GText { id: errorText; text: "Error"; baseColor: AppTheme.danger }
    GText { id: emphasized; text: "Emphasis"; baseBold: true }
    GText { id: spacedCaption; basePixelSize: 9; baseLetterSpacing: 0.6; baseCapitalization: Font.AllUppercase; text: "Places" }
    GText { id: technical; baseFontFamily: "monospace"; basePixelSize: 11; text: "0644" }
    GText { id: symbol; text: "✓"; baseBold: true }
    GButton { id: button; text: "Menu" }
    SignalSpy { id: bodyFontChanges; target: body; signalName: "fontChanged" }
    TypographyDialog { id: dialog; lang: language }
    TestCase {
        name: "Typography"
        when: windowShown
        property string saved
        property bool qtTheme
        function init() {
            saved = AppTheme.typographySettings.rolesJson
            qtTheme = AppTheme.useQtTheme
            AppTheme.resetTypography()
        }
        function cleanup() {
            dialog.close()
            AppTheme.typographySettings.rolesJson = saved
            AppTheme.setQtTheme(qtTheme)
            wait(200)
        }
        function test_role_changes_and_semantic_colors() {
            compare(heading.typographyRole, "title")
            compare(description.typographyRole, "description")
            AppTheme.setTypography("title", "pixels", 28)
            AppTheme.setTypography("title", "color", "#123456")
            compare(heading.font.pixelSize, 28)
            compare(String(heading.color), "#123456")
            compare(body.font.pixelSize, 12)
            AppTheme.setTypography("body", "color", "#abcdef")
            compare(String(body.color), "#abcdef")
            compare(errorText.color, AppTheme.danger)
            AppTheme.setTypography("title", "pixels", 0)
            compare(heading.font.pixelSize, 20)
        }
        function test_font_and_persistence_validation() {
            const family = AppTheme.availableFontFamilies[0]
            AppTheme.setTypography("body", "family", family)
            compare(body.font.family, family)
            compare(JSON.parse(AppTheme.typographySettings.rolesJson).body.family, family)
            AppTheme.setTypography("body", "family", "missing-font-987")
            compare(body.font.family, family)
            AppTheme.setTypography("body", "pixels", 999)
            compare(body.font.pixelSize, 48)
            AppTheme.setTypography("body", "color", "not a color")
            compare(body.color, AppTheme.text)
        }
        function test_menu_controls_and_default_font() {
            compare(body.font.family, AppTheme.defaultFontFamily)
            compare(body.font.hintingPreference, Font.PreferVerticalHinting)
            AppTheme.setTypography("menu", "pixels", 19)
            compare(button.font.pixelSize, 19)
            AppTheme.setTypography("menu", "family", AppTheme.availableFontFamilies[0])
            compare(button.font.family, AppTheme.availableFontFamilies[0])
        }
        function test_styles_override_defaults_and_reset() {
            compare(emphasized.font.weight, Font.Normal)
            AppTheme.setTypography("body", "bold", false)
            compare(emphasized.font.weight, Font.Normal)
            AppTheme.setTypography("body", "bold", true)
            compare(body.font.weight, Font.Bold)
            for (const field of ["italic", "underline", "overline", "strikeout"]) {
                AppTheme.setTypography("body", field, true)
                compare(body.font[field], true, field)
                compare(symbol.font[field], false)
                compare(JSON.parse(AppTheme.typographySettings.rolesJson).body[field], true)
                AppTheme.setTypography("menu", field, true)
                compare(button.font[field], true)
            }
            AppTheme.setTypography("body", "bold", "false")
            compare(body.font.weight, Font.Bold) // Invalid types are not coerced.
            AppTheme.setTypography("body", "bold", null)
            compare(body.font.weight, Font.Normal)
            compare(emphasized.font.weight, Font.Normal)
            AppTheme.resetTypography()
            verify(!body.font.italic && !body.font.underline && !body.font.strikeout)
            verify(!button.font.italic && !button.font.underline && !button.font.strikeout)
        }
        function test_only_the_changed_role_rebuilds_cached_fonts() {
            const unrelatedFonts = AppTheme.menuTypography.fonts
            const bodyFonts = AppTheme.bodyTypography.fonts
            bodyFontChanges.clear()
            AppTheme.setTypography("body", "color", "#123456")
            compare(bodyFontChanges.count, 0)
            verify(AppTheme.bodyTypography.fonts === bodyFonts)
            AppTheme.setTypography("body", "italic", true)
            compare(bodyFontChanges.count, 1)
            verify(AppTheme.menuTypography.fonts === unrelatedFonts)
            const count = Object.keys(AppTheme.bodyTypography.fonts).length
            for (let i = 0; i < 100; ++i) AppTheme.bodyTypography.fontFor(23, Font.Normal, false, false, false, false, false)
            verify(Object.keys(AppTheme.bodyTypography.fonts).length <= count + 1)
        }
        function test_body_preview_starts_normal_and_matches_style_selection() {
            dialog.open()
            tryCompare(dialog, "opened", true)
            const preview = findChild(dialog.contentItem, "typographyPreview_body")
            const toggle = findChild(dialog.contentItem, "typographyStyle_body_bold")
            compare(preview.font.weight, Font.Normal)
            compare(toggle.checkState, Qt.Unchecked)
            AppTheme.setTypography("body", "bold", true)
            compare(preview.font.weight, Font.Bold)
            compare(toggle.checkState, Qt.Checked)
            AppTheme.setTypography("body", "bold", false)
            compare(preview.font.weight, Font.Normal)
            compare(toggle.checkState, Qt.Unchecked)
        }
        function test_context_font_fields_keep_size_family_and_style_binding() {
            compare(spacedCaption.font.pixelSize, 9)
            compare(spacedCaption.font.family, AppTheme.defaultFontFamily)
            verify(Math.abs(spacedCaption.font.letterSpacing - 0.6) < 0.02)
            compare(spacedCaption.font.capitalization, Font.AllUppercase)
            compare(technical.font.pixelSize, 11)
            compare(technical.font.family, "monospace")
            AppTheme.setTypography("description", "pixels", 15)
            AppTheme.setTypography("description", "italic", true)
            compare(spacedCaption.font.pixelSize, 15)
            compare(spacedCaption.font.italic, true)
            verify(Math.abs(spacedCaption.font.letterSpacing - 0.6) < 0.02)
            compare(spacedCaption.font.capitalization, Font.AllUppercase)
            const style = AppTheme.bodyTypography
            for (let i = 0; i < 100; ++i)
                style.fontFor(12, Font.Normal, false, false, false, false, false, "monospace", i / 10, Font.MixedCase)
            verify(Object.keys(style.fonts).length <= 64)
        }
        function test_previews_use_real_interface_text_in_both_languages() {
            for (const locale of ["en", "tr"]) {
                language.language = locale
                dialog.open()
                tryCompare(dialog, "opened", true)
                const expected = locale === "tr"
                    ? ["Kategoriler", "İkon boyutları", "Sana Özel", "İndirilenler",
                       "Her kategori kendi kaynaklarını ve indeks ayarlarını yönetir", "Kopyala"]
                    : ["Categories", "Icon sizes", "Private", "Downloads",
                       "Each category manages its own sources and indexing settings", "Copy"]
                AppTheme.typographyRoles.forEach(function(role, index) {
                    const preview = findChild(dialog.contentItem, "typographyPreview_" + role)
                    const context = findChild(dialog.contentItem, "typographyContext_" + role)
                    compare(preview.text, expected[index])
                    compare(preview.typographyRole, role)
                    verify(context.text.length > 0)
                })
                dialog.close()
                tryCompare(dialog, "opened", false)
                wait(150)
            }
            language.language = "en"
        }
        function test_wheel_scroll_moves_the_settings_view() {
            const oldHeight = dialog.height
            dialog.height = 440
            dialog.open()
            tryCompare(dialog, "opened", true)
            const area = findChild(dialog.contentItem, "typographyScrollView")
            const view = area.contentItem
            tryVerify(function() { return view.contentHeight > view.height + 100 })
            const top = view.originY || 0
            view.contentY = top
            mouseWheel(area, area.width - 18, area.height / 2, 0, -120, Qt.NoButton, Qt.NoModifier, 0)
            tryVerify(function() { return view.contentY > top + 20 })
            mouseWheel(area, area.width - 18, area.height / 2, 0, 120, Qt.NoButton, Qt.NoModifier, 0)
            tryVerify(function() { return Math.abs(view.contentY - top) < 3 })
            dialog.height = oldHeight
        }
        function test_style_controls_are_boolean_on_off() {
            dialog.open()
            tryCompare(dialog, "opened", true)
            for (const field of ["bold", "italic", "underline", "overline", "strikeout"]) {
                const toggle = findChild(dialog.contentItem, "typographyStyle_title_" + field)
                verify(toggle)
                compare(toggle.checkState, Qt.Unchecked)
                mouseClick(toggle, 10, 10)
                compare(AppTheme.typographyValue("title")[field], true)
                compare(heading.font[field], true, field)
                mouseClick(toggle, 10, 10)
                compare(AppTheme.typographyValue("title")[field], false)
                compare(heading.font[field], false)
                verify(!toggle.tristate)
                mouseClick(toggle, 10, 10)
                compare(toggle.checkState, Qt.Checked)
                mouseClick(toggle, 10, 10)
                compare(toggle.checkState, Qt.Unchecked)
            }
        }
        function test_dialog_and_color_editor_theme() {
            AppTheme.setQtTheme(true)
            dialog.open()
            tryCompare(dialog, "opened", true)
            verify(findChild(dialog.contentItem, "typographyFont_title"))
            const size = findChild(dialog.contentItem, "typographySize_body")
            verify(size)
            size.activated(10)
            compare(AppTheme.typographyValue("body").pixels, 17)
            const picker = findChild(dialog.contentItem, "typographyFont_body")
            picker.edited(AppTheme.availableFontFamilies[0])
            compare(body.font.family, AppTheme.availableFontFamilies[0])
            const color = findChild(dialog.contentItem, "typographyColor_body")
            color.clicked()
            const editor = inputTestHelper.objectChild(dialog, "typographyColorDialog")
            verify(editor)
            tryCompare(editor, "opened", true)
            verify(findChild(editor.background, "nativeOverlaySurface"))
            editor.close()
            AppTheme.resetTypography()
            compare(body.font.pixelSize, 12)
            compare(body.color, AppTheme.text)
        }
    }
}
