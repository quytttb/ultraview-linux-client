import QtQuick
import QtQuick.Controls
import Ultraview

// Small count badge for a toolbar icon button (unread chat messages, active file
// transfers).  Hidden at zero, saturating at "99+" so the pill stays small.
Rectangle {
    id: badge

    property int count: 0
    property bool highlight: false

    visible: count > 0
    width: Math.max(14, label.implicitWidth + 6)
    height: 14
    radius: height / 2
    color: highlight ? Theme.warning : Theme.accent
    border.width: 1
    border.color: Theme.surface

    Label {
        id: label
        anchors.centerIn: parent
        text: badge.count > 99 ? "99+" : String(badge.count)
        color: Theme.textInverted
        font.pixelSize: 9
        font.weight: Font.Bold
    }
}
