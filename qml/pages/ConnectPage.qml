import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Ultraview

Item {
    id: page
    signal connectRequested

    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingLarge
        spacing: Theme.spacingLarge

        // ---------------------------------------------------------------- form
        ColumnLayout {
            Layout.fillWidth: false
            Layout.preferredWidth: 420
            Layout.minimumWidth: 360
            Layout.fillHeight: true
            spacing: Theme.spacing

            EmptyState {
                Layout.fillWidth: true
                title: qsTr("Control an UltraViewer partner")
                description: qsTr("This client speaks the recovered UltraViewer wire protocol to a "
                                  + "Windows partner. It needs your registration profile (the identity "
                                  + "of this controller), the partner ID and the partner's password.")
            }

            Rectangle {
                id: formCard
                Layout.fillWidth: true
                implicitHeight: cardColumn.implicitHeight + 2 * (Theme.spacing + 4)
                color: Theme.surface
                radius: Theme.radius
                border.width: 1
                border.color: Theme.border

                ColumnLayout {
                    id: cardColumn
                    anchors.fill: parent
                    anchors.margins: Theme.spacing + 4
                    spacing: Theme.spacing

                    AppTextField {
                        id: partnerField
                        Layout.fillWidth: true
                        label: qsTr("Partner ID")
                        placeholderText: qsTr("the ID shown in the partner's UltraViewer window")
                        text: controller.partnerId
                        onTextEdited: controller.partnerId = text
                    }

                    PasswordField {
                        id: passwordField
                        Layout.fillWidth: true
                        text: controller.password
                        onTextEdited: controller.password = text
                        onAccepted: page.connectRequested()
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSmall

                        AppButton {
                            text: qsTr("Connect")
                            primary: true
                            enabled: controller.canConnect
                            onClicked: page.connectRequested()
                        }

                        AppButton {
                            text: qsTr("Disconnect")
                            enabled: controller.busy
                            onClicked: controller.disconnectNow()
                        }

                        Item { Layout.fillWidth: true }
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: controller.canConnect ? Theme.textMuted : Theme.warning
                font.pixelSize: Theme.fontSmall
                text: controller.configurationHint
                visible: text.length > 0
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.dangerText
                font.pixelSize: Theme.fontSmall
                visible: controller.tlsWarning.length > 0
                text: qsTr("TLS warning: %1").arg(controller.tlsWarning)
            }

            Item { Layout.fillHeight: true }
        }

        // --------------------------------------------------------------- recent
        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 320
            Layout.fillHeight: true
            spacing: Theme.spacingSmall + 2

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall

                Label {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    text: qsTr("Recent partners")
                    color: Theme.text
                    font.pixelSize: Theme.fontHeading
                    font.weight: Font.DemiBold
                }

                AppButton {
                    Layout.alignment: Qt.AlignVCenter
                    text: qsTr("Forget all")
                    ghost: true
                    enabled: controller.recent.count > 0
                    onClicked: controller.recent.clear()
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.surfaceSunken
                radius: Theme.radius
                border.width: 1
                border.color: Theme.border

                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: Theme.spacingXSmall + 2
                    visible: controller.recent.count === 0

                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("No partner has been contacted yet.")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontNormal
                    }

                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("Create the profile templates first, then enter the partner ID.")
                        color: Theme.textFaint
                        font.pixelSize: Theme.fontSmall
                    }
                }

                ListView {
                    id: recentList
                    anchors.fill: parent
                    anchors.margins: Theme.spacingSmall - 2
                    clip: true
                    model: controller.recent
                    spacing: 2
                    visible: controller.recent.count > 0

                    delegate: Rectangle {
                        required property string partnerId
                        required property string lastUsedText

                        width: recentList.width
                        height: 46
                        radius: Theme.radiusSmall
                        color: recentMouse.containsMouse ? Theme.surfaceHover : "transparent"

                        Behavior on color {
                            ColorAnimation { duration: Theme.durationFast }
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.spacing
                            anchors.rightMargin: Theme.spacing
                            spacing: Theme.spacingSmall

                            Label {
                                Layout.fillWidth: true
                                text: partnerId
                                color: Theme.text
                                font.pixelSize: Theme.fontNormal
                                font.weight: Font.Medium
                                elide: Text.ElideRight
                            }

                            Label {
                                text: lastUsedText
                                color: Theme.textFaint
                                font.pixelSize: Theme.fontSmall
                            }
                        }

                        MouseArea {
                            id: recentMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                controller.partnerId = partnerId
                                partnerField.text = partnerId
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall

                AppButton {
                    Layout.alignment: Qt.AlignVCenter
                    text: qsTr("Create profile templates")
                    iconSource: Icon.folder
                    onClicked: controller.ensureProfileTemplates()
                }

                Label {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    wrapMode: Text.WordWrap
                    color: Theme.textFaint
                    font.pixelSize: Theme.fontSmall
                    text: qsTr("Templates are written to the application configuration directory; "
                               + "fill in the registration values in the settings.")
                }
            }
        }
    }
}
