import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Ultraview

// Tab button with an accent underline instead of the Basic style chrome.
TabButton {
    id: control
    implicitWidth: content.implicitWidth + Theme.spacing + Theme.spacingSmall
    implicitHeight: 38
    font.pixelSize: Theme.fontNormal

    contentItem: Label {
        id: content
        text: control.text
        color: control.checked ? Theme.text : Theme.textMuted
        font.pixelSize: Theme.fontNormal
        font.weight: control.checked ? Font.DemiBold : Font.Medium
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    background: Item {
        Rectangle {
            anchors.fill: parent
            anchors.bottomMargin: 2
            radius: Theme.radiusSmall
            color: control.hovered && !control.checked ? Theme.surfaceHover : "transparent"
            Behavior on color {
                ColorAnimation { duration: Theme.durationFast }
            }
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 2
            radius: 1
            color: Theme.accent
            opacity: control.checked ? 1 : 0
            Behavior on opacity {
                NumberAnimation { duration: Theme.durationFast }
            }
        }
    }
}
