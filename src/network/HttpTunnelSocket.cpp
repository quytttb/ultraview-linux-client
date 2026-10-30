#include "network/HttpTunnelSocket.h"

#include "protocol/PacketCodec.h"

#include <QList>
#include <QSslError>
#include <QSslSocket>
#include <QTimer>

namespace ultraview::network {
namespace {

/// The reference caps the HTTP header at 16384 bytes before giving up.
constexpr int kMaxHttpHeaderBytes = 16384;

ErrorCode classifySocketError(QAbstractSocket::SocketError error)
{
    switch (error) {
    case QAbstractSocket::ConnectionRefusedError:
    case QAbstractSocket::RemoteHostClosedError:
    case QAbstractSocket::HostNotFoundError:
    case QAbstractSocket::SocketAccessError:
    case QAbstractSocket::SocketResourceError:
    case QAbstractSocket::NetworkError:
    case QAbstractSocket::AddressInUseError:
    case QAbstractSocket::SocketAddressNotAvailableError:
    case QAbstractSocket::ProxyConnectionRefusedError:
    case QAbstractSocket::ProxyConnectionClosedError:
    case QAbstractSocket::ProxyNotFoundError:
    case QAbstractSocket::ProxyConnectionTimeoutError:
        return ErrorCode::Network;
    case QAbstractSocket::SocketTimeoutError:
        return ErrorCode::Timeout;
    case QAbstractSocket::SslHandshakeFailedError:
        return ErrorCode::Tls;
    default:
        return ErrorCode::Network;
    }
}

} // namespace

HttpTunnelSocket::HttpTunnelSocket(QObject *parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
{
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &HttpTunnelSocket::onTimeout);
}

HttpTunnelSocket::~HttpTunnelSocket() = default;

void HttpTunnelSocket::setPhase(Phase phase)
{
    m_phase = phase;
}

void HttpTunnelSocket::armTimer(int timeoutMs)
{
    if (timeoutMs <= 0) {
        m_timer->stop();
        return;
    }
    m_timer->start(timeoutMs);
}

void HttpTunnelSocket::start(const TunnelRequest &request)
{
    if (m_socket) {
        fail(ErrorCode::Internal, tr("the channel is already in use"));
        return;
    }
    if (request.host.isEmpty()) {
        fail(ErrorCode::Configuration, tr("empty tunnel host"));
        return;
    }
    m_request = request;

    m_socket = new QSslSocket(this);
    m_closedEmitted = false;
    m_failed = false;
    m_headerBuffer.clear();
    m_buffer.clear();
    m_responseHeader.clear();

    // Backpressure: a bounded Qt read buffer keeps a fast peer from making the
    // client allocate without limit while the decode loop is busy.
    m_socket->setReadBufferSize(request.readBufferLimit);

    connect(m_socket, &QSslSocket::connected, this, &HttpTunnelSocket::onConnected);
    connect(m_socket, &QSslSocket::encrypted, this, &HttpTunnelSocket::onEncrypted);
    connect(m_socket, &QSslSocket::readyRead, this, &HttpTunnelSocket::onReadyRead);
    connect(m_socket, &QSslSocket::disconnected, this, &HttpTunnelSocket::onDisconnected);
    connect(m_socket, &QSslSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) { onError(); });
    connect(m_socket, &QSslSocket::sslErrors, this, &HttpTunnelSocket::onSslErrors);

    setPhase(Phase::Connecting);
    const QString peerName = request.peerNameOverride.isEmpty() ? request.host : request.peerNameOverride;
    m_peerName = peerName;
    try {
        // The configuration must be installed before the handshake starts so that
        // peer verification (and the optional CA bundle) apply to it.
        m_socket->setSslConfiguration(m_request.tlsPolicy.configurationFor());
        // The expected certificate name is a socket property. Default to the
        // connect host; a route may explicitly supply its authenticated DNS name
        // when the TCP connection uses an IP address instead.
        m_socket->setPeerVerifyName(peerName);
    } catch (const UvException &exception) {
        fail(exception.code(), exception.message(), exception.detail());
        return;
    }
    armTimer(request.connectTimeoutMs);
    m_socket->connectToHostEncrypted(request.host, request.port);
}

void HttpTunnelSocket::abort()
{
    m_timer->stop();
    if (m_socket) {
        m_socket->blockSignals(true);
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    const bool wasOpen = !m_closedEmitted;
    setPhase(Phase::Closed);
    m_closedEmitted = true;
    if (wasOpen)
        emit closed();
}

void HttpTunnelSocket::closeGracefully()
{
    if (m_socket == nullptr || m_phase != Phase::Ready) {
        abort();
        return;
    }
    m_timer->stop();
    setPhase(Phase::Closing);
    m_socket->disconnectFromHost();
}

void HttpTunnelSocket::onConnected()
{
    // Input events and screen acknowledgements are small, latency-sensitive
    // writes. Avoid holding them behind TCP's Nagle/delayed-ACK interaction.
    m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    // TCP is up; the TLS handshake is driven by Qt right after this signal.
    setPhase(Phase::Handshaking);
    armTimer(m_request.handshakeTimeoutMs);
}

void HttpTunnelSocket::onEncrypted()
{
    m_timer->stop();
    if (m_request.httpHostHeader.isEmpty()) {
        setPhase(Phase::Ready);
        emit ready();
        return;
    }
    setPhase(Phase::Preface);
    m_socket->write(protocol::httpServicePreface(m_request.httpHostHeader));
    setPhase(Phase::Header);
    armTimer(m_request.headerTimeoutMs);
    processHeader();
}

void HttpTunnelSocket::processHeader()
{
    if (m_phase != Phase::Header)
        return;
    const qsizetype end = m_headerBuffer.indexOf("\r\n\r\n");
    if (end < 0) {
        if (m_headerBuffer.size() >= kMaxHttpHeaderBytes) {
            fail(ErrorCode::Protocol, tr("HTTP header exceeds limit"),
                 QStringLiteral("%1 bytes").arg(m_headerBuffer.size()));
        }
        return;
    }
    const qsizetype cut = end + 4;
    if (cut > kMaxHttpHeaderBytes) {
        fail(ErrorCode::Protocol, tr("HTTP header exceeds limit"));
        return;
    }
    m_responseHeader = m_headerBuffer.left(cut);
    m_buffer.append(m_headerBuffer.mid(cut));
    m_headerBuffer.clear();
    m_timer->stop();
    setPhase(Phase::Ready);
    emit ready();
    if (!m_buffer.isEmpty())
        emit dataAvailable();
}

void HttpTunnelSocket::onReadyRead()
{
    if (!m_socket)
        return;
    const QByteArray chunk = m_socket->readAll();
    if (chunk.isEmpty())
        return;
    if (m_phase == Phase::Header) {
        m_headerBuffer.append(chunk);
        processHeader();
        return;
    }
    if (m_phase != Phase::Ready)
        return;
    if (m_buffer.size() + chunk.size() > m_request.readBufferLimit) {
        fail(ErrorCode::Resource, tr("the peer sent more buffered data than the limit allows"),
             QStringLiteral("%1 bytes").arg(m_request.readBufferLimit));
        return;
    }
    m_buffer.append(chunk);
    emit dataAvailable();
}

void HttpTunnelSocket::onError()
{
    if (!m_socket || m_phase == Phase::Closed || m_phase == Phase::Closing)
        return;
    const auto error = m_socket->error();
    const QString text = m_socket->errorString();
    fail(classifySocketError(error), tr("socket error"), text);
}

void HttpTunnelSocket::onSslErrors(const QList<QSslError> &errors)
{
    const QList<QSslError> ignorable = m_request.tlsPolicy.ignorableErrors(errors);
    if (!ignorable.isEmpty()) {
        // Documented compatibility mode: the policy decided these are acceptable.
        m_socket->ignoreSslErrors(ignorable);
        return;
    }
    QStringList descriptions;
    descriptions.reserve(errors.size());
    for (const QSslError &error : errors)
        descriptions.append(error.errorString());
    // A host-name mismatch is otherwise undiagnosable: name the expected host and
    // the names the peer certificate actually carries (no secret material).
    QString detail = descriptions.join(QStringLiteral("; "));
    if (!m_peerName.isEmpty())
        detail += tr("; verified name: %1").arg(m_peerName);
    if (m_socket) {
        const QSslCertificate certificate = m_socket->peerCertificate();
        if (!certificate.isNull()) {
            QStringList names;
            const auto alternativeNames = certificate.subjectAlternativeNames();
            for (auto it = alternativeNames.constBegin(); it != alternativeNames.constEnd(); ++it) {
                const QString kind = it.key() == QSsl::DnsEntry
                    ? QStringLiteral("DNS")
                    : (it.key() == QSsl::IpAddressEntry ? QStringLiteral("IP") : tr("other"));
                names.append(QStringLiteral("%1:%2").arg(kind, it.value()));
            }
            detail += tr("; peer certificate: %1; subject alternative names: %2")
                          .arg(security::TlsPolicy::describePeerCertificate(certificate),
                               names.isEmpty() ? tr("none")
                                               : names.join(QStringLiteral(", ")));
        }
    }
    fail(ErrorCode::Tls, tr("TLS certificate verification failed"), detail);
}

void HttpTunnelSocket::onDisconnected()
{
    m_timer->stop();
    if (m_phase == Phase::Closed)
        return;
    // A peer that disappears mid-handshake is a failure; one that disappears after
    // ready() is a normal end of session and is reported through closed().
    const bool wasReady = (m_phase == Phase::Ready || m_phase == Phase::Closing);
    setPhase(Phase::Closed);
    if (!m_closedEmitted) {
        m_closedEmitted = true;
        emit closed();
    }
    if (!wasReady && !m_failed)
        emit failed(ErrorInfo{ErrorCode::Network, tr("the peer closed the connection")});
}

void HttpTunnelSocket::onTimeout()
{
    QString phaseName;
    switch (m_phase) {
    case Phase::Connecting: phaseName = tr("connect"); break;
    case Phase::Handshaking: phaseName = tr("TLS handshake"); break;
    case Phase::Header: phaseName = tr("HTTP tunnel header"); break;
    default: phaseName = tr("channel"); break;
    }
    fail(ErrorCode::Timeout, tr("the channel timed out"), phaseName);
}

void HttpTunnelSocket::fail(ErrorCode code, const QString &message, const QString &detail)
{
    if (m_failed || m_phase == Phase::Closed)
        return;
    m_failed = true;
    m_timer->stop();
    if (m_socket) {
        m_socket->blockSignals(true);
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    setPhase(Phase::Closed);
    const ErrorInfo info{code, message, detail};
    if (!m_closedEmitted) {
        m_closedEmitted = true;
        emit failed(info);
        emit closed();
    } else {
        emit failed(info);
    }
}

QByteArray HttpTunnelSocket::takeBuffered()
{
    QByteArray out;
    out.swap(m_buffer);
    return out;
}

void HttpTunnelSocket::write(const QByteArray &data)
{
    if (!m_socket || m_phase != Phase::Ready || data.isEmpty())
        return;
    if (m_socket->bytesToWrite() + data.size() > m_request.readBufferLimit) {
        fail(ErrorCode::Resource, tr("outbound channel buffer exceeds limit"));
        return;
    }
    m_socket->write(data);
}

QString HttpTunnelSocket::peerDescription() const
{
    if (!m_socket)
        return tr("closed");
    const QHostAddress address = m_socket->peerAddress();
    return QStringLiteral("%1:%2").arg(address.toString()).arg(m_socket->peerPort());
}

QString HttpTunnelSocket::negotiatedProtocol() const
{
    if (!m_socket)
        return {};
    return m_socket->sessionProtocol() == QSsl::TlsV1_3 ? QStringLiteral("TLS 1.3")
                                                        : QStringLiteral("TLS");
}

QString HttpTunnelSocket::peerCertificateSummary() const
{
    if (!m_socket)
        return {};
    return security::TlsPolicy::describePeerCertificate(m_socket->peerCertificate());
}

QList<QSslCertificate> HttpTunnelSocket::peerCertificateChain() const
{
    if (m_socket == nullptr)
        return {};
    return m_socket->peerCertificateChain();
}

bool HttpTunnelSocket::isSecure() const
{
    return m_socket != nullptr && m_socket->isEncrypted();
}

} // namespace ultraview::network
