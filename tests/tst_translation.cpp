#include <QCoreApplication>
#include <QTest>
#include <QTranslator>

#include "models/ConnectionViewModel.h"
#include "models/HostViewModel.h"
#include "platform/GlobalMenu.h"

class TranslationTest : public QObject
{
    Q_OBJECT
private slots:
    void vietnamese();
};

void TranslationTest::vietnamese()
{
    QTranslator translator;
    QVERIFY(translator.load(QStringLiteral(UV_TRANSLATION_FILE)));
    QVERIFY(QCoreApplication::installTranslator(&translator));
    QCOMPARE(QCoreApplication::translate("ConnectPage", "Connect"), QString::fromUtf8("Kết nối"));
    ultraview::models::ConnectionViewModel controller;
    QCOMPARE(controller.statusText(), QString::fromUtf8("Sẵn sàng"));
    ultraview::models::HostViewModel host(controller.settingsObject());
    QCOMPARE(host.statusText(), QString::fromUtf8("Chưa chia sẻ. Chọn Bắt đầu chia sẻ để chia sẻ máy tính này."));
    const auto menu = ultraview::platform::describeMenu({});
    QCOMPARE(menu.first().text, QString::fromUtf8("Phiên"));
    QCoreApplication::removeTranslator(&translator);
}

QTEST_MAIN(TranslationTest)
#include "tst_translation.moc"
