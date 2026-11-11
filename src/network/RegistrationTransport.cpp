#include "network/RegistrationTransport.h"

#include <QCoreApplication>
#include <QHostInfo>
#include <QTimer>
#include "protocol/RegistrationCodec.h"

namespace ultraview::network {

RegistrationTransport::RegistrationTransport(QObject *parent)
    : QObject(parent)
    , m_frames(false, m_options.maxFrameBytes)
{
    connect(&m_channel, &HttpTunnelSocket::ready, this, &RegistrationTransport::onReady);
    connect(&m_channel, &HttpTunnelSocket::dataAvailable, this, &RegistrationTransport::onData);
    connect(&m_channel, &HttpTunnelSocket::failed, this, &RegistrationTransport::onChannelFailed);
    connect(&m_channel, &HttpTunnelSocket::closed, this, &RegistrationTransport::onClosed);
}

RegistrationTransport::~RegistrationTransport() = default;

void RegistrationTransport::start()
{
    ++m_generation;
    m_stopped = false;
    m_fallbackActive = false;
    m_fallbackAddresses.clear();
    m_originalTlsError = ErrorInfo{};
    m_initialAddress = QHostAddress();
    m_ready = false;
    m_frames.clear();
    m_frames.setExpectHttpHeader(false);
    m_frames.setMaxFrameBytes(m_options.maxFrameBytes);
    startAddress(m_options.host);
}

void RegistrationTransport::startAddress(const QString &connectHost)
{
    // A fallback changes only the TCP destination. Keep the original service
    // name for SNI/certificate verification and the original HTTP Host header.
    const QString peerName = m_options.peerNameOverride.isEmpty() ? m_options.host
                                                                  : m_options.peerNameOverride;
    m_channel.start(TunnelRequest{connectHost,
                                  m_options.port,
                                  m_options.httpHostHeader.isEmpty() ? m_options.host
                                                                     : m_options.httpHostHeader,
                                  peerName,
                                  m_options.tlsPolicy,
                                  m_options.timeoutMs,
                                  m_options.timeoutMs,
                                  m_options.timeoutMs,
                                  4 * 1024 * 1024});
}

void RegistrationTransport::stop()
{
    m_stopped = true;
    ++m_generation;
    if (m_lookupId >= 0) {
        QHostInfo::abortHostLookup(m_lookupId);
        m_lookupId = -1;
    }
    m_fallbackAddresses.clear();
    m_channel.abort();
}

void RegistrationTransport::onChannelFailed(const ErrorInfo &error)
{
    if (m_stopped)
        return;
    if (m_fallbackActive) {
        if (!m_ready && (error.code == ErrorCode::Tls || error.code == ErrorCode::Network
                         || error.code == ErrorCode::Timeout)) {
            const quint64 generation = m_generation;
            QTimer::singleShot(0, this, [this, generation] {
                if (!m_stopped && generation == m_generation)
                    tryNextAddress();
            });
            return;
        }
        m_fallbackActive = false;
        emit failed(error);
        return;
    }

    // Only retry when the first TLS certificate was actually rejected. Never
    // retry after registration data has been sent or change the TLS policy.
    if (!m_ready && m_channel.certificateVerificationFailed()
        && m_options.tlsPolicy.mode() == security::TlsPolicy::Mode::VerifyPeer) {
        m_originalTlsError = error;
        m_initialAddress = m_channel.lastPeerAddress();
        m_fallbackActive = true;
        if (!m_options.fallbackAddressesForTesting.isEmpty()) {
            const auto addresses = m_options.fallbackAddressesForTesting;
            const quint64 generation = m_generation;
            QTimer::singleShot(0, this, [this, addresses, generation] {
                if (!m_stopped && generation == m_generation)
                    useFallbackAddresses(addresses);
            });
            return;
        }
        QHostAddress literal;
        if (literal.setAddress(m_options.host)) {
            m_fallbackActive = false;
            emit failed(error); // A literal address has no DNS alternatives.
            return;
        }
        const quint64 generation = m_generation;
        m_lookupId = QHostInfo::lookupHost(m_options.host, this, [this, generation](const QHostInfo &info) {
            if (m_stopped || generation != m_generation)
                return;
            m_lookupId = -1;
            useFallbackAddresses(info.addresses());
        });
        return;
    }
    emit failed(error);
}

void RegistrationTransport::useFallbackAddresses(const QList<QHostAddress> &addresses)
{
    // Bound work and never send the same failed destination twice.
    constexpr int kMaxAlternatives = 8;
    for (const QHostAddress &address : addresses) {
        if (address.isNull() || address == m_initialAddress || m_fallbackAddresses.contains(address))
            continue;
        m_fallbackAddresses.append(address);
        if (m_fallbackAddresses.size() >= kMaxAlternatives)
            break;
    }
    tryNextAddress();
}

void RegistrationTransport::tryNextAddress()
{
    if (m_stopped || !m_fallbackActive)
        return;
    if (m_fallbackAddresses.isEmpty()) {
        m_fallbackActive = false;
        emit failed(m_originalTlsError);
        return;
    }
    const QHostAddress address = m_fallbackAddresses.takeFirst();
    emit retryingVerifiedAddress();
    if (!m_stopped)
        startAddress(address.toString());
}

void RegistrationTransport::onReady()
{
    // The reference requires the status line to be *exactly* "HTTP/1.1 200 OK"
    // here (`response_header.split(b"\\r\\n", 1)[0] != b"HTTP/1.1 200 OK"`).
    const QByteArray header = m_channel.responseHeader();
    const QByteArray statusLine = header.left(header.indexOf("\r\n"));
    if (!m_options.httpHostHeader.isEmpty() && statusLine != QByteArrayLiteral("HTTP/1.1 200 OK")) {
        const ErrorInfo info{ErrorCode::Protocol,
                             QCoreApplication::translate("RegistrationTransport", "registration tunnel did not return HTTP 200"),
                             QString::fromLatin1(statusLine).left(64)};
        emit failed(info);
        m_channel.abort();
        return;
    }
    m_ready = true;
    m_fallbackActive = false;
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
            m_ignoredSummary = QCoreApplication::translate("RegistrationTransport", "drained %1 bytes after the handshake").arg(chunk.size());
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
            emit failed(ErrorInfo{ErrorCode::Protocol, QCoreApplication::translate("RegistrationTransport", "expected mode1 frame")});
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
        emit failed(ErrorInfo{ErrorCode::Internal, QCoreApplication::translate("RegistrationTransport", "the registration channel is not connected")});
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
        emit failed(ErrorInfo{ErrorCode::Resource, QCoreApplication::translate("RegistrationTransport", "outbound frame exceeds limit"),
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
