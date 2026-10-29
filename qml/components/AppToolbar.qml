import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Ultraview

// Application header: brand mark, product name and the window-chrome actions.
Rectangle {
    id: bar
    implicitHeight: Theme.toolbarHeight
    color: Theme.surface

    property string mode: "control"
    property bool modeSwitchEnabled: true

    signal settingsRequested
    signal aboutRequested
    signal modeRequested(string mode)

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.border
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacing
        anchors.rightMargin: Theme.spacingSmall + 2
        spacing: Theme.spacingSmall + 2

        Rectangle {
            Layout.alignment: Qt.AlignVCenter
            implicitWidth: 30
            implicitHeight: 30
            radius: 9
            gradient: Gradient {
                GradientStop { position: 0.0; color: Theme.accentHover }
                GradientStop { position: 1.0; color: Theme.accentPressed }
            }

            Label {
                anchors.centerIn: parent
                text: "U"
                color: Theme.textInverted
                font.pixelSize: 16
                font.weight: Font.Bold
            }
        }

        ColumnLayout {
            Layout.alignment: Qt.AlignVCenter
            spacing: 1

            Label {
                text: qsTr("UltraViewer")
                color: Theme.text
                font.pixelSize: Theme.fontNormal
                font.weight: Font.DemiBold
            }

            Label {
                text: qsTr("native Linux remote desktop")
                color: Theme.textFaint
                font.pixelSize: Theme.fontTiny
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignVCenter
            Layout.leftMargin: Theme.spacingSmall
            spacing: Theme.spacingXSmall

            AppButton {
                text: qsTr("Control")
                active: bar.mode !== "host"
                enabled: bar.modeSwitchEnabled || bar.mode !== "host"
                onClicked: bar.modeRequested("control")
            }

            AppButton {
                text: qsTr("Host")
                active: bar.mode === "host"
                enabled: bar.modeSwitchEnabled || bar.mode === "host"
                onClicked: bar.modeRequested("host")
            }
        }

        Item { Layout.fillWidth: true }

        IconButton {
            Layout.alignment: Qt.AlignVCenter
            iconSource: Icon.gear
            tooltip: qsTr("Settings")
            onClicked: bar.settingsRequested()
        }

        IconButton {
            Layout.alignment: Qt.AlignVCenter
            iconSource: Icon.info
            tooltip: qsTr("About this client")
            onClicked: bar.aboutRequested()
        }
    }
}
