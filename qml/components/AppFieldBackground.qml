import QtQuick
import Ultraview

// The rounded input surface shared by the text fields: sunken at rest, accent
// border plus a soft halo while the field holds focus.
Rectangle {
    id: frame

    property bool focused: false
    property bool hovered: false

    radius: Theme.radiusSmall
    color: frame.focused ? Theme.surfaceAlt : Theme.surfaceSunken
    border.width: 1
    border.color: frame.focused ? Theme.accent : (frame.hovered ? Theme.borderStrong : Theme.border)

    Behavior on color {
        ColorAnimation { duration: Theme.durationFast }
    }
    Behavior on border.color {
        ColorAnimation { duration: Theme.durationFast }
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: -3
        radius: frame.radius + 3
        color: Theme.focusRing
        opacity: frame.focused ? 1 : 0
        Behavior on opacity {
            NumberAnimation { duration: Theme.durationFast }
        }
    }
}
