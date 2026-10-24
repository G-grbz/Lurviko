import QtQuick
import QtTest
import Lurviko.App
import Lurviko.Backend

Item {
    width: 330; height: 700
    QtObject { id: language; property string language: "en"; function t(key) { return key } }
    FavoritesModel { id: favorites }
    QtObject { id: storage; property var favoriteItems: [] }
    AppSidebar {
        id: sidebar
        anchors.fill: parent
        lang: language
        favoritesModel: favorites
        storageModel: storage
    }
    SidebarItem { id: normalItem; visible: false; width: 300; height: 60; title: "Downloads" }
    QuickAccessTile { id: shortcut; visible: false; width: 300; height: 90; title: "Last location" }
    ContentCategoryTile { id: category; visible: false; width: 300; height: 90; title: "Private"; categoryKey: "private" }
    CloudCard { id: cloud; visible: false; width: 300; height: 90; provider: "Google Drive" }
    TestCase {
        name: "SidebarHeadings"
        when: windowShown
        property string savedTypography
        function init() {
            savedTypography = AppTheme.typographySettings.rolesJson
            AppTheme.resetTypography()
        }
        function cleanup() { AppTheme.typographySettings.rolesJson = savedTypography }
        function bodyTitles() {
            return [findChild(normalItem, "sidebarItemTitle"), findChild(shortcut, "quickAccessTitle"),
                findChild(category, "categoryTileTitle"), findChild(cloud, "cloudCardTitle")]
        }
        function test_body_titles_match_the_private_card_without_extra_weight() {
            const titles = bodyTitles()
            const reference = titles[2]
            for (const title of titles) {
                verify(title)
                compare(title.typographyRole, title === titles[0] ? "sidebar" : "body")
                compare(title.font.weight, Font.Normal)
                compare(title.font.family, reference.font.family)
                compare(title.fontInfo.family, reference.fontInfo.family)
                compare(title.fontInfo.weight, reference.fontInfo.weight)
            }
            normalItem.active = true
            compare(titles[0].font.weight, Font.Normal)
            normalItem.active = false
        }
        function test_sidebar_preferences_are_independent_from_body() {
            const titles = bodyTitles()
            const sidebarFont = titles[0].font.family
            const sidebarCache = AppTheme.sidebarTypography.fonts
            AppTheme.setTypography("body", "family", "DejaVu Sans")
            AppTheme.setTypography("body", "bold", true)
            compare(titles[0].font.family, sidebarFont)
            compare(titles[0].font.weight, Font.Normal)
            verify(AppTheme.sidebarTypography.fonts === sidebarCache)
            for (const title of titles.slice(1)) {
                compare(title.font.family, "DejaVu Sans")
                compare(title.font.weight, Font.Bold)
            }
            AppTheme.setTypography("sidebar", "bold", true)
            compare(titles[0].font.weight, Font.Bold)
            AppTheme.setTypography("body", "bold", false)
            compare(titles[0].font.weight, Font.Bold)
            for (const title of titles.slice(1)) compare(title.font.weight, Font.Normal)
            AppTheme.setTypography("sidebar", "bold", false)
            compare(titles[0].font.weight, Font.Normal)
        }
        function test_all_sidebar_section_headings_have_identical_sizes_and_role() {
            for (const section of ["Places", "Favorites", "Network", "Disks"]) {
                const icon = findChild(sidebar, "sidebar" + section + "HeadingIcon")
                const label = findChild(sidebar, "sidebar" + section + "HeadingText")
                verify(icon && label)
                compare(icon.width, 13)
                compare(icon.height, 13)
                compare(label.typographyRole, "sidebar")
                compare(label.font.pixelSize, 10)
                compare(label.font.weight, Font.Normal)
            }
        }
        function test_places_heading_stays_inside_the_scroll_clip() {
            const view = findChild(sidebar, "sidebarNavigation")
            const heading = findChild(sidebar, "sidebarPlacesHeading")
            const icon = findChild(sidebar, "sidebarPlacesHeadingIcon")
            const label = findChild(sidebar, "sidebarPlacesHeadingText")
            verify(view && heading && icon && label)
            compare(icon.width, 13)
            compare(icon.height, 13)
            compare(label.font.pixelSize, 10)
            compare(label.font.weight, Font.Normal)
            wait(50)
            const position = icon.mapToItem(view.contentItem, 0, 0)
            verify(position.y >= 0, "The folder icon must stay below the top clipping boundary")
            verify(heading.mapToItem(view.contentItem, 0, 0).y >= 0)
            verify(position.y + icon.height <= view.contentItem.height)
        }
    }
}
