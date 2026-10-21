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

                AppTabButton { text: qsTr("Account") }
                AppTabButton { text: qsTr("Security") }
                AppTabButton { text: qsTr("Performance") }
                AppTabButton { text: qsTr("Display") }
                AppTabButton { text: qsTr("Advanced") }
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
                        label: qsTr("Account file")
                        value: controller.settings.registrationProfilePath
                        hint: qsTr("The private file that identifies your UltraViewer account on "
                                   + "this computer. Use your own installation; the project never "
                                   + "ships one.")
                        onPicked: (path) => controller.settings.registrationProfilePath = path
                    }

                    FilePicker {
                        label: qsTr("Connection template (optional)")
                        value: controller.settings.partnerTemplatePath
                        hint: qsTr("A prepared connection request. The partner ID and password are "
                                   + "filled in for each session.")
                        onPicked: (path) => controller.settings.partnerTemplatePath = path
                    }

                    FilePicker {
                        label: qsTr("Screen dictionary")
                        value: controller.settings.dictionaryPath
                        hint: qsTr("Required to decode the partner's screen. Choose 8bit.dat from "
                                   + "your UltraViewer installation. Status: %1")
                                 .arg(controller.settings.dictionaryStatus())
                        onPicked: (path) => controller.loadDictionary(path)
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSmall

                        AppButton {
                            text: qsTr("Create example files")
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
                        text: qsTr("Without a valid account file the UltraViewer service refuses the "
                                   + "connection. This project cannot create an account for you.")
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
                        text: qsTr("Server certificate")
                        color: Theme.text
                        font.pixelSize: Theme.fontHeading
                        font.weight: Font.DemiBold
                    }

                    AppComboBox {
                        Layout.fillWidth: true
                        model: [
                            qsTr("Verify the server (recommended)"),
                            qsTr("Check the certificate but not the server name (unsafe)"),
                            qsTr("Accept any server (unsafe)")
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
                        text: qsTr("UltraViewer's servers present certificates. The recommended "
                                   + "setting checks that the app is really talking to UltraViewer. "
                                   + "The unsafe modes are only for troubleshooting: they make it "
                                   + "easier for someone to intercept your connection, screen and "
                                   + "password.")
                    }

                    Label {
                        text: qsTr("Saved passwords")
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
                              ? qsTr("Passwords are stored in your system keyring.")
                              : qsTr("This build has no system keyring: passwords are kept only "
                                     + "until the app closes and are never written to disk.")
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
                        textFromValue: (value) => qsTr("Connection timeout: %1 ms").arg(value)
                    }

                    AppSpinBox {
                        Layout.fillWidth: true
                        from: 4096
                        to: 64 * 1024 * 1024
                        stepSize: 65536
                        editable: true
                        value: controller.settings.maxFrameBytes
                        onValueModified: controller.settings.maxFrameBytes = value
                        textFromValue: (value) => qsTr("Largest screen update: %1 bytes").arg(value)
                    }

                    AppSpinBox {
                        Layout.fillWidth: true
                        from: 4
                        to: 256
                        editable: true
                        value: controller.settings.jpegBlockSize
                        onValueModified: controller.settings.jpegBlockSize = value
                        textFromValue: (value) => qsTr("Image block size: %1 px").arg(value)
                    }

                    AppSpinBox {
                        Layout.fillWidth: true
                        from: 1
                        to: 100000
                        stepSize: 100
                        editable: true
                        value: controller.settings.cacheLimit
                        onValueModified: controller.settings.cacheLimit = value
                        textFromValue: (value) => qsTr("Cached screen tiles: %1").arg(value)
                    }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textFaint
                        font.pixelSize: Theme.fontSmall
                        text: qsTr("These defaults match the original UltraViewer client. Change them "
                                   + "only if a connection is unstable.")
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
                            qsTr("Fit the screen (black bars)"),
                            qsTr("Fill the window (crop the edges)"),
                            qsTr("Stretch to the window")
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
                        text: qsTr("Hide my pointer while controlling the partner")
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
                            textFromValue: (value) => qsTr("Login screen width: %1").arg(value)
                        }

                        AppSpinBox {
                            from: 200
                            to: 8192
                            stepSize: 60
                            editable: true
                            value: controller.settings.viewportHeight
                            onValueModified: controller.settings.viewportHeight = value
                            textFromValue: (value) => qsTr("Login screen height: %1").arg(value)
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textFaint
                        font.pixelSize: Theme.fontSmall
                        text: qsTr("The original client sends 1280×720 at login; the partner's real "
                                   + "screen size is read from its answer.")
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
                              ? qsTr("These overrides are active: the app is not using UltraViewer's "
                                     + "normal servers.")
                              : qsTr("Leave all fields empty to use UltraViewer's normal servers.")
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: Theme.spacing
                        rowSpacing: Theme.spacingSmall + 2

                        Label { text: qsTr("Login server"); color: Theme.textMuted; font.pixelSize: Theme.fontSmall }
                        AppInlineField {
                            Layout.fillWidth: true
                            text: controller.settings.registrationHostOverride
                            placeholderText: qsTr("functions3.ultraviewer.net")
                            onEditingFinished: controller.settings.registrationHostOverride = text
                        }

                        Label { text: qsTr("Login port"); color: Theme.textMuted; font.pixelSize: Theme.fontSmall }
                        AppInlineField {
                            Layout.fillWidth: true
                            text: controller.settings.registrationPortOverride
                            placeholderText: "443"
                            validator: IntValidator { bottom: 0; top: 65535 }
                            onEditingFinished: controller.settings.registrationPortOverride =
                                parseInt(text.length > 0 ? text : "0")
                        }

                        Label { text: qsTr("Login host header"); color: Theme.textMuted; font.pixelSize: Theme.fontSmall }
                        AppInlineField {
                            Layout.fillWidth: true
                            text: controller.settings.registrationHttpHostOverride
                            onEditingFinished: controller.settings.registrationHttpHostOverride = text
                        }

                        Label { text: qsTr("Login certificate name"); color: Theme.textMuted; font.pixelSize: Theme.fontSmall }
                        AppInlineField {
                            Layout.fillWidth: true
                            text: controller.settings.registrationPeerNameOverride
                            placeholderText: qsTr("registration host (default)")
                            onEditingFinished: controller.settings.registrationPeerNameOverride = text
                        }

                        Label { text: qsTr("Screen server"); color: Theme.textMuted; font.pixelSize: Theme.fontSmall }
                        AppInlineField {
                            Layout.fillWidth: true
                            text: controller.settings.relayHostOverride
                            placeholderText: qsTr("route value")
                            onEditingFinished: controller.settings.relayHostOverride = text
                        }

                        Label { text: qsTr("Screen server port"); color: Theme.textMuted; font.pixelSize: Theme.fontSmall }
                        AppInlineField {
                            Layout.fillWidth: true
                            text: controller.settings.relayPortOverride
                            placeholderText: "443"
                            validator: IntValidator { bottom: 0; top: 65535 }
                            onEditingFinished: controller.settings.relayPortOverride =
                                parseInt(text.length > 0 ? text : "0")
                        }

                        Label { text: qsTr("Screen host header"); color: Theme.textMuted; font.pixelSize: Theme.fontSmall }
                        AppInlineField {
                            Layout.fillWidth: true
                            text: controller.settings.relayHttpHostOverride
                            placeholderText: qsTr("route value")
                            onEditingFinished: controller.settings.relayHttpHostOverride = text
                        }

                        Label { text: qsTr("Screen certificate name"); color: Theme.textMuted; font.pixelSize: Theme.fontSmall }
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
                        text: qsTr("Only for private test servers or offline testing. Leave empty "
                                   + "for normal use.")
                    }
                    Item { Layout.fillHeight: true }
                }
            }
        }
    }
}
