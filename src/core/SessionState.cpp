#include "core/SessionState.h"

namespace ultraview {

QString sessionStateName(SessionState state)
{
    switch (state) {
    case SessionState::Disconnected: return QStringLiteral("Disconnected");
    case SessionState::Connecting: return QStringLiteral("Connecting");
    case SessionState::Registering: return QStringLiteral("Registering");
    case SessionState::RequestingPartner: return QStringLiteral("RequestingPartner");
    case SessionState::RelayConnecting: return QStringLiteral("RelayConnecting");
    case SessionState::Authenticating: return QStringLiteral("Authenticating");
    case SessionState::Connected: return QStringLiteral("Connected");
    case SessionState::Disconnecting: return QStringLiteral("Disconnecting");
    case SessionState::Failed: return QStringLiteral("Failed");
    }
    return QStringLiteral("Unknown");
}

bool sessionStateIsActive(SessionState state)
{
    switch (state) {
    case SessionState::Connecting:
    case SessionState::Registering:
    case SessionState::RequestingPartner:
    case SessionState::RelayConnecting:
    case SessionState::Authenticating:
    case SessionState::Connected:
    case SessionState::Disconnecting:
        return true;
    case SessionState::Disconnected:
    case SessionState::Failed:
        return false;
    }
    return false;
}

bool sessionStateIsTerminal(SessionState state)
{
    return state == SessionState::Disconnected || state == SessionState::Failed;
}

} // namespace ultraview
