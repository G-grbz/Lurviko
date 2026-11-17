import QtQuick
import QtQuick.Controls
import QtTest
import Lurviko.App
import Lurviko.Backend

Item {
    width: 760
    height: 900
    ApplicationWindow {
        id: window
        width: 760; height: 900; visible: true
        color: AppTheme.background
        LanguageManager { id: language; language: "tr" }
        ChecksumPanel {
            id: panel
            x: 16; y: 16; width: 700
            lang: language
            fileSize: 1024
        }
        TestCase {
            name: "ChecksumPanel"
            when: windowShown
            function calculator() { return findChild(panel, "checksumCalculator") }

            function test_auto_compute_and_verdict() {
                panel.location = String(Qt.resolvedUrl("fixtures/landscape.svg"))
                tryVerify(function() { return calculator().results["SHA256"] !== undefined }, 5000)
                for (const name of ["MD5", "SHA1", "SHA256", "SHA512"])
                    verify(String(calculator().results[name]).length > 0)
                verify(calculator().results["BLAKE2b-512"] === undefined)

                const field = findChild(panel, "checksumExpectedField")
                const sha = calculator().results["SHA256"]
                field.text = sha.toUpperCase() + "  landscape.svg"
                tryCompare(panel, "verdict", "match")
                compare(panel.matched, "SHA256")
                tryVerify(function() { return findChild(panel, "checksumRow_SHA256").isMatch })

                field.text = (sha[0] === "0" ? "1" : "0") + sha.substring(1)
                tryCompare(panel, "verdict", "mismatch")

                // A BLAKE2b value starts the one extra pass it needs.
                panel.showAll = true
                field.text = "b2:" + "0".repeat(128)
                tryVerify(function() { return calculator().results["BLAKE2b-512"] !== undefined }, 5000)
                tryCompare(panel, "verdict", "mismatch")
                field.text = "not a checksum"
                compare(panel.verdict, "")
                verify(!panel.expectedValid)
            }

            function test_closing_clears_and_cancels() {
                panel.location = ""
                compare(calculator().path, "")
                verify(!calculator().busy)
                compare(Object.keys(calculator().results).length, 0)
            }
        }
    }
}
