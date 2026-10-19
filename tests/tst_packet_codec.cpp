//
// Parity tests for protocol/PacketCodec: framing, relay headers, the HTTP tunnel
// preface and the R200 byte-array fields.
//
// Every expectation comes from `tests/data/fixtures.json`, which is produced by
// running the Python reference (`tools/generate_fixtures.py`), so these are
// byte-exact comparisons against the recovered implementation rather than
// self-consistency checks.
//
#include "TestSupport.h"

#include "core/ErrorTypes.h"
#include "protocol/PacketCodec.h"

#include <QtTest>

using namespace ultraview;
using namespace ultraview::protocol;

class TestPacketCodec : public QObject {
    Q_OBJECT

private slots:
    void transportFrames();
    void transportFrameLimit();
    void typedTransportFrames();
    void relayFrames();
    void relayFrameValidation();
    void ipv4Formatting();
    void transportBodies();
    void unknownTransportBodies();
    void invalidTransportBodies();
    void frameStream();
    void frameStreamLimit();
    void httpPreface();
    void byteArrayFields();
    void byteArrayFieldCountValidation();
};

void TestPacketCodec::transportFrames()
{
    const QJsonArray cases = uvtest::Fixtures::instance().array(QStringLiteral("transport_frames"));
    QVERIFY(!cases.isEmpty());
    for (const QJsonValue &value : cases) {
        const QJsonObject entry = value.toObject();
        const QByteArray body = uvtest::Fixtures::entryBytes(entry, QStringLiteral("body_hex"));
        const QByteArray wire = uvtest::Fixtures::entryBytes(entry, QStringLiteral("wire_hex"));
        QCOMPARE(encodeTransportFrame(body), wire);

        // The reference guard is `len(buffer) > 4`, so a zero-length body needs
        // one more byte before it is produced; that byte then stays pending.
        const bool zeroLengthBody = wire.size() == 4;
        FrameStream stream;
        const QList<QByteArray> frames =
            stream.feed(zeroLengthBody ? wire + QByteArrayLiteral("X") : wire);
        QCOMPARE(frames.size(), 1);
        if (!frames.isEmpty())
            QCOMPARE(frames.first(), body);
        QCOMPARE(stream.buffer(), zeroLengthBody ? QByteArrayLiteral("X") : QByteArray());
        QCOMPARE(stream.hasPendingData(), zeroLengthBody);
    }
}

void TestPacketCodec::transportFrameLimit()
{
    FrameStream stream(false, 4);
    // 5 bytes of body with a limit of 4: the reference transport refuses the
    // frame instead of growing the buffer without bound.
    QVERIFY_EXCEPTION_THROWN(stream.feed(uvtest::fromHex(QStringLiteral("050000000001020304"))), UvException);
    QCOMPARE(stream.bufferedBytes(), 0);
}

void TestPacketCodec::typedTransportFrames()
{
    const QJsonArray cases = uvtest::Fixtures::instance().array(QStringLiteral("typed_transport_frames"));
    QVERIFY(!cases.isEmpty());
    for (const QJsonValue &value : cases) {
        const QJsonObject entry = value.toObject();
        const QByteArray payload = uvtest::Fixtures::entryBytes(entry, QStringLiteral("payload_hex"));
        const QByteArray wire = uvtest::Fixtures::entryBytes(entry, QStringLiteral("wire_hex"));
        const int kind = entry.value(QStringLiteral("kind")).toInt();
        QCOMPARE(encodeTypedTransportFrame(payload, static_cast<quint8>(kind)), wire);

        // A typed frame is a length frame whose body is `<kind><payload>`, so it is
        // only a valid *transport* frame for the kinds the dispatcher accepts.
        const QByteArray body = wire.mid(4);
        QCOMPARE(static_cast<int>(static_cast<quint8>(body.at(0))), kind);
        QCOMPARE(body.mid(1), payload);
        if (kind == 1) {
            const TransportBody decoded = decodeTransportBody(body);
            QCOMPARE(int(decoded.kind), int(TransportBody::Kind::Direct));
            QCOMPARE(decoded.data, payload);
        } else if (kind != 0) {
            QCOMPARE(int(decodeTransportBody(body).kind), int(TransportBody::Kind::Unknown));
        }
    }
}

void TestPacketCodec::relayFrames()
{
    const QJsonArray cases = uvtest::Fixtures::instance().array(QStringLiteral("relay_frames"));
    QVERIFY(!cases.isEmpty());
    for (const QJsonValue &value : cases) {
        const QJsonObject entry = value.toObject();
        const QByteArray payload = uvtest::Fixtures::entryBytes(entry, QStringLiteral("payload_hex"));
        const QByteArray wire = uvtest::Fixtures::entryBytes(entry, QStringLiteral("wire_hex"));
        const QString ipv4 = entry.value(QStringLiteral("ipv4")).toString();
        const quint16 port = static_cast<quint16>(entry.value(QStringLiteral("port")).toInt());
        const quint8 flag = static_cast<quint8>(entry.value(QStringLiteral("flag")).toInt());
        QCOMPARE(encodeRelayFrame(payload, ipv4, port, flag), wire);

        // A full frame is a length frame whose body is the relay header + payload.
        const QByteArray body = wire.mid(4);
        const TransportBody decoded = decodeTransportBody(body);
        QCOMPARE(int(decoded.kind), int(TransportBody::Kind::Relay));
        QCOMPARE(decoded.flag, flag != 0);
        QCOMPARE(decoded.port, port);
        QCOMPARE(decoded.data, payload);
    }
}

void TestPacketCodec::relayFrameValidation()
{
    // The reference accepts exactly four decimal octets in 0..255.
    for (const QString &invalid : {QStringLiteral("1.2.3"), QStringLiteral("1.2.3.4.5"),
                                   QStringLiteral("a.b.c.d"), QStringLiteral("1.2.3.256"),
                                   QStringLiteral(""), QStringLiteral("1.2.3.-4"),
                                   QStringLiteral("1.2.3.4 "), QStringLiteral("1.2.3.0004")}) {
        QVERIFY2(!parseCanonicalIpv4(invalid, nullptr), qPrintable(invalid));
        QVERIFY_EXCEPTION_THROWN(encodeRelayFrame(QByteArrayLiteral("x"), invalid, 1, 0), UvException);
    }
    quint32 value = 0;
    QVERIFY(parseCanonicalIpv4(QStringLiteral("0.0.0.0"), &value));
    QCOMPARE(value, 0u);
    QVERIFY(parseCanonicalIpv4(QStringLiteral("255.255.255.255"), &value));
    QCOMPARE(value, 0xFFFFFFFFu);
}

void TestPacketCodec::ipv4Formatting()
{
    const QJsonArray cases = uvtest::Fixtures::instance().array(QStringLiteral("relay_frames"));
    for (const QJsonValue &value : cases) {
        const QString ipv4 = value.toObject().value(QStringLiteral("ipv4")).toString();
        quint32 address = 0;
        QVERIFY(parseCanonicalIpv4(ipv4, &address));
        QCOMPARE(formatIpv4(address), ipv4);
    }
    // 0x12345678 is the address value of the `transport_bodies` fixture, i.e. the
    // octets 0x12 0x34 0x56 0x78 = 18.52.86.120.
    QCOMPARE(formatIpv4(305419896u), QStringLiteral("18.52.86.120"));
}

void TestPacketCodec::transportBodies()
{
    const QJsonArray cases = uvtest::Fixtures::instance().array(QStringLiteral("transport_bodies"));
    QVERIFY(!cases.isEmpty());
    for (const QJsonValue &value : cases) {
        const QJsonObject entry = value.toObject();
        const QByteArray body = uvtest::Fixtures::entryBytes(entry, QStringLiteral("body_hex"));
        const QString kind = entry.value(QStringLiteral("kind")).toString();
        const TransportBody decoded = decodeTransportBody(body);
        if (kind == QLatin1String("relay")) {
            QCOMPARE(int(decoded.kind), int(TransportBody::Kind::Relay));
            QCOMPARE(decoded.flag, entry.value(QStringLiteral("flag")).toBool());
            QCOMPARE(static_cast<qint64>(decoded.addressValue),
                     static_cast<qint64>(entry.value(QStringLiteral("address_value")).toDouble()));
            QCOMPARE(decoded.port, static_cast<quint16>(entry.value(QStringLiteral("port")).toInt()));
        } else {
            QCOMPARE(int(decoded.kind), int(TransportBody::Kind::Direct));
            QVERIFY(!decoded.flag);
        }
        QCOMPARE(decoded.data, uvtest::Fixtures::entryBytes(entry, QStringLiteral("data_hex")));
    }
}

void TestPacketCodec::unknownTransportBodies()
{
    // Unknown kinds are returned (not raised) by the reference; the native codec
    // keeps that contract and lets the caller decide what to do.
    const QJsonArray cases = uvtest::Fixtures::instance().array(QStringLiteral("unknown_transport_bodies"));
    QVERIFY(!cases.isEmpty());
    for (const QJsonValue &value : cases) {
        const QByteArray body = uvtest::Fixtures::entryBytes(value.toObject(), QStringLiteral("body_hex"));
        const TransportBody decoded = decodeTransportBody(body);
        QCOMPARE(int(decoded.kind), int(TransportBody::Kind::Unknown));
    }
}

void TestPacketCodec::invalidTransportBodies()
{
    const QJsonArray cases = uvtest::Fixtures::instance().array(QStringLiteral("invalid_transport_bodies"));
    QVERIFY(!cases.isEmpty());
    for (const QJsonValue &value : cases) {
        const QByteArray body = uvtest::Fixtures::entryBytes(value.toObject(), QStringLiteral("body_hex"));
        try {
            decodeTransportBody(body);
            QFAIL("a malformed transport body was accepted");
        } catch (const UvException &exception) {
            QCOMPARE(exception.code(), ErrorCode::Protocol);
        }
    }
}

void TestPacketCodec::frameStream()
{
    const QJsonArray cases = uvtest::Fixtures::instance().array(QStringLiteral("frame_stream"));
    QVERIFY(!cases.isEmpty());
    for (const QJsonValue &value : cases) {
        const QJsonObject entry = value.toObject();
        FrameStream stream(entry.value(QStringLiteral("expect_http_header")).toBool());
        QList<QByteArray> frames;
        const QJsonArray chunks = entry.value(QStringLiteral("chunks_hex")).toArray();
        for (const QJsonValue &chunk : chunks) {
            const QList<QByteArray> produced = stream.feed(uvtest::fromHex(chunk.toString()));
            frames.append(produced);
        }
        const QJsonArray expected = entry.value(QStringLiteral("frames_hex")).toArray();
        QCOMPARE(frames.size(), expected.size());
        for (int index = 0; index < expected.size() && index < frames.size(); ++index)
            QCOMPARE(frames.at(index), uvtest::fromHex(expected.at(index).toString()));
        QCOMPARE(uvtest::toHex(stream.buffer()),
                 uvtest::toHex(uvtest::fromHex(entry.value(QStringLiteral("remaining_hex")).toString())));
    }

    // The reference keeps exactly four buffered bytes pending, so an empty body
    // needs one trailing byte before it is produced.
    FrameStream stream;
    QVERIFY(stream.feed(uvtest::fromHex(QStringLiteral("00000000"))).isEmpty());
    QCOMPARE(uvtest::toHex(stream.buffer()), QStringLiteral("00000000"));
    const QList<QByteArray> frames = stream.feed(QByteArrayLiteral("X"));
    QCOMPARE(frames.size(), 1);
    QCOMPARE(frames.value(0).size(), 0);
    QCOMPARE(stream.buffer(), QByteArrayLiteral("X"));
    QCOMPARE(stream.pendingFrameBytes(), 1);
}

void TestPacketCodec::frameStreamLimit()
{
    FrameStream stream(false, kDefaultMaxFrameBytes);
    QCOMPARE(stream.maxFrameBytes(), kDefaultMaxFrameBytes);
    QCOMPARE(kDefaultMaxFrameBytes, 1024u * 1024u);
    QVERIFY_EXCEPTION_THROWN(stream.setMaxFrameBytes(0), UvException);
    QVERIFY_EXCEPTION_THROWN(stream.setMaxFrameBytes(kAbsoluteMaxFrameBytes + 1), UvException);
}

void TestPacketCodec::httpPreface()
{
    const QJsonArray cases = uvtest::Fixtures::instance().array(QStringLiteral("http_preface"));
    QVERIFY(!cases.isEmpty());
    for (const QJsonValue &value : cases) {
        const QJsonObject entry = value.toObject();
        const QByteArray expected = uvtest::Fixtures::entryBytes(entry);
        const QString host = entry.value(QStringLiteral("host")).toString();
        const bool post = entry.value(QStringLiteral("post")).toBool();
        const QByteArray preface = httpServicePreface(host, post);
        QCOMPARE(preface, expected);
        QVERIFY(preface.endsWith("\r\n\r\n"));
        // The literal content length is copied from the artifact, not computed.
        QVERIFY(preface.contains("Content-length: 8589934590"));
        QCOMPARE(kHttpPrefaceContentLength, 8589934590LL);
    }
}

void TestPacketCodec::byteArrayFields()
{
    const QJsonArray cases = uvtest::Fixtures::instance().array(QStringLiteral("byte_array_fields"));
    QVERIFY(!cases.isEmpty());
    for (const QJsonValue &value : cases) {
        const QJsonObject entry = value.toObject();
        QList<QByteArray> fields;
        const QJsonArray fieldArray = entry.value(QStringLiteral("fields_hex")).toArray();
        for (const QJsonValue &field : fieldArray)
            fields.append(field.isNull() ? QByteArray() : uvtest::fromHex(field.toString()));
        const QByteArray wire = uvtest::Fixtures::entryBytes(entry, QStringLiteral("wire_hex"));
        QCOMPARE(encodeByteArrayFields(fields), wire);

        const QJsonArray decoded = entry.value(QStringLiteral("decoded_hex")).toArray();
        QList<QByteArray> expected;
        for (const QJsonValue &field : decoded)
            expected.append(field.isNull() ? QByteArray() : uvtest::fromHex(field.toString()));
        QCOMPARE(decodeByteArrayFields(wire), expected);
        QCOMPARE(decodeByteArrayFields(wire, fields.size()), expected);
    }
}

void TestPacketCodec::byteArrayFieldCountValidation()
{
    const QByteArray wire = uvtest::fromHex(QStringLiteral("0100000001"));
    QCOMPARE(decodeByteArrayFields(wire).size(), 1);
    try {
        decodeByteArrayFields(wire, 2);
        QFAIL("a field count mismatch was accepted");
    } catch (const UvException &exception) {
        QCOMPARE(exception.code(), ErrorCode::Protocol);
    }
    // Truncated length and truncated value are both protocol violations.
    QVERIFY_EXCEPTION_THROWN(decodeByteArrayFields(uvtest::fromHex(QStringLiteral("010000")), -1), UvException);
    QVERIFY_EXCEPTION_THROWN(
        decodeByteArrayFields(uvtest::fromHex(QStringLiteral("040000004142")), -1), UvException);
}

QTEST_MAIN(TestPacketCodec)
#include "tst_packet_codec.moc"
