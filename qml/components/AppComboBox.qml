import QtQuick
import QtQuick.Controls
import Ultraview

// ComboBox dressed in the application theme.  The Basic style chrome (its own
// font, indicator and popup) is replaced; the control's API is untouched.
ComboBox {
    id: control
    implicitHeight: Theme.controlHeight
    font.pixelSize: Theme.fontNormal

    contentItem: Label {
        leftPadding: Theme.spacing
        rightPadding: 36
        text: control.displayText
        color: control.enabled ? Theme.text : Theme.textFaint
        font: control.font
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: AppFieldBackground {
        focused: control.activeFocus || control.popup.visible
        hovered: control.hovered
    }

    indicator: Image {
        x: control.width - width - Theme.spacing - 2
        y: (control.height - height) / 2
        source: Icon.chevronDown
        sourceSize.width: 16
        sourceSize.height: 16
        opacity: control.enabled ? 1 : 0.4
    }

    popup: Popup {
        y: control.height + 6
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + 2 * padding, 280)
        padding: 5

        background: Rectangle {
            radius: Theme.radiusSmall
            color: Theme.surfaceAlt
            border.width: 1
            border.color: Theme.borderStrong
        }

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator { }
        }
    }

    delegate: ItemDelegate {
        id: item
        required property var modelData
        required property int index

        width: ListView.view ? ListView.view.width : 0
        implicitHeight: 34
        highlighted: ListView.isCurrentItem

        contentItem: Label {
            text: item.modelData
            color: item.highlighted ? Theme.text : Theme.textMuted
            font.pixelSize: Theme.fontNormal
            leftPadding: Theme.spacingSmall + 2
            rightPadding: Theme.spacingSmall
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        background: Rectangle {
            radius: 6
            color: item.highlighted ? Theme.accentSoft
                                    : (item.hovered ? Theme.surfaceHover : "transparent")
            Behavior on color {
                ColorAnimation { duration: Theme.durationFast }
            }
        }
    }
}
