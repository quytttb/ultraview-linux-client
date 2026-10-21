import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Ultraview

ApplicationWindow {
    id: app
    width: 1024
    height: 720
    minimumWidth: 720
    minimumHeight: 520
    visible: true
    title: qsTr("UltraViewer — Linux client")
    color: Theme.windowBackground

    // Development aids used by `--self-test` and `--screenshot` (see
    // src/app/main.cpp); they stay false during a normal session.
    property bool devViewer: false
    property string devDialog: ""
    property int devDialogTab: 0

    // Full screen hides the application chrome (header and footer) so the remote
    // desktop uses the whole display; the viewer toolbar floats over the screen
    // and only appears when the pointer reaches the top edge. During remote input
    // its buttons (not a keyboard shortcut) change this state so F11/Esc still
    // reach the Windows partner.
    property bool fullScreen: false
    // Returning from full screen restores the window state the user had, normally
    // maximized (a session maximizes the window when the viewer opens).
    property int visibilityBeforeFullScreen: Window.Maximized

    function setFullScreen(enabled) {
        if (fullScreen === enabled)
            return
        if (enabled) {
            if (visibility === Window.Windowed || visibility === Window.Maximized)
                visibilityBeforeFullScreen = visibility
            fullScreen = true
            // Call the QWindow slot instead of merely changing the declarative
            // visibility enum: KWin then receives an explicit full-screen
            // request and removes its server-side decoration as well.
            app.showFullScreen()
        } else {
            fullScreen = false
            if (visibilityBeforeFullScreen === Window.Maximized)
                app.showMaximized()
            else
                app.showNormal()
        }
    }

    function toggleFullScreen() {
        setFullScreen(!fullScreen)
    }

    // A compositor shortcut (or the window manager) can change the visibility
    // without going through setFullScreen(); keep the chrome state in sync.
    onVisibilityChanged: function(windowVisibility) {
        if (windowVisibility === Window.FullScreen)
            fullScreen = true
        else if (windowVisibility === Window.Windowed || windowVisibility === Window.Maximized)
            fullScreen = false
    }

    onDevDialogChanged: {
        if (devDialog === "settings")
            settingsDialog.open()
        else if (devDialog === "about")
            aboutDialog.open()
        else if (devDialog === "log")
            logDialog.open()
    }

    header: AppToolbar {
        id: appToolbar
        visible: !app.fullScreen
        implicitHeight: app.fullScreen ? 0 : Theme.toolbarHeight
        onSettingsRequested: settingsDialog.open()
        onAboutRequested: aboutDialog.open()
    }

    // The viewer replaces the connect page while a session exists (busy or
    // connected); the pages keep their own state so nothing is lost on resize.
    StackLayout {
        id: pages
        anchors.fill: parent
        currentIndex: app.devViewer || controller.connected || controller.busy ? 1 : 0

        // A session opens maximized so the partner desktop has the display; leaving
        // the viewer while full screen would hide every visible way back (the
        // floating toolbar owns the exit button), so restore the windowed layout.
        onCurrentIndexChanged: {
            if (currentIndex === 1 && !app.fullScreen
                    && app.visibility !== Window.Maximized
                    && app.visibility !== Window.FullScreen
                    && app.visibility !== Window.Minimized)
                app.showMaximized()
            if (currentIndex === 0 && app.fullScreen)
                app.setFullScreen(false)
        }

        ConnectPage {
            id: connectPage
            onConnectRequested: controller.connectNow()
        }

        ViewerPage {
            id: viewerPage
            fullScreen: app.fullScreen
            onFullScreenRequested: app.toggleFullScreen()
            statisticsVisible: globalMenu.statisticsVisible
        }
    }

    footer: Rectangle {
        id: statusBar
        visible: !app.fullScreen
        implicitHeight: app.fullScreen ? 0 : 30
        color: Theme.windowBackground

        Rectangle {
            anchors.top: parent.top
            width: parent.width
            height: 1
            color: Theme.border
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
                elide: Text.ElideRight
                text: controller.noticeText.length > 0 ? controller.noticeText : controller.statusText
                color: statusMouse.containsMouse
                       ? Theme.text
                       : (controller.noticeText.length > 0 ? Theme.warning : Theme.textMuted)
                font.pixelSize: Theme.fontSmall

                // The status line elides; clicking it opens the complete log
                // (full TLS diagnostics included) with a copy button.
                ToolTip.visible: statusMouse.containsMouse
                ToolTip.text: qsTr("Click to view and copy the full session log")
                ToolTip.delay: 700

                MouseArea {
                    id: statusMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: logDialog.open()
                }
            }

            Label {
                Layout.alignment: Qt.AlignVCenter
                visible: controller.tlsWarning.length > 0
                text: qsTr("TLS: %1").arg(controller.tlsWarning)
                color: Theme.dangerText
                font.pixelSize: Theme.fontSmall
                elide: Text.ElideRight
                Layout.maximumWidth: 320
            }
        }
    }

    SettingsDialog {
        id: settingsDialog
        initialTab: app.devDialogTab
    }

    AboutDialog {
        id: aboutDialog
    }

    LogDialog {
        id: logDialog
    }

    // The desktop's application menu belongs to the platform layer, but the
    // dialogs and the lifetime of the application are QML's; the menu asks for
    // them over these signals instead of owning them.  Ctrl+Q is what the menu
    // advertises for Quit, so the window has to provide it.
    Connections {
        target: globalMenu

        function onSettingsRequested() {
            settingsDialog.open()
        }
        function onAboutRequested() {
            aboutDialog.open()
        }
        function onQuitRequested() {
            Qt.quit()
        }
    }

    Shortcut {
        // The menu advertises the platform's quit keys; `sequences` binds all of
        // them (Ctrl+Q, and the dedicated Exit key where the desktop has one).
        // While remote input is on, Ctrl+Q belongs to the Windows partner like
        // every other normal key; disconnect or disable input to quit locally.
        sequences: [StandardKey.Quit]
        enabled: !controller.connected || !controller.inputEnabled
        onActivated: Qt.quit()
    }

    Shortcut {
        sequence: "F11"
        // Do not reserve a normal Windows function key while remote input is
        // enabled. The toolbar provides the local full-screen control instead.
        enabled: pages.currentIndex === 1 && (!controller.connected || !controller.inputEnabled)
        onActivated: app.toggleFullScreen()
    }

    Shortcut {
        // Escape belongs to the partner while its input is enabled. When input is
        // disabled, it can still leave full screen unless a dialog owns it.
        sequences: [StandardKey.Cancel]
        enabled: app.fullScreen && (!controller.connected || !controller.inputEnabled)
                 && !settingsDialog.opened && !aboutDialog.opened
                 && !logDialog.opened
        onActivated: app.setFullScreen(false)
    }

    // Keyboard safety: a key held while the window loses focus must be released on
    // the partner side, otherwise it stays pressed there.
    onActiveChanged: {
        if (!active)
            controller.input.releaseAll()
    }
}
