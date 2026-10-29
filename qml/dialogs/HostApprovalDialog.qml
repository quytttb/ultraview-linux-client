import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Ultraview

// Local approval gate for an incoming viewer.  There is deliberately no
// auto-approve: the dialog cannot be dismissed by clicking outside, and Escape
// refuses the request.  The countdown mirrors HostSession's 45-second deadline;
// when it reaches zero the session itself refuses the viewer.
Dialog {
    id: dialog

    property string viewerId: ""
    property string viewerName: ""
    property int secondsRemaining: 0

    signal allowControlRequested
    signal viewOnlyRequested
    signal refuseRequested

    title: qsTr("Incoming connection")
    modal: true
    padding: 0
    width: Math.min(540, (Overlay.overlay ? Overlay.overlay.width : 1024) - 64)
    anchors.centerIn: parent
    standardButtons: Dialog.NoButton
    closePolicy: Popup.NoAutoClose

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
            anchors.rightMargin: Theme.spacingLarge
            spacing: Theme.spacingSmall

            Rectangle {
                Layout.alignment: Qt.AlignVCenter
                implicitWidth: 34
                implicitHeight: 34
                radius: 10
                color: Theme.accentSoft

                Image {
                    anchors.centerIn: parent
                    source: Icon.monitor
                    sourceSize.width: 19
                    sourceSize.height: 19
                }
            }

            Label {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                text: dialog.title
                color: Theme.text
                font.pixelSize: Theme.fontTitle - 4
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingLarge
        anchors.rightMargin: Theme.spacingLarge
        anchors.topMargin: Theme.spacingXSmall
        anchors.bottomMargin: Theme.spacing
        spacing: Theme.spacingSmall + 2

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("A Windows UltraViewer wants to see this computer.")
            color: Theme.text
            font.pixelSize: Theme.fontNormal
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: whoColumn.implicitHeight + 2 * Theme.spacingSmall + 4
            radius: Theme.radiusSmall
            color: Theme.surfaceSunken
            border.width: 1
            border.color: Theme.border

            ColumnLayout {
                id: whoColumn
                anchors.fill: parent
                anchors.margins: Theme.spacingSmall + 2
                spacing: 2

                Label {
                    Layout.fillWidth: true
                    text: dialog.viewerName.length > 0 ? dialog.viewerName
                                                       : qsTr("Unnamed Windows viewer")
                    color: Theme.text
                    font.pixelSize: Theme.fontNormal
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                Label {
                    Layout.fillWidth: true
                    text: dialog.viewerId.length > 0
                          ? qsTr("ID %1").arg(dialog.viewerId)
                          : qsTr("ID unavailable")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSmall
                    font.family: "monospace"
                    elide: Text.ElideRight
                }
            }
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("Allow control lets the viewer use the keyboard and mouse. View only "
                       + "shares the screen without input. Nothing is shared until you choose "
                       + "one of these actions.")
            color: Theme.textMuted
            font.pixelSize: Theme.fontSmall
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall

            Label {
                Layout.fillWidth: true
                text: qsTr("Request expires in %1 s").arg(dialog.secondsRemaining)
                color: dialog.secondsRemaining <= 10 ? Theme.warning : Theme.textMuted
                font.pixelSize: Theme.fontSmall
                font.weight: Font.Medium
            }

            Rectangle {
                Layout.preferredWidth: 120
                implicitHeight: 6
                radius: 3
                color: Theme.surfaceSunken

                Rectangle {
                    width: parent.width * Math.max(0, Math.min(1,
                           dialog.secondsRemaining / 45))
                    height: parent.height
                    radius: 3
                    color: dialog.secondsRemaining <= 10 ? Theme.warning : Theme.accent

                    Behavior on width {
                        NumberAnimation { duration: 250 }
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall

            AppButton {
                Layout.fillWidth: true
                text: qsTr("Refuse")
                danger: true
                onClicked: dialog.refuseRequested()
            }

            AppButton {
                Layout.fillWidth: true
                text: qsTr("View only")
                onClicked: dialog.viewOnlyRequested()
            }

            AppButton {
                Layout.fillWidth: true
                text: qsTr("Allow control")
                primary: true
                onClicked: dialog.allowControlRequested()
            }
        }
    }

    // Escape is a refusal, never an implicit approval.
    Shortcut {
        sequences: [StandardKey.Cancel]
        enabled: dialog.opened
        onActivated: dialog.refuseRequested()
    }
}
