import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Ultraview

// Session tools (commands 22/23/31/32/40/46/53 and the display preferences).
// Every action is a single command to the partner; none of them changes local
// state except the toggles, which persist through SettingsModel.
Popup {
    id: popup

    width: 336
    padding: Theme.spacing
    modal: false
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle {
        color: Theme.surface
        radius: Theme.radius
        border.width: 1
        border.color: Theme.border
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacingSmall

        Label {
            Layout.fillWidth: true
            text: qsTr("Session actions")
            color: Theme.textMuted
            font.pixelSize: Theme.fontSmall
            font.weight: Font.DemiBold
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 0.6
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall

            AppButton {
                Layout.fillWidth: true
                text: qsTr("Ctrl+Alt+Del")
                enabled: controller.connected
                onClicked: {
                    controller.requestCtrlAltDel()
                    popup.close()
                }
            }

            AppButton {
                Layout.fillWidth: true
                text: qsTr("Lock")
                enabled: controller.connected
                onClicked: {
                    controller.requestLock()
                    popup.close()
                }
            }
        }

        AppButton {
            Layout.fillWidth: true
            text: qsTr("Restart the partner computer")
            danger: true
            enabled: controller.connected
            onClicked: {
                controller.requestReboot()
                popup.close()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.border
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("Display quality")
            color: Theme.textMuted
            font.pixelSize: Theme.fontSmall
            font.weight: Font.DemiBold
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 0.6
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall

            AppButton {
                Layout.fillWidth: true
                text: qsTr("Speed")
                active: controller.quality === 30
                enabled: controller.connected
                onClicked: controller.setQuality(30)
            }
            AppButton {
                Layout.fillWidth: true
                text: qsTr("Balanced")
                active: controller.quality === 45
                enabled: controller.connected
                onClicked: controller.setQuality(45)
            }
            AppButton {
                Layout.fillWidth: true
                text: qsTr("Quality")
                active: controller.quality === 70
                enabled: controller.connected
                onClicked: controller.setQuality(70)
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.border
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("This viewer")
            color: Theme.textMuted
            font.pixelSize: Theme.fontSmall
            font.weight: Font.DemiBold
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 0.6
        }

        AppCheckBox {
            Layout.fillWidth: true
            text: qsTr("Partner wallpaper")
            checked: controller.wallpaperShown
            enabled: controller.connected
            onToggled: controller.setWallpaperShown(checked)
        }

        AppCheckBox {
            Layout.fillWidth: true
            text: qsTr("Draw the partner cursor")
            checked: controller.settings.remoteCursor
            onToggled: controller.settings.remoteCursor = checked
        }

        AppCheckBox {
            Layout.fillWidth: true
            text: qsTr("Synchronize the clipboard")
            checked: controller.settings.clipboardSync
            onToggled: controller.settings.clipboardSync = checked
        }

        AppCheckBox {
            Layout.fillWidth: true
            text: qsTr("Accept incoming files automatically")
            checked: controller.settings.transferAutoAccept
            onToggled: controller.settings.transferAutoAccept = checked
        }

        AppCheckBox {
            Layout.fillWidth: true
            text: qsTr("Partner computer sound")
            checked: controller.audioEnabled
            enabled: controller.connected
            onToggled: controller.setAudioEnabled(checked)
        }

        Label {
            Layout.fillWidth: true
            visible: controller.audioEnabled
            text: controller.audioAvailable
                  ? qsTr("Playing through %1").arg(controller.audioDescription)
                  : qsTr("No audio output is available: %1").arg(controller.audioDescription)
            color: controller.audioAvailable ? Theme.textFaint : Theme.warning
            font.pixelSize: Theme.fontTiny
            wrapMode: Text.WordWrap
        }

        Label {
            Layout.fillWidth: true
            visible: controller.viewOnly
            text: qsTr("The partner limited this session to viewing; input is ignored there.")
            color: Theme.warning
            font.pixelSize: Theme.fontTiny
            wrapMode: Text.WordWrap
        }
    }
}
