import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Ultraview

Item {
    id: page

    property bool fullScreen: false
    property bool statisticsVisible: false
    property var statisticsRows: []
    // Outside full screen the toolbar is part of the layout.  In full screen it
    // floats over the remote desktop and only appears when the pointer reaches
    // the top edge, so the screen is not permanently shortened by a fixed bar.
    property bool toolbarRevealed: false
    readonly property bool toolbarShown: !fullScreen || toolbarRevealed

    signal fullScreenRequested

    function revealToolbar() {
        if (!fullScreen)
            return
        toolbarRevealed = true
        hideTimer.restart()
    }

    onFullScreenChanged: {
        // Entering or leaving full screen resets the overlay: it must not stay
        // stuck visible from a previous visit, and hiding later must not rely on
        // a timer that belonged to the old window state.
        toolbarRevealed = false
        hideTimer.stop()
    }

    RowLayout {
        anchors.fill: parent
        anchors.topMargin: page.fullScreen ? 0 : toolbar.height
        spacing: 0

        // ----------------------------------------------------------- remote screen
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

        // -------------------------------------------------------------- statistics
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

    // --------------------------------------------------------- viewer toolbar
    ViewerToolbar {
        id: toolbar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        z: 2
        visible: page.toolbarShown
        fullScreen: page.fullScreen
        inputEnabled: controller.inputEnabled
        statisticsVisible: page.statisticsVisible
        onDisconnectRequested: controller.disconnectNow()
        onInputToggled: controller.inputEnabled = !controller.inputEnabled
        onStatisticsToggled: globalMenu.statisticsVisible = !globalMenu.statisticsVisible
        onFullScreenRequested: page.fullScreenRequested()

        HoverHandler {
            id: toolbarHover
            enabled: page.fullScreen
            onHoveredChanged: {
                if (hovered)
                    hideTimer.stop()
                else if (page.fullScreen)
                    hideTimer.restart()
            }
        }
    }

    // Soft shade below the floating toolbar; `enabled: false` keeps it out of
    // hit testing so the top rows of the remote desktop stay clickable.
    Rectangle {
        anchors.top: toolbar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 10
        z: 1
        enabled: false
        visible: page.fullScreen && toolbar.visible
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(0.0, 0.0, 0.0, 0.45) }
            GradientStop { position: 1.0; color: Qt.rgba(0.0, 0.0, 0.0, 0.0) }
        }
    }

    // Hot edge: reaching the top with the pointer reveals the floating toolbar.
    Item {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 6
        z: 1
        enabled: page.fullScreen

        HoverHandler {
            onHoveredChanged: {
                if (hovered)
                    page.revealToolbar()
            }
        }
    }

    Timer {
        id: hideTimer
        interval: 1800
        repeat: false
        onTriggered: {
            if (page.fullScreen && !toolbarHover.hovered)
                page.toolbarRevealed = false
        }
    }

    // The panel is opened from the viewer toolbar and from the desktop's
    // application menu; both write the menu's state, this page only shows it.
    Connections {
        target: controller
        function onStateChanged() {
            // The login fields owned keyboard focus before the viewer replaced
            // the connect page. Give it to the remote screen immediately, so a
            // Super/Meta key released by the Wayland compositor is forwarded to
            // the partner rather than handled by this laptop.
            if (controller.connected && page.visible && !screenMouse.activeFocus)
                screenMouse.forceActiveFocus()
        }
        function onInputEnabledChanged() {
            // Re-enabling input after pressing the toolbar button must put the
            // keyboard back on the remote desktop, not leave it on that button.
            if (controller.connected && controller.inputEnabled && page.visible
                    && !screenMouse.activeFocus)
                screenMouse.forceActiveFocus()
        }
        function onStatisticsChanged() {
            page.statisticsRows = controller.statisticsRows()
        }
    }

    onVisibleChanged: {
        if (visible && controller.connected && !screenMouse.activeFocus)
            screenMouse.forceActiveFocus()
    }

    Component.onCompleted: page.statisticsRows = controller.statisticsRows()
}
