import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Ultraview

// One "label: value" line of the statistics panel.  Values are monospaced so the
// numbers line up while the session statistics update.
RowLayout {
    id: row
    spacing: Theme.spacing

    property string label: ""
    property string value: ""
    property bool emphasised: false

    Label {
        Layout.fillWidth: true
        text: row.label
        color: Theme.textMuted
        font.pixelSize: Theme.fontSmall
        elide: Text.ElideRight
    }

    Label {
        Layout.alignment: Qt.AlignRight
        text: row.value
        color: row.emphasised ? Theme.text : Theme.textMuted
        font.pixelSize: Theme.fontSmall
        font.family: "monospace"
        font.bold: row.emphasised
        elide: Text.ElideRight
    }
}
