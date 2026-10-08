import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import Lurviko.App

Item {
    id: panel
    property var metadata: ({})
    property var itemData: null
    required property var lang
    property bool shown: true
    property bool animationsEnabled: true

    readonly property string state: metadata && metadata.state ? String(metadata.state) : "loading"
    readonly property bool ready: state === "ready"
    readonly property string titleText: ready && metadata.title
                                        ? String(metadata.title)
                                        : (metadata && metadata.parsedTitle ? String(metadata.parsedTitle)
                                                                            : (itemData ? String(itemData.name || "") : ""))
    readonly property bool tvSeries: String(metadata && metadata.mediaType ? metadata.mediaType : "") === "tv"
    readonly property color cardText: "#F8FBFF"
    readonly property color cardTextMuted: "#D7DEEA"
    readonly property color cardTextFaint: "#AFBCD0"
    readonly property color cardAccent: "#D9D94B"
    readonly property string premiumFont: "Noto Sans"
    readonly property string premiumDisplayFont: "Noto Sans"
    readonly property bool posterBackdropFallback: panel.ready
                                                   && !String(panel.metadata.backdropUrl || "").length
                                                   && String(panel.metadata.posterUrl || "").length > 0
    readonly property string backdropSource: panel.ready
                                             ? (String(panel.metadata.backdropUrl || "").length
                                                ? String(panel.metadata.backdropUrl)
                                                : String(panel.metadata.posterUrl || ""))
                                             : ""

    opacity: shown ? 1.0 : 0.0
    scale: shown ? 1.0 : 0.972
    transformOrigin: Item.Center
    Behavior on opacity { NumberAnimation { duration: panel.animationsEnabled ? 150 : 0; easing.type: Easing.OutCubic } }
    Behavior on scale { NumberAnimation { duration: panel.animationsEnabled ? 185 : 0; easing.type: Easing.OutCubic } }
    readonly property string metaSummary: {
        const parts = []
        if (Number(panel.metadata.year || 0) > 0)
            parts.push(String(panel.metadata.year))
        const genres = panel.metadata.genres || []
        if (genres.length > 0)
            parts.push(genres.slice(0, 3).join(", ").toUpperCase())
        if (Number(panel.metadata.runtime || 0) > 0) {
            const mins = Number(panel.metadata.runtime)
            const h = Math.floor(mins / 60)
            const m = mins % 60
            parts.push((h > 0 ? h + "h " : "") + m + "m")
        }
        return parts.join("    ")
    }

    enabled: false

    Rectangle {
        x: 4
        y: 8
        width: Math.max(1, parent.width - 8)
        height: Math.max(1, parent.height - 8)
        radius: 28
        color: "#1C14263A"
        visible: panel.visible
        opacity: 0.55
    }

    // Actual rounded mask source. QtQuick's clip:true is rectangular and does not
    // follow Rectangle.radius, so the artwork must be alpha-masked explicitly.
    Rectangle {
        id: roundedMask
        anchors.fill: card
        radius: 26
        color: "white"
        antialiasing: true
        layer.enabled: true
        z: -100
    }

    Item {
        id: card
        x: 0
        y: 0
        width: Math.max(1, parent.width)
        height: Math.max(1, parent.height)

        Rectangle {
            anchors.fill: parent
            radius: 26
            color: "#09111B"
            antialiasing: true
        }

        Item {
            id: maskedArtwork
            anchors.fill: parent
            layer.enabled: true
            layer.effect: MultiEffect {
                maskEnabled: true
                maskSource: roundedMask
                // A non-zero lower threshold is essential here: with the
                // MultiEffect default (0.0), fully transparent mask pixels are
                // not "below" the threshold and the square source corners can
                // survive outside the rounded border.
                maskThresholdMin: 0.01
                maskThresholdMax: 1.0
                maskSpreadAtMin: 0.0
                maskSpreadAtMax: 0.0
                autoPaddingEnabled: false
            }

            Image {
                id: backdrop
                anchors.fill: parent
                source: panel.backdropSource
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                cache: true
                opacity: status === Image.Ready ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 180 } }
                layer.enabled: panel.posterBackdropFallback
                layer.effect: MultiEffect {
                    blurEnabled: panel.posterBackdropFallback
                    blur: panel.posterBackdropFallback ? 0.16 : 0.0
                    blurMax: 18
                    autoPaddingEnabled: false
                }
            }

            // Global contrast mask.
            Rectangle {
                anchors.fill: parent
                color: panel.posterBackdropFallback ? "#42040A12" : "#24040A12"
            }

            // Reference-like readability mask: preserve artwork on the left,
            // gradually darken the information area on the right.
            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: "#08050C14" }
                    GradientStop { position: 0.38; color: "#1D050C14" }
                    GradientStop { position: 0.66; color: "#9B050C14" }
                    GradientStop { position: 1.0; color: "#E8050C14" }
                }
            }

            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    orientation: Gradient.Vertical
                    GradientStop { position: 0.0; color: "#12000000" }
                    GradientStop { position: 0.56; color: "#04000000" }
                    GradientStop { position: 1.0; color: "#B6000000" }
                }
            }
        }

        Item {
            id: contentLayer
            anchors.fill: parent
            anchors.margins: 26
            visible: panel.ready

            Text {
                id: metaLine
                anchors.top: parent.top
                anchors.right: parent.right
                width: Math.min(parent.width * 0.48, 430)
                horizontalAlignment: Text.AlignRight
                text: panel.metaSummary
                visible: text.length > 0
                color: panel.cardTextMuted
                font.family: panel.premiumFont
                font.pixelSize: 11
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 1.0
                elide: Text.ElideRight
            }

            // One vertical flow owns all right-side content. No independently
            // anchored overview/crew blocks, so long descriptions can never overlap.
            ColumnLayout {
                id: detailsColumn
                anchors.top: parent.top
                anchors.topMargin: panel.posterBackdropFallback ? 86 : 70
                anchors.right: parent.right
                anchors.bottom: footerRow.top
                anchors.bottomMargin: 14
                width: Math.min(parent.width * 0.52, 420)
                spacing: 7

                Item {
                    id: titleBlock
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(94, Math.max(64, card.height * 0.19))

                    Image {
                        id: logo
                        anchors.fill: parent
                        source: panel.metadata.logoUrl ? String(panel.metadata.logoUrl) : ""
                        fillMode: Image.PreserveAspectFit
                        horizontalAlignment: Image.AlignRight
                        verticalAlignment: Image.AlignVCenter
                        asynchronous: true
                        cache: true
                        visible: status === Image.Ready
                    }

                    Text {
                        anchors.fill: parent
                        visible: logo.status !== Image.Ready
                        text: panel.titleText.toUpperCase()
                        color: panel.cardText
                        horizontalAlignment: Text.AlignRight
                        verticalAlignment: Text.AlignVCenter
                        font.family: panel.premiumDisplayFont
                        font.pixelSize: Math.max(24, Math.min(36, card.width * 0.043))
                        font.weight: Font.Light
                        font.letterSpacing: 3.2
                        elide: Text.ElideRight
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: String(panel.metadata.tagline || "").length > 0
                    text: "“" + String(panel.metadata.tagline || "") + "”"
                    color: panel.cardTextMuted
                    horizontalAlignment: Text.AlignRight
                    font.family: panel.premiumFont
                    font.pixelSize: 11
                    font.italic: true
                    elide: Text.ElideRight
                    maximumLineCount: 1
                }

                Text {
                    Layout.fillWidth: true
                    Layout.maximumHeight: 82
                    text: String(panel.metadata.overview || (panel.lang.language === "tr" ? "Konu bilgisi bulunamadı." : "No overview available."))
                    color: panel.cardText
                    horizontalAlignment: Text.AlignRight
                    font.family: panel.premiumFont
                    font.pixelSize: 12
                    font.weight: Font.Normal
                    lineHeight: 1.18
                    wrapMode: Text.Wrap
                    elide: Text.ElideRight
                    maximumLineCount: 3
                }

                Item {
                    Layout.fillHeight: true
                    Layout.minimumHeight: 4
                }

                ColumnLayout {
                    id: crewBlock
                    Layout.fillWidth: true
                    spacing: 4

                    Text {
                        Layout.fillWidth: true
                        visible: panel.tvSeries && (panel.metadata.creators || []).length > 0
                        text: (panel.lang.language === "tr" ? "Yaratıcı  " : "Creator  ")
                              + (panel.metadata.creators || []).join(", ")
                        color: panel.cardTextMuted
                        horizontalAlignment: Text.AlignRight
                        font.family: panel.premiumFont
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: (panel.metadata.directors || []).length > 0
                        text: (panel.lang.language === "tr" ? "Yönetmen  " : "Director  ")
                              + (panel.metadata.directors || []).join(", ")
                        color: panel.cardTextMuted
                        horizontalAlignment: Text.AlignRight
                        font.family: panel.premiumFont
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: (panel.metadata.producers || []).length > 0
                        text: (panel.lang.language === "tr" ? "Yapımcı  " : "Producer  ")
                              + (panel.metadata.producers || []).join(", ")
                        color: panel.cardTextMuted
                        horizontalAlignment: Text.AlignRight
                        font.family: panel.premiumFont
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: (panel.metadata.cast || []).length > 0
                        text: (panel.lang.language === "tr" ? "Oyuncular  " : "Cast  ")
                              + (panel.metadata.cast || []).join(", ")
                        color: panel.cardText
                        horizontalAlignment: Text.AlignRight
                        font.family: panel.premiumFont
                        font.pixelSize: 10
                        elide: Text.ElideRight
                    }
                }
            }

            RowLayout {
                id: footerRow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                spacing: 8

                Text {
                    Layout.fillWidth: true
                    text: panel.itemData ? String(panel.itemData.name || "") : ""
                    color: panel.cardTextFaint
                    font.family: panel.premiumFont
                    font.pixelSize: 9
                    elide: Text.ElideMiddle
                }
                Text {
                    text: "TMDB"
                    color: panel.cardTextMuted
                    font.family: panel.premiumFont
                    font.pixelSize: 9
                    font.weight: Font.DemiBold
                    transform: Translate { y: 7 }
                }
            }
        }

        // Rating + age-certification badges are a direct card overlay instead of
        // participating in the content/footer layout. This keeps them visible in
        // every compact hover size and places them just above the file title.
        Item {
            id: badgeOverlay
            anchors.left: parent.left
            anchors.leftMargin: 26
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 48
            width: badgePositioner.implicitWidth
            height: 58
            z: 90
            visible: panel.ready
                     && (Number(panel.metadata.rating || 0) > 0
                         || String(panel.metadata.certification || "").length > 0)

            Row {
                id: badgePositioner
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                spacing: 8

                Item {
                    id: certificationBadge
                    width: visible ? 58 : 0
                    height: 58
                    visible: String(panel.metadata.certification || "").length > 0

                    Rectangle {
                        anchors.centerIn: parent
                        width: 48
                        height: 48
                        radius: width / 2
                        color: "#B20A121D"
                        border.width: 1
                        border.color: "#4AFFFFFF"
                    }

                    Canvas {
                        id: certificationCanvas
                        anchors.fill: parent
                        antialiasing: true
                        onPaint: {
                            const ctx = getContext("2d")
                            ctx.clearRect(0, 0, width, height)
                            const cx = width / 2
                            const cy = height / 2
                            const r = Math.min(width, height) / 2 - 4
                            ctx.lineWidth = 3.2
                            ctx.lineCap = "round"
                            ctx.strokeStyle = "rgba(255,255,255,0.18)"
                            ctx.beginPath()
                            ctx.arc(cx, cy, r, 0, Math.PI * 2)
                            ctx.stroke()
                            ctx.strokeStyle = "#5FD0FF"
                            ctx.beginPath()
                            ctx.arc(cx, cy, r, -Math.PI / 2, Math.PI * 1.5)
                            ctx.stroke()
                        }
                        Connections {
                            target: panel
                            function onMetadataChanged() { certificationCanvas.requestPaint() }
                        }
                        Component.onCompleted: requestPaint()
                    }

                    Text {
                        anchors.centerIn: parent
                        text: String(panel.metadata.certification || "")
                        color: panel.cardText
                        font.family: panel.premiumFont
                        font.pixelSize: text.length > 4 ? 10 : 13
                        font.weight: Font.DemiBold
                        font.capitalization: Font.AllUppercase
                    }
                }

                Item {
                    id: ratingBadge
                    width: visible ? 58 : 0
                    height: 58
                    visible: Number(panel.metadata.rating || 0) > 0

                    Rectangle {
                        anchors.centerIn: parent
                        width: 48
                        height: 48
                        radius: width / 2
                        color: "#B20A121D"
                        border.width: 1
                        border.color: "#4AFFFFFF"
                    }

                    Canvas {
                        id: ratingCanvas
                        anchors.fill: parent
                        antialiasing: true
                        onPaint: {
                            const ctx = getContext("2d")
                            ctx.clearRect(0, 0, width, height)
                            const cx = width / 2
                            const cy = height / 2
                            const r = Math.min(width, height) / 2 - 4
                            const start = -Math.PI / 2
                            const value = Math.max(0, Math.min(10, Number(panel.metadata.rating || 0)))
                            ctx.lineWidth = 3.2
                            ctx.lineCap = "round"
                            ctx.strokeStyle = "rgba(255,255,255,0.20)"
                            ctx.beginPath()
                            ctx.arc(cx, cy, r, 0, Math.PI * 2)
                            ctx.stroke()
                            ctx.strokeStyle = "#D9D94B"
                            ctx.beginPath()
                            ctx.arc(cx, cy, r, start, start + Math.PI * 2 * (value / 10.0))
                            ctx.stroke()
                        }
                        Connections {
                            target: panel
                            function onMetadataChanged() { ratingCanvas.requestPaint() }
                        }
                        Component.onCompleted: requestPaint()
                    }

                    Text {
                        anchors.centerIn: parent
                        text: Number(panel.metadata.rating || 0).toFixed(1)
                        color: panel.cardText
                        font.family: panel.premiumFont
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                    }
                }
            }
        }

        // Draw the border last, above the artwork and all masks, so loading the
        // backdrop cannot visually erase or cover it.
        Rectangle {
            anchors.fill: parent
            radius: 26
            color: "transparent"
            border.width: 1
            border.color: "#66D5DEEA"
            antialiasing: true
            z: 100
        }

        ColumnLayout {
            visible: !panel.ready
            anchors.centerIn: parent
            width: Math.min(parent.width - 70, 430)
            spacing: 12

            GBusyIndicator {
                Layout.alignment: Qt.AlignHCenter
                visible: panel.state === "loading"
                running: visible
            }
            Text {
                Layout.fillWidth: true
                text: panel.titleText
                color: panel.cardText
                font.family: panel.premiumFont
                font.pixelSize: 21
                font.weight: Font.Bold
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                text: {
                    if (panel.state === "needs_token")
                        return panel.lang.language === "tr"
                                ? "TMDB API Read Access Token ayarlanmadı. Video sayfasındaki TMDB düğmesinden ekleyebilirsin."
                                : "TMDB API Read Access Token is not configured. Add it from the TMDB button on the video page."
                    if (panel.state === "unmatched")
                        return panel.lang.language === "tr"
                                ? "Dosya adına güvenli bir TMDB eşleşmesi bulunamadı."
                                : "No confident TMDB match was found for this filename."
                    if (panel.state === "missing_file")
                        return panel.lang.language === "tr" ? "Video dosyası artık mevcut değil." : "The video file no longer exists."
                    if (panel.state === "error")
                        return String(panel.metadata.message || "TMDB error")
                    return panel.lang.language === "tr" ? "TMDB bilgileri hazırlanıyor…" : "Loading TMDB metadata…"
                }
                color: panel.cardTextMuted
                font.family: panel.premiumFont
                font.pixelSize: 11
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }
}
