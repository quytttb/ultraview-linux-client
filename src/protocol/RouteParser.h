#pragma once
//
// Command 8 reply parser: the route the registration server hands to the client.
//
// Ported from `target_route.decode_target_route` (Form1.ManipulateTargetInfo) and
// the status handling of `control_session.ControlSession.connect`.
//
#include <QByteArray>
#include <QMetaType>
#include <QString>

#include "core/ErrorTypes.h"
#include "protocol/AnsiCodec.h"

#include <cstdint>
#include <optional>

namespace ultraview::protocol {

/// Route fields recovered from a status-1 command 8 reply.  Endpoint strings keep
/// their native sentinel values (for example "0").
struct TargetRoute {
    QString lanIp;
    QString publicIp;
    quint16 publicPort = 0;
    quint16 lanPort = 0;
    quint16 commandPort = 0;
    QString computerNameEncoded;
    quint16 tcpPort = 0;
    QString tcpServerIp;
    qint32 matchedCustomPassword = 0;
    qint32 targetSessionId = 0;
    QString tcpServerHostname;
    std::optional<QString> partnerSecretToken;
};

/// Outcome of a command 8 reply: either a route or a server-side refusal.  The
/// reference maps status codes to messages with these exact meanings:
///   2  -> wrong password
///   11 -> partner offline or unknown ID
///   99 -> the server could not reach the partner
class RouteParser {
public:
    static TargetRoute parseRoute(const QByteArray &payload, const AnsiCodec &codec);

    /// True when the payload is a command 8 reply whose status field is not "1"
    /// (i.e. the server refused the request).
    static bool isFailureStatus(const QByteArray &payload, const AnsiCodec &codec);

    /// Extract the numeric status field of a command 8 reply.
    static QString statusField(const QByteArray &payload, const AnsiCodec &codec);

    /// Map a refusal status to a classified error.  Never mentions credentials.
    static UvException authenticationError(const QString &status);
};

} // namespace ultraview::protocol

/// Sent from the session worker thread to the GUI, so it must be a metatype.
Q_DECLARE_METATYPE(ultraview::protocol::TargetRoute)
