import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Ultraview

// Application header: brand mark, product name and the window-chrome actions.
Rectangle {
    id: bar
    implicitHeight: Theme.toolbarHeight
    color: Theme.surface

    property string mode: "control"
    property bool modeSwitchEnabled: true
    property var windowRef: null
    readonly property bool maximized: windowRef && windowRef.visibility === Window.Maximized

    signal settingsRequested
    signal aboutRequested
    signal modeRequested(string mode)

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.border
    }

    MouseArea {
        anchors.fill: parent
        onPressed: function(mouse) {
            if (mouse.button === Qt.LeftButton && bar.windowRef)
                bar.windowRef.startSystemMove()
        }
        onDoubleClicked: function(mouse) {
            if (mouse.button === Qt.LeftButton && bar.windowRef) {
                if (bar.maximized)
                    bar.windowRef.showNormal()
                else
                    bar.windowRef.showMaximized()
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacing
        anchors.rightMargin: Theme.spacingSmall + 2
        spacing: Theme.spacingSmall + 2

        Image {
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: 30
            Layout.preferredHeight: 30
            source: Qt.resolvedUrl("../assets/icons/ultraview.svg")
            sourceSize: Qt.size(60, 60)
            fillMode: Image.PreserveAspectFit
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

        Rectangle {
            Layout.alignment: Qt.AlignVCenter
            Layout.leftMargin: Theme.spacingXSmall
            implicitWidth: 1
            implicitHeight: 22
            color: Theme.border
        }

        IconButton {
            iconSource: Icon.windowMinimize
            tooltip: qsTr("Minimize")
            onClicked: if (bar.windowRef) bar.windowRef.showMinimized()
        }
        IconButton {
            iconSource: bar.maximized ? Icon.windowRestore : Icon.windowMaximize
            tooltip: bar.maximized ? qsTr("Restore") : qsTr("Maximize")
            onClicked: {
                if (!bar.windowRef) return
                if (bar.maximized) bar.windowRef.showNormal()
                else bar.windowRef.showMaximized()
            }
        }
        IconButton {
            iconSource: Icon.close
            tooltip: qsTr("Close")
            danger: true
            onClicked: if (bar.windowRef) bar.windowRef.close()
        }
    }
}
