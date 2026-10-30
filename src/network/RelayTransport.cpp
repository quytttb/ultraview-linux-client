#include "network/RelayTransport.h"

#include <QCoreApplication>

namespace ultraview::network {

RelayTransport::RelayTransport(QObject *parent)
    : QObject(parent)
    , m_frames(false, protocol::kDefaultMaxFrameBytes)
{
    connect(&m_channel, &HttpTunnelSocket::ready, this, &RelayTransport::onReady);
    connect(&m_channel, &HttpTunnelSocket::dataAvailable, this, &RelayTransport::onData);
    connect(&m_channel, &HttpTunnelSocket::failed, this, &RelayTransport::failed);
    connect(&m_channel, &HttpTunnelSocket::closed, this, &RelayTransport::onClosed);
}

RelayTransport::~RelayTransport() = default;

void RelayTransport::start(const Endpoint &endpoint, const security::TlsPolicy &policy, int timeoutMs,
                           quint32 maxFrameBytes)
{
    m_endpoint = endpoint;
    m_policy = policy;
    m_maxFrameBytes = maxFrameBytes;
    m_ready = false;
    m_errorReported = false;
    m_frames.clear();
    m_frames.setExpectHttpHeader(false);
    m_frames.setMaxFrameBytes(maxFrameBytes);
    m_channel.start(TunnelRequest{endpoint.host, endpoint.port, endpoint.httpHostHeader,
                                  endpoint.peerNameOverride, policy, timeoutMs, timeoutMs, timeoutMs,
                                  4 * 1024 * 1024});
}

void RelayTransport::stop()
{
    m_channel.abort();
}

void RelayTransport::closeGracefully()
{
    m_channel.closeGracefully();
}

void RelayTransport::onReady()
{
    // Relay tunnel: the reference only requires the "HTTP/1.1 200 " prefix.
    const QByteArray header = m_channel.responseHeader();
    const QByteArray statusLine = header.left(header.indexOf("\r\n"));
    if (!m_endpoint.httpHostHeader.isEmpty() && !statusLine.startsWith("HTTP/1.1 200 ")) {
        const ErrorInfo info{ErrorCode::Protocol,
                             QCoreApplication::translate("RelayTransport", "session tunnel did not return HTTP 200"),
                             QString::fromLatin1(statusLine).left(64)};
        m_errorReported = true;
        emit failed(info);
        m_channel.abort();
        return;
    }
    m_ready = true;
    emit ready();
}

void RelayTransport::onData()
{
    const QByteArray chunk = m_channel.takeBuffered();
    if (chunk.isEmpty())
        return;
    m_bytesReceived += chunk.size();
    QList<QByteArray> frames;
    try {
        frames = m_frames.feed(chunk);
    } catch (const UvException &exception) {
        m_errorReported = true;
        emit failed(exception.info());
        m_channel.abort();
        return;
    }
    for (const QByteArray &frame : frames) {
        protocol::TransportBody body;
        try {
            body = protocol::decodeTransportBody(frame);
        } catch (const UvException &exception) {
            m_errorReported = true;
            emit failed(exception.info());
            m_channel.abort();
            return;
        }
        if (body.kind == protocol::TransportBody::Kind::Direct) {
            if (!m_endpoint.allowSystemMessages) {
                // The reference raises `unexpected session transport packet`
                // here; a direct (P2P) body is never expected on this channel.
                m_errorReported = true;
                const ErrorInfo info{ErrorCode::UnsupportedOperation,
                                     QCoreApplication::translate("RelayTransport", "unexpected session transport packet"),
                                     QCoreApplication::translate("RelayTransport", "direct/P2P body on a relay channel")};
                emit failed(info);
                m_channel.abort();
                return;
            }
            emit systemPayload(body.data);
            continue;
        }
        if (body.kind != protocol::TransportBody::Kind::Relay) {
            m_errorReported = true;
            const ErrorInfo info{ErrorCode::UnsupportedOperation,
                                 QCoreApplication::translate("RelayTransport", "unexpected session transport packet"),
                                 QCoreApplication::translate("RelayTransport", "unknown transport kind")};
            emit failed(info);
            m_channel.abort();
            return;
        }
        emit payload(body.data);
        emit payloadFrom(body.data, body.addressValue, body.port, body.flag);
    }
}

void RelayTransport::onClosed()
{
    if (m_ready) {
        m_ready = false;
        emit closed();
    }
}

void RelayTransport::sendPayload(const QByteArray &payload, quint8 flag)
{
    if (!m_ready || payload.isEmpty())
        return;
    QByteArray framed;
    try {
        framed = protocol::encodeRelayFrame(payload, m_endpoint.targetIpv4, m_endpoint.targetPort, flag);
    } catch (const UvException &exception) {
        emit failed(exception.info());
        return;
    }
    writeFramed(framed);
}

void RelayTransport::sendPayloadTo(const QString &ipv4, quint16 port, const QByteArray &payload,
                                   quint8 flag)
{
    if (!m_ready || payload.isEmpty())
        return;
    QByteArray framed;
    try {
        framed = protocol::encodeRelayFrame(payload, ipv4, port, flag);
    } catch (const UvException &exception) {
        emit failed(exception.info());
        return;
    }
    writeFramed(framed);
}

void RelayTransport::sendSystemPayload(const QByteArray &payload, bool withFlag)
{
    if (!m_ready || payload.isEmpty())
        return;
    QByteArray body;
    body.reserve(payload.size() + (withFlag ? 2 : 1));
    body.append(static_cast<char>(0x01));
    if (withFlag)
        body.append(static_cast<char>(0x00));
    body.append(payload);
    try {
        writeFramed(protocol::encodeTransportFrame(body));
    } catch (const UvException &exception) {
        emit failed(exception.info());
    }
}

void RelayTransport::sendRelayKeepalive()
{
    sendSystemPayload(QByteArrayLiteral("ping"), false);
}

bool RelayTransport::writeFramed(const QByteArray &framed)
{
    if (framed.size() - 4 > static_cast<qint64>(m_maxFrameBytes)) {
        const ErrorInfo info{ErrorCode::Resource, QCoreApplication::translate("RelayTransport", "outbound frame exceeds limit"),
                             QStringLiteral("%1 > %2 bytes")
                                 .arg(framed.size() - 4)
                                 .arg(m_maxFrameBytes)};
        emit failed(info);
        return false;
    }
    m_bytesSent += framed.size();
    m_channel.write(framed);
    return true;
}

} // namespace ultraview::network
