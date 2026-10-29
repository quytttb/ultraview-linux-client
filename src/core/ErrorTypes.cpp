#include "core/ErrorTypes.h"

#include <QCoreApplication>

namespace ultraview {

QString errorCodeName(ErrorCode code)
{
    switch (code) {
    case ErrorCode::None: return QStringLiteral("None");
    case ErrorCode::NotImplemented: return QStringLiteral("NotImplemented");
    case ErrorCode::UnsupportedOperation: return QStringLiteral("UnsupportedOperation");
    case ErrorCode::Network: return QStringLiteral("Network");
    case ErrorCode::Timeout: return QStringLiteral("Timeout");
    case ErrorCode::Tls: return QStringLiteral("Tls");
    case ErrorCode::Protocol: return QStringLiteral("Protocol");
    case ErrorCode::Authentication: return QStringLiteral("Authentication");
    case ErrorCode::Resource: return QStringLiteral("Resource");
    case ErrorCode::Cancelled: return QStringLiteral("Cancelled");
    case ErrorCode::Configuration: return QStringLiteral("Configuration");
    case ErrorCode::Internal: return QStringLiteral("Internal");
    }
    return QStringLiteral("Unknown");
}

QString errorCodeDescription(ErrorCode code)
{
    switch (code) {
    case ErrorCode::None:
        return QCoreApplication::translate("ErrorCode", "No error.");
    case ErrorCode::NotImplemented:
        return QCoreApplication::translate(
            "ErrorCode",
            "This protocol operation is known but not implemented by this client yet.");
    case ErrorCode::UnsupportedOperation:
        return QCoreApplication::translate(
            "ErrorCode",
            "The partner requested a screen operation this client refuses to apply, "
            "so the update was dropped instead of being silently ignored.");
    case ErrorCode::Network:
        return QCoreApplication::translate("ErrorCode", "Network connection failed.");
    case ErrorCode::Timeout:
        return QCoreApplication::translate("ErrorCode", "The peer did not answer in time.");
    case ErrorCode::Tls:
        return QCoreApplication::translate(
            "ErrorCode", "TLS negotiation or certificate verification failed.");
    case ErrorCode::Protocol:
        return QCoreApplication::translate(
            "ErrorCode", "The peer sent data that does not match the recovered protocol.");
    case ErrorCode::Authentication:
        return QCoreApplication::translate("ErrorCode", "Authentication was refused.");
    case ErrorCode::Resource:
        return QCoreApplication::translate(
            "ErrorCode", "A resource limit was exceeded and the operation was refused.");
    case ErrorCode::Cancelled:
        return QCoreApplication::translate("ErrorCode", "The operation was cancelled.");
    case ErrorCode::Configuration:
        return QCoreApplication::translate("ErrorCode", "The configuration is invalid.");
    case ErrorCode::Internal:
        return QCoreApplication::translate("ErrorCode", "Internal error.");
    }
    return QCoreApplication::translate("ErrorCode", "Unknown error.");
}

QString ErrorInfo::summary() const
{
    const QString description = errorCodeDescription(code);
    if (message.isEmpty() || message == description)
        return description;
    if (detail.isEmpty())
        return QCoreApplication::translate("ErrorInfo", "%1 (%2)").arg(description, message);
    return QCoreApplication::translate("ErrorInfo", "%1 (%2: %3)").arg(description, message, detail);
}

UvException::UvException(ErrorCode code, QString message, QString detail)
    : std::runtime_error(message.toStdString())
    , m_code(code)
    , m_message(std::move(message))
    , m_detail(std::move(detail))
{
}

UvException::~UvException() = default;

ErrorInfo UvException::info() const
{
    return ErrorInfo{m_code, m_message, m_detail};
}

void throwError(ErrorCode code, const QString &message, const QString &detail)
{
    throw UvException(code, message, detail);
}

void throwProtocol(const QString &message, const QString &detail)
{
    throw UvException(ErrorCode::Protocol, message, detail);
}

void throwUnsupported(const QString &message, const QString &detail)
{
    throw UvException(ErrorCode::UnsupportedOperation, message, detail);
}

void throwResource(const QString &message, const QString &detail)
{
    throw UvException(ErrorCode::Resource, message, detail);
}

} // namespace ultraview
