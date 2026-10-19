import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Ultraview

// A read-only path field with a file chooser.  Used by the settings dialog for the
// profile files and the screen dictionary (all three are user supplied).
ColumnLayout {
    id: picker
    Layout.fillWidth: true
    spacing: Theme.spacingXSmall + 2

    property string label: ""
    property string value: ""
    property string hint: ""
    signal picked(string path)

    Label {
        text: picker.label
        color: Theme.text
        font.pixelSize: Theme.fontNormal
        font.weight: Font.Medium
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.spacingSmall

        TextField {
            Layout.fillWidth: true
            implicitHeight: Theme.controlHeight
            text: picker.value
            placeholderText: qsTr("not configured")
            readOnly: true
            color: picker.value.length > 0 ? Theme.textMuted : Theme.textFaint
            font.pixelSize: Theme.fontSmall
            font.family: "monospace"
            leftPadding: Theme.spacing
            rightPadding: Theme.spacing
            selectByMouse: true
            background: AppFieldBackground { }
        }

        AppButton {
            text: qsTr("Choose…")
            iconSource: Icon.folder
            onClicked: chooser.open()
        }
    }

    Label {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        text: picker.hint
        color: Theme.textMuted
        font.pixelSize: Theme.fontSmall
    }

    FileDialog {
        id: chooser
        title: picker.label
        fileMode: FileDialog.OpenFile
        onAccepted: picker.picked(controller.localFileFromUrl(chooser.selectedFile))
    }
}
