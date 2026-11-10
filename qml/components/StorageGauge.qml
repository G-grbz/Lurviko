import QtQuick
import QtQuick.Shapes
import Lurviko.App

// Ring gauge: a track and a rounded arc for `value` (0..1), with a centred
// label. The arc turns amber above 80 % and red above 92 %.
Item {
    id: gauge
    property real value: 0
    property real thickness: 10
    property string centerText: Math.round(Math.max(0, Math.min(1, value)) * 100) + "%"
    property string captionText: ""
    readonly property real clamped: Math.max(0, Math.min(1, value))
    readonly property color arcColor: clamped >= 0.92 ? AppTheme.danger
                                    : clamped >= 0.80 ? "#E8A23A" : AppTheme.accent

    implicitWidth: 112
    implicitHeight: 112

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            fillColor: "transparent"
            strokeColor: AppTheme.dark ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(0.05, 0.08, 0.19, 0.08)
            strokeWidth: gauge.thickness
            capStyle: ShapePath.RoundCap
            PathAngleArc {
                centerX: gauge.width / 2; centerY: gauge.height / 2
                radiusX: (Math.min(gauge.width, gauge.height) - gauge.thickness) / 2
                radiusY: radiusX
                startAngle: 135
                sweepAngle: 270
            }
        }
        ShapePath {
            fillColor: "transparent"
            strokeColor: gauge.arcColor
            strokeWidth: gauge.thickness
            capStyle: ShapePath.RoundCap
            PathAngleArc {
                centerX: gauge.width / 2; centerY: gauge.height / 2
                radiusX: (Math.min(gauge.width, gauge.height) - gauge.thickness) / 2
                radiusY: radiusX
                startAngle: 135
                sweepAngle: Math.max(0.5, 270 * gauge.clamped)
                Behavior on sweepAngle { NumberAnimation { duration: 420; easing.type: Easing.OutCubic } }
            }
        }
    }
    Column {
        anchors.centerIn: parent
        spacing: 0
        GText {
            anchors.horizontalCenter: parent.horizontalCenter
            text: gauge.centerText
            baseColor: AppTheme.text
            basePixelSize: Math.max(12, Math.round(gauge.width * 0.2))
            baseFontWeight: Font.DemiBold
        }
        GText {
            visible: gauge.captionText.length > 0
            anchors.horizontalCenter: parent.horizontalCenter
            text: gauge.captionText
            baseColor: AppTheme.textMuted
            basePixelSize: 9
        }
    }
}
