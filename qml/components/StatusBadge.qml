import QtQuick
import QtQuick.Controls
import Ultraview

// Session state pill: soft tinted background, status dot and label.
Rectangle {
    id: badge

    property string stateName: "Disconnected"
    readonly property color themeColor: Theme.statusColor(stateName)
    function displayState(state) {
        switch (state) {
        case "Disconnected": return qsTr("Disconnected")
        case "Connecting": return qsTr("Connecting")
        case "Registering": return qsTr("Registering")
        case "RequestingPartner": return qsTr("Requesting partner")
        case "RelayConnecting": return qsTr("Connecting to relay")
        case "Authenticating": return qsTr("Authenticating")
        case "Connected": return qsTr("Connected")
        case "Disconnecting": return qsTr("Disconnecting")
        case "Failed": return qsTr("Failed")
        case "Not hosting": return qsTr("Not hosting")
        case "Preparing screen sharing": return qsTr("Preparing screen sharing")
        case "Waiting for a viewer": return qsTr("Waiting for a viewer")
        case "Preparing incoming connection": return qsTr("Preparing incoming connection")
        case "Waiting for local approval": return qsTr("Waiting for local approval")
        case "Starting viewer": return qsTr("Starting viewer")
        case "Sharing": return qsTr("Sharing")
        case "Host mode failed": return qsTr("Host mode failed")
        default: return state
        }
    }

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
            text: badge.displayState(badge.stateName)
            color: badge.themeColor
            font.pixelSize: Theme.fontSmall
            font.weight: Font.DemiBold
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
