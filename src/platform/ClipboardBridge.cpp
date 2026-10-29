#include "platform/ClipboardBridge.h"

#include "core/SessionManager.h"

#include <QClipboard>
#include <QGuiApplication>

namespace ultraview::platform {

ClipboardBridge::ClipboardBridge(QObject *parent)
    : QObject(parent)
{
    if (QClipboard *clipboard = QGuiApplication::clipboard()) {
        connect(clipboard, &QClipboard::dataChanged, this,
                &ClipboardBridge::onLocalClipboardChanged);
    }
}

void ClipboardBridge::setSession(SessionManager *session)
{
    if (m_session == session)
        return;
    if (m_session != nullptr)
        m_session->disconnect(this);
    m_session = session;
    if (m_session != nullptr) {
        connect(m_session, &SessionManager::clipboardText, this,
                &ClipboardBridge::applyRemoteText);
    }
}

void ClipboardBridge::setEnabled(bool enabled)
{
    m_enabled = enabled;
}

void ClipboardBridge::onLocalClipboardChanged()
{
    if (!m_enabled || m_applying || m_session == nullptr || !m_session->isConnected())
        return;
    // The documented guard window: a change caused by our own remote update must
    // not be sent back to the partner.
    if (m_guard.isValid() && m_guard.elapsed() < kEchoGuardMs)
        return;
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr)
        return;
    const QString text = clipboard->text(QClipboard::Clipboard);
    if (text.isEmpty() || text == m_lastRemoteText)
        return;
    m_session->sendClipboard(text);
}

void ClipboardBridge::applyRemoteText(const QString &text)
{
    // Disabling the sync means the partner's text must not replace the local
    // clipboard either; the session still receives the message, it is only not
    // applied.
    if (!m_enabled || text == m_lastRemoteText)
        return;
    m_lastRemoteText = text;
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr)
        return;
    m_applying = true;
    clipboard->setText(text, QClipboard::Clipboard);
    m_applying = false;
    m_guard.start();
}

} // namespace ultraview::platform
