import QtQuick
import QtQuick.Shapes
import Ultraview

// Accent ring spinner.  The Basic style BusyIndicator draws Qt's default dotted
// circle, which does not match the rest of the interface.
Item {
    id: spinner
    property bool running: true
    implicitWidth: 34
    implicitHeight: 34
    opacity: running ? 1 : 0

    Behavior on opacity {
        NumberAnimation { duration: Theme.duration }
    }

    Shape {
        anchors.fill: parent
        layer.enabled: true
        layer.samples: 4

        ShapePath {
            strokeColor: Theme.tinted(Theme.accent, 0.18)
            strokeWidth: 3
            fillColor: "transparent"

            PathAngleArc {
                centerX: spinner.width / 2
                centerY: spinner.height / 2
                radiusX: spinner.width / 2 - 2.5
                radiusY: spinner.height / 2 - 2.5
                startAngle: 0
                sweepAngle: 360
            }
        }

        ShapePath {
            strokeColor: Theme.accent
            strokeWidth: 3
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap

            PathAngleArc {
                centerX: spinner.width / 2
                centerY: spinner.height / 2
                radiusX: spinner.width / 2 - 2.5
                radiusY: spinner.height / 2 - 2.5
                startAngle: -90
                sweepAngle: 270
            }
        }
    }

    RotationAnimator {
        target: spinner
        from: 0
        to: 360
        duration: 900
        loops: Animation.Infinite
        running: spinner.running
    }
}
