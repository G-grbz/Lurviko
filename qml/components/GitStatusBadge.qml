import QtQuick

Rectangle {
    id: badge
    property string status: ""
    property string language: "en"
    objectName: "gitStatusBadge"
    visible: status.length > 0
    width: 18; height: 18; radius: 9
    readonly property color ink: status === "conflict" || status === "deleted" ? "#dc5263"
        : status === "added" || status === "untracked" || status === "clean" ? "#259967"
        : status === "repository" ? "#588ee1"
        : status === "renamed" ? "#588ee1" : status === "ignored" ? "#858b99" : "#d28a20"
    color: AppTheme.surfaceRaised
    border.color: ink
    Text {
        anchors.centerIn: parent
        text: ({modified: "M", added: "+", untracked: "?", deleted: "−", renamed: "R", ignored: "I", conflict: "!", repository: "G", clean: "✓"})[badge.status] || ""
        color: badge.ink; font.pixelSize: 11; font.bold: true
    }
    HoverHandler { id: hover }
    GToolTip {
        visible: badge.visible && hover.hovered
        text: "Git · " + ((badge.language === "tr"
            ? {modified: "Değiştirildi", added: "Eklendi", untracked: "İzlenmiyor", deleted: "Silindi", renamed: "Yeniden adlandırıldı", ignored: "Yok sayılıyor", conflict: "Çakışma", repository: "Git deposu · Durum henüz alınmadı", clean: "Temiz depo"}
            : {modified: "Modified", added: "Added", untracked: "Untracked", deleted: "Deleted", renamed: "Renamed", ignored: "Ignored", conflict: "Conflict", repository: "Repository · Status not yet available", clean: "Clean repository"})[badge.status] || "")
    }
}
