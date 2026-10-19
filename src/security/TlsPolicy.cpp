#include "security/TlsPolicy.h"

#include "core/ErrorTypes.h"

#include <QFile>
#include <QSslError>
#include <QStringList>

namespace ultraview::security {

QSslConfiguration TlsPolicy::configurationFor() const
{
    QSslConfiguration configuration = QSslConfiguration::defaultConfiguration();

    // Extra CAs (private deployment or test endpoint) are added on top of the
    // system store; the system store is never replaced.
    if (!m_additionalCaFile.isEmpty()) {
        QFile file(m_additionalCaFile);
        if (!file.open(QIODevice::ReadOnly)) {
            throwError(ErrorCode::Tls, QStringLiteral("cannot read the additional CA bundle"),
                       m_additionalCaFile);
        }
        const QList<QSslCertificate> extra = QSslCertificate::fromData(file.readAll(), QSsl::Pem);
        if (extra.isEmpty())
            throwError(ErrorCode::Tls, QStringLiteral("the additional CA bundle contains no certificate"),
                       m_additionalCaFile);
        QList<QSslCertificate> authorities = configuration.caCertificates();
        authorities.append(extra);
        configuration.setCaCertificates(authorities);
    }

    if (m_mode == Mode::AcceptAnyCertificate) {
        // The original program's callback accepts everything.  Verification is
        // requested and then explicitly waived in `ignorableErrors`, so the
        // decision is visible in one place.
        configuration.setPeerVerifyMode(QSslSocket::VerifyNone);
    } else {
        configuration.setPeerVerifyMode(QSslSocket::VerifyPeer);
    }
    return configuration;
}

QList<QSslError> TlsPolicy::ignorableErrors(const QList<QSslError> &errors) const
{
    switch (m_mode) {
    case Mode::VerifyPeer:
        return {};
    case Mode::VerifyPeerAllowHostMismatch: {
        QList<QSslError> ignorable;
        for (const QSslError &error : errors) {
            if (error.error() == QSslError::HostNameMismatch)
                ignorable.append(error);
        }
        return ignorable;
    }
    case Mode::AcceptAnyCertificate:
        return errors;
    }
    return {};
}

QString TlsPolicy::warningText() const
{
    switch (m_mode) {
    case Mode::VerifyPeer:
        return {};
    case Mode::VerifyPeerAllowHostMismatch:
        return QStringLiteral(
            "Certificate chain verification is active, but the certificate host name is not "
            "checked. Only use this when the deployment really serves a different name.");
    case Mode::AcceptAnyCertificate:
        return QStringLiteral(
            "TLS certificate verification is DISABLED (the original program's behaviour). "
            "The server identity is not authenticated; anyone able to intercept the connection "
            "can read or modify the session, including keystrokes and screen contents.");
    }
    return {};
}

QString TlsPolicy::settingsValue() const
{
    switch (m_mode) {
    case Mode::VerifyPeer: return QStringLiteral("verify");
    case Mode::VerifyPeerAllowHostMismatch: return QStringLiteral("verify-allow-hostname-mismatch");
    case Mode::AcceptAnyCertificate: return QStringLiteral("accept-any");
    }
    return QStringLiteral("verify");
}

TlsPolicy TlsPolicy::fromSettingsValue(const QString &value)
{
    if (value == QLatin1String("accept-any"))
        return TlsPolicy(Mode::AcceptAnyCertificate);
    if (value == QLatin1String("verify-allow-hostname-mismatch"))
        return TlsPolicy(Mode::VerifyPeerAllowHostMismatch);
    return TlsPolicy(Mode::VerifyPeer);
}

QString TlsPolicy::describePeerCertificate(const QSslCertificate &certificate)
{
    if (certificate.isNull())
        return QStringLiteral("no certificate");
    const QStringList names = certificate.subjectInfo(QSslCertificate::CommonName);
    const QStringList altNames = certificate.subjectAlternativeNames().values().isEmpty()
        ? QStringList()
        : QStringList{QString::number(certificate.subjectAlternativeNames().count())};
    return QStringLiteral("subject CN=%1, issuer CN=%2, valid until %3")
        .arg(names.join(QLatin1Char(',')),
             certificate.issuerInfo(QSslCertificate::CommonName).join(QLatin1Char(',')),
             certificate.expiryDate().toString(Qt::ISODate));
}

} // namespace ultraview::security
