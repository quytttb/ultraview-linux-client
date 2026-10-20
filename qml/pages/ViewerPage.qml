import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Ultraview

Item {
    id: page

    property bool fullScreen: false
    signal fullScreenRequested

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ViewerToolbar {
            id: toolbar
            Layout.fillWidth: true
            fullScreen: page.fullScreen
            inputEnabled: controller.inputEnabled
            statisticsVisible: page.statisticsVisible
            onDisconnectRequested: controller.disconnectNow()
            onInputToggled: controller.inputEnabled = !controller.inputEnabled
            onStatisticsToggled: globalMenu.statisticsVisible = !globalMenu.statisticsVisible
            onFullScreenRequested: page.fullScreenRequested()
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ------------------------------------------------------- remote screen
            Item {
                id: screenArea
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                Rectangle {
                    anchors.fill: parent
                    color: Theme.viewerBackground
                }

                RemoteScreen {
                    id: remoteScreen
                    anchors.fill: parent
                    focus: false
                    Component.onCompleted: controller.attachScreenItem(remoteScreen)
                }

                MouseArea {
                    id: screenMouse
                    // Only the displayed desktop area is interactive: the
                    // letter-boxed borders must not send pointer positions.
                    x: remoteScreen.contentRect.x
                    y: remoteScreen.contentRect.y
                    width: remoteScreen.contentRect.width
                    height: remoteScreen.contentRect.height
                    hoverEnabled: true
                    focus: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                    cursorShape: controller.settings.hideLocalPointer ? Qt.BlankCursor : Qt.ArrowCursor
                    enabled: controller.inputEnabled && controller.connected

                    onPositionChanged: (mouse) => {
                        const point = remoteScreen.mapToSource(mouse.x + screenMouse.x,
                                                               mouse.y + screenMouse.y)
                        if (point.x >= 0)
                            controller.input.moveTo(point.x, point.y)
                    }
                    onPressed: (mouse) => {
                        screenMouse.forceActiveFocus()
                        const point = remoteScreen.mapToSource(mouse.x + screenMouse.x,
                                                               mouse.y + screenMouse.y)
                        if (point.x >= 0) {
                            controller.input.moveTo(point.x, point.y)
                            controller.input.press(mouse.button)
                        }
                    }
                    onReleased: (mouse) => controller.input.release(mouse.button)
                    onWheel: (wheel) => controller.input.wheel(wheel.angleDelta.y)

                    Keys.onPressed: (event) => {
                        if (controller.input.keyPress(event.key, event.modifiers, event.text,
                                                      event.isAutoRepeat))
                            event.accepted = true
                    }
                    Keys.onReleased: (event) => {
                        if (event.isAutoRepeat) {
                            event.accepted = true
                            return
                        }
                        if (controller.input.keyRelease(event.key, event.modifiers))
                            event.accepted = true
                    }
                }

                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: Theme.spacing
                    visible: !remoteScreen.hasContent

                    AppSpinner {
                        running: visible
                        Layout.alignment: Qt.AlignHCenter
                    }
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: controller.connected
                                  ? qsTr("Waiting for the first screen update…")
                                  : controller.statusText
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontNormal
                    }
                }
            }

            // ------------------------------------------------------- statistics
            Rectangle {
                id: statisticsPanel
                Layout.fillHeight: true
                Layout.preferredWidth: page.statisticsVisible ? 320 : 0
                color: Theme.surface
                clip: true

                Behavior on Layout.preferredWidth {
                    NumberAnimation { duration: 140; easing.type: Easing.OutCubic }
                }

                Rectangle {
                    anchors.left: parent.left
                    width: 1
                    height: parent.height
                    color: Theme.border
                }

                ListView {
                    id: statisticsList
                    anchors.fill: parent
                    anchors.margins: Theme.spacing + 2
                    clip: true
                    spacing: Theme.spacingSmall
                    model: page.statisticsRows

                    header: Label {
                        text: qsTr("Session statistics")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSmall
                        font.weight: Font.DemiBold
                        font.capitalization: Font.AllUppercase
                        font.letterSpacing: 0.6
                        bottomPadding: Theme.spacingSmall
                    }

                    delegate: StatRow {
                        required property var modelData
                        width: statisticsList.width
                        label: modelData.label
                        value: modelData.value
                    }
                }
            }
        }
    }

    // The panel is opened from the viewer toolbar and from the desktop's
    // application menu; both write the menu's state, this page only shows it.
    property bool statisticsVisible: false
    property var statisticsRows: []

    Connections {
        target: controller
        function onStatisticsChanged() {
            page.statisticsRows = controller.statisticsRows()
        }
    }

    Component.onCompleted: page.statisticsRows = controller.statisticsRows()
}
