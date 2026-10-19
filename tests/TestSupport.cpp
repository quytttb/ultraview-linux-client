#include "TestSupport.h"

#include "core/ErrorTypes.h"
#include "protocol/PacketCodec.h"
#include "protocol/RegistrationCodec.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QSslKey>
#include <QSslSocket>
#include <QTcpServer>
#include <QThread>

#include <cstring>

using namespace ultraview;
using namespace ultraview::protocol;

namespace uvtest {
namespace {

QByteArray int32le(qint32 value)
{
    const quint32 bits = static_cast<quint32>(value);
    QByteArray out(4, '\0');
    out[0] = static_cast<char>(bits & 0xFFu);
    out[1] = static_cast<char>((bits >> 8) & 0xFFu);
    out[2] = static_cast<char>((bits >> 16) & 0xFFu);
    out[3] = static_cast<char>((bits >> 24) & 0xFFu);
    return out;
}

/// The listener hands the raw descriptor to the mock server instead of creating a
/// QTcpSocket, which is the pattern Qt documents for TLS servers: only then can a
/// QSslSocket own the accepted connection.
class MockListenServer : public QTcpServer {
public:
    explicit MockListenServer(MockUltraviewServer *owner)
        : QTcpServer(owner)
        , m_owner(owner)
    {
    }

protected:
    void incomingConnection(qintptr socketDescriptor) override { m_owner->adoptSocket(socketDescriptor); }

private:
    MockUltraviewServer *m_owner = nullptr;
};

enum class FrameResult { Incomplete, Complete, TooLarge };

FrameResult takeFrame(QByteArray *buffer, QByteArray *body, quint32 maxFrameBytes, quint32 *declared)
{
    if (buffer->size() < 4)
        return FrameResult::Incomplete;
    const uchar *header = reinterpret_cast<const uchar *>(buffer->constData());
    const quint32 length = static_cast<quint32>(header[0]) | (static_cast<quint32>(header[1]) << 8)
        | (static_cast<quint32>(header[2]) << 16) | (static_cast<quint32>(header[3]) << 24);
    if (declared != nullptr)
        *declared = length;
    if (length > maxFrameBytes)
        return FrameResult::TooLarge;
    if (static_cast<quint32>(buffer->size() - 4) < length)
        return FrameResult::Incomplete;
    *body = buffer->mid(4, static_cast<int>(length));
    buffer->remove(0, static_cast<int>(length) + 4);
    return FrameResult::Complete;
}

/// RFC-style rendering of the recovered route fields (fixture `route`).
QByteArray buildRoutePayload(const QString &tcpServerIp, quint16 tcpPort, const QString &publicIp,
                             quint16 publicPort, const QString &hostname, const QString &secretToken)
{
    const QStringList fields{
        QStringLiteral("8"),           // 0  command
        QStringLiteral("1"),           // 1  status 1 = route follows
        publicIp,                      // 2  LAN ip of the partner (same value in the fixture)
        QStringLiteral("0"),           // 3  unused
        publicIp,                      // 4  public ip of the partner
        QString::number(publicPort),   // 5  public port
        QString::number(publicPort),   // 6  LAN port
        QStringLiteral("49999"),       // 7  command port
        QStringLiteral("DESKTOP-UV"),  // 8  computer name
        QString::number(tcpPort),      // 9  partner tcp port
        QStringLiteral("0"),           // 10 unused
        tcpServerIp,                   // 11 relay host
        QStringLiteral("0"),           // 12 matched custom password
        QStringLiteral("7654321"),     // 13 target session id
        hostname,                      // 14 relay host name for the Host: header
        QStringLiteral("0"),           // 15 unused
        QStringLiteral("0"),           // 16 unused
        QStringLiteral("0"),           // 17 unused
        QStringLiteral("0"),           // 18 unused
        QStringLiteral("0"),           // 19 unused
        secretToken,                   // 20 partner secret token
    };
    return fields.join(QLatin1Char(':')).toLatin1();
}

} // namespace

// ---------------------------------------------------------------------------
// hex helpers
// ---------------------------------------------------------------------------

QByteArray fromHex(const QString &hex)
{
    return QByteArray::fromHex(hex.trimmed().toLatin1());
}

QString toHex(const QByteArray &bytes)
{
    return QString::fromLatin1(bytes.toHex());
}

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

#ifndef UV_TEST_FIXTURES
#error "tests/CMakeLists.txt must define UV_TEST_FIXTURES (path of fixtures.json)"
#endif

Fixtures::Fixtures()
{
    QFile file(QStringLiteral(UV_TEST_FIXTURES));
    if (!file.open(QIODevice::ReadOnly)) {
        qFatal("cannot open the byte-level fixtures at %s: %s", UV_TEST_FIXTURES,
               qPrintable(file.errorString()));
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        qFatal("fixtures.json is not valid JSON: %s", qPrintable(parseError.errorString()));
    }
    m_root = document.object();
    m_dataDirectory = QFileInfo(QStringLiteral(UV_TEST_FIXTURES)).absolutePath();

    QFile dictionary(m_dataDirectory + QStringLiteral("/test-dict.bin"));
    if (!dictionary.open(QIODevice::ReadOnly)) {
        qFatal("cannot open the synthetic test dictionary %s: %s",
               qPrintable(m_dataDirectory + QStringLiteral("/test-dict.bin")),
               qPrintable(dictionary.errorString()));
    }
    m_dictionary = dictionary.readAll();
}

const Fixtures &Fixtures::instance()
{
    static const Fixtures fixtures;
    return fixtures;
}

QJsonObject Fixtures::object(const QString &key) const
{
    return m_root.value(key).toObject();
}

QJsonArray Fixtures::array(const QString &key) const
{
    return m_root.value(key).toArray();
}

QByteArray Fixtures::bytes(const QString &key) const
{
    const QJsonValue value = m_root.value(key);
    if (value.isString())
        return fromHex(value.toString());
    if (value.isObject())
        return entryBytes(value.toObject());
    return QByteArray();
}

QByteArray Fixtures::entryBytes(const QJsonObject &object, const QString &member)
{
    return fromHex(object.value(member).toString());
}

QString Fixtures::dictionaryPath() const
{
    return m_dataDirectory + QStringLiteral("/test-dict.bin");
}

QString Fixtures::tlsCertificatePath() const
{
    return m_dataDirectory + QStringLiteral("/tls/server.crt");
}

// ---------------------------------------------------------------------------
// Shared session data
// ---------------------------------------------------------------------------

QByteArray routePayload(const QString &tcpServerIp, quint16 tcpPort, const QString &publicIp,
                        quint16 publicPort, const QString &hostname, const QString &secretToken)
{
    return buildRoutePayload(tcpServerIp, tcpPort, publicIp, publicPort, hostname, secretToken);
}

QStringList testRegistrationTerms()
{
    // The recovered schema requires: 26 terms, term 0 == "3", term 3 == "0",
    // term 5 == "0", term 12 == "", term 22 == three tick strings joined by ':'
    // and no other term may contain ':' or NUL (RegistrationCodec validates all of
    // this, so a mistake here fails the test loudly).
    QStringList terms;
    terms.reserve(26);
    for (int index = 0; index < 26; ++index)
        terms.append(QStringLiteral("0"));
    terms[0] = QStringLiteral("3");
    terms[1] = QStringLiteral("5001");
    terms[2] = QStringLiteral("5123456");
    terms[3] = QStringLiteral("0");
    terms[4] = QStringLiteral("test");
    terms[5] = QStringLiteral("0");
    terms[6] = QStringLiteral("5123456");
    terms[7] = QStringLiteral("639000000000000000");
    terms[8] = QStringLiteral("-1");
    terms[9] = QStringLiteral("1920");
    terms[10] = QStringLiteral("1080");
    terms[11] = QStringLiteral("8");
    terms[12] = QString();
    terms[13] = QStringLiteral("420");
    terms[14] = QStringLiteral("1");
    terms[15] = QStringLiteral("0");
    terms[16] = QStringLiteral("0");
    terms[17] = QStringLiteral("10");
    terms[18] = QStringLiteral("1");
    terms[19] = QStringLiteral("0");
    terms[20] = QStringLiteral("0");
    terms[21] = QStringLiteral("0");
    terms[22] = QStringLiteral("638000000000000000:638000000000000001:638000000000000002");
    terms[23] = QStringLiteral("0");
    terms[24] = QStringLiteral("0");
    terms[25] = QStringLiteral("test-profile");
    return terms;
}

QStringList testRequestTerms()
{
    // Term 2 (password value), 6 (controller id) and 7 (request digest) are
    // replaced by AuthenticationCodec; the placeholders are deliberately obvious.
    QStringList terms;
    terms.reserve(17);
    for (int index = 0; index < 17; ++index)
        terms.append(QStringLiteral("0"));
    terms[0] = QStringLiteral("8");
    terms[1] = QStringLiteral("123456789"); // partner id
    terms[3] = QStringLiteral("6.6.133");
    terms[4] = QStringLiteral("26");
    terms[5] = QStringLiteral("2");
    terms[8] = QStringLiteral("-1");
    terms[9] = QStringLiteral("0");
    terms[10] = QStringLiteral("0");
    terms[11] = QStringLiteral("1");
    terms[12] = QStringLiteral("0");
    terms[13] = QStringLiteral("420");
    terms[14] = QStringLiteral("0");
    terms[15] = QStringLiteral("0");
    terms[16] = QString(); // no P2P suffix in a relay-only test
    return terms;
}

// ---------------------------------------------------------------------------
// MockUltraviewServer
// ---------------------------------------------------------------------------

MockUltraviewServer::MockUltraviewServer(QObject *parent)
    : QObject(parent)
{
    auto *listener = new MockListenServer(this);
    m_server = listener;
    connect(m_server, &QTcpServer::newConnection, this, &MockUltraviewServer::onNewConnection);
}

MockUltraviewServer::~MockUltraviewServer()
{
    for (QSslSocket *socket : std::as_const(m_sockets))
        socket->abort();
}

QString MockUltraviewServer::certificatePath() const
{
    return m_options.certificateFile.isEmpty() ? Fixtures::instance().tlsCertificatePath()
                                             : m_options.certificateFile;
}

bool MockUltraviewServer::loadCertificate()
{
    if (m_certificateLoaded)
        return true;
    const QString directory = Fixtures::instance().dataDirectory();
    QFile certificateFile(certificatePath());
    QFile keyFile(m_options.privateKeyFile.isEmpty() ? directory + QStringLiteral("/tls/server.key")
                                                   : m_options.privateKeyFile);
    if (!certificateFile.open(QIODevice::ReadOnly) || !keyFile.open(QIODevice::ReadOnly)) {
        fail(QStringLiteral("the test TLS certificate is missing in %1").arg(directory));
        return false;
    }
    m_certificate = QSslCertificate(&certificateFile, QSsl::Pem);
    m_privateKey = QSslKey(&keyFile, QSsl::Rsa, QSsl::Pem);
    if (m_certificate.isNull() || m_privateKey.isNull()) {
        fail(QStringLiteral("the test TLS certificate could not be parsed"));
        return false;
    }
    m_certificateLoaded = true;
    return true;
}

bool MockUltraviewServer::listen()
{
    if (!loadCertificate())
        return false;
    if (!m_server->listen(QHostAddress::LocalHost, 0)) {
        fail(QStringLiteral("cannot listen on 127.0.0.1: %1").arg(m_server->errorString()));
        return false;
    }
    m_port = m_server->serverPort();
    return true;
}

void MockUltraviewServer::adoptSocket(qintptr socketDescriptor)
{
    if (!loadCertificate()) {
        // Close the raw descriptor through a throw-away socket so that no file
        // descriptor leaks when the certificate is unusable.
        QTcpSocket closer;
        closer.setSocketDescriptor(socketDescriptor);
        closer.abort();
        return;
    }

    auto *socket = new QSslSocket(this);
    if (!socket->setSocketDescriptor(socketDescriptor)) {
        fail(QStringLiteral("cannot adopt the accepted connection: %1").arg(socket->errorString()));
        delete socket;
        return;
    }
    socket->setLocalCertificate(m_certificate);
    socket->setPrivateKey(m_privateKey);
    socket->setPeerVerifyMode(QSslSocket::VerifyNone);
    m_sockets.append(socket);

    connect(socket, &QSslSocket::encrypted, this, [this, socket] { onEncrypted(socket); });
    connect(socket, &QSslSocket::readyRead, this, [this, socket] { onReadyRead(socket); });
    connect(socket, &QSslSocket::disconnected, this, [this, socket] { onDisconnected(socket); });
    connect(socket, &QSslSocket::errorOccurred, this, [this, socket](QAbstractSocket::SocketError) {
        // RemoteHostClosedError and co. are part of the expected teardown; only a
        // failure *before* the channel was identified is worth reporting.
        if (!m_roles.contains(socket))
            fail(QStringLiteral("socket error before the channel preface: %1").arg(socket->errorString()));
    });
    socket->startServerEncryption();
}

void MockUltraviewServer::onNewConnection()
{
    // The descriptor has already been adopted by MockListenServer.
    while (m_server->hasPendingConnections()) {
        QTcpSocket *unexpected = m_server->nextPendingConnection();
        unexpected->abort();
        unexpected->deleteLater();
        fail(QStringLiteral("the listener produced a QTcpSocket in addition to the adopted descriptor"));
    }
}

void MockUltraviewServer::onEncrypted(QSslSocket *socket)
{
    Q_UNUSED(socket)
}

void MockUltraviewServer::onReadyRead(QSslSocket *socket)
{
    QByteArray &buffer = m_buffers[socket];
    buffer += socket->readAll();

    while (!buffer.isEmpty()) {
        if (!m_roles.contains(socket)) {
            const int end = buffer.indexOf("\r\n\r\n");
            if (end < 0) {
                if (buffer.size() > 16384) {
                    fail(QStringLiteral("the HTTP preface exceeds the reference limit"));
                    socket->abort();
                }
                return;
            }
            const QByteArray preface = buffer.left(end + 4);
            buffer.remove(0, end + 4);
            handlePreface(socket, preface);
            continue;
        }

        QByteArray body;
        quint32 declared = 0;
        const FrameResult result = takeFrame(&buffer, &body, protocol::kAbsoluteMaxFrameBytes, &declared);
        if (result == FrameResult::Incomplete)
            return;
        if (result == FrameResult::TooLarge) {
            fail(QStringLiteral("the client sent a frame of %1 bytes").arg(declared));
            socket->abort();
            return;
        }
        if (m_roles.value(socket) == QLatin1String("registration"))
            handleRegistrationBody(socket, body);
        else
            handleRelayBody(socket, body);
    }
}

void MockUltraviewServer::handlePreface(QSslSocket *socket, const QByteArray &prefix)
{
    m_prefaces.append(prefix);
    const bool registration = m_registrationSockets == 0;
    if (registration) {
        ++m_registrationSockets;
        m_roles.insert(socket, QStringLiteral("registration"));
        m_registrationOpen = true;
    } else {
        ++m_relaySockets;
        ++m_relayConnections;
        m_roles.insert(socket, QStringLiteral("relay"));
        m_relayOpen = true;
    }
    socket->write(QByteArrayLiteral("HTTP/1.1 200 OK\r\n\r\n"));
    socket->flush();
    emit progressed(registration ? QStringLiteral("registration-preface")
                                 : QStringLiteral("relay-preface"));
}

void MockUltraviewServer::handleRegistrationBody(QSslSocket *socket, const QByteArray &body)
{
    if (body.isEmpty() || static_cast<quint8>(body.at(0)) != 1) {
        fail(QStringLiteral("the client sent a registration frame without the mode 1 byte"));
        socket->abort();
        return;
    }
    if (static_cast<quint8>(body.at(1)) != 0) {
        fail(QStringLiteral("the client sent a registration frame with an unexpected flag byte"));
        socket->abort();
        return;
    }
    QByteArray plaintext;
    try {
        plaintext = protocol::RegistrationCodec::decryptRegistrationBytes(body.mid(2));
    } catch (const UvException &exception) {
        fail(QStringLiteral("cannot decrypt the registration payload: %1").arg(exception.info().summary()));
        socket->abort();
        return;
    }

    if (!m_command3Seen) {
        m_command3Seen = true;
        m_registrationCommand = plaintext;
        emit progressed(QStringLiteral("command3"));
        sendFrame(socket, QByteArrayLiteral("\x01") + m_options.identityPayload);
        return;
    }

    if (!m_connectionRequestSeen) {
        m_connectionRequestSeen = true;
        m_connectionRequest = plaintext;
        emit progressed(QStringLiteral("command8"));
        if (m_options.muteRegistration)
            return;
        if (m_options.routeStatus != QLatin1String("1")) {
            sendFrame(socket, QByteArrayLiteral("\x01") + "8:" + m_options.routeStatus);
            emit progressed(QStringLiteral("refusal"));
            return;
        }
        m_routeSent = true;
        sendFrame(socket, QByteArrayLiteral("\x01")
                             + routePayload(m_options.routeTcpServerIp, m_options.routeTcpPort,
                                            m_options.routePublicIp, m_options.routePublicPort,
                                            m_options.routeHostname, m_options.routeSecretToken));
        emit progressed(QStringLiteral("route"));
        return;
    }

    emit progressed(QStringLiteral("registration-extra"));
}

void MockUltraviewServer::handleRelayBody(QSslSocket *socket, const QByteArray &body)
{
    protocol::TransportBody decoded;
    try {
        decoded = protocol::decodeTransportBody(body);
    } catch (const UvException &exception) {
        fail(QStringLiteral("cannot decode a relay frame: %1").arg(exception.info().summary()));
        socket->abort();
        return;
    }
    if (decoded.kind != protocol::TransportBody::Kind::Relay) {
        fail(QStringLiteral("expected a relay transport body"));
        socket->abort();
        return;
    }

    if (!m_loginSeen) {
        m_loginSeen = true;
        m_loginPayload = decoded.data;
        emit progressed(QStringLiteral("login"));
        if (m_options.muteRelay)
            return;

        // Kind 0 login acknowledgement: <discriminator 0><Int32LE width><Int32LE height>.
        const QByteArray acknowledgement = protocol::encodeByteArrayFields(
            {QByteArray(1, '\0'), int32le(m_options.desktopSize.width()),
             int32le(m_options.desktopSize.height())});
        sendRelayPacket(socket, acknowledgement);
        for (const QByteArray &packet : std::as_const(m_options.relayPackets))
            sendRelayPacket(socket, packet);
        if (m_options.closeRelayAfterLogin) {
            socket->flush();
            socket->disconnectFromHost();
        }
        return;
    }

    m_relayPayloads.append(decoded.data);
    emit progressed(QStringLiteral("relay-payload"));
}

void MockUltraviewServer::sendFrame(QSslSocket *socket, const QByteArray &body)
{
    if (!socket->isEncrypted())
        return;
    socket->write(protocol::encodeTransportFrame(body));
    socket->flush();
}

void MockUltraviewServer::sendRelayPacket(QSslSocket *socket, const QByteArray &payload)
{
    if (!socket->isEncrypted())
        return;
    socket->write(protocol::encodeRelayFrame(payload, m_options.routePublicIp, m_options.routeTcpPort, 0));
    socket->flush();
}

void MockUltraviewServer::fail(const QString &message)
{
    m_errors.append(message);
}

bool MockUltraviewServer::waitFor(const std::function<bool()> &predicate, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate()) {
        if (timer.elapsed() >= timeoutMs)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(1);
    }
    return true;
}

bool MockUltraviewServer::waitForRelayPayload(const QByteArray &payload, int timeoutMs)
{
    return waitFor([this, &payload] { return m_relayPayloads.contains(payload); }, timeoutMs);
}

void MockUltraviewServer::onDisconnected(QSslSocket *socket)
{
    const QString role = m_roles.value(socket);
    if (role == QLatin1String("registration")) {
        m_registrationOpen = false;
        emit progressed(QStringLiteral("registration-closed"));
    } else if (role == QLatin1String("relay")) {
        m_relayOpen = false;
        emit progressed(QStringLiteral("relay-closed"));
    }
}

void MockUltraviewServer::dropRegistration()
{
    for (QSslSocket *socket : std::as_const(m_sockets)) {
        if (m_roles.value(socket) == QLatin1String("registration"))
            socket->abort();
    }
}

void MockUltraviewServer::dropRelay()
{
    for (QSslSocket *socket : std::as_const(m_sockets)) {
        if (m_roles.value(socket) == QLatin1String("relay"))
            socket->abort();
    }
}

} // namespace uvtest
