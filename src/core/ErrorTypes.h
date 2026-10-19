#pragma once
//
// Error taxonomy for the native client.
//
// The Python reference signals faults with `ValueError`/`TimeoutError`/socket
// errors and (in `viewer.py`) keeps only `type(exc).__name__` for diagnostics.
// The native client needs the same failures to be *actionable*, so every fault
// is classified once, here, and every user-visible message is produced from that
// classification (never by leaking protocol bytes, passwords or tokens).
//
#include <QString>
#include <QMetaType>
#include <stdexcept>

namespace ultraview {

enum class ErrorCode {
    None = 0,
    /// The feature exists in the wire protocol but this client does not implement it.
    NotImplemented,
    /// A screen/transport operation was observed but is deliberately refused
    /// (for example BitBlt in field 5 of a kind-1 screen update).
    UnsupportedOperation,
    Network,
    Timeout,
    Tls,
    /// Wire format violation: wrong magic, truncated packet, out-of-range field.
    Protocol,
    /// Server refused authentication (wrong password, unknown/offline partner).
    Authentication,
    /// A bound would be exceeded (decompression bomb, oversized frame, huge canvas).
    Resource,
    Cancelled,
    Configuration,
    Internal,
};

QString errorCodeName(ErrorCode code);

/// Human readable, credential-free description suitable for the UI.
QString errorCodeDescription(ErrorCode code);

struct ErrorInfo {
    ErrorCode code = ErrorCode::None;
    QString message{};
    QString detail{};

    bool isError() const { return code != ErrorCode::None; }
    /// "message" plus, when present, the detail, joined for a status bar.
    QString summary() const;
};

/// Exception type used for all recoverable protocol/transport faults.
///
/// Exceptions are only thrown inside parsing/validation and transport helpers and
/// are always caught at the boundaries (`ConnectionManager`, transports) so that
/// the GUI thread never sees one.
class UvException : public std::runtime_error {
public:
    UvException(ErrorCode code, QString message, QString detail = {});
    ~UvException() override;

    ErrorCode code() const noexcept { return m_code; }
    /// Stable, credential-free message (ASCII safe for logs).
    QString message() const { return m_message; }
    QString detail() const { return m_detail; }
    ErrorInfo info() const;

private:
    ErrorCode m_code;
    QString m_message;
    QString m_detail;
};

[[noreturn]] void throwError(ErrorCode code, const QString &message, const QString &detail = {});

/// Throws ErrorCode::Protocol.
[[noreturn]] void throwProtocol(const QString &message, const QString &detail = {});
/// Throws ErrorCode::UnsupportedOperation.
[[noreturn]] void throwUnsupported(const QString &message, const QString &detail = {});
/// Throws ErrorCode::Resource.
[[noreturn]] void throwResource(const QString &message, const QString &detail = {});

} // namespace ultraview

Q_DECLARE_METATYPE(ultraview::ErrorInfo)
Q_DECLARE_METATYPE(ultraview::ErrorCode)
