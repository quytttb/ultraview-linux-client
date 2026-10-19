#pragma once
//
// Session lifecycle, mirroring the stages the Python reference goes through in
// `control_session.ControlSession.connect()` plus the reconnect/disconnect paths
// added by this client.
//
#include <QString>
#include <QMetaType>

namespace ultraview {

enum class SessionState {
    Disconnected = 0,
    /// TCP connect + TLS + HTTP tunnel preface on the registration endpoint.
    Connecting,
    /// Sending command 3 (`InitMyPassword`) and reading the server-issued identity.
    Registering,
    /// Sending command 8 (partner connection request) and reading the route reply.
    RequestingPartner,
    /// TCP connect + TLS + HTTP tunnel preface on the partner relay endpoint.
    RelayConnecting,
    /// Sending the `login:` BSTR frame and waiting for the login acknowledgement.
    Authenticating,
    Connected,
    Disconnecting,
    Failed,
};

/// Number of states; used to size the transition table.
inline constexpr int kSessionStateCount = 9;

QString sessionStateName(SessionState state);

/// True while a socket is expected to be open.
bool sessionStateIsActive(SessionState state);

/// True when the session can be torn down gracefully (as opposed to hard-abort).
bool sessionStateIsTerminal(SessionState state);

} // namespace ultraview

// The state is broadcast from the session worker thread to the GUI.
Q_DECLARE_METATYPE(ultraview::SessionState)
