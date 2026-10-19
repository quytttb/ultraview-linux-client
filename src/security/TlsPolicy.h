#pragma once
//
// TLS policy for the recovered endpoints.
//
// Facts recovered from the reference implementation:
//   * `registration_transport.py` connects with `ssl.create_default_context()`
//     (full verification) and `server_hostname="ultraviewer"`, i.e. it requests
//     SNI "ultraviewer" and verifies that host name;
//   * its `original_certificate_policy` flag mirrors the original RC40 certificate
//     callback that **returns True for every certificate** (`check_hostname=False`,
//     `verify_mode=CERT_NONE`).  RUN.md states that this matches the original
//     program but does not authenticate the server, and that standard verification
//     "has not been proven to work with the current infrastructure".
//
// The native client therefore:
//   * verifies by default (chain + host name of the endpoint it connects to),
//   * offers `AcceptAnyCertificate` only as an explicit, warned, opt-in
//     compatibility mode that must be enabled per connection in the UI,
//   * never disables verification silently, and reports which mode is active in
//     the session statistics and in a persistent UI warning.
//
#include <QList>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QString>

namespace ultraview::security {

class TlsPolicy {
public:
    enum class Mode {
        /// Full verification: chain, validity and host name.
        VerifyPeer = 0,
        /// Chain verification with the host-name check disabled.  Useful when the
        /// deployed certificate legitimately carries a different name; still
        /// authenticated, still warned about.
        VerifyPeerAllowHostMismatch,
        /// The original program's behaviour: every certificate is accepted.
        /// Explicit opt-in, always warned about, never the default.
        AcceptAnyCertificate,
    };

    TlsPolicy() = default;
    explicit TlsPolicy(Mode mode) : m_mode(mode) {}

    Mode mode() const { return m_mode; }
    void setMode(Mode mode) { m_mode = mode; }

    /// PEM bundle of additional trusted CAs (used for private deployments and by
    /// the offline test suite, which runs its own TLS endpoint).
    void setAdditionalCaCertificateFile(const QString &path) { m_additionalCaFile = path; }
    QString additionalCaCertificateFile() const { return m_additionalCaFile; }

    /// The SSL configuration to install on the socket *before* connecting.
    /// Peer-name verification is not part of `QSslConfiguration`; callers pass
    /// the expected name to `QSslSocket::setPeerVerifyName()` (see
    /// `HttpTunnelSocket::connect`).
    QSslConfiguration configurationFor() const;

    /// Error codes that may be ignored for this policy, given the errors the
    /// socket reported.  Empty for Mode::VerifyPeer.
    QList<QSslError> ignorableErrors(const QList<QSslError> &errors) const;

    /// True when the policy cannot authenticate the peer; used for UI warnings.
    bool isInsecure() const { return m_mode == Mode::AcceptAnyCertificate; }

    /// User-facing warning text (empty for a fully verifying policy).
    QString warningText() const;

    /// Stable identifier for settings/logs.
    QString settingsValue() const;
    static TlsPolicy fromSettingsValue(const QString &value);

    /// Certificate chain of the connected peer, for the diagnostics panel.
    static QString describePeerCertificate(const QSslCertificate &certificate);

private:
    Mode m_mode = Mode::VerifyPeer;
    QString m_additionalCaFile;
};

} // namespace ultraview::security
