#pragma once
//
// Relay channel: the tunnel to the partner's relay server.
//
// Recovered behaviour (`session_transport.py`):
//   * connect to `route.tcp_server_ip:443` with the *relay* host name as the HTTP
//     tunnel host (`http_hostname=route.tcp_server_hostname`); the reference only
//     requires the status line to start with "HTTP/1.1 200 ",
//   * outbound payloads are wrapped in a relay frame that repeats the partner's
//     public IP and TCP port from the route (`encode_relay_frame`),
//   * inbound bodies are transport headers (`decode_transport_body`) whose kind
//     must be relay; the payload is decoded by the caller (ConnectionManager).
//
#include <QByteArray>
#include <QObject>
#include <QString>

#include "core/ErrorTypes.h"
#include "network/HttpTunnelSocket.h"
#include "protocol/PacketCodec.h"

namespace ultraview::network {

class RelayTransport : public QObject {
    Q_OBJECT

public:
    struct Endpoint {
        /// Connect host (route.tcp_server_ip).
        QString host;
        quint16 port = 443;
        /// `Host:` header of the HTTP preface (route.tcp_server_hostname).
        QString httpHostHeader;
        /// Certificate name to verify (route.tcp_server_hostname when set).
        QString peerNameOverride;
        /// Partner endpoint repeated in every outbound relay frame.
        QString targetIpv4;
        quint16 targetPort = 0;
    };

    explicit RelayTransport(QObject *parent = nullptr);
    ~RelayTransport() override;

    void start(const Endpoint &endpoint, const security::TlsPolicy &policy, int timeoutMs,
               quint32 maxFrameBytes);
    void stop();

    /// Wrap `payload` in a relay frame addressed to the partner endpoint.
    /// `flag` is 1 for UTF-16LE payloads (the login frame) and 0 for ASCII.
    void sendPayload(const QByteArray &payload, quint8 flag);

    bool isReady() const { return m_ready; }
    QString peerDescription() const { return m_channel.peerDescription(); }
    QString peerCertificateSummary() const { return m_channel.peerCertificateSummary(); }
    QString negotiatedProtocol() const { return m_channel.negotiatedProtocol(); }
    qint64 bytesSent() const { return m_bytesSent; }
    qint64 bytesReceived() const { return m_bytesReceived; }
    bool errorReported() const { return m_errorReported; }

signals:
    void ready();
    /// Payload of a relay-kind transport body.
    void payload(const QByteArray &payload);
    void failed(const ultraview::ErrorInfo &error);
    void closed();

private:
    void onReady();
    void onData();
    void onClosed();

    Endpoint m_endpoint;
    HttpTunnelSocket m_channel;
    protocol::FrameStream m_frames;
    security::TlsPolicy m_policy;
    quint32 m_maxFrameBytes = protocol::kDefaultMaxFrameBytes;
    bool m_ready = false;
    bool m_errorReported = false;
    qint64 m_bytesSent = 0;
    qint64 m_bytesReceived = 0;
};

} // namespace ultraview::network
