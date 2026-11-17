import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Lurviko.Backend
import Lurviko.App

// "Checksums" tab of the file properties. Beyond Dolphin's four algorithms
// and a paste field: every requested hash in one read of the file with
// progress, speed and cancel; SHA-224/384, SHA3, BLAKE2 and CRC32; pasted
// values are recognised in their usual forms ("hash  name", BSD, SRI) and
// checked automatically; verification files next to the file are found and
// can be written.
ColumnLayout {
    id: panel
    property var lang
    // Local file to hash; "" while the dialog is closed or not applicable.
    property string location: ""
    property var textEditMenu: null
    // Size of the file: small files get the common checksums right away.
    property double fileSize: 0
    readonly property double autoComputeLimit: 512 * 1024 * 1024
    signal notify(string message)
    readonly property bool tr: lang && lang.language === "tr"
    function t(trText, enText) { return tr ? trText : enText }

    property bool showAll: false
    readonly property var parsed: calculator.identify(expectedField.text)
    readonly property bool expectedValid: !!parsed.valid
    readonly property string verdict: {
        const revision = calculator.results
        return expectedValid ? calculator.compare(expectedField.text) : ""
    }
    readonly property string matched: {
        const revision = calculator.results
        return expectedValid ? calculator.matchingAlgorithm(expectedField.text) : ""
    }
    readonly property var visibleAlgorithms: showAll ? calculator.algorithms() : calculator.defaultAlgorithms()
    readonly property string fileName: {
        const parts = String(location).split("/")
        return decodeURIComponent(parts[parts.length - 1] || "")
    }

    spacing: 14

    function formatBytes(bytes) {
        const n = Number(bytes || 0)
        if (n < 1024) return n + " B"
        if (n < 1024 * 1024) return (n / 1024).toFixed(1) + " KiB"
        if (n < 1024 * 1024 * 1024) return (n / (1024 * 1024)).toFixed(1) + " MiB"
        return (n / (1024 * 1024 * 1024)).toFixed(2) + " GiB"
    }
    function copyText(text, what) {
        clipboardHelper.text = text
        clipboardHelper.selectAll()
        clipboardHelper.copy()
        clipboardHelper.text = ""
        panel.notify(what + panel.t(" panoya kopyalandı.", " copied to the clipboard."))
    }
    function verifyAgainst(sidecar) {
        const algorithm = String(sidecar.algorithm || "")
        expectedField.text = (algorithm.length ? algorithm + ":" : "") + String(sidecar.expected || "")
    }

    onLocationChanged: {
        calculator.path = location
        expectedField.text = ""
        if (!location.length)
            return
        calculator.findSidecars()
        if (fileSize > 0 && fileSize <= autoComputeLimit)
            calculator.compute(calculator.defaultAlgorithms())
    }

    // The size can arrive after the tab opened (statistics are async).
    onFileSizeChanged: {
        if (location.length && fileSize > 0 && fileSize <= autoComputeLimit
                && !calculator.busy && Object.keys(calculator.results).length === 0)
            calculator.compute(calculator.defaultAlgorithms())
    }

    TextEdit { id: clipboardHelper; visible: false }

    ChecksumCalculator {
        id: calculator
        objectName: "checksumCalculator"
    }

    // Pasted values are checked once typing pauses.
    Timer {
        id: autoCompute
        interval: 250
        onTriggered: if (panel.expectedValid && panel.location.length) calculator.compute(panel.parsed.candidates)
    }

    // --- Verify -----------------------------------------------------------
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: verifyColumn.implicitHeight + 30
        radius: 16
        color: AppTheme.surfaceRaised
        border.color: panel.verdict === "match" ? AppTheme.success
                    : panel.verdict === "mismatch" ? AppTheme.danger : AppTheme.border
        border.width: panel.verdict.length ? 2 : 1

        ColumnLayout {
            id: verifyColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 15
            spacing: 10

            GText {
                Layout.fillWidth: true
                text: panel.t("Dosyayı indirdiğin sitenin verdiği sağlama toplamını yapıştır; türü tanınır ve otomatik doğrulanır. \"hash  dosya\", \"SHA256 (dosya) = …\" ve \"sha256-…\" biçimleri de olur.",
                              "Paste the checksum published where you downloaded the file; its type is recognised and checked automatically. \"hash  file\", \"SHA256 (file) = …\" and \"sha256-…\" forms work too.")
                wrapMode: Text.Wrap
                baseColor: AppTheme.textMuted
                basePixelSize: 11
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                GTextField {
                    id: expectedField
                    objectName: "checksumExpectedField"
                    Layout.fillWidth: true
                    maximumLength: 4096
                    baseFontFamily: "monospace"
                    placeholderText: panel.t("Beklenen sağlama toplamı (MD5, SHA1, SHA256, SHA512, …)",
                                             "Expected checksum (MD5, SHA1, SHA256, SHA512, …)")
                    selectByMouse: true
                    persistentSelection: true
                    onTextChanged: autoCompute.restart()
                    EditorContextMenu { menu: panel.textEditMenu }
                }
                GModalButton {
                    objectName: "checksumPaste"
                    text: panel.t("Yapıştır", "Paste")
                    onClicked: { expectedField.selectAll(); expectedField.paste() }
                }
                GModalButton {
                    visible: expectedField.text.length > 0
                    text: panel.t("Temizle", "Clear")
                    onClicked: expectedField.text = ""
                }
            }

            // Verdict.
            RowLayout {
                Layout.fillWidth: true
                visible: expectedField.text.trim().length > 0
                spacing: 10
                Rectangle {
                    Layout.preferredWidth: 30
                    Layout.preferredHeight: 30
                    radius: 15
                    color: panel.verdict === "match" ? AppTheme.success
                         : panel.verdict === "mismatch" ? AppTheme.danger
                         : panel.expectedValid ? AppTheme.accentSoft : AppTheme.surfaceHover
                    GText {
                        anchors.centerIn: parent
                        text: panel.verdict === "match" ? "✓" : panel.verdict === "mismatch" ? "✕"
                            : panel.expectedValid ? "…" : "?"
                        baseColor: panel.verdict.length ? "white" : AppTheme.textMuted
                        basePixelSize: 15
                        baseFontWeight: Font.Bold
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    GText {
                        objectName: "checksumVerdict"
                        Layout.fillWidth: true
                        text: !panel.expectedValid ? panel.t("Tanınmayan biçim", "Unrecognised format")
                            : panel.verdict === "match" ? panel.t("Eşleşiyor — dosya doğrulandı", "Match — the file is verified")
                            : panel.verdict === "mismatch" ? panel.t("Eşleşmiyor — dosya farklı veya bozuk", "Mismatch — the file differs or is damaged")
                            : panel.t("Hesaplanıyor…", "Calculating…")
                        baseColor: panel.verdict === "match" ? AppTheme.success
                                 : panel.verdict === "mismatch" ? AppTheme.danger : AppTheme.text
                        basePixelSize: 13
                        baseFontWeight: Font.DemiBold
                    }
                    GText {
                        Layout.fillWidth: true
                        visible: panel.expectedValid
                        text: (panel.matched.length ? panel.matched
                               : panel.t("Algılanan: ", "Detected: ") + (panel.parsed.candidates || []).join(panel.t(" veya ", " or ")))
                              + (String(panel.parsed.fileName || "").length && String(panel.parsed.fileName).split("/").pop() !== panel.fileName
                                 ? panel.t("  ·  Uyarı: yapıştırılan satır başka bir dosyayı adlandırıyor (", "  ·  Note: the pasted line names another file (")
                                   + panel.parsed.fileName + ")"
                                 : "")
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        baseColor: AppTheme.textMuted
                        basePixelSize: 10
                    }
                }
            }

            // Verification files next to the file.
            Repeater {
                model: calculator.sidecars
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredHeight: sidecarRow.implicitHeight + 16
                    radius: 10
                    color: AppTheme.accentSoft
                    border.color: AppTheme.accentBorder
                    RowLayout {
                        id: sidecarRow
                        anchors.fill: parent
                        anchors.margins: 8
                        anchors.leftMargin: 12
                        spacing: 8
                        GText {
                            Layout.fillWidth: true
                            text: panel.t("Doğrulama dosyası bulundu: ", "Verification file found: ")
                                  + modelData.fileName + "  ·  " + modelData.algorithm
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            baseColor: AppTheme.text
                            basePixelSize: 11
                        }
                        GModalButton {
                            objectName: "checksumUseSidecar"
                            primary: true
                            text: panel.t("Doğrula", "Verify")
                            onClicked: panel.verifyAgainst(modelData)
                        }
                    }
                }
            }
        }
    }

    // --- Checksums --------------------------------------------------------
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: hashColumn.implicitHeight + 30
        radius: 16
        color: AppTheme.surfaceRaised
        border.color: AppTheme.border

        ColumnLayout {
            id: hashColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 15
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                GText {
                    Layout.fillWidth: true
                    text: panel.t("Sağlama toplamları", "Checksums")
                    baseColor: AppTheme.text
                    basePixelSize: 13
                    baseFontWeight: Font.DemiBold
                }
                GModalButton {
                    objectName: "checksumComputeAll"
                    primary: true
                    text: panel.t("Tümünü hesapla", "Calculate all")
                    enabled: panel.location.length > 0
                             && panel.visibleAlgorithms.some(function(name) { return calculator.results[name] === undefined })
                    onClicked: calculator.compute(panel.visibleAlgorithms)
                }
                GModalButton {
                    text: panel.t("Tümünü kopyala", "Copy all")
                    enabled: Object.keys(calculator.results).length > 0
                    onClicked: panel.copyText(calculator.summaryText(), panel.t("Sağlama toplamları", "Checksums"))
                }
            }

            // Progress of the single pass.
            ColumnLayout {
                Layout.fillWidth: true
                visible: calculator.busy
                spacing: 4
                RowLayout {
                    Layout.fillWidth: true
                    GProgressBar {
                        objectName: "checksumProgress"
                        Layout.fillWidth: true
                        from: 0; to: 1
                        value: calculator.progress
                    }
                    GModalButton {
                        text: panel.t("İptal", "Cancel")
                        onClicked: calculator.cancel()
                    }
                }
                GText {
                    Layout.fillWidth: true
                    text: Math.round(calculator.progress * 100) + "%  ·  "
                          + panel.formatBytes(calculator.bytesDone) + " / " + panel.formatBytes(calculator.bytesTotal)
                          + (calculator.bytesPerSecond > 0 ? "  ·  " + panel.formatBytes(calculator.bytesPerSecond) + "/s" : "")
                          + "  ·  " + calculator.pending.join(", ")
                    baseColor: AppTheme.textMuted
                    basePixelSize: 10
                    elide: Text.ElideRight
                }
            }

            GText {
                Layout.fillWidth: true
                visible: calculator.error.length > 0
                text: calculator.error
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                baseColor: AppTheme.danger
                basePixelSize: 11
            }

            Repeater {
                model: panel.visibleAlgorithms
                delegate: Rectangle {
                    id: hashRow
                    objectName: "checksumRow_" + modelData
                    required property string modelData
                    readonly property string value: calculator.results[modelData] || ""
                    readonly property bool working: calculator.busy && calculator.pending.indexOf(modelData) >= 0
                    readonly property bool isMatch: panel.matched === modelData
                    Layout.fillWidth: true
                    Layout.preferredHeight: 48
                    radius: 10
                    color: isMatch ? Qt.rgba(AppTheme.success.r, AppTheme.success.g, AppTheme.success.b, 0.10) : AppTheme.surface
                    border.color: isMatch ? AppTheme.success : AppTheme.border

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 6
                        spacing: 8
                        GText {
                            Layout.preferredWidth: 96
                            text: hashRow.modelData
                            baseColor: hashRow.isMatch ? AppTheme.success : AppTheme.text
                            basePixelSize: 11
                            baseFontWeight: Font.DemiBold
                        }
                        GTextField {
                            objectName: "checksumValue_" + hashRow.modelData
                            Layout.fillWidth: true
                            visible: hashRow.value.length > 0
                            readOnly: true
                            text: hashRow.value
                            baseFontFamily: "monospace"
                            basePixelSize: 11
                            selectByMouse: true
                            persistentSelection: true
                            EditorContextMenu { menu: panel.textEditMenu }
                        }
                        GText {
                            Layout.fillWidth: true
                            visible: hashRow.value.length === 0
                            text: hashRow.working ? panel.t("Hesaplanıyor…", "Calculating…") : ""
                            baseColor: AppTheme.textMuted
                            basePixelSize: 11
                        }
                        GModalButton {
                            visible: hashRow.value.length === 0 && !hashRow.working
                            text: panel.t("Hesapla", "Calculate")
                            enabled: panel.location.length > 0
                            onClicked: calculator.compute([hashRow.modelData])
                        }
                        GToolButton {
                            visible: hashRow.value.length > 0
                            implicitWidth: 30; implicitHeight: 30
                            icon.source: AppTheme.icon("edit-copy.svg")
                            icon.width: 14; icon.height: 14
                            onClicked: panel.copyText(hashRow.value, hashRow.modelData)
                            Accessible.name: panel.t("Kopyala", "Copy")
                            GToolTip { text: panel.t("Kopyala", "Copy") }
                        }
                        GToolButton {
                            visible: hashRow.value.length > 0
                            implicitWidth: 30; implicitHeight: 30
                            icon.source: AppTheme.icon("plus.svg")
                            icon.width: 14; icon.height: 14
                            onClicked: {
                                const problem = calculator.writeSidecar(hashRow.modelData)
                                panel.notify(problem.length ? problem
                                    : panel.fileName + "." + calculator.extensionFor(hashRow.modelData)
                                      + panel.t(" oluşturuldu.", " created."))
                            }
                            Accessible.name: panel.t("Doğrulama dosyası oluştur", "Create verification file")
                            GToolTip {
                                text: panel.t("Doğrulama dosyası oluştur: ", "Create verification file: ")
                                      + panel.fileName + "." + calculator.extensionFor(hashRow.modelData)
                            }
                        }
                    }
                }
            }

            GButton {
                objectName: "checksumMoreAlgorithms"
                Layout.alignment: Qt.AlignLeft
                text: panel.showAll ? panel.t("Daha az algoritma", "Fewer algorithms")
                                    : panel.t("Diğer algoritmalar (SHA-224/384, SHA3, BLAKE2, CRC32)",
                                              "More algorithms (SHA-224/384, SHA3, BLAKE2, CRC32)")
                onClicked: panel.showAll = !panel.showAll
                background: Rectangle { radius: 8; color: parent.hovered ? AppTheme.surfaceHover : "transparent" }
            }
        }
    }
}
