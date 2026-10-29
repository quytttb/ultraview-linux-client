#include "protocol/AuthenticationCodec.h"

#include <QCoreApplication>
#include "core/ErrorTypes.h"
#include "protocol/Digest.h"
#include "protocol/RegistrationCodec.h"

#include <QHostInfo>
#include <QRandomGenerator>

namespace ultraview::protocol {
namespace {

bool containsWireDelimiter(const QString &value)
{
    return value.contains(QLatin1Char(':')) || value.contains(QChar(u'\0'));
}

} // namespace

QString AuthenticationCodec::connectionRequestPasswordValue(const QString &password, qint32 passwordType,
                                                            const AnsiCodec &codec)
{
    if (passwordType == 0)
        return md5HexLower(codec.encode(QString(32, QLatin1Char(' ')) + password));
    return password;
}

QString AuthenticationCodec::loginNumericSaltDigest(const QString &password, const QString &salt,
                                                    const AnsiCodec &codec)
{
    if (salt.size() != 8)
        throwError(ErrorCode::Configuration, QCoreApplication::translate("AuthenticationCodec", "login salt must contain eight digits"));
    for (const QChar unit : salt) {
        if (unit < QLatin1Char('0') || unit > QLatin1Char('9'))
            throwError(ErrorCode::Configuration, QCoreApplication::translate("AuthenticationCodec", "login salt must contain eight ASCII digits"));
    }
    return md5HexLower(codec.encode(salt + password));
}

LoginPasswordFields AuthenticationCodec::loginPasswordFields(const QString &password, qint32 passwordType,
                                                             const std::optional<QString> &salt,
                                                             const AnsiCodec &codec)
{
    if (passwordType == 0) {
        if (!salt.has_value())
            throwError(ErrorCode::Configuration, QCoreApplication::translate("AuthenticationCodec", "type 0 login requires an explicit eight-digit salt"));
        LoginPasswordFields fields;
        fields.salt = *salt;
        fields.primary = loginNumericSaltDigest(password, *salt, codec);
        fields.secondary = connectionRequestPasswordValue(password, 0, codec);
        return fields;
    }
    // Non-zero types copy TargetPassword into both digest positions and pad the
    // salt with 32 spaces (R191).
    LoginPasswordFields fields;
    fields.salt = QString(32, QLatin1Char(' '));
    fields.primary = password;
    fields.secondary = password;
    return fields;
}

QStringList AuthenticationCodec::fillLoginFields(const QStringList &fields, const QString &password,
                                                 qint32 passwordType, const std::optional<QString> &salt,
                                                 const AnsiCodec &codec)
{
    if (fields.size() != 22)
        throwError(ErrorCode::Configuration,
                   QCoreApplication::translate("AuthenticationCodec", "login requires exactly 22 fields after its prefix"),
                   QCoreApplication::translate("AuthenticationCodec", "got %1").arg(fields.size()));
    const LoginPasswordFields auth = loginPasswordFields(password, passwordType, salt, codec);
    QStringList values = fields;
    values[0] = auth.salt;
    values[1] = auth.primary;
    values[12] = auth.secondary;
    // build_login_with_password joins the filled fields through
    // build_login_command, which owns the delimiter subset; validate here so a
    // caller cannot receive a field list that cannot be encoded.
    for (const QString &value : values) {
        if (containsWireDelimiter(value))
            throwError(ErrorCode::Configuration, QCoreApplication::translate("AuthenticationCodec", "login fields cannot contain colon or NUL"));
    }
    return values;
}

QByteArray AuthenticationCodec::buildLoginCommand(const QStringList &fields)
{
    if (fields.size() != 22)
        throwError(ErrorCode::Configuration,
                   QCoreApplication::translate("AuthenticationCodec", "login requires exactly 22 fields after its prefix"),
                   QCoreApplication::translate("AuthenticationCodec", "got %1").arg(fields.size()));
    for (const QString &value : fields) {
        if (containsWireDelimiter(value))
            throwError(ErrorCode::Configuration, QCoreApplication::translate("AuthenticationCodec", "login fields cannot contain colon or NUL"));
    }
    return utf16leBytes(QStringLiteral("login:") + fields.join(QLatin1Char(':')));
}

QByteArray AuthenticationCodec::buildLoginWithPassword(const QStringList &fields, const QString &password,
                                                       qint32 passwordType, const std::optional<QString> &salt,
                                                       const AnsiCodec &codec)
{
    return buildLoginCommand(fillLoginFields(fields, password, passwordType, salt, codec));
}

QByteArray AuthenticationCodec::buildConnectionRequestCommand(const QStringList &terms, const AnsiCodec &codec)
{
    if (terms.size() != 17)
        throwError(ErrorCode::Configuration,
                   QCoreApplication::translate("AuthenticationCodec", "command8 requires 17 string terms"),
                   QCoreApplication::translate("AuthenticationCodec", "got %1").arg(terms.size()));
    if (terms.at(0) != QLatin1String("8"))
        throwError(ErrorCode::Configuration, QCoreApplication::translate("AuthenticationCodec", "command8 must begin with 8"));
    for (const QString &term : terms) {
        if (containsWireDelimiter(term))
            throwError(ErrorCode::Configuration, QCoreApplication::translate("AuthenticationCodec", "command8 terms must not contain colon or NUL"));
    }
    return codec.encode(terms.join(QLatin1Char(':')));
}

QStringList AuthenticationCodec::resolveConnectionRequestTerms(const ConnectionRequestContext &context,
                                                               qint32 controllerId)
{
    if (context.templateTerms.size() != 17)
        throwError(ErrorCode::Configuration,
                   QCoreApplication::translate("AuthenticationCodec", "partner request needs seventeen terms"),
                   QCoreApplication::translate("AuthenticationCodec", "got %1").arg(context.templateTerms.size()));
    QStringList terms = context.templateTerms;
    if (!context.partnerId.isEmpty())
        terms[1] = context.partnerId;
    terms[2] = connectionRequestPasswordValue(context.password, context.passwordType, context.codec);
    terms[6] = QString::number(controllerId);
    // Request digest: MD5(ANSI(clientId + registrationProfile.terms[2])) (E163).
    terms[7] = md5HexLower(context.codec.encode(terms.at(6) + context.registrationTerm2));
    return terms;
}

QString AuthenticationCodec::connectionRequestP2pSuffix(bool enabled, const QString &publicIp,
                                                        const QString &publicSourcePort,
                                                        const QString &localIp, const QString &localPort,
                                                        const QString &encryptionKey)
{
    if (!enabled || publicIp.isEmpty())
        return QString();
    const QStringList fields{publicIp, publicSourcePort, localIp, localPort, encryptionKey};
    // Delimiter rejection is a reconstruction policy of the reference, not native
    // escaping behaviour.
    for (const QString &value : fields) {
        if (value.contains(QLatin1Char('|')) || containsWireDelimiter(value))
            throwError(ErrorCode::Configuration, QCoreApplication::translate("AuthenticationCodec", "P2P suffix fields must not contain wire delimiters"));
    }
    return fields.join(QLatin1Char('|'));
}

QString AuthenticationCodec::randomDigits(int count)
{
    if (count <= 0)
        return QString();
    QString out;
    out.reserve(count);
    QRandomGenerator *generator = QRandomGenerator::system();
    for (int index = 0; index < count; ++index)
        out.append(QChar(u'0' + static_cast<ushort>(generator->bounded(10))));
    return out;
}

QString AuthenticationCodec::clientVersion()
{
    return QStringLiteral("6.6.133");
}

QStringList AuthenticationCodec::defaultLoginFields(qint32 controllerId, const QString &hostName,
                                                    const QString &partnerSecretToken, int viewportWidth,
                                                    int viewportHeight)
{
    const auto validViewport = [](int value) { return value > 0 && value <= 8192; };
    if (!validViewport(viewportWidth) || !validViewport(viewportHeight))
        throwError(ErrorCode::Configuration, QCoreApplication::translate("AuthenticationCodec", "viewport dimensions are outside the modelled range"));
    // Field order copied from `control_session.ControlSession.connect`; indices in
    // the comments are the 0-based wire positions.
    return QStringList{
        QString(),                                        //  0 salt (filled by loginPasswordFields)
        QString(),                                        //  1 primary digest (filled)
        QString::number(viewportWidth),                   //  2 negotiation width
        QString::number(viewportHeight),                  //  3 negotiation height
        QStringLiteral("30"),                             //  4
        QStringLiteral("15"),                             //  5
        QStringLiteral("15"),                             //  6
        QStringLiteral("0"),                              //  7
        QStringLiteral("0"),                              //  8
        QString::number(controllerId),                    //  9 controller id issued by command 3
        hostName,                                         // 10 controller host name
        QStringLiteral("0"),                              // 11
        QString(),                                        // 12 secondary digest (filled)
        QString(),                                        // 13
        randomDigits(16),                                 // 14
        QStringLiteral("2"),                              // 15
        QStringLiteral("80"),                             // 16
        QStringLiteral("0"),                              // 17
        QStringLiteral("0"),                              // 18
        partnerSecretToken,                               // 19 route partner secret token
        clientVersion(),                                  // 20
        QStringLiteral("0"),                              // 21
    };
}

} // namespace ultraview::protocol
