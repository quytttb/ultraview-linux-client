#pragma once
//
// TLS + HTTP tunnel channel, the transport both endpoints share.
//
// Recovered sequence (`registration_transport.connect` / `session_transport.connect`):
//
//   1. TCP connect to host:443,
//   2. TLS handshake (SNI + verified host name; the reference uses the same
//      "ultraviewer" literal for the registration and relay channels),
//   3. send `POST /UVService/ HTTP/1.1 ... Host: <host>\r\n\r\n` (the preface),
//   4. read until CRLFCRLF, cap the header at 16384 bytes,
//   5. everything after that header is the framed byte stream.
//
// The class performs steps 1-5 with Qt Network's asynchronous API and a timeout
// for every phase; status-line validation is left to the transports because the
// registration channel requires exactly "HTTP/1.1 200 OK" while the relay channel
// only requires the "HTTP/1.1 200 " prefix (both differences are in the
// reference and are preserved).
//
#include <QByteArray>
#include <QObject>
#include <QString>

#include "core/ErrorTypes.h"
#include "security/TlsPolicy.h"

class QSslSocket;
class QTimer;

namespace ultraview::network {

struct TunnelRequest {
    QString host;
    quint16 port = 443;
    /// Value of the `Host:` header of the HTTP preface; empty means "do not send
    /// the preface at all" (plain TLS channel).
    QString httpHostHeader;
    /// Name the certificate is verified against; empty means "the connect host".
    QString peerNameOverride;
    security::TlsPolicy tlsPolicy;
    int connectTimeoutMs = 8000;
    int handshakeTimeoutMs = 8000;
    int headerTimeoutMs = 8000;
    /// Cap on bytes buffered after the header (backpressure).  Defaults to the
    /// reference's frame limit plus one frame.
    qint64 readBufferLimit = 4 * 1024 * 1024;
};

class HttpTunnelSocket : public QObject {
    Q_OBJECT

public:
    enum class Phase { Idle, Connecting, Handshaking, Preface, Header, Ready, Closing, Closed };

    explicit HttpTunnelSocket(QObject *parent = nullptr);
    ~HttpTunnelSocket() override;

    void start(const TunnelRequest &request);
    /// Immediate teardown; safe to call from any state and idempotent.
    void abort();

    bool isReady() const { return m_phase == Phase::Ready; }
    Phase phase() const { return m_phase; }
    const TunnelRequest &request() const { return m_request; }

    /// Bytes received after the HTTP header that the transport has not consumed.
    QByteArray takeBuffered();

    /// The HTTP tunnel response header (including the terminating CRLFCRLF), as
    /// received.  Empty when no preface was requested.  The transports validate
    /// the status line themselves because the two endpoints differ in strictness.
    QByteArray responseHeader() const { return m_responseHeader; }

    void write(const QByteArray &data);

    /// "peer 203.0.113.7:443" style description for the statistics panel.
    QString peerDescription() const;
    QString negotiatedProtocol() const;
    QString peerCertificateSummary() const;
    /// Certificate chain the peer presented (leaf first).  Used to carry the
    /// registration endpoint's verified intermediate to the relay channel.
    QList<QSslCertificate> peerCertificateChain() const;

    bool isSecure() const;

signals:
    /// TLS is up and (when a preface was requested) the HTTP header was received.
    void ready();
    /// New bytes are available through takeBuffered().
    void dataAvailable();
    void failed(const ultraview::ErrorInfo &error);
    /// Emitted once when the channel is gone (also after failed()).
    void closed();

private:
    void setPhase(Phase phase);
    void fail(ErrorCode code, const QString &message, const QString &detail = {});
    void armTimer(int timeoutMs);
    void onConnected();
    void onEncrypted();
    void onReadyRead();
    void onError();
    void onSslErrors(const QList<QSslError> &errors);
    void onDisconnected();
    void onTimeout();
    void processHeader();

    QSslSocket *m_socket = nullptr;
    QTimer *m_timer = nullptr;
    TunnelRequest m_request;
    /// Name set on the socket for certificate verification (diagnostics).
    QString m_peerName;
    Phase m_phase = Phase::Idle;
    QByteArray m_headerBuffer;
    QByteArray m_buffer;
    QByteArray m_responseHeader;
    bool m_closedEmitted = true;
    bool m_failed = false;
};

} // namespace ultraview::network
