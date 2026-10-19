#pragma once
//
// Registration channel: command 3 ("InitMyPassword") and its reply.
//
// Recovered behaviour (`registration_transport.py`):
//   * connect to functions3.ultraviewer.net:443, TLS, then the HTTP preface with
//     `Host: <host>`,
//   * the answer must be exactly "HTTP/1.1 200 OK",
//   * outbound commands are `\x01\x00` + TripleDES(ANSI command) inside a
//     length-framed body (mode 1 frame with the "unicode_bytes" flag 0),
//   * inbound bodies must start with 0x01, the rest is the plaintext payload.
//
// The channel stays open while the relay session runs (the reference keeps the
// registration transport alive) and is drained in the background so the socket
// buffer cannot fill up.
//
#include <QByteArray>
#include <QObject>
#include <QString>

#include "core/ErrorTypes.h"
#include "network/HttpTunnelSocket.h"
#include "protocol/PacketCodec.h"

namespace ultraview::network {

class RegistrationTransport : public QObject {
    Q_OBJECT

public:
    struct Options {
        QString host = QStringLiteral("functions3.ultraviewer.net");
        quint16 port = 443;
        int timeoutMs = 8000;
        quint32 maxFrameBytes = protocol::kDefaultMaxFrameBytes;
        security::TlsPolicy tlsPolicy;
        /// Overrides the `Host:` header of the preface (defaults to `host`).
        QString httpHostHeader;
        /// Name verified on the certificate; the reference uses "ultraviewer".
        /// Empty means "verify the connect host", which is standard TLS.
        QString peerNameOverride;
    };

    explicit RegistrationTransport(QObject *parent = nullptr);
    ~RegistrationTransport() override;

    void setOptions(const Options &options) { m_options = options; }
    const Options &options() const { return m_options; }

    /// Connect, handshake and validate the tunnel header; emits ready().
    void start();
    void stop();

    /// Encrypt and send a caller-supplied ANSI command.
    void sendCommand(const QByteArray &plaintextAnsi);

    /// After the route is known the channel is no longer part of the control path
    /// (the reference keeps it open but never reads it).  In drain mode incoming
    /// bytes are counted and dropped instead of being interpreted, so a late
    /// server message cannot break a running session.
    void setDrainPayloads(bool drain) { m_drainPayloads = drain; }
    bool drainingPayloads() const { return m_drainPayloads; }

    bool isReady() const { return m_ready; }
    QString peerDescription() const { return m_channel.peerDescription(); }
    QString peerCertificateSummary() const { return m_channel.peerCertificateSummary(); }
    QString negotiatedProtocol() const { return m_channel.negotiatedProtocol(); }
    qint64 bytesSent() const { return m_bytesSent; }
    qint64 bytesReceived() const { return m_bytesReceived; }
    /// Payloads that arrived after the route step and were drained without being
    /// interpreted (kept for diagnostics instead of being silently lost).
    int ignoredPayloads() const { return m_ignoredPayloads; }
    qint64 ignoredBytes() const { return m_ignoredBytes; }
    QString ignoredPayloadSummary() const { return m_ignoredSummary; }

signals:
    void ready();
    /// One plaintext mode-1 payload, in arrival order.
    void payload(const QByteArray &payload);
    void failed(const ultraview::ErrorInfo &error);
    void closed();

private:
    void onReady();
    void onData();
    void onClosed();

    Options m_options;
    HttpTunnelSocket m_channel;
    protocol::FrameStream m_frames;
    bool m_ready = false;
    bool m_drainPayloads = false;
    qint64 m_bytesSent = 0;
    qint64 m_bytesReceived = 0;
    qint64 m_ignoredBytes = 0;
    int m_ignoredPayloads = 0;
    QString m_ignoredSummary;
};

} // namespace ultraview::network
