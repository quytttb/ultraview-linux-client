#pragma once
//
// Text clipboard bridge between the local desktop and the partner (commands
// 5 and 33).
//
// The bridge lives in the GUI thread and owns the echo suppression:
//
//   * local clipboard changes are posted to the session only while it is
//     connected and clipboard sync is enabled,
//   * a remote update is written to the local clipboard and the next 1000 ms of
//     local changes are ignored, because X11/Wayland clipboards report the
//     programmatic write as a normal change (the documented guard window),
//   * text the partner sent is remembered, so re-applying the same text cannot
//     bounce back and forth even after the guard window.
//
// Only text is synchronized.  Images, rich text and the X11 primary selection
// are deliberately out of scope: UltraViewer's clipboard protocol carries
// UTF-16 text only.
//
#include <QElapsedTimer>
#include <QObject>
#include <QString>

namespace ultraview {
class SessionManager;
}

namespace ultraview::platform {

class ClipboardBridge : public QObject {
    Q_OBJECT

public:
    static constexpr int kEchoGuardMs = 1000;

    explicit ClipboardBridge(QObject *parent = nullptr);

    /// The session to publish local changes to.  May be called once.
    void setSession(SessionManager *session);
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }

signals:
    /// One-shot, credential-free report (e.g. a clipboard the platform refuses).
    void statusMessage(const QString &message);

public slots:
    /// The session received text from the partner.
    void applyRemoteText(const QString &text);

private:
    void onLocalClipboardChanged();

    SessionManager *m_session = nullptr;
    QElapsedTimer m_guard;
    QString m_lastRemoteText;
    bool m_enabled = true;
    bool m_applying = false;
};

} // namespace ultraview::platform
