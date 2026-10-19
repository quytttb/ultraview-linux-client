import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Ultraview

// Introductory block used at the top of a page or of an empty list: an optional
// icon tile, a title and a description.
ColumnLayout {
    id: empty
    spacing: Theme.spacingSmall

    property string title: ""
    property string description: ""
    property string iconSource: ""

    Rectangle {
        visible: empty.iconSource !== ""
        implicitWidth: 46
        implicitHeight: 46
        radius: Theme.radius
        color: Theme.accentSoft

        Image {
            anchors.centerIn: parent
            source: empty.iconSource
            sourceSize.width: 24
            sourceSize.height: 24
        }
    }

    Label {
        Layout.fillWidth: true
        text: empty.title
        color: Theme.text
        font.pixelSize: Theme.fontHeading
        font.weight: Font.DemiBold
        wrapMode: Text.WordWrap
    }

    Label {
        Layout.fillWidth: true
        text: empty.description
        color: Theme.textMuted
        font.pixelSize: Theme.fontNormal
        lineHeight: 1.25
        wrapMode: Text.WordWrap
    }
}
