import QtQuick
import Lurviko.App

GMenu {
    id: menu
    property var controller: null
    property var lang
    readonly property bool tr: lang && lang.language === "tr"
    signal requested(string action, var controller)
    objectName: "gitActionsMenu"
    title: "Git"
    icon.source: AppTheme.icon("git.svg")
    menuEntryVisible: AppTheme.gitEnabled && controller !== null && controller.available
    enabled: menuEntryVisible && !controller.busy
    GMenuItem { objectName: "gitCommitAction"; text: menu.tr ? "Commit oluştur…" : "Create commit…"; icon.source: AppTheme.icon("edit.svg"); onTriggered: menu.requested("commit", menu.controller) }
    GMenuItem { objectName: "gitSyncAction"; text: menu.tr ? "Depoyu eşitle" : "Synchronize repository"; icon.source: AppTheme.icon("refresh.svg"); onTriggered: menu.requested("sync", menu.controller) }
    GMenuSeparator {}
    GMenuItem { text: "Push"; icon.name: "go-up"; onTriggered: menu.requested("push", menu.controller) }
    GMenuItem { text: "Pull"; icon.name: "go-down"; onTriggered: menu.requested("pull", menu.controller) }
    GMenuItem { text: "Fetch"; icon.source: AppTheme.icon("refresh.svg"); onTriggered: menu.requested("fetch", menu.controller) }
    GMenuSeparator {}
    GMenuItem { text: menu.tr ? "Değişiklikleri görüntüle" : "View changes"; icon.source: AppTheme.icon("properties.svg"); onTriggered: menu.requested("diff", menu.controller) }
    GMenuItem { text: menu.tr ? "Commit geçmişi" : "Commit history"; icon.source: AppTheme.icon("history.svg"); onTriggered: menu.requested("history", menu.controller) }
    GMenuItem { text: menu.tr ? "Uzak depoda aç" : "Open remote repository"; icon.source: AppTheme.icon("viewer-external.svg"); enabled: menu.controller !== null && String(menu.controller.info.webUrl || "").length > 0; onTriggered: menu.controller.openRemote() }
}
