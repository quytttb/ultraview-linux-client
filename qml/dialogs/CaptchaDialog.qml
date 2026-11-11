import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtWebEngine
import QtWebChannel
import Ultraview

// Off-the-record browser, created only for the lifetime of one challenge.
Item {
  property bool testMode: false
  signal testFinished(bool passed)
  function runTest() {
      // Synthetic page, not a request to UltraViewer. The early callback checks
      // both document-creation injection and the asynchronous channel queue.
      browser.loadHtml("<html><head><script>window.external.FromJS_CaptchaCorrect('', " +
                       "typeof String.prototype.includes === 'undefined' ? 'synthetic-ok' : 'bad');" +
                       "</script></head><body>Offline verification test</body></html>",
                       "https://www.ultraviewer.net/api/enter_captcha.aspx")
  }
  Dialog {
    id: dialog
    parent: Overlay.overlay
    title: qsTr("UltraViewer verification")
    modal: true
    anchors.centerIn: parent
    width: Math.min(760, Overlay.overlay.width - 48)
    height: Math.min(650, Overlay.overlay.height - 48)
    closePolicy: Popup.NoAutoClose
    standardButtons: Dialog.NoButton
    padding: 12
    property bool loadFailed: false

    Component.onCompleted: open()
    onClosed: {
        if (controller.captchaPending)
            controller.disconnectNow()
    }

    background: Rectangle {
        color: Theme.surface
        radius: Theme.radiusLarge
        border.color: Theme.borderStrong
    }

    ColumnLayout {
        anchors.fill: parent
        Label {
            Layout.fillWidth: true
            text: qsTr("Complete the verification on the official UltraViewer page to continue.")
            color: Theme.text
            wrapMode: Text.WordWrap
        }
        Label {
            Layout.fillWidth: true
            visible: dialog.loadFailed
            text: qsTr("The verification page could not be loaded. Check your connection and try again.")
            color: Theme.dangerText
            wrapMode: Text.WordWrap
        }
        WebEngineView {
            id: browser
            Layout.fillWidth: true
            Layout.fillHeight: true
            // Do not start navigation until the script collection is installed.
            Component.onCompleted: {
                if (controller.captchaPending)
                    url = controller.captchaUrl
            }
            profile: WebEngineProfile { offTheRecord: true }
            webChannel: WebChannel {
                registeredObjects: [QtObject {
                    WebChannel.id: "captcha"
                    function complete(token) {
                        if (testMode)
                            testFinished(token === "synthetic-ok" && controller.isCaptchaPage(browser.url))
                        else if (controller.captchaPending && controller.isCaptchaPage(browser.url))
                            controller.submitCaptcha(token)
                    }
                }]
            }
            // Install the legacy callback before the page starts. Queue a result
            // if Qt's asynchronous WebChannel is not ready yet.
            userScripts.collection: [
                {
                    name: "ultraview-captcha-legacy",
                    injectionPoint: WebEngineScript.DocumentCreation,
                    worldId: WebEngineScript.MainWorld,
                    runsOnSubFrames: false,
                    sourceCode: "(function(){" +
                        "if(location.protocol!=='https:'||location.hostname!=='www.ultraviewer.net'||location.pathname!=='/api/enter_captcha.aspx')return;" +
                        "var pending=null, bridge=null;" +
                        "var external=new Proxy({}, {get:function(_,name){return function(){" +
                        "if(name==='FromJS_CaptchaCorrect'&&arguments.length>1){" +
                        "var t=String(arguments[1]);if(bridge)bridge.complete(t);else pending=t;}}},has:function(){return true;}});" +
                        "try{Object.defineProperty(window,'external',{value:external,configurable:true});}catch(e){}" +
                        "try{delete String.prototype.includes;}catch(e){}" +
                        "window.__ultraviewCaptchaReady=function(b){bridge=b;if(pending!==null){bridge.complete(pending);pending=null;}};" +
                        "})();"
                },
                {
                    name: "ultraview-webchannel-library",
                    sourceUrl: "qrc:///qtwebchannel/qwebchannel.js",
                    injectionPoint: WebEngineScript.DocumentCreation,
                    worldId: WebEngineScript.MainWorld,
                    runsOnSubFrames: false
                },
                {
                    name: "ultraview-webchannel-connect",
                    injectionPoint: WebEngineScript.DocumentReady,
                    worldId: WebEngineScript.MainWorld,
                    runsOnSubFrames: false,
                    sourceCode: "(function(){if(window.__ultraviewCaptchaReady&&typeof QWebChannel==='function'&&window.qt&&qt.webChannelTransport)" +
                        "new QWebChannel(qt.webChannelTransport,function(c){window.__ultraviewCaptchaReady(c.objects.captcha);});})();"
                }
            ]
            onNavigationRequested: function(request) {
                if (!testMode && !controller.isCaptchaPage(request.url))
                    request.reject()
            }
            onLoadingChanged: function(request) {
                if (request.status === WebEngineView.LoadFailedStatus)
                    dialog.loadFailed = true
                else if (request.status === WebEngineView.LoadStartedStatus)
                    dialog.loadFailed = false
            }
            // Website console messages and loading URLs may contain secrets.
            onJavaScriptConsoleMessage: function(level, message, lineNumber, sourceID) {}
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button {
                visible: dialog.loadFailed
                text: qsTr("Try again")
                onClicked: browser.reload()
            }
            Button {
                text: qsTr("Cancel")
                onClicked: dialog.close()
            }
        }
    }
  }
}
