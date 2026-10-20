import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Ultraview

// Full diagnostic log for the current run. The status bar elides its message,
// so the complete text (including TLS certificate diagnostics) lives here and
// can be copied to the clipboard for a bug report.
Dialog {
    id: dialog
    title: qsTr("Session log")
    modal: true
    padding: 0
    width: Math.min(820, (Overlay.overlay ? Overlay.overlay.width : 1024) - 64)
    height: Math.min(600, (Overlay.overlay ? Overlay.overlay.height : 720) - 64)
    anchors.centerIn: parent
    standardButtons: Dialog.NoButton
    closePolicy: Popup.CloseOnEscape

    property bool copied: false

    background: Rectangle {
        color: Theme.surface
        radius: Theme.radiusLarge
        border.width: 1
        border.color: Theme.borderStrong
    }

    header: Item {
        implicitHeight: 68

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacingLarge
            anchors.rightMargin: Theme.spacing
            spacing: Theme.spacingSmall

            Label {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                text: dialog.title
                color: Theme.text
                font.pixelSize: Theme.fontTitle - 2
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            IconButton {
                Layout.alignment: Qt.AlignVCenter
                iconSource: Icon.close
                tooltip: qsTr("Close")
                onClicked: dialog.close()
            }
        }
    }

    footer: Rectangle {
        implicitHeight: 74
        color: "transparent"

        Rectangle {
            anchors.top: parent.top
            width: parent.width
            height: 1
            color: Theme.border
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacingLarge
            anchors.rightMargin: Theme.spacingLarge
            spacing: Theme.spacingSmall

            Label {
                Layout.fillWidth: true
                text: qsTr("The log never contains the partner password.")
                color: Theme.textMuted
                font.pixelSize: Theme.fontSmall
                elide: Text.ElideRight
            }

            AppButton {
                text: dialog.copied ? qsTr("Copied") : qsTr("Copy log")
                onClicked: {
                    if (controller.copyLogToClipboard())
                        dialog.copied = true
                }
            }

            AppButton {
                text: qsTr("Close")
                primary: true
                onClicked: dialog.close()
            }
        }
    }

    Timer {
        id: copiedTimer
        interval: 1600
        running: dialog.copied
        onTriggered: dialog.copied = false
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingLarge
        anchors.rightMargin: Theme.spacingLarge
        anchors.topMargin: Theme.spacingSmall
        anchors.bottomMargin: Theme.spacingSmall
        spacing: Theme.spacingSmall

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            TextArea {
                id: logArea
                readOnly: true
                wrapMode: TextArea.Wrap
                color: Theme.text
                font.family: "monospace"
                font.pixelSize: Theme.fontSmall
                text: controller.logText
                selectByMouse: true
                leftPadding: Theme.spacing
                rightPadding: Theme.spacing
                topPadding: Theme.spacingSmall
                bottomPadding: Theme.spacingSmall
                background: Rectangle {
                    color: Theme.surfaceSunken
                    radius: Theme.radiusSmall
                    border.width: 1
                    border.color: Theme.border
                }
            }
        }
    }

    onAboutToShow: {
        copied = false
        logArea.cursorPosition = logArea.length
    }
}
