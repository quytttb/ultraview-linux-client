import QtQuick
import QtQuick.Controls
import Ultraview

// Session state pill: soft tinted background, status dot and label.
Rectangle {
    id: badge

    property string stateName: "Disconnected"
    readonly property color themeColor: Theme.statusColor(stateName)

    implicitWidth: row.implicitWidth + Theme.spacing + Theme.spacingSmall
    implicitHeight: 24
    radius: Theme.radiusPill
    color: Theme.tinted(badge.themeColor, 0.14)
    border.width: 1
    border.color: Theme.tinted(badge.themeColor, 0.35)

    Row {
        id: row
        anchors.centerIn: parent
        spacing: Theme.spacingSmall - 2

        Rectangle {
            width: 7
            height: 7
            radius: width / 2
            color: badge.themeColor
            anchors.verticalCenter: parent.verticalCenter
        }

        Label {
            text: badge.stateName
            color: badge.themeColor
            font.pixelSize: Theme.fontSmall
            font.weight: Font.DemiBold
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
