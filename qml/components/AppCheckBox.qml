import QtQuick
import QtQuick.Controls
import Ultraview

// Check box with the theme's own indicator: the Basic style indicator cannot be
// recoloured through the palette, so it is drawn here instead.
CheckBox {
    id: control
    spacing: Theme.spacingSmall + 2
    font.pixelSize: Theme.fontNormal
    topPadding: 0
    bottomPadding: 0

    indicator: Rectangle {
        implicitWidth: 20
        implicitHeight: 20
        x: control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        radius: 6
        color: control.checked ? Theme.accent : Theme.surfaceSunken
        border.width: 1
        border.color: control.checked ? Theme.accent
                                      : (control.hovered ? Theme.borderStrong : Theme.border)

        Behavior on color {
            ColorAnimation { duration: Theme.durationFast }
        }
        Behavior on border.color {
            ColorAnimation { duration: Theme.durationFast }
        }

        Image {
            anchors.centerIn: parent
            source: Icon.check
            sourceSize.width: 13
            sourceSize.height: 13
            opacity: control.checkState === Qt.Checked ? 1 : 0
            scale: control.checkState === Qt.Checked ? 1 : 0.6
            Behavior on opacity {
                NumberAnimation { duration: Theme.durationFast }
            }
            Behavior on scale {
                NumberAnimation { duration: Theme.durationFast }
            }
        }
    }

    contentItem: Label {
        text: control.text
        color: control.enabled ? Theme.text : Theme.textFaint
        font.pixelSize: Theme.fontNormal
        leftPadding: control.indicator.width + control.spacing
        rightPadding: control.rightPadding
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
