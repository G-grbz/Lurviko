import QtQuick
import Lurviko.App
import "MusicArtists.js" as Artists

GText {
    id: control
    property string artistText: ""
    property string suffixText: ""
    property string fallbackText: ""
    readonly property var artists: Artists.split(artistText)
    signal artistRequested(string artist)
    textFormat: Text.StyledText
    maximumLineCount: 1
    clip: true
    text: artists.length ? artists.map(function(name, index) {
        return '<a href="artist:' + index + '" style="text-decoration:none">'
            + Artists.escapeHtml(name) + '</a>'
    }).join(" · ") + Artists.escapeHtml(suffixText) : Artists.escapeHtml(fallbackText)
    linkColor: hoveredLink.length ? AppTheme.accent : color
    Accessible.name: artists.length ? artists.join(" · ") + suffixText : fallbackText
    onLinkActivated: function(link) {
        const index = Number(link.slice(7))
        if (index >= 0 && index < artists.length) artistRequested(artists[index])
    }
    HoverHandler { cursorShape: control.hoveredLink.length ? Qt.PointingHandCursor : Qt.ArrowCursor }
}
