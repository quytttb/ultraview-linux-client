import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Ultraview

// The application button.  Variants: primary (accent fill), secondary (default,
// raised surface), ghost (borderless, hover tint) and danger (borderless, red).
// `active` renders the toggled-on look used by the viewer toolbar.
Rectangle {
    id: button

    property bool primary: false
    property bool danger: false
    property bool ghost: false
    property bool active: false
    property string iconSource: ""
    property alias text: label.text
    signal clicked

    readonly property bool hovered: mouse.containsMouse

    implicitHeight: Theme.controlHeight
    implicitWidth: Math.max(content.implicitWidth + Theme.spacing * 2, 84)
    radius: Theme.radiusSmall
    border.width: (button.primary || button.ghost || button.danger || button.active) ? 0 : 1
    border.color: mouse.containsMouse ? Theme.borderStrong : Theme.border

    color: {
        if (!button.enabled)
            return button.primary ? Theme.surfaceAlt : "transparent"
        if (button.danger) {
            if (mouse.pressed)
                return Theme.tinted(Theme.danger, 0.24)
            if (mouse.containsMouse)
                return Theme.tinted(Theme.danger, 0.14)
            return "transparent"
        }
        if (button.active) {
            if (mouse.pressed || mouse.containsMouse)
                return Theme.tinted(Theme.accent, 0.26)
            return Theme.accentSoft
        }
        if (button.ghost) {
            if (mouse.pressed)
                return Theme.surfaceAlt
            if (mouse.containsMouse)
                return Theme.surfaceHover
            return "transparent"
        }
        if (button.primary) {
            if (mouse.pressed)
                return Theme.accentPressed
            if (mouse.containsMouse)
                return Theme.accentHover
            return Theme.accent
        }
        if (mouse.pressed)
            return Theme.surfaceAlt
        if (mouse.containsMouse)
            return Theme.surfaceHover
        return Theme.surfaceAlt
    }

    Behavior on color {
        ColorAnimation { duration: Theme.durationFast }
    }
    Behavior on border.color {
        ColorAnimation { duration: Theme.durationFast }
    }

    RowLayout {
        id: content
        anchors.centerIn: parent
        spacing: Theme.spacingSmall - 2

        Image {
            visible: button.iconSource !== ""
            source: button.iconSource
            sourceSize.width: 17
            sourceSize.height: 17
            Layout.alignment: Qt.AlignVCenter
            opacity: button.enabled ? 1 : 0.4
        }

        Label {
            id: label
            Layout.alignment: Qt.AlignVCenter
            color: {
                if (!button.enabled)
                    return Theme.textFaint
                if (button.danger)
                    return Theme.dangerText
                if (button.primary)
                    return Theme.textInverted
                if (button.active)
                    return Theme.accentText
                return Theme.text
            }
            font.pixelSize: Theme.fontNormal
            font.weight: Font.Medium
            elide: Text.ElideRight
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        enabled: button.enabled
        onClicked: button.clicked()
    }
}
