//
// Tests for file transfers: the wire codec (commands 13/14/15/16/21) and the
// state machine that owns the files.  The service under test is driven exactly
// like the session drives it: outbound payloads are captured, inbound messages
// are fed in, and the resulting rows/disk contents are checked.
//
#include "TestSupport.h"

#include "core/ErrorTypes.h"
#include "protocol/FileTransferCodec.h"
#include "transfer/FileTransferService.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <cmath>
#include <limits>

using namespace ultraview;
using namespace ultraview::protocol;
using namespace ultraview::transfer;

namespace {

QVariantMap findRow(const QVariantList &rows, const QString &id)
{
    for (const QVariant &value : rows) {
        const QVariantMap map = value.toMap();
        if (map.value(QStringLiteral("id")).toString() == id)
            return map;
    }
    return {};
}

QByteArray readAll(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

} // namespace

class TestTransfer : public QObject {
    Q_OBJECT

private slots:
    void offerAndAcceptEncoding();
    void chunkEncoding();
    void doubleEncoding();
    void doubleRejectsInvalidValues();
    void safeFileNames();
    void multipartParsing();
    void textParsing();
    void malformedMessagesThrow();
    void uploadPacesOnAcknowledgements();
    void downloadAcceptsAndWritesTheFile();
    void downloadRejectsAWrongOffset();
    void duplicateOfferIsCancelled();
    void incomingCancelStopsTheDownload();
    void unsafeOfferIsRefused();
};

void TestTransfer::offerAndAcceptEncoding()
{
    QCOMPARE(FileTransferCodec::offer(QStringLiteral("123"), QStringLiteral("a.txt"), 42),
             QByteArrayLiteral("13|||123|||a.txt|||42"));
    QCOMPARE(FileTransferCodec::accept(QStringLiteral("123"), true),
             QByteArrayLiteral("14|||123|||1"));
    QCOMPARE(FileTransferCodec::accept(QStringLiteral("123"), false),
             QByteArrayLiteral("14|||123|||0"));
    QCOMPARE(FileTransferCodec::cancel(QStringLiteral("123")), QByteArrayLiteral("16|||123"));
    QCOMPARE(FileTransferCodec::acknowledge(QStringLiteral("123")), QByteArrayLiteral("21|||123"));
}

void TestTransfer::chunkEncoding()
{
    const QByteArray payload =
        FileTransferCodec::chunk(QStringLiteral("77"), 32000, QByteArrayLiteral("data"));
    QCOMPARE(payload, QByteArrayLiteral("15|||77|||32000|||data"));

    bool threw = false;
    try {
        FileTransferCodec::chunk(QStringLiteral("77"), 0,
                                 QByteArray(FileTransferCodec::kMaxChunkBytes + 1, 'x'));
    } catch (const UvException &exception) {
        threw = true;
        QCOMPARE(int(exception.code()), int(ErrorCode::Configuration));
    }
    QVERIFY(threw);
}

void TestTransfer::doubleEncoding()
{
    const QByteArray encoded = FileTransferCodec::encodeDouble(1234567);
    QCOMPARE(encoded.size(), 8);
    // 1234567.0 as IEEE-754 double, little endian.
    QCOMPARE(encoded.toHex(), QByteArrayLiteral("0000000087d63241"));
    qint64 decoded = 0;
    QVERIFY(FileTransferCodec::decodeDouble(encoded, &decoded));
    QCOMPARE(decoded, 1234567);
}

void TestTransfer::doubleRejectsInvalidValues()
{
    qint64 value = 0;
    QVERIFY(!FileTransferCodec::decodeDouble(QByteArray(7, '\0'), &value));
    // Negative infinity placed at the low bytes of the double.
    QByteArray infinity(8, '\0');
    infinity[7] = static_cast<char>(0xff);
    infinity[6] = static_cast<char>(0xf0);
    QVERIFY(!FileTransferCodec::decodeDouble(infinity, &value));
    QVERIFY(!FileTransferCodec::decodeDouble(QByteArray(8, '\0'), nullptr));
}

void TestTransfer::safeFileNames()
{
    QVERIFY(FileTransferCodec::isSafeFileName(QStringLiteral("report.pdf")));
    QVERIFY(FileTransferCodec::isSafeFileName(QStringLiteral("báo cáo.docx")));
    QVERIFY(!FileTransferCodec::isSafeFileName(QStringLiteral("../etc/passwd")));
    QVERIFY(!FileTransferCodec::isSafeFileName(QStringLiteral("a/b.txt")));
    QVERIFY(!FileTransferCodec::isSafeFileName(QStringLiteral("a\\b.txt")));
    QVERIFY(!FileTransferCodec::isSafeFileName(QStringLiteral("..")));
    QVERIFY(!FileTransferCodec::isSafeFileName(QString()));
    QVERIFY(!FileTransferCodec::isSafeFileName(QStringLiteral("a|||b")));
    QVERIFY(!FileTransferCodec::isSafeFileName(QStringLiteral("bad\nname")));
}

void TestTransfer::multipartParsing()
{
    const FileTransferMessage offer = FileTransferCodec::parse(
        {QByteArray(1, 13), QByteArrayLiteral("5"), QByteArrayLiteral("notes.txt"),
         FileTransferCodec::encodeDouble(2048)});
    QCOMPARE(int(offer.kind), int(FileTransferMessage::Kind::Offer));
    QCOMPARE(offer.offer.transferId, QStringLiteral("5"));
    QCOMPARE(offer.offer.name, QStringLiteral("notes.txt"));
    QCOMPARE(offer.offer.size, 2048);

    const FileTransferMessage chunk = FileTransferCodec::parse(
        {QByteArray(1, 15), QByteArrayLiteral("5"), FileTransferCodec::encodeDouble(100),
         QByteArrayLiteral("abcdef")});
    QCOMPARE(int(chunk.kind), int(FileTransferMessage::Kind::Chunk));
    QCOMPARE(chunk.chunk.offset, 100);
    QCOMPARE(chunk.chunk.data, QByteArrayLiteral("abcdef"));

    const FileTransferMessage accept =
        FileTransferCodec::parse({QByteArray(1, 14), QByteArrayLiteral("5"), QByteArray(1, 1)});
    QCOMPARE(int(accept.kind), int(FileTransferMessage::Kind::Accept));
    QVERIFY(accept.accepted);

    const FileTransferMessage cancel =
        FileTransferCodec::parse({QByteArray(1, 16), QByteArrayLiteral("5")});
    QCOMPARE(int(cancel.kind), int(FileTransferMessage::Kind::Cancel));

    const FileTransferMessage ack =
        FileTransferCodec::parse({QByteArray(1, 21), QByteArrayLiteral("5")});
    QCOMPARE(int(ack.kind), int(FileTransferMessage::Kind::Ack));

    QCOMPARE(int(FileTransferCodec::parse({QByteArray(1, 1)}).kind),
             int(FileTransferMessage::Kind::None));
    QVERIFY(FileTransferCodec::isTransferCommand({QByteArray(1, 15)}));
    QVERIFY(!FileTransferCodec::isTransferCommand({QByteArray(1, 1)}));
}

void TestTransfer::textParsing()
{
    const FileTransferMessage offer =
        FileTransferCodec::parseText(QByteArrayLiteral("13|||9|||photo.jpg|||4096"));
    QCOMPARE(int(offer.kind), int(FileTransferMessage::Kind::Offer));
    QCOMPARE(offer.offer.transferId, QStringLiteral("9"));
    QCOMPARE(offer.offer.name, QStringLiteral("photo.jpg"));
    QCOMPARE(offer.offer.size, 4096);

    const FileTransferMessage cancel = FileTransferCodec::parseText(QByteArrayLiteral("16|||9"));
    QCOMPARE(int(cancel.kind), int(FileTransferMessage::Kind::Cancel));
    QCOMPARE(cancel.transferId, QStringLiteral("9"));

    QCOMPARE(int(FileTransferCodec::parseText(QByteArrayLiteral("hello")).kind),
             int(FileTransferMessage::Kind::None));
}

void TestTransfer::malformedMessagesThrow()
{
    QVERIFY_EXCEPTION_THROWN(
        FileTransferCodec::parse({QByteArray(1, 13), QByteArrayLiteral("1")}), UvException);
    QVERIFY_EXCEPTION_THROWN(
        FileTransferCodec::parse({QByteArray(1, 13), QByteArrayLiteral("1"),
                                  QByteArrayLiteral(".."), FileTransferCodec::encodeDouble(1)}),
        UvException);
    QVERIFY_EXCEPTION_THROWN(
        FileTransferCodec::parse({QByteArray(1, 15), QByteArrayLiteral("1"),
                                  FileTransferCodec::encodeDouble(0), QByteArray()}),
        UvException);
}

void TestTransfer::uploadPacesOnAcknowledgements()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString source = directory.filePath(QStringLiteral("upload.bin"));
    QFile file(source);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QByteArray content(70000, '\0');
    for (qsizetype index = 0; index < content.size(); ++index)
        content[index] = static_cast<char>(index % 251);
    QVERIFY(file.write(content) == content.size());
    file.close();

    FileTransferService service;
    service.setIncomingDirectory(directory.filePath(QStringLiteral("incoming")));
    QSignalSpy outbound(&service, &FileTransferService::outbound);
    QSignalSpy notice(&service, &FileTransferService::notice);

    QString error;
    const QString id = service.offerFile(source, &error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));
    QCOMPARE(outbound.count(), 1);
    QVERIFY(outbound.at(0).at(0).toByteArray().startsWith(QByteArrayLiteral("13|||")));

    // The service waits for the partner's acceptance before reading the file.
    service.handleInbound({QByteArray(1, 14), id.toLatin1(), QByteArray(1, 1)});
    QCOMPARE(outbound.count(), 2);
    const QByteArray firstChunk = outbound.at(1).at(0).toByteArray();
    QVERIFY(firstChunk.startsWith(QByteArrayLiteral("15|||") + id.toLatin1()
                                  + QByteArrayLiteral("|||0|||")));
    QCOMPARE(firstChunk.size(), 22 + FileTransferCodec::kMaxChunkBytes);

    // Acknowledge every chunk until the transfer completes.
    int guard = 0;
    while (findRow(service.rows(), id).value(QStringLiteral("state")).toString()
               == QLatin1String("transferring")
           && guard++ < 10) {
        service.handleInbound({QByteArray(1, 21), id.toLatin1()});
    }
    QCOMPARE(findRow(service.rows(), id).value(QStringLiteral("state")).toString(),
             QStringLiteral("completed"));
    QCOMPARE(findRow(service.rows(), id).value(QStringLiteral("transferred")).toLongLong(),
             qint64(content.size()));
    QVERIFY(notice.count() >= 2);
}

void TestTransfer::downloadAcceptsAndWritesTheFile()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    FileTransferService service;
    service.setIncomingDirectory(directory.path());
    QSignalSpy outbound(&service, &FileTransferService::outbound);

    const QString id = QStringLiteral("424242");
    service.handleInbound({QByteArray(1, 13), id.toLatin1(), QByteArrayLiteral("hello.txt"),
                           FileTransferCodec::encodeDouble(11)});
    const QVariantMap waiting = findRow(service.rows(), id);
    QCOMPARE(waiting.value(QStringLiteral("state")).toString(), QStringLiteral("waiting"));
    QCOMPARE(waiting.value(QStringLiteral("direction")).toString(), QStringLiteral("download"));
    QVERIFY(service.hasPendingOffers());

    service.acceptIncoming(id, true);
    QCOMPARE(outbound.count(), 1);
    QCOMPARE(outbound.at(0).at(0).toByteArray(), QByteArrayLiteral("14|||424242|||1"));

    const QByteArray data = QByteArrayLiteral("hello world");
    service.handleInbound({QByteArray(1, 15), id.toLatin1(), FileTransferCodec::encodeDouble(0),
                           data});
    QCOMPARE(outbound.count(), 2);
    QCOMPARE(outbound.at(1).at(0).toByteArray(), QByteArrayLiteral("21|||424242"));

    const QVariantMap completed = findRow(service.rows(), id);
    QCOMPARE(completed.value(QStringLiteral("state")).toString(), QStringLiteral("completed"));
    const QString path = completed.value(QStringLiteral("path")).toString();
    QCOMPARE(readAll(path), data);
    QCOMPARE(QFileInfo(path).fileName(), QStringLiteral("hello.txt"));
}

void TestTransfer::downloadRejectsAWrongOffset()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    FileTransferService service;
    service.setIncomingDirectory(directory.path());
    QSignalSpy outbound(&service, &FileTransferService::outbound);

    const QString id = QStringLiteral("77");
    service.handleInbound({QByteArray(1, 13), id.toLatin1(), QByteArrayLiteral("part.bin"),
                           FileTransferCodec::encodeDouble(10)});
    service.acceptIncoming(id, true);
    service.handleInbound({QByteArray(1, 15), id.toLatin1(), FileTransferCodec::encodeDouble(4),
                           QByteArrayLiteral("nope")});

    const QVariantMap failed = findRow(service.rows(), id);
    QCOMPARE(failed.value(QStringLiteral("state")).toString(), QStringLiteral("failed"));
    QVERIFY(!failed.value(QStringLiteral("error")).toString().isEmpty());
    // Cancel is always sent, and the partial file is removed.
    bool sawCancel = false;
    for (const QList<QVariant> &arguments : outbound) {
        if (arguments.at(0).toByteArray() == QByteArrayLiteral("16|||77"))
            sawCancel = true;
    }
    QVERIFY(sawCancel);
    QVERIFY(!QFileInfo::exists(failed.value(QStringLiteral("path")).toString()));
}

void TestTransfer::duplicateOfferIsCancelled()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    FileTransferService service;
    service.setIncomingDirectory(directory.path());
    QSignalSpy outbound(&service, &FileTransferService::outbound);

    const QString id = QStringLiteral("55");
    const QList<QByteArray> offer = {QByteArray(1, 13), id.toLatin1(), QByteArrayLiteral("x.txt"),
                                     FileTransferCodec::encodeDouble(1)};
    service.handleInbound(offer);
    service.handleInbound(offer);
    QCOMPARE(service.rows().size(), 1);
    QCOMPARE(outbound.count(), 1);
    QCOMPARE(outbound.at(0).at(0).toByteArray(), QByteArrayLiteral("16|||55"));
}

void TestTransfer::incomingCancelStopsTheDownload()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    FileTransferService service;
    service.setIncomingDirectory(directory.path());

    const QString id = QStringLiteral("66");
    service.handleInbound({QByteArray(1, 13), id.toLatin1(), QByteArrayLiteral("y.txt"),
                           FileTransferCodec::encodeDouble(4)});
    service.acceptIncoming(id, true);
    service.handleInbound({QByteArray(1, 15), id.toLatin1(), FileTransferCodec::encodeDouble(0),
                           QByteArrayLiteral("ab")});
    service.handleInbound({QByteArray(1, 16), id.toLatin1()});

    const QVariantMap cancelled = findRow(service.rows(), id);
    QCOMPARE(cancelled.value(QStringLiteral("state")).toString(), QStringLiteral("cancelled"));
    QVERIFY(!QFileInfo::exists(cancelled.value(QStringLiteral("path")).toString()));
}

void TestTransfer::unsafeOfferIsRefused()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    FileTransferService service;
    service.setIncomingDirectory(directory.path());

    // The codec refuses the message, so the session turns it into a notice and
    // the service never creates a transfer row.
    QVERIFY_EXCEPTION_THROWN(
        service.handleInbound({QByteArray(1, 13), QByteArrayLiteral("1"),
                               QByteArrayLiteral("../../escape.txt"),
                               FileTransferCodec::encodeDouble(1)}),
        UvException);
    QVERIFY(service.rows().isEmpty());
}

QTEST_MAIN(TestTransfer)
#include "tst_transfer.moc"
