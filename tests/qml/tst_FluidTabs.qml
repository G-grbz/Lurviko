import QtQuick
import QtTest
import Lurviko.App

Item {
    id: root
    width: 1100; height: 380
    Component {
        id: fixtureComponent
        Item {
            property real zoom: 1
            property real tabHeight: 40
            width: Math.ceil(232 * zoom)
            height: Math.ceil((tabHeight + 24) * zoom)
            Rectangle { anchors.fill: parent; color: "black" }
            FluidTabSurface {
                objectName: "contour"
                x: 12 * parent.zoom; y: 12 * parent.zoom
                width: 208; height: parent.tabHeight
                scale: parent.zoom
                transformOrigin: Item.TopLeft
                selected: true
                navigationColor: "white"
            }
        }
    }
    Component {
        id: overlappingTabsComponent
        Item {
            property real zoom: 1
            property int activeIndex: 1
            property int hiddenIndex: -1
            property bool trimOverlap: true
            width: Math.ceil(632 * zoom); height: Math.ceil(64 * zoom)
            Rectangle { anchors.fill: parent; color: "#547b96" }
            Item {
                width: 632; height: 64
                scale: parent.zoom; transformOrigin: Item.TopLeft
                Repeater {
                    model: 3
                    FluidTabSurface {
                        required property int index
                        x: 12 + index * 190; y: 12
                        width: 208; height: 40
                        selected: index === activeIndex
                        visible: index !== hiddenIndex
                        z: selected ? 10 : 1
                        tabOverlap: 18
                        occludedOnLeft: trimOverlap && index > 0 && activeIndex === index - 1
                        occludedOnRight: trimOverlap && index < 2 && !selected
                    }
                }
            }
        }
    }
    TestCase {
        name: "FluidTabGeometry"
        when: windowShown
        property bool savedQt
        property bool savedDark
        function initTestCase() {
            savedQt = AppTheme.useQtTheme
            savedDark = AppTheme.settings.darkMode
            AppTheme.setQtTheme(false)
            AppTheme.settings.darkMode = false
        }
        function cleanupTestCase() {
            AppTheme.settings.darkMode = savedDark
            AppTheme.setQtTheme(savedQt)
        }
        function firstFilled(image, y) {
            for (let x = 0; x < image.width; ++x)
                if (image.red(x, y) > 127) return x
            fail("Missing tab silhouette at row " + y)
        }
        function lastFilled(image, y) {
            for (let x = image.width - 1; x >= 0; --x)
                if (image.red(x, y) > 127) return x
            fail("Missing tab silhouette at row " + y)
        }
        function test_symmetric_straight_shoulders_data() {
            return [
                {tag: "100", zoom: 1, tabHeight: 40},
                {tag: "150", zoom: 1.5, tabHeight: 40},
                {tag: "250", zoom: 2.5, tabHeight: 40},
                {tag: "large-font", zoom: 1.5, tabHeight: 68}
            ]
        }
        function test_symmetric_straight_shoulders(data) {
            AppTheme.setQtTheme(false)
            AppTheme.settings.darkMode = false
            const fixture = createTemporaryObject(fixtureComponent, root, {zoom: data.zoom, tabHeight: data.tabHeight})
            verify(fixture !== null)
            verify(waitForRendering(fixture))
            const contour = findChild(fixture, "contour")
            const image = grabImage(fixture)
            image.save("/tmp/lurviko-tab-outline-" + data.tag + ".png")
            const scale = data.zoom * image.width / fixture.width
            const upper = contour.capTop + contour.corner
            const lower = contour.base - contour.lowerRadius
            const rows = [0.25, 0.5, 0.75].map(t => Math.round((12 + upper + (lower - upper) * t) * scale))
            const left = rows.map(y => firstFilled(image, y))
            // A bowed/mountain-like shoulder fails this rendered-pixel check.
            verify(Math.abs(left[1] - (left[0] + left[2]) / 2) <= 1,
                "The shoulder must be a straight segment at " + data.tag + "%")
            for (const y of rows)
                verify(Math.abs(firstFilled(image, y) - (image.width - 1 - lastFilled(image, y))) <= 1,
                    "Left and right edges must be mirror images")
            const topRows = [60, 104, 148].map(x => {
                const px = Math.round((12 + x) * scale)
                for (let y = 0; y < image.height; ++y)
                    if (image.red(px, y) > 127) return y
                return -1
            })
            verify(topRows[0] > 0, "A flat top must be visible above the tab face")
            compare(topRows[0], topRows[1])
            compare(topRows[1], topRows[2])
            let antialiased = false
            for (let y = rows[0]; y <= rows[2] && !antialiased; ++y) {
                const edge = firstFilled(image, y)
                for (let x = edge - 2; x <= edge + 1; ++x) {
                    const coverage = image.red(x, y)
                    if (coverage > 0 && coverage < 180) antialiased = true
                }
            }
            verify(antialiased, "The outline must be antialiased without a separate layer")
            verify(!contour.layer.enabled)
            verify(findChild(contour, "fluidTabGlow") === null)
        }
        function test_administrator_color_is_confined_to_the_tab_data() {
            return [{tag: "light", qt: false, dark: false}, {tag: "dark", qt: false, dark: true},
                {tag: "qt", qt: true, dark: false}]
        }
        function test_administrator_color_is_confined_to_the_tab(data) {
            AppTheme.settings.darkMode = data.dark
            AppTheme.setQtTheme(data.qt)
            const fixture = createTemporaryObject(fixtureComponent, root)
            const contour = findChild(fixture, "contour")
            contour.administrator = true
            verify(waitForRendering(fixture))
            verify(contour.activeColor !== contour.navigationColor,
                "Administrator tabs keep their warning tint without tinting the navigation surface")
            compare(contour.activeColor, AppTheme.dark ? "#3a2a18" : "#fff0d8")
            compare(contour.outlineColor, AppTheme.warning)
        }
        function test_translucent_neighbors_do_not_show_through_the_front_tab_data() {
            const cases = []
            for (const dark of [false, true])
                for (const zoom of [1, 1.5])
                    for (const pair of [{name: "passive", active: 2, hidden: 0, right: true},
                                        {name: "active-right", active: 1, hidden: 0, right: true},
                                        {name: "active-left", active: 1, hidden: 2, right: false}])
                        cases.push({tag: pair.name + "-" + dark + "-" + zoom,
                            dark: dark, zoom: zoom, active: pair.active, hidden: pair.hidden, right: pair.right})
            return cases
        }
        function test_translucent_neighbors_do_not_show_through_the_front_tab(data) {
            inputTestHelper.setSystemThemeTestAppearance(data.dark, true)
            AppTheme.setQtTheme(true)
            tryCompare(AppTheme, "translucentWindow", true)
            const fixture = createTemporaryObject(overlappingTabsComponent, root,
                {zoom: data.zoom, activeIndex: data.active})
            verify(waitForRendering(fixture))
            wait(130)
            const combined = grabImage(fixture)
            fixture.hiddenIndex = data.hidden
            verify(waitForRendering(fixture))
            const reference = grabImage(fixture)
            // These pixels are inside the front tab and behind the covered
            // neighbor's foot. Removing that neighbor must not change them.
            const scale = combined.width / 632
            let error = 0
            for (let offset = 2; offset <= 5; ++offset) {
                const x = Math.floor((12 + data.hidden * 190 + (data.right ? 208 - offset : offset)) * scale)
                const y = Math.ceil(52 * scale) - 1
                for (const channel of ["red", "green", "blue"])
                    error = Math.max(error, Math.abs(combined[channel](x, y) - reference[channel](x, y)))
            }
            if (error > 2) {
                combined.save("/tmp/lurviko-join-combined-" + data.tag + ".png")
                reference.save("/tmp/lurviko-join-reference-" + data.tag + ".png")
            }
            verify(error <= 2, "Covered border/fill must not bleed through; channel error=" + error)
            fixture.hiddenIndex = -1
            fixture.trimOverlap = false
            verify(waitForRendering(fixture))
            const untrimmed = grabImage(fixture)
            let oldError = 0
            for (let offset = 2; offset <= 5; ++offset) {
                const x = Math.floor((12 + data.hidden * 190 + (data.right ? 208 - offset : offset)) * scale)
                const y = Math.ceil(52 * scale) - 1
                for (const channel of ["red", "green", "blue"])
                    oldError = Math.max(oldError, Math.abs(untrimmed[channel](x, y) - reference[channel](x, y)))
            }
            verify(oldError > 2, "The pixel check must detect the original overlapping paint")
            fixture.trimOverlap = true
            verify(waitForRendering(fixture))
            grabImage(fixture).save("/tmp/lurviko-translucent-tabs-" + data.tag + ".png")
            inputTestHelper.restoreSystemThemeTestAppearance()
        }
    }
}
