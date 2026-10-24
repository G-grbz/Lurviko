import QtQuick
import QtTest
import Lurviko.App

Item {
    id: host
    width: 800; height: 600
    Component { id: raw; Text { text: "File name for preview.png"; font.family: AppTheme.defaultFontFamily; font.pixelSize: 12 } }
    Component { id: styled; GText { text: "File name for preview.png"; basePixelSize: 12 } }
    TestCase {
        name: "TypographyPerformance"
        when: windowShown
        function batch(component, count) {
            const objects = []
            const start = Date.now()
            for (let i = 0; i < count; ++i) objects.push(component.createObject(host, {visible: false}))
            const elapsed = Date.now() - start
            return {objects: objects, elapsed: elapsed}
        }
        function test_text_creation_and_role_update_cost() {
            const saved = AppTheme.typographySettings.rolesJson
            AppTheme.resetTypography()
            const warm = batch(styled, 30)
            warm.objects.forEach(function(item) { item.destroy() })
            const plain = batch(raw, 1200)
            const themed = batch(styled, 1200)
            const start = Date.now()
            AppTheme.setTypography("body", "family", AppTheme.availableFontFamilies[0])
            const update = Date.now() - start
            console.info("TYPOGRAPHY_BENCH raw=" + plain.elapsed + "ms styled=" + themed.elapsed + "ms fontUpdate=" + update + "ms")
            plain.objects.concat(themed.objects).forEach(function(item) { item.destroy() })
            AppTheme.typographySettings.rolesJson = saved
            wait(50)
        }
    }
}
