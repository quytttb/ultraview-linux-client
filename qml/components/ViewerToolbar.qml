import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Ultraview

// Viewer header: session state, status line and the session actions.  Input and
// statistics are toggles, so their buttons carry the `active` (toggled-on) look.
Rectangle {
    id: bar
    implicitHeight: Theme.toolbarHeight - 4
    color: Theme.surface

    signal disconnectRequested
    signal statisticsToggled
    signal inputToggled
    signal fullScreenRequested
    property bool statisticsVisible: false
    property bool inputEnabled: true
    property bool fullScreen: false

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.border
    }

    // In full screen this bar floats over the remote desktop. Consume pointer
    // events in its empty regions so merely revealing it never moves or clicks
    // the partner's top row; the action buttons above it remain interactive.
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.AllButtons
        onWheel: (wheel) => wheel.accepted = true
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacing
        anchors.rightMargin: Theme.spacing
        spacing: Theme.spacingSmall

        StatusBadge {
            Layout.alignment: Qt.AlignVCenter
            stateName: controller.stateName
        }

        Label {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            text: controller.statusText
            color: Theme.textMuted
            font.pixelSize: Theme.fontNormal
            elide: Text.ElideRight
        }

        AppButton {
            Layout.alignment: Qt.AlignVCenter
            text: bar.inputEnabled ? qsTr("Disable input") : qsTr("Enable input")
            iconSource: Icon.keyboard
            ghost: true
            onClicked: bar.inputToggled()
        }

        AppButton {
            Layout.alignment: Qt.AlignVCenter
            text: qsTr("Statistics")
            iconSource: Icon.chart
            ghost: true
            active: bar.statisticsVisible
            onClicked: bar.statisticsToggled()
        }

        AppButton {
            Layout.alignment: Qt.AlignVCenter
            text: bar.fullScreen ? qsTr("Exit full screen") : qsTr("Full screen")
            iconSource: Icon.monitor
            ghost: true
            active: bar.fullScreen
            onClicked: bar.fullScreenRequested()
        }

        AppButton {
            Layout.alignment: Qt.AlignVCenter
            text: qsTr("Disconnect")
            iconSource: Icon.power
            danger: true
            onClicked: bar.disconnectRequested()
        }
    }
}
