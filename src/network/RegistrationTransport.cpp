#include "network/RegistrationTransport.h"

#include "protocol/RegistrationCodec.h"

namespace ultraview::network {

RegistrationTransport::RegistrationTransport(QObject *parent)
    : QObject(parent)
    , m_frames(false, m_options.maxFrameBytes)
{
    connect(&m_channel, &HttpTunnelSocket::ready, this, &RegistrationTransport::onReady);
    connect(&m_channel, &HttpTunnelSocket::dataAvailable, this, &RegistrationTransport::onData);
    connect(&m_channel, &HttpTunnelSocket::failed, this, &RegistrationTransport::failed);
    connect(&m_channel, &HttpTunnelSocket::closed, this, &RegistrationTransport::onClosed);
}

RegistrationTransport::~RegistrationTransport() = default;

void RegistrationTransport::start()
{
    m_ready = false;
    m_frames.clear();
    m_frames.setExpectHttpHeader(false);
    m_frames.setMaxFrameBytes(m_options.maxFrameBytes);
    m_channel.start(TunnelRequest{m_options.host,
                                  m_options.port,
                                  m_options.httpHostHeader.isEmpty() ? m_options.host
                                                                     : m_options.httpHostHeader,
                                  m_options.peerNameOverride,
                                  m_options.tlsPolicy,
                                  m_options.timeoutMs,
                                  m_options.timeoutMs,
                                  m_options.timeoutMs,
                                  4 * 1024 * 1024});
}

void RegistrationTransport::stop()
{
    m_channel.abort();
}

void RegistrationTransport::onReady()
{
    // The reference requires the status line to be *exactly* "HTTP/1.1 200 OK"
    // here (`response_header.split(b"\\r\\n", 1)[0] != b"HTTP/1.1 200 OK"`).
    const QByteArray header = m_channel.responseHeader();
    const QByteArray statusLine = header.left(header.indexOf("\r\n"));
    if (!m_options.httpHostHeader.isEmpty() && statusLine != QByteArrayLiteral("HTTP/1.1 200 OK")) {
        const ErrorInfo info{ErrorCode::Protocol,
                             QStringLiteral("registration tunnel did not return HTTP 200"),
                             QString::fromLatin1(statusLine).left(64)};
        emit failed(info);
        m_channel.abort();
        return;
    }
    m_ready = true;
    emit ready();
}

void RegistrationTransport::onData()
{
    const QByteArray chunk = m_channel.takeBuffered();
    if (chunk.isEmpty())
        return;
    m_bytesReceived += chunk.size();
    if (m_drainPayloads) {
        // The handshake is over; nothing on this channel is interpreted any more.
        m_ignoredBytes += chunk.size();
        ++m_ignoredPayloads;
        if (m_ignoredSummary.isEmpty())
            m_ignoredSummary = QStringLiteral("drained %1 bytes after the handshake").arg(chunk.size());
        return;
    }
    QList<QByteArray> frames;
    try {
        frames = m_frames.feed(chunk);
    } catch (const UvException &exception) {
        emit failed(exception.info());
        m_channel.abort();
        return;
    }
    for (const QByteArray &frame : frames) {
        if (frame.isEmpty() || static_cast<quint8>(frame.at(0)) != 1) {
            emit failed(ErrorInfo{ErrorCode::Protocol, QStringLiteral("expected mode1 frame")});
            m_channel.abort();
            return;
        }
        emit payload(frame.mid(1));
    }
}

void RegistrationTransport::onClosed()
{
    if (m_ready) {
        m_ready = false;
        emit closed();
    }
}

void RegistrationTransport::sendCommand(const QByteArray &plaintextAnsi)
{
    if (!m_ready) {
        emit failed(ErrorInfo{ErrorCode::Internal, QStringLiteral("the registration channel is not connected")});
        return;
    }
    QByteArray body;
    try {
        const QByteArray encrypted = protocol::RegistrationCodec::encryptRegistrationBytes(plaintextAnsi);
        // `InitMyPassword` passes Send argument6=True (mode 1) and argument5=False
        // which becomes byte 0 at body offset 1 (E151).
        body = QByteArrayLiteral("\x01\x00") + encrypted;
    } catch (const UvException &exception) {
        emit failed(exception.info());
        return;
    }
    if (static_cast<quint32>(body.size()) > m_options.maxFrameBytes) {
        emit failed(ErrorInfo{ErrorCode::Resource, QStringLiteral("outbound frame exceeds limit"),
                              QStringLiteral("%1 > %2 bytes").arg(body.size()).arg(m_options.maxFrameBytes)});
        return;
    }
    QByteArray framed;
    try {
        framed = protocol::encodeTransportFrame(body);
    } catch (const UvException &exception) {
        emit failed(exception.info());
        return;
    }
    m_bytesSent += framed.size();
    m_channel.write(framed);
}

} // namespace ultraview::network
