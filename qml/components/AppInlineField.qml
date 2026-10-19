import QtQuick
import QtQuick.Controls
import Ultraview

// Compact single-line field for dense grids (the endpoint overrides in the
// settings dialog), where full-height inputs would look out of place.
TextField {
    id: control
    implicitHeight: Theme.controlHeight - 4
    color: Theme.text
    placeholderTextColor: Theme.textFaint
    font.pixelSize: Theme.fontSmall
    leftPadding: Theme.spacingSmall + 2
    rightPadding: Theme.spacingSmall + 2
    selectByMouse: true
    background: AppFieldBackground {
        focused: control.activeFocus
        hovered: control.hovered
    }
}
