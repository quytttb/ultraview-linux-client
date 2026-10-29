import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Ultraview

// Viewer header: session state, status line and the session actions.  Input and
// statistics are toggles, so their buttons carry the `active` (toggled-on) look.
// The peer tools (chat, transfers, sound, session actions) are icon buttons with
// a badge when they need attention, so the bar stays usable at narrow widths.
Rectangle {
    id: bar
    implicitHeight: Theme.toolbarHeight - 4
    color: Theme.surface

    signal disconnectRequested
    signal statisticsToggled
    signal inputToggled
    signal fullScreenRequested
    signal chatToggled
    signal transfersToggled
    property bool statisticsVisible: false
    property bool inputEnabled: true
    property bool fullScreen: false
    property bool chatVisible: false
    property bool transfersVisible: false
    property int chatUnread: 0
    property int transferActive: 0
    property bool devTools: false

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

        // --- peer tools -----------------------------------------------------
        IconButton {
            id: chatButton
            Layout.alignment: Qt.AlignVCenter
            iconSource: Icon.chat
            tooltip: bar.chatUnread > 0 ? qsTr("Chat (%1 unread)").arg(bar.chatUnread)
                                        : qsTr("Chat")
            active: bar.chatVisible
            onClicked: bar.chatToggled()

            CountBadge {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.rightMargin: 1
                anchors.topMargin: 1
                count: bar.chatUnread
            }
        }

        IconButton {
            Layout.alignment: Qt.AlignVCenter
            iconSource: Icon.folder
            tooltip: qsTr("File transfers")
            active: bar.transfersVisible
            onClicked: bar.transfersToggled()

            CountBadge {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.rightMargin: 1
                anchors.topMargin: 1
                count: bar.transferActive
                highlight: controller.transferPending
            }
        }

        IconButton {
            Layout.alignment: Qt.AlignVCenter
            iconSource: controller.audioEnabled ? Icon.volume : Icon.volumeOff
            tooltip: controller.audioEnabled ? qsTr("Stop the partner sound")
                                             : qsTr("Play the partner sound")
            active: controller.audioEnabled
            enabled: controller.connected
            onClicked: controller.toggleAudio()
        }

        IconButton {
            id: toolsButton
            Layout.alignment: Qt.AlignVCenter
            iconSource: Icon.gear
            tooltip: qsTr("Session tools")
            active: toolsPanel.visible
            onClicked: toolsPanel.visible ? toolsPanel.close() : toolsPanel.open()
        }

        IconButton {
            Layout.alignment: Qt.AlignVCenter
            iconSource: Icon.keyboard
            tooltip: bar.inputEnabled ? qsTr("Disable input") : qsTr("Enable input")
            active: !bar.inputEnabled
            onClicked: bar.inputToggled()
        }

        IconButton {
            Layout.alignment: Qt.AlignVCenter
            iconSource: Icon.chart
            tooltip: qsTr("Statistics")
            active: bar.statisticsVisible
            onClicked: bar.statisticsToggled()
        }

        IconButton {
            Layout.alignment: Qt.AlignVCenter
            iconSource: Icon.monitor
            tooltip: bar.fullScreen ? qsTr("Exit full screen") : qsTr("Full screen")
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

    onDevToolsChanged: {
        if (devTools)
            toolsPanel.open()
    }

    Component.onCompleted: {
        if (devTools)
            toolsPanel.open()
    }

    ToolsPanel {
        id: toolsPanel
        y: bar.height - 2
        x: Math.max(Theme.spacing, Math.min(bar.width - width - Theme.spacing,
                                           toolsButton.mapToItem(bar, 0, 0).x
                                               + toolsButton.width - width))
    }
}
