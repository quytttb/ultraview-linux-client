import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Ultraview

// Password entry with a reveal toggle.  The value is only ever read back into the
// view model; nothing in the UI logs or persists it (see security/CredentialStore).
ColumnLayout {
    id: field
    spacing: Theme.spacingXSmall + 2

    property alias label: caption.text
    property alias text: input.text
    property alias placeholderText: input.placeholderText
    property alias hint: hintLabel.text
    property bool revealed: false
    property int inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText | Qt.ImhHiddenText

    signal accepted
    signal editingFinished
    signal textEdited

    Label {
        id: caption
        color: Theme.textMuted
        font.pixelSize: Theme.fontSmall
        font.weight: Font.Medium
        visible: text.length > 0
    }

    Item {
        Layout.fillWidth: true
        Layout.preferredHeight: Theme.controlHeight

        TextField {
            id: input
            anchors.fill: parent
            echoMode: field.revealed ? TextInput.Normal : TextInput.Password
            color: Theme.text
            placeholderTextColor: Theme.textFaint
            font.pixelSize: Theme.fontNormal
            leftPadding: Theme.spacing
            rightPadding: 42
            selectByMouse: true
            inputMethodHints: field.inputMethodHints
            background: AppFieldBackground {
                focused: input.activeFocus
                hovered: input.hovered
            }
            onAccepted: field.accepted()
            onEditingFinished: field.editingFinished()
            onTextEdited: field.textEdited()
        }

        IconButton {
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingXSmall
            anchors.verticalCenter: parent.verticalCenter
            iconSource: field.revealed ? Icon.eyeOff : Icon.eye
            tooltip: field.revealed ? qsTr("Hide the password") : qsTr("Show the password")
            onClicked: field.revealed = !field.revealed
        }
    }

    Label {
        id: hintLabel
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: Theme.textMuted
        font.pixelSize: Theme.fontSmall
        visible: text.length > 0
    }
}
