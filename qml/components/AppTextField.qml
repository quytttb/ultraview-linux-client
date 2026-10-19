import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Ultraview

// Labelled text input on the shared theme surface with an optional hint line.
ColumnLayout {
    id: field
    spacing: Theme.spacingXSmall + 2

    property alias label: caption.text
    property alias text: input.text
    property alias placeholderText: input.placeholderText
    property alias readOnly: input.readOnly
    property alias echoMode: input.echoMode
    property alias hint: hintLabel.text
    property int inputMethodHints: Qt.ImhNone

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

    TextField {
        id: input
        Layout.fillWidth: true
        implicitHeight: Theme.controlHeight
        color: Theme.text
        placeholderTextColor: Theme.textFaint
        font.pixelSize: Theme.fontNormal
        leftPadding: Theme.spacing
        rightPadding: Theme.spacing
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

    Label {
        id: hintLabel
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: Theme.textMuted
        font.pixelSize: Theme.fontSmall
        visible: text.length > 0
    }
}
