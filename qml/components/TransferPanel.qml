import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Ultraview

// File transfers (commands 13/14/15/16/21).  An incoming offer waits here until
// the user accepts or declines it: the client never writes a partner's file into
// the download directory on its own (unless the user enabled auto-accept).
Rectangle {
    id: panel

    property bool active: false
    signal closeRequested

    color: Theme.surface
    clip: true

    onActiveChanged: {
        if (active && controller.transferPending)
            transferList.positionViewAtBeginning()
    }

    Rectangle {
        anchors.left: parent.left
        width: 1
        height: parent.height
        color: Theme.border
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing
        spacing: Theme.spacingSmall

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall

            Label {
                Layout.fillWidth: true
                text: qsTr("File transfers")
                color: Theme.text
                font.pixelSize: Theme.fontHeading
                font.weight: Font.DemiBold
            }

            IconButton {
                iconSource: Icon.folder
                tooltip: qsTr("Send a file…")
                enabled: controller.connected
                onClicked: chooser.open()
            }

            IconButton {
                iconSource: Icon.close
                tooltip: qsTr("Close transfers")
                onClicked: panel.closeRequested()
            }
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("Received files are saved to %1").arg(controller.downloadDirectory)
            color: Theme.textFaint
            font.pixelSize: Theme.fontTiny
            elide: Text.ElideMiddle
            wrapMode: Text.NoWrap
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: transferList
                anchors.fill: parent
                clip: true
                spacing: Theme.spacingSmall
                model: controller.transfers

                delegate: Rectangle {
                    id: transferRow
                    required property var modelData

                    width: transferList.width
                    height: content.implicitHeight + Theme.spacing

                    radius: Theme.radiusSmall
                    color: Theme.surfaceAlt
                    border.width: 1
                    border.color: transferRow.modelData.state === "waiting"
                                  && transferRow.modelData.direction === "download"
                                  ? Theme.accent : Theme.border

                    ColumnLayout {
                        id: content
                        anchors.fill: parent
                        anchors.margins: Theme.spacingSmall
                        spacing: Theme.spacingXSmall

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingSmall

                            Label {
                                Layout.fillWidth: true
                                text: transferRow.modelData.name
                                color: Theme.text
                                font.pixelSize: Theme.fontNormal
                                font.weight: Font.Medium
                                elide: Text.ElideMiddle
                            }

                            Label {
                                text: transferRow.modelData.transferredText + " / "
                                      + transferRow.modelData.sizeText
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontTiny
                                font.family: "monospace"
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            height: 3
                            radius: 2
                            color: Theme.surfaceSunken
                            visible: transferRow.modelData.state === "transferring"

                            Rectangle {
                                width: parent.width * Math.max(0.02, transferRow.modelData.progress)
                                height: parent.height
                                radius: 2
                                color: Theme.accent
                                Behavior on width {
                                    NumberAnimation { duration: Theme.durationFast }
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingSmall

                            Label {
                                Layout.fillWidth: true
                                text: {
                                    const data = transferRow.modelData
                                    if (data.state === "waiting" && data.direction === "download")
                                        return qsTr("The partner offers this file")
                                    if (data.state === "waiting")
                                        return qsTr("Waiting for the partner to accept")
                                    if (data.state === "transferring")
                                        return data.direction === "upload" ? qsTr("Sending…")
                                                                           : qsTr("Receiving…")
                                    if (data.state === "completed")
                                        return data.direction === "upload" ? qsTr("Sent") : qsTr("Saved")
                                    if (data.state === "cancelled")
                                        return qsTr("Cancelled")
                                    return data.error.length > 0 ? qsTr("Failed: %1").arg(data.error)
                                                                 : qsTr("Failed")
                                }
                                color: transferRow.modelData.state === "failed" ? Theme.dangerText
                                       : Theme.textMuted
                                font.pixelSize: Theme.fontTiny
                                elide: Text.ElideRight
                            }

                            AppButton {
                                visible: transferRow.modelData.state === "waiting"
                                         && transferRow.modelData.direction === "download"
                                text: qsTr("Accept")
                                primary: true
                                onClicked: controller.acceptTransfer(transferRow.modelData.id)
                            }

                            AppButton {
                                visible: transferRow.modelData.state === "waiting"
                                         && transferRow.modelData.direction === "download"
                                text: qsTr("Decline")
                                danger: true
                                onClicked: controller.declineTransfer(transferRow.modelData.id)
                            }

                            AppButton {
                                visible: transferRow.modelData.state === "waiting"
                                         || transferRow.modelData.state === "transferring"
                                text: qsTr("Cancel")
                                danger: true
                                onClicked: controller.cancelTransfer(transferRow.modelData.id)
                            }
                        }
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                visible: transferList.count === 0
                text: qsTr("No transfers. Use the folder button to send a file.")
                color: Theme.textFaint
                font.pixelSize: Theme.fontSmall
            }
        }

        AppButton {
            Layout.fillWidth: true
            visible: controller.transferActiveCount > 0
            text: qsTr("Cancel all transfers")
            danger: true
            onClicked: controller.cancelAllTransfers()
        }
    }

    FileDialog {
        id: chooser
        title: qsTr("Send a file to the partner")
        fileMode: FileDialog.OpenFile
        onAccepted: controller.offerFile(chooser.selectedFile)
    }
}
