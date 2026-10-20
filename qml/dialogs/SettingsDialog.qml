import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Ultraview

Dialog {
    id: dialog
    title: qsTr("Settings")
    modal: true
    padding: 0
    property int initialTab: 0
    width: Math.min(780, (Overlay.overlay ? Overlay.overlay.width : 1024) - 64)
    height: Math.min(660, (Overlay.overlay ? Overlay.overlay.height : 720) - 64)
    anchors.centerIn: parent
    closePolicy: Popup.CloseOnEscape
    standardButtons: Dialog.NoButton

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

            AppButton {
                text: qsTr("Reset to defaults")
                ghost: true
                onClicked: controller.settings.resetToDefaults()
            }

            Item { Layout.fillWidth: true }

            AppButton {
                text: qsTr("Close")
                onClicked: dialog.close()
            }

            AppButton {
                text: qsTr("Save")
                primary: true
                onClicked: {
                    controller.settingsApplied()
                    dialog.close()
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingLarge
        anchors.rightMargin: Theme.spacingLarge
        anchors.topMargin: Theme.spacingXSmall + 2
        anchors.bottomMargin: Theme.spacingXSmall + 2
        spacing: Theme.spacingSmall + 2

        Item {
            Layout.fillWidth: true
            implicitHeight: tabs.implicitHeight + 1

            TabBar {
                id: tabs
                anchors.left: parent.left
                anchors.top: parent.top
                spacing: Theme.spacingXSmall
                currentIndex: dialog.initialTab

                background: Item { }

                AppTabButton { text: qsTr("Profiles") }
                AppTabButton { text: qsTr("Security") }
                AppTabButton { text: qsTr("Decoder") }
                AppTabButton { text: qsTr("View") }
                AppTabButton { text: qsTr("Endpoints") }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.border
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            // ------------------------------------------------------------ profiles
            ScrollView {
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: Theme.spacing + 2

                    FilePicker {
                        label: qsTr("Registration profile (26 terms)")
                        value: controller.settings.registrationProfilePath
                        hint: qsTr("The identity of this controller. The project never ships one: "
                                   + "it contains per-installation values.")
                        onPicked: (path) => controller.settings.registrationProfilePath = path
                    }

                    FilePicker {
                        label: qsTr("Partner template (17 request terms)")
                        value: controller.settings.partnerTemplatePath
                        hint: qsTr("Shared template of the connection request; the partner ID, the "
                                   + "password value and the digests are filled in per connection.")
                        onPicked: (path) => controller.settings.partnerTemplatePath = path
                    }

                    FilePicker {
                        label: qsTr("Screen dictionary (8bit.dat)")
                        value: controller.settings.dictionaryPath
                        hint: qsTr("Required for compressed RGB555 screen updates. "
                                   + "Status: %1").arg(controller.settings.dictionaryStatus())
                        onPicked: (path) => controller.loadDictionary(path)
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSmall

                        AppButton {
                            text: qsTr("Create scaffold templates")
                            iconSource: Icon.folder
                            onClicked: controller.ensureProfileTemplates()
                        }

                        Item { Layout.fillWidth: true }
                    }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textFaint
                        font.pixelSize: Theme.fontSmall
                        text: qsTr("This client works without the original program, but it cannot "
                                   + "invent your registration identity: without a valid profile the "
                                   + "registration server will refuse the session.")
                    }
                    Item { Layout.fillHeight: true }
                }
            }

            // ------------------------------------------------------------ security
            ScrollView {
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: Theme.spacing

                    Label {
                        text: qsTr("Certificate handling")
                        color: Theme.text
                        font.pixelSize: Theme.fontHeading
                        font.weight: Font.DemiBold
                    }

                    AppComboBox {
                        Layout.fillWidth: true
                        model: [
                            qsTr("Verify chain and host name (default)"),
                            qsTr("Verify chain, do not check the host name"),
                            qsTr("Accept any certificate (insecure, matches the original program)")
                        ]
                        currentIndex: controller.settings.tlsMode
                        onActivated: (index) => controller.settings.tlsMode = index
                    }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.dangerText
                        font.pixelSize: Theme.fontSmall
                        visible: controller.settings.tlsWarning.length > 0
                        text: controller.settings.tlsWarning
                    }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSmall
                        text: qsTr("The recovered endpoints present certificates; the original Windows "
                                   + "client accepts every certificate. This client verifies by default "
                                   + "and warns whenever verification is relaxed. Relaxing it is only "
                                   + "recommended when the deployment's certificate legitimately cannot "
                                   + "match.")
                    }

                    Label {
                        text: qsTr("Passwords")
                        color: Theme.text
                        font.pixelSize: Theme.fontHeading
                        font.weight: Font.DemiBold
                        topPadding: Theme.spacingSmall
                    }

                    AppCheckBox {
                        text: qsTr("Remember partner passwords")
                        enabled: controller.settings.passwordPersistenceAvailable
                        checked: controller.settings.rememberPassword
                        onToggled: controller.settings.rememberPassword = checked
                    }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSmall
                        text: controller.settings.passwordPersistenceAvailable
                              ? qsTr("Passwords are stored in the platform secret service.")
                              : qsTr("No platform secret service is available in this build: "
                                     + "passwords are kept in memory only and are never written to disk.")
                    }
                    Item { Layout.fillHeight: true }
                }
            }

            // ------------------------------------------------------------- decoder
            ScrollView {
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: Theme.spacing

                    AppSpinBox {
                        Layout.fillWidth: true
                        from: 1000
                        to: 120000
                        stepSize: 500
                        editable: true
                        value: controller.settings.timeoutMs
                        onValueModified: controller.settings.timeoutMs = value
                        textFromValue: (value) => qsTr("Transport timeout: %1 ms").arg(value)
                    }

                    AppSpinBox {
                        Layout.fillWidth: true
                        from: 4096
                        to: 64 * 1024 * 1024
                        stepSize: 65536
                        editable: true
                        value: controller.settings.maxFrameBytes
                        onValueModified: controller.settings.maxFrameBytes = value
                        textFromValue: (value) => qsTr("Maximum frame size: %1 bytes").arg(value)
                    }

                    AppSpinBox {
                        Layout.fillWidth: true
                        from: 4
                        to: 256
                        editable: true
                        value: controller.settings.jpegBlockSize
                        onValueModified: controller.settings.jpegBlockSize = value
                        textFromValue: (value) => qsTr("JPEG atlas block size: %1 px").arg(value)
                    }

                    AppSpinBox {
                        Layout.fillWidth: true
                        from: 1
                        to: 100000
                        stepSize: 100
                        editable: true
                        value: controller.settings.cacheLimit
                        onValueModified: controller.settings.cacheLimit = value
                        textFromValue: (value) => qsTr("Tile cache entries: %1").arg(value)
                    }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textFaint
                        font.pixelSize: Theme.fontSmall
                        text: qsTr("The defaults are the values recovered from the original client "
                                   + "(8 s timeout, 1 MiB frames, 32 px blocks, 1000 cached tiles). "
                                   + "The frame limit is also an absolute safety bound against a "
                                   + "hostile peer.")
                    }
                    Item { Layout.fillHeight: true }
                }
            }

            // ---------------------------------------------------------------- view
            ScrollView {
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: Theme.spacing

                    AppComboBox {
                        Layout.fillWidth: true
                        model: [
                            qsTr("Fit the desktop (letter-boxed)"),
                            qsTr("Fill and crop"),
                            qsTr("Stretch")
                        ]
                        currentIndex: controller.settings.fillMode
                        onActivated: (index) => controller.settings.fillMode = index
                    }

                    AppCheckBox {
                        text: qsTr("Smooth scaling")
                        checked: controller.settings.smoothScaling
                        onToggled: controller.settings.smoothScaling = checked
                    }

                    AppCheckBox {
                        text: qsTr("Hide the local pointer while controlling the partner")
                        checked: controller.settings.hideLocalPointer
                        onToggled: controller.settings.hideLocalPointer = checked
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacing

                        AppSpinBox {
                            from: 320
                            to: 8192
                            stepSize: 80
                            editable: true
                            value: controller.settings.viewportWidth
                            onValueModified: controller.settings.viewportWidth = value
                            textFromValue: (value) => qsTr("Viewport hint width: %1").arg(value)
                        }

                        AppSpinBox {
                            from: 200
                            to: 8192
                            stepSize: 60
                            editable: true
                            value: controller.settings.viewportHeight
                            onValueModified: controller.settings.viewportHeight = value
                            textFromValue: (value) => qsTr("height: %1").arg(value)
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textFaint
                        font.pixelSize: Theme.fontSmall
                        text: qsTr("The viewport hint is part of the login schema (the reference "
                                   + "sends 1280×720). The partner's real desktop size is read from "
                                   + "the login acknowledgement.")
                    }
                    Item { Layout.fillHeight: true }
                }
            }

            // ----------------------------------------------------------- endpoints
            ScrollView {
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: Theme.spacing

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.warning
                        font.pixelSize: Theme.fontSmall
                        text: controller.settings.endpointOverrideActive
                              ? qsTr("Endpoint overrides are active. The session does not talk to the "
                                     + "recovered production endpoints.")
                              : qsTr("Empty values mean the recovered production endpoints are used.")
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: Theme.spacing
                        rowSpacing: Theme.spacingSmall + 2

                        Label { text: qsTr("Registration host"); color: Theme.textMuted; font.pixelSize: Theme.fontSmall }
                        AppInlineField {
                            Layout.fillWidth: true
                            text: controller.settings.registrationHostOverride
                            placeholderText: qsTr("functions3.ultraviewer.net")
                            onEditingFinished: controller.settings.registrationHostOverride = text
                        }

                        Label { text: qsTr("Registration port"); color: Theme.textMuted; font.pixelSize: Theme.fontSmall }
                        AppInlineField {
                            Layout.fillWidth: true
                            text: controller.settings.registrationPortOverride
                            placeholderText: "443"
                            validator: IntValidator { bottom: 0; top: 65535 }
                            onEditingFinished: controller.settings.registrationPortOverride =
                                parseInt(text.length > 0 ? text : "0")
                        }

                        Label { text: qsTr("Registration Host header"); color: Theme.textMuted; font.pixelSize: Theme.fontSmall }
                        AppInlineField {
                            Layout.fillWidth: true
                            text: controller.settings.registrationHttpHostOverride
                            onEditingFinished: controller.settings.registrationHttpHostOverride = text
                        }

                        Label { text: qsTr("Registration certificate name"); color: Theme.textMuted; font.pixelSize: Theme.fontSmall }
                        AppInlineField {
                            Layout.fillWidth: true
                            text: controller.settings.registrationPeerNameOverride
                            placeholderText: qsTr("registration host (default)")
                            onEditingFinished: controller.settings.registrationPeerNameOverride = text
                        }

                        Label { text: qsTr("Relay host override"); color: Theme.textMuted; font.pixelSize: Theme.fontSmall }
                        AppInlineField {
                            Layout.fillWidth: true
                            text: controller.settings.relayHostOverride
                            placeholderText: qsTr("route value")
                            onEditingFinished: controller.settings.relayHostOverride = text
                        }

                        Label { text: qsTr("Relay port override"); color: Theme.textMuted; font.pixelSize: Theme.fontSmall }
                        AppInlineField {
                            Layout.fillWidth: true
                            text: controller.settings.relayPortOverride
                            placeholderText: "443"
                            validator: IntValidator { bottom: 0; top: 65535 }
                            onEditingFinished: controller.settings.relayPortOverride =
                                parseInt(text.length > 0 ? text : "0")
                        }

                        Label { text: qsTr("Relay Host header override"); color: Theme.textMuted; font.pixelSize: Theme.fontSmall }
                        AppInlineField {
                            Layout.fillWidth: true
                            text: controller.settings.relayHttpHostOverride
                            placeholderText: qsTr("route value")
                            onEditingFinished: controller.settings.relayHttpHostOverride = text
                        }

                        Label { text: qsTr("Relay certificate name"); color: Theme.textMuted; font.pixelSize: Theme.fontSmall }
                        AppInlineField {
                            Layout.fillWidth: true
                            text: controller.settings.relayPeerNameOverride
                            placeholderText: qsTr("registration certificate name (default)")
                            onEditingFinished: controller.settings.relayPeerNameOverride = text
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textFaint
                        font.pixelSize: Theme.fontSmall
                        text: qsTr("Overrides exist for private deployments and for the offline test "
                                   + "suite; they are stored in the settings file and shown here.")
                    }
                    Item { Layout.fillHeight: true }
                }
            }
        }
    }
}
