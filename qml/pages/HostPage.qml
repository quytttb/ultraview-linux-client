import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Ultraview

// Host (sender) page.  It is intentionally inert until the user presses
// "Start hosting": opening the page only loads the local identity and shows the
// ID/password the Windows viewer has to enter.
Item {
    id: page
    signal startRequested
    signal stopRequested

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: content.implicitHeight + 2 * Theme.spacingLarge
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: content
            x: Theme.spacingLarge
            y: Theme.spacingLarge
            width: page.width - 2 * Theme.spacingLarge
            spacing: Theme.spacing

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing

                EmptyState {
                    Layout.fillWidth: true
                    iconSource: Icon.monitor
                    title: qsTr("Let a Windows UltraViewer control this computer")
                    description: qsTr("Start hosting to register this Linux computer with "
                                      + "UltraViewer. The Windows partner enters the ID and the "
                                      + "8-digit password shown below; every incoming connection "
                                      + "still needs your approval on this screen.")
                }

                ColumnLayout {
                    Layout.alignment: Qt.AlignTop
                    spacing: Theme.spacingSmall

                    StatusBadge {
                        Layout.alignment: Qt.AlignRight
                        stateName: host.stateName
                    }

                    AppSpinner {
                        Layout.alignment: Qt.AlignRight
                        running: host.starting
                        visible: host.starting
                    }
                }
            }

            // ------------------------------------------------------- identity
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Theme.spacing

                Rectangle {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 340
                    implicitHeight: identityColumn.implicitHeight + 2 * (Theme.spacing + 4)
                    color: Theme.surface
                    radius: Theme.radius
                    border.width: 1
                    border.color: Theme.border

                    ColumnLayout {
                        id: identityColumn
                        anchors.fill: parent
                        anchors.margins: Theme.spacing + 4
                        spacing: Theme.spacing

                        Label {
                            text: qsTr("This computer")
                            color: Theme.text
                            font.pixelSize: Theme.fontHeading
                            font.weight: Font.DemiBold
                        }

                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: qsTr("Give these credentials to the person connecting from "
                                       + "Windows. They identify this installation; the password "
                                       + "is stored only in the private host profile.")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontSmall
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingSmall

                            Rectangle {
                                Layout.fillWidth: true
                                implicitHeight: idColumn.implicitHeight + 2 * Theme.spacingSmall + 4
                                radius: Theme.radiusSmall
                                color: Theme.surfaceSunken
                                border.width: 1
                                border.color: Theme.border

                                ColumnLayout {
                                    id: idColumn
                                    anchors.fill: parent
                                    anchors.margins: Theme.spacingSmall + 2
                                    spacing: 2

                                    Label {
                                        text: qsTr("ID")
                                        color: Theme.textMuted
                                        font.pixelSize: Theme.fontSmall
                                        font.weight: Font.Medium
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        text: host.hostId.length > 0
                                              ? host.hostId
                                              : qsTr("Not assigned")
                                        color: host.hostId.length > 0 ? Theme.text : Theme.textFaint
                                        font.pixelSize: host.hostId.length > 0
                                                       ? Theme.fontHeading : Theme.fontSmall
                                        font.family: "monospace"
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }
                                }
                            }

                            Rectangle {
                                Layout.preferredWidth: 180
                                implicitHeight: passwordColumn.implicitHeight + 2 * Theme.spacingSmall + 4
                                radius: Theme.radiusSmall
                                color: Theme.surfaceSunken
                                border.width: 1
                                border.color: Theme.border

                                ColumnLayout {
                                    id: passwordColumn
                                    anchors.fill: parent
                                    anchors.margins: Theme.spacingSmall + 2
                                    spacing: 2

                                    Label {
                                        text: qsTr("Password")
                                        color: Theme.textMuted
                                        font.pixelSize: Theme.fontSmall
                                        font.weight: Font.Medium
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        text: host.password.length > 0
                                              ? host.password
                                              : qsTr("Not loaded")
                                        color: host.password.length > 0
                                               ? Theme.text : Theme.textFaint
                                        font.pixelSize: host.password.length > 0
                                                       ? Theme.fontHeading : Theme.fontSmall
                                        font.family: "monospace"
                                        font.weight: Font.DemiBold
                                        font.letterSpacing: 2
                                    }
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            height: 1
                            color: Theme.border
                        }

                        StatRow {
                            Layout.fillWidth: true
                            label: qsTr("Computer name")
                            value: host.displayName.length > 0 ? host.displayName : "—"
                        }
                        StatRow {
                            Layout.fillWidth: true
                            label: qsTr("Local address")
                            value: host.localAddress.length > 0 ? host.localAddress : "—"
                        }
                        StatRow {
                            Layout.fillWidth: true
                            label: qsTr("Screen backend")
                            value: host.backendName.length > 0 ? host.backendName : "—"
                        }
                        StatRow {
                            Layout.fillWidth: true
                            label: qsTr("Shared desktop")
                            value: host.layoutText.length > 0 ? host.layoutText : "—"
                        }

                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: Theme.textFaint
                            font.pixelSize: Theme.fontTiny
                            text: host.profilePath.length > 0
                                  ? qsTr("Profile: %1").arg(host.profilePath)
                                  : host.captureHint
                        }
                    }
                }

                // ---------------------------------------------------- viewer/start
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 280
                    Layout.alignment: Qt.AlignTop
                    spacing: Theme.spacing

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: viewerColumn.implicitHeight + 2 * (Theme.spacing + 4)
                        color: Theme.surface
                        radius: Theme.radius
                        border.width: 1
                        border.color: Theme.border

                        ColumnLayout {
                            id: viewerColumn
                            anchors.fill: parent
                            anchors.margins: Theme.spacing + 4
                            spacing: Theme.spacingSmall

                            Label {
                                text: qsTr("Viewer access")
                                color: Theme.text
                                font.pixelSize: Theme.fontHeading
                                font.weight: Font.DemiBold
                            }

                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                visible: !host.viewerConnected
                                text: qsTr("No Windows viewer is connected. When one signs in, "
                                           + "an approval dialog appears here before any screen "
                                           + "frame is sent.")
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontSmall
                            }

                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                visible: host.viewerConnected
                                text: host.viewerName.length > 0 ? host.viewerName
                                                                : qsTr("Windows viewer")
                                color: Theme.text
                                font.pixelSize: Theme.fontNormal
                                font.weight: Font.DemiBold
                            }

                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                visible: host.viewerConnected && host.viewerId.length > 0
                                text: qsTr("Viewer ID: %1").arg(host.viewerId)
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontSmall
                                font.family: "monospace"
                            }

                            AppCheckBox {
                                id: controlToggle
                                Layout.fillWidth: true
                                visible: host.viewerConnected
                                text: qsTr("Allow keyboard and mouse")
                                checked: host.controlAllowed
                                onClicked: host.controlAllowed = checked
                            }

                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                visible: host.viewerConnected
                                text: host.controlAllowed
                                      ? qsTr("The viewer can use this computer's keyboard and mouse.")
                                      : qsTr("View only: the viewer sees the screen but cannot "
                                             + "control it.")
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontSmall
                            }

                            Connections {
                                target: host
                                function onControlChanged() {
                                    controlToggle.checked = host.controlAllowed
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSmall

                        AppButton {
                            Layout.fillWidth: true
                            text: host.hosting ? qsTr("Hosting…") : qsTr("Start hosting")
                            primary: true
                            enabled: host.captureSupported && !host.hosting
                            onClicked: page.startRequested()
                        }

                        AppButton {
                            Layout.fillWidth: true
                            text: qsTr("Stop sharing")
                            danger: true
                            enabled: host.hosting
                            onClicked: page.stopRequested()
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textFaint
                        font.pixelSize: Theme.fontSmall
                        text: qsTr("A desktop permission dialog may appear when hosting starts; "
                                   + "the screen is not captured until you approve it.")
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: statusColumn.implicitHeight + 2 * Theme.spacing
                        color: Theme.surface
                        radius: Theme.radius
                        border.width: 1
                        border.color: Theme.border

                        ColumnLayout {
                            id: statusColumn
                            anchors.fill: parent
                            anchors.margins: Theme.spacing
                            spacing: Theme.spacingXSmall + 2

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spacingSmall

                                Label {
                                    text: qsTr("Status")
                                    color: Theme.textMuted
                                    font.pixelSize: Theme.fontSmall
                                    font.weight: Font.DemiBold
                                    font.capitalization: Font.AllUppercase
                                }

                                Item { Layout.fillWidth: true }

                                StatusBadge {
                                    stateName: host.stateName
                                }
                            }

                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: host.statusText
                                color: Theme.text
                                font.pixelSize: Theme.fontNormal
                            }

                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                visible: host.tlsWarning.length > 0
                                text: qsTr("TLS warning: %1").arg(host.tlsWarning)
                                color: Theme.dangerText
                                font.pixelSize: Theme.fontSmall
                            }

                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: qsTr("Screen and input only: chat, clipboard sync, file "
                                           + "transfer and remote audio are not implemented on the "
                                           + "host side and are refused rather than guessed.")
                                color: Theme.textFaint
                                font.pixelSize: Theme.fontSmall
                            }

                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: qsTr("Five wrong passwords lock new connections for five "
                                           + "minutes. Every viewer needs local approval; a request "
                                           + "expires after 45 seconds.")
                                color: Theme.textFaint
                                font.pixelSize: Theme.fontSmall
                            }
                        }
                    }
                }
            }

            // -------------------------------------------------------- messages
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: messageColumn.implicitHeight + 2 * (Theme.spacing + 2)
                visible: host.errorText.length > 0 || host.profileError.length > 0
                color: Theme.tinted(Theme.danger, 0.08)
                radius: Theme.radius
                border.width: 1
                border.color: Theme.tinted(Theme.danger, 0.35)

                ColumnLayout {
                    id: messageColumn
                    anchors.fill: parent
                    anchors.margins: Theme.spacing + 2
                    spacing: Theme.spacingXSmall

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: host.errorText.length > 0 ? host.errorText : host.profileError
                        color: Theme.dangerText
                        font.pixelSize: Theme.fontNormal
                        font.weight: Font.DemiBold
                    }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        visible: host.errorDetail.length > 0
                        text: host.errorDetail
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSmall
                        textFormat: Text.PlainText
                    }
                }
            }
        }
    }
}
