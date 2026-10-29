import QtQuick
import QtQuick.Controls
import Ultraview

// Square, borderless icon button with a themed tooltip.  Used by the window
// chrome (settings/about), the password reveal toggle and the dialog close
// button, so those actions stay compact instead of growing a text label.
Rectangle {
    id: button

    property string iconSource: ""
    property string tooltip: ""
    property bool danger: false
    property bool active: false
    property int iconSize: 18
    property int size: Theme.controlHeight - 4
    signal clicked

    implicitWidth: size
    implicitHeight: size
    radius: Theme.radiusSmall

    color: {
        if (!button.enabled)
            return "transparent"
        if (button.active)
            return mouse.containsMouse ? Theme.tinted(Theme.accent, 0.26) : Theme.accentSoft
        if (button.danger) {
            if (mouse.pressed)
                return Theme.tinted(Theme.danger, 0.24)
            if (mouse.containsMouse)
                return Theme.tinted(Theme.danger, 0.14)
            return "transparent"
        }
        if (mouse.pressed)
            return Theme.surfaceAlt
        if (mouse.containsMouse)
            return Theme.surfaceHover
        return "transparent"
    }

    Behavior on color {
        ColorAnimation { duration: Theme.durationFast }
    }

    Image {
        anchors.centerIn: parent
        source: button.iconSource
        sourceSize.width: button.iconSize
        sourceSize.height: button.iconSize
        opacity: button.enabled ? 1 : 0.4
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        enabled: button.enabled
        onClicked: button.clicked()
    }

    Timer {
        id: tipTimer
        interval: 450
        running: mouse.containsMouse && button.tooltip.length > 0
        repeat: false
        property bool fired: false
        onRunningChanged: {
            if (!running)
                fired = false
        }
        onTriggered: fired = true
    }

    ToolTip {
        id: tip
        text: button.tooltip
        visible: tipTimer.fired
        background: Rectangle {
            color: Theme.surfaceHover
            radius: 6
            border.width: 1
            border.color: Theme.borderStrong
        }
        contentItem: Text {
            text: tip.text
            color: Theme.text
            font.pixelSize: Theme.fontSmall
        }
    }
}
