import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Ultraview

// In-session chat (command 12).  The panel is a side column of the viewer, so it
// never covers the remote desktop; the transcript lives in the view model and is
// bounded there, this file only renders it.
Rectangle {
    id: panel

    property bool active: false
    signal closeRequested

    color: Theme.surface
    clip: true

    // Opening the panel is what marks the partner's messages as read.
    onActiveChanged: {
        if (active)
            controller.markChatRead()
    }

    Rectangle {
        anchors.left: parent.left
        width: 1
        height: parent.height
        color: Theme.border
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing
        spacing: Theme.spacingSmall

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall

            Label {
                Layout.fillWidth: true
                text: qsTr("Chat")
                color: Theme.text
                font.pixelSize: Theme.fontHeading
                font.weight: Font.DemiBold
            }

            IconButton {
                iconSource: Icon.close
                tooltip: qsTr("Close chat")
                onClicked: panel.closeRequested()
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: chatList
                anchors.fill: parent
                clip: true
                spacing: Theme.spacingSmall
                model: controller.chatLog
                onCountChanged: positionViewAtEnd()
                onHeightChanged: positionViewAtEnd()

                delegate: Item {
                    id: bubbleRow
                    required property var modelData

                    width: chatList.width
                    height: bubble.height

                    Rectangle {
                        id: bubble
                        readonly property int maxWidth: Math.round(chatList.width * 0.84)

                        anchors.left: modelData.outgoing ? undefined : parent.left
                        anchors.right: modelData.outgoing ? parent.right : undefined
                        width: Math.min(label.implicitWidth + Theme.spacing * 1.6, maxWidth)
                        height: label.implicitHeight + Theme.spacing
                        radius: Theme.radiusSmall
                        color: modelData.outgoing ? Theme.accentSoft : Theme.surfaceAlt
                        border.width: modelData.outgoing ? 0 : 1
                        border.color: Theme.border

                        Label {
                            id: label
                            width: Math.min(implicitWidth, bubble.maxWidth - Theme.spacing * 1.6)
                            anchors.centerIn: parent
                            text: bubbleRow.modelData.text
                            color: Theme.text
                            font.pixelSize: Theme.fontNormal
                            wrapMode: Text.WrapAnywhere
                        }
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                visible: chatList.count === 0
                text: qsTr("No messages yet. Anything typed here is shown in a window on the partner's screen.")
                color: Theme.textFaint
                font.pixelSize: Theme.fontSmall
            }
        }

        Label {
            Layout.fillWidth: true
            visible: controller.partnerTyping
            text: qsTr("The partner is typing…")
            color: Theme.accentText
            font.pixelSize: Theme.fontSmall
            font.italic: true
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.border
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall

            TextField {
                id: chatInput
                Layout.fillWidth: true
                implicitHeight: Theme.controlHeight
                placeholderText: qsTr("Message to the partner…")
                placeholderTextColor: Theme.textFaint
                color: Theme.text
                font.pixelSize: Theme.fontNormal
                leftPadding: Theme.spacing
                rightPadding: Theme.spacing
                selectByMouse: true
                background: AppFieldBackground {
                    focused: chatInput.activeFocus
                    hovered: chatInput.hovered
                }
                onAccepted: panel.send()
                onTextEdited: {
                    typingTimer.restart()
                    controller.notifyTyping(text.length > 0)
                }
            }

            AppButton {
                text: qsTr("Send")
                primary: true
                enabled: chatInput.text.trim().length > 0 && controller.connected
                onClicked: panel.send()
            }
        }
    }

    Timer {
        id: typingTimer
        interval: 1800
        repeat: false
        onTriggered: controller.notifyTyping(false)
    }

    function send() {
        if (!controller.sendChatMessage(chatInput.text))
            return
        chatInput.clear()
        typingTimer.stop()
        controller.notifyTyping(false)
    }
}
