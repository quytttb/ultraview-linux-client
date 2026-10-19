import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Ultraview

Dialog {
    id: dialog
    title: qsTr("About this client")
    modal: true
    padding: 0
    width: Math.min(740, (Overlay.overlay ? Overlay.overlay.width : 1024) - 64)
    height: Math.min(620, (Overlay.overlay ? Overlay.overlay.height : 720) - 64)
    anchors.centerIn: parent
    standardButtons: Dialog.NoButton
    closePolicy: Popup.CloseOnEscape

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

            Item { Layout.fillWidth: true }

            AppButton {
                text: qsTr("Copy diagnostics")
                onClicked: {
                    diagnosticsArea.selectAll()
                    diagnosticsArea.copy()
                    diagnosticsArea.cursorPosition = 0
                }
            }

            AppButton {
                text: qsTr("Close")
                primary: true
                onClicked: dialog.close()
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingLarge
        anchors.rightMargin: Theme.spacingLarge
        anchors.topMargin: Theme.spacingSmall
        anchors.bottomMargin: Theme.spacingSmall
        spacing: Theme.spacing

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("An independent, native Linux controller that speaks the recovered "
                       + "UltraViewer wire protocol. It contains no code or data from the original "
                       + "Windows program.")
            color: Theme.text
            font.pixelSize: Theme.fontNormal
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: qsTr("Refused on purpose: BitBlt screen operations, audio playback and P2P "
                       + "transports are not implemented and are reported instead of being silently "
                       + "ignored. The 8bit.dat screen dictionary and the registration profile must be "
                       + "supplied by the user; they are not part of this project.")
            color: Theme.textMuted
            font.pixelSize: Theme.fontSmall
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            TextArea {
                id: diagnosticsArea
                readOnly: true
                wrapMode: TextArea.Wrap
                color: Theme.text
                font.family: "monospace"
                font.pixelSize: Theme.fontSmall
                text: dialog.diagnosticsText
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

    property string diagnosticsText: ""

    onAboutToShow: diagnosticsText = controller.diagnostics()
}
