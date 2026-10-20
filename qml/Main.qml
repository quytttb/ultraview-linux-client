import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
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
        onSettingsRequested: settingsDialog.open()
        onAboutRequested: aboutDialog.open()
    }

    // The viewer replaces the connect page while a session exists (busy or
    // connected); the pages keep their own state so nothing is lost on resize.
    StackLayout {
        id: pages
        anchors.fill: parent
        currentIndex: app.devViewer || controller.connected || controller.busy ? 1 : 0

        ConnectPage {
            id: connectPage
            onConnectRequested: controller.connectNow()
        }

        ViewerPage {
            id: viewerPage
            statisticsVisible: globalMenu.statisticsVisible
        }
    }

    footer: Rectangle {
        id: statusBar
        implicitHeight: 30
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
        sequences: [StandardKey.Quit]
        onActivated: Qt.quit()
    }

    // Keyboard safety: a key held while the window loses focus must be released on
    // the partner side, otherwise it stays pressed there.
    onActiveChanged: {
        if (!active)
            controller.input.releaseAll()
    }
}
