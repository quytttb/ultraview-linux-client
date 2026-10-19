import QtQuick
import QtQuick.Controls
import Ultraview

// SpinBox with the theme's own surface and step buttons.  The Basic style spin
// buttons look foreign next to the rest of the dialog, so they are redrawn.
SpinBox {
    id: control
    implicitHeight: Theme.controlHeight
    font.pixelSize: Theme.fontNormal
    // The template derives the control padding from the step buttons (30 px on
    // both sides in this Qt version); the content item handles its own insets.
    leftPadding: 0
    rightPadding: 0

    contentItem: TextInput {
        text: control.displayText
        color: control.enabled ? Theme.text : Theme.textFaint
        selectionColor: Theme.accent
        selectedTextColor: Theme.textInverted
        font: control.font
        leftPadding: Theme.spacing
        rightPadding: 2 * control.up.indicator.width + Theme.spacingSmall
        horizontalAlignment: Qt.AlignLeft
        verticalAlignment: Text.AlignVCenter
        clip: true
        selectByMouse: true
        readOnly: !control.editable
        validator: control.validator
        inputMethodHints: Qt.ImhFormattedNumbersOnly
    }

    background: AppFieldBackground {
        focused: control.activeFocus
        hovered: control.hovered
    }

    up.indicator: Rectangle {
        x: control.mirrored ? 0 : control.width - width
        y: 0
        width: 30
        height: control.height / 2
        radius: Theme.radiusSmall
        color: control.up.pressed ? Theme.surfaceAlt
                                  : (control.up.hovered ? Theme.surfaceHover : "transparent")
        Behavior on color {
            ColorAnimation { duration: Theme.durationFast }
        }

        Image {
            anchors.centerIn: parent
            source: Icon.chevronUp
            sourceSize.width: 13
            sourceSize.height: 13
            opacity: control.enabled ? 1 : 0.4
        }
    }

    down.indicator: Rectangle {
        x: control.mirrored ? 0 : control.width - width
        y: control.height - height
        width: 30
        height: control.height / 2
        radius: Theme.radiusSmall
        color: control.down.pressed ? Theme.surfaceAlt
                                    : (control.down.hovered ? Theme.surfaceHover : "transparent")
        Behavior on color {
            ColorAnimation { duration: Theme.durationFast }
        }

        Image {
            anchors.centerIn: parent
            source: Icon.chevronDown
            sourceSize.width: 13
            sourceSize.height: 13
            opacity: control.enabled ? 1 : 0.4
        }
    }
}
