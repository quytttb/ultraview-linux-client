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
        /// Host relays receive relay-originated system messages (`0:port:ip`
        /// and `ping`).  Controller relays keep rejecting them as before.
        bool allowSystemMessages = false;
    };

    explicit RelayTransport(QObject *parent = nullptr);
    ~RelayTransport() override;

    void start(const Endpoint &endpoint, const security::TlsPolicy &policy, int timeoutMs,
               quint32 maxFrameBytes);
    void stop();

    /// Wrap `payload` in a relay frame addressed to the partner endpoint.
    /// `flag` is 1 for UTF-16LE payloads (the login frame) and 0 for ASCII.
    void sendPayload(const QByteArray &payload, quint8 flag);
    /// Wrap `payload` for an explicit viewer route (host mode).
    void sendPayloadTo(const QString &ipv4, quint16 port, const QByteArray &payload,
                       quint8 flag);
    /// Relay system envelope: `01 [flag] payload` (host relay negotiation).
    void sendSystemPayload(const QByteArray &payload, bool withFlag);
    /// The recovered relay keepalive `01 "ping"` sent every 60 seconds.
    void sendRelayKeepalive();

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
    /// Host mode: relay frame with its explicit route and UTF-16 flag.
    void payloadFrom(const QByteArray &payload, quint32 addressValue, quint16 port, bool unicode);
    /// Host mode: relay-originated system body (`0:port:ip`, `ping`).
    void systemPayload(const QByteArray &payload);
    void failed(const ultraview::ErrorInfo &error);
    void closed();

private:
    void onReady();
    void onData();
    void onClosed();
    bool writeFramed(const QByteArray &framed);

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
