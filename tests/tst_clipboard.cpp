//
// Tests for the peer application messages that are plain codecs: the bounded
// Zstandard helper, the clipboard encodings (commands 5 and 33) and the control
// commands (chat, typing, session actions, quality, wallpaper, view-only).
//
// The modern clipboard upload and download use *different* shapes (an ASCII
// header on upload, a binary length field in the multipart download), so the
// download form is assembled here exactly as the protocol reference describes it.
//
#include "TestSupport.h"

#include "core/ErrorTypes.h"
#include "protocol/ClipboardCodec.h"
#include "protocol/ControlCommands.h"
#include "protocol/ZstdCodec.h"
#include "platform/ClipboardBridge.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QtTest>

#include <cstring>

using namespace ultraview;
using namespace ultraview::protocol;

namespace {

QByteArray littleEndian32(quint32 value)
{
    QByteArray bytes(4, Qt::Uninitialized);
    for (int index = 0; index < 4; ++index)
        bytes[index] = static_cast<char>((value >> (8 * index)) & 0xffU);
    return bytes;
}

} // namespace

class TestClipboard : public QObject {
    Q_OBJECT

private slots:
    void zstdRoundTrip();
    void zstdRejectsAnUnexpectedSize();
    void zstdRespectsTheResourceLimit();
    void zstdRejectsGarbage();
    void modernUploadCarriesAnAsciiHeader();
    void modernDownloadRoundTrip();
    void modernDownloadRejectsAShortMessage();
    void emptyTextFallsBackToLegacy();
    void modernFormCarriesTheDelimiter();
    void legacyParseAcceptsPrefixAndBareText();
    void legacyParseRejectsOddBytes();
    void chatMessageRoundTrip();
    void chatRejectsDelimiterAndLength();
    void typingIndicators();
    void sessionActions();
    void qualityPresets();
    void wallpaperAndAudioToggles();
    void cursorTrackingAndShapeRequest();
    void viewOnlyParsing();
    void chatMultipartParsing();
    void clipboardBridgeHonoursTheEnabledFlag();
};

void TestClipboard::zstdRoundTrip()
{
    const QByteArray payload = QStringLiteral("hello — 你好 — удалить").toUtf8();
    const QByteArray compressed = ZstdCodec::compress(payload);
    QVERIFY(!compressed.isEmpty());
    QVERIFY(ZstdCodec::hasFrameMagic(compressed));
    QCOMPARE(ZstdCodec::decompressFrame(compressed), payload);
    QCOMPARE(ZstdCodec::decompress(compressed, payload.size()), payload);
}

void TestClipboard::zstdRejectsAnUnexpectedSize()
{
    const QByteArray compressed = ZstdCodec::compress(QByteArrayLiteral("0123456789"));
    bool threw = false;
    try {
        ZstdCodec::decompress(compressed, 5);
    } catch (const UvException &exception) {
        threw = true;
        QCOMPARE(int(exception.code()), int(ErrorCode::Protocol));
    }
    QVERIFY(threw);
}

void TestClipboard::zstdRespectsTheResourceLimit()
{
    const QByteArray compressed = ZstdCodec::compress(QByteArray(4096, 'a'));
    bool threw = false;
    try {
        ZstdCodec::decompress(compressed, 4096, 1024);
    } catch (const UvException &exception) {
        threw = true;
        QCOMPARE(int(exception.code()), int(ErrorCode::Resource));
    }
    QVERIFY(threw);
}

void TestClipboard::zstdRejectsGarbage()
{
    QVERIFY(!ZstdCodec::hasFrameMagic(QByteArrayLiteral("not zstd")));
    bool threw = false;
    try {
        ZstdCodec::decompressFrame(QByteArrayLiteral("not a frame"));
    } catch (const UvException &exception) {
        threw = true;
        QCOMPARE(int(exception.code()), int(ErrorCode::Protocol));
    }
    QVERIFY(threw);
}

void TestClipboard::modernUploadCarriesAnAsciiHeader()
{
    const QString text = QStringLiteral("copy me");
    const ClipboardCodec::Upload upload = ClipboardCodec::buildModern(text);
    QCOMPARE(int(upload.flag), 0);

    const QByteArray expectedHeader =
        QByteArrayLiteral("33|||") + QByteArray::number(text.size() * 2) + QByteArrayLiteral("|||");
    QVERIFY(upload.payload.startsWith(expectedHeader));
    const QByteArray compressed = upload.payload.mid(expectedHeader.size());
    QVERIFY(ZstdCodec::hasFrameMagic(compressed));
    QCOMPARE(ZstdCodec::decompressFrame(compressed), ClipboardCodec::toUtf16(text));
}

void TestClipboard::modernDownloadRoundTrip()
{
    const QString text = QStringLiteral("partner → viewer");
    const QByteArray utf16 = ClipboardCodec::toUtf16(text);
    const QList<QByteArray> fields = {QByteArray(1, 33), littleEndian32(static_cast<quint32>(utf16.size())),
                                      ZstdCodec::compress(utf16)};
    QVERIFY(ClipboardCodec::isModernCommand(fields));
    const std::optional<QString> parsed = ClipboardCodec::parseModern(fields);
    QVERIFY(parsed.has_value());
    QCOMPARE(*parsed, text);
}

void TestClipboard::modernDownloadRejectsAShortMessage()
{
    bool threw = false;
    try {
        ClipboardCodec::parseModern({QByteArray(1, 33)});
    } catch (const UvException &exception) {
        threw = true;
        QCOMPARE(int(exception.code()), int(ErrorCode::Protocol));
    }
    QVERIFY(threw);
}

void TestClipboard::emptyTextFallsBackToLegacy()
{
    const ClipboardCodec::Upload upload = ClipboardCodec::buildUpload(QString());
    QCOMPARE(int(upload.flag), 1);
    QCOMPARE(ClipboardCodec::fromUtf16(upload.payload).value(), QStringLiteral("5|||"));
}

void TestClipboard::modernFormCarriesTheDelimiter()
{
    // The modern form puts the text inside a length-prefixed frame, so a delimiter
    // in the text is not ambiguous and must not be refused.
    const QString text = QStringLiteral("a|||b");
    const ClipboardCodec::Upload upload = ClipboardCodec::buildModern(text);
    QCOMPARE(int(upload.flag), 0);
    const QByteArray expectedHeader =
        QByteArrayLiteral("33|||") + QByteArray::number(text.size() * 2) + QByteArrayLiteral("|||");
    QVERIFY(upload.payload.startsWith(expectedHeader));
    QCOMPARE(ZstdCodec::decompressFrame(upload.payload.mid(expectedHeader.size())),
             ClipboardCodec::toUtf16(text));

    // `buildUpload` prefers the modern form, so the same text is sendable.
    QCOMPARE(int(ClipboardCodec::buildUpload(text).flag), 0);

    // Only the delimited legacy form refuses it, because the host would split it.
    bool threw = false;
    try {
        ClipboardCodec::buildLegacy(text);
    } catch (const UvException &exception) {
        threw = true;
        QCOMPARE(int(exception.code()), int(ErrorCode::UnsupportedOperation));
    }
    QVERIFY(threw);
}

void TestClipboard::legacyParseAcceptsPrefixAndBareText()
{
    const QByteArray prefixed = ClipboardCodec::toUtf16(QStringLiteral("5|||héllo"));
    QCOMPARE(ClipboardCodec::parseLegacy(prefixed).value(), QStringLiteral("héllo"));

    const QByteArray bare = ClipboardCodec::toUtf16(QStringLiteral("bare text"));
    QCOMPARE(ClipboardCodec::parseLegacy(bare).value(), QStringLiteral("bare text"));
}

void TestClipboard::legacyParseRejectsOddBytes()
{
    QVERIFY(!ClipboardCodec::parseLegacy(QByteArrayLiteral("abc")).has_value());
}

void TestClipboard::chatMessageRoundTrip()
{
    const CommandPayload payload = ControlCommands::chat(QStringLiteral("Xin chào"));
    QCOMPARE(int(payload.flag), 1);
    const QByteArray expected =
        ClipboardCodec::toUtf16(QStringLiteral("12|||Xin chào"));
    QCOMPARE(payload.payload, expected);
}

void TestClipboard::chatRejectsDelimiterAndLength()
{
    bool threw = false;
    try {
        ControlCommands::chat(QStringLiteral("a|||b"));
    } catch (const UvException &exception) {
        threw = true;
        QCOMPARE(int(exception.code()), int(ErrorCode::UnsupportedOperation));
    }
    QVERIFY(threw);

    threw = false;
    try {
        ControlCommands::chat(QString(ControlCommands::kMaxChatCodeUnits + 1, QLatin1Char('a')));
    } catch (const UvException &exception) {
        threw = true;
        QCOMPARE(int(exception.code()), int(ErrorCode::Resource));
    }
    QVERIFY(threw);
}

void TestClipboard::typingIndicators()
{
    QCOMPARE(ControlCommands::typing(true).payload, ClipboardCodec::toUtf16(QStringLiteral("29")));
    QCOMPARE(ControlCommands::typing(false).payload, ClipboardCodec::toUtf16(QStringLiteral("30")));
}

void TestClipboard::sessionActions()
{
    QCOMPARE(ControlCommands::ctrlAltDel().payload, ClipboardCodec::toUtf16(QStringLiteral("22")));
    QCOMPARE(ControlCommands::reboot().payload, ClipboardCodec::toUtf16(QStringLiteral("40")));
    QCOMPARE(ControlCommands::lockWorkstation().payload,
             ClipboardCodec::toUtf16(QStringLiteral("53")));
    QCOMPARE(int(ControlCommands::lockWorkstation().flag), 1);
}

void TestClipboard::qualityPresets()
{
    QCOMPARE(ControlCommands::quality(30).payload, QByteArrayLiteral("31|||30"));
    QCOMPARE(ControlCommands::quality(45).payload, QByteArrayLiteral("31|||45"));
    QCOMPARE(ControlCommands::quality(70).payload, QByteArrayLiteral("31|||70"));
    QCOMPARE(int(ControlCommands::quality(70).flag), 0);

    bool threw = false;
    try {
        ControlCommands::quality(50);
    } catch (const UvException &exception) {
        threw = true;
        QCOMPARE(int(exception.code()), int(ErrorCode::Configuration));
    }
    QVERIFY(threw);
}

void TestClipboard::wallpaperAndAudioToggles()
{
    QCOMPARE(ControlCommands::wallpaper(true).payload,
             ClipboardCodec::toUtf16(QStringLiteral("23|||-1")));
    QCOMPARE(ControlCommands::wallpaper(false).payload,
             ClipboardCodec::toUtf16(QStringLiteral("23|||0")));
    QCOMPARE(ControlCommands::audio(true).payload,
             ClipboardCodec::toUtf16(QStringLiteral("46|||-1")));
    QCOMPARE(ControlCommands::audio(false).payload,
             ClipboardCodec::toUtf16(QStringLiteral("46|||0")));
}

void TestClipboard::cursorTrackingAndShapeRequest()
{
    QCOMPARE(ControlCommands::cursorTracking(true).payload, QByteArrayLiteral("28|||1"));
    QCOMPARE(ControlCommands::cursorTracking(false).payload, QByteArrayLiteral("28|||0"));
    QCOMPARE(ControlCommands::cursorShapeRequest(305419896).payload,
             QByteArrayLiteral("4|||305419896"));
}

void TestClipboard::viewOnlyParsing()
{
    const std::optional<bool> viewOnly =
        ControlCommands::parseViewOnly({QByteArray(1, 49), QByteArray(1, 1)});
    QVERIFY(viewOnly.has_value());
    QVERIFY(*viewOnly);
    const std::optional<bool> control =
        ControlCommands::parseViewOnly({QByteArray(1, 49), QByteArray(1, 0)});
    QVERIFY(control.has_value());
    QVERIFY(!*control);
    QVERIFY(!ControlCommands::parseViewOnly({QByteArray(1, 49)}).has_value());
}

void TestClipboard::chatMultipartParsing()
{
    const QList<QByteArray> fields = {QByteArray(1, 12),
                                      ClipboardCodec::toUtf16(QStringLiteral("hello"))};
    QVERIFY(ControlCommands::isChat(fields));
    QCOMPARE(ControlCommands::parseChat(fields).value(), QStringLiteral("hello"));

    bool started = false;
    QVERIFY(ControlCommands::isTyping({QByteArray(1, 29)}, &started));
    QVERIFY(started);
    QVERIFY(ControlCommands::isTyping({QByteArray(1, 30)}, &started));
    QVERIFY(!started);
    QVERIFY(!ControlCommands::isTyping({QByteArray(1, 12)}, &started));
}

void TestClipboard::clipboardBridgeHonoursTheEnabledFlag()
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr)
        QSKIP("the platform plugin has no clipboard");
    clipboard->clear();

    // With the sync disabled, neither direction may touch the clipboard.
    platform::ClipboardBridge bridge;
    bridge.setEnabled(false);
    bridge.applyRemoteText(QStringLiteral("blocked"));
    QVERIFY(clipboard->text() != QStringLiteral("blocked"));

    bridge.setEnabled(true);
    bridge.applyRemoteText(QStringLiteral("partner text"));
    QCOMPARE(clipboard->text(), QStringLiteral("partner text"));

    // Re-applying the same text is a no-op, so it cannot bounce back.
    bridge.applyRemoteText(QStringLiteral("partner text"));
    QCOMPARE(clipboard->text(), QStringLiteral("partner text"));
}

QTEST_MAIN(TestClipboard)
#include "tst_clipboard.moc"
