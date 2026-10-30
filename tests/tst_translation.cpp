#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTranslator>

#include "core/Language.h"
#include "models/ConnectionViewModel.h"
#include "models/HostViewModel.h"
#include "models/SettingsModel.h"
#include "platform/GlobalMenu.h"

class TranslationTest : public QObject
{
    Q_OBJECT
private slots:
    void vietnamese();
    void languageCodes();
    void languageSelection();
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

void TranslationTest::languageCodes()
{
    using ultraview::localization::normalizeLanguageCode;
    QCOMPARE(normalizeLanguageCode(QStringLiteral("system")), QStringLiteral("system"));
    QCOMPARE(normalizeLanguageCode(QString()), QStringLiteral("system"));
    QCOMPARE(normalizeLanguageCode(QStringLiteral("vi")), QStringLiteral("vi"));
    QCOMPARE(normalizeLanguageCode(QStringLiteral("vi_VN")), QStringLiteral("vi"));
    QCOMPARE(normalizeLanguageCode(QStringLiteral("vi-VN")), QStringLiteral("vi"));
    QCOMPARE(normalizeLanguageCode(QStringLiteral("en")), QStringLiteral("en"));
    QCOMPARE(normalizeLanguageCode(QStringLiteral("en_US")), QStringLiteral("en"));
    QCOMPARE(normalizeLanguageCode(QStringLiteral("fr_FR")), QStringLiteral("system"));

    // English is the source language, so no catalog is installed for it.
    QTranslator english;
    QVERIFY(!ultraview::localization::installTranslator(english, QStringLiteral("en")));
}

void TranslationTest::languageSelection()
{
    QTemporaryDir home;
    QVERIFY(home.isValid());
    qputenv("XDG_CONFIG_HOME", home.path().toUtf8());

    ultraview::models::SettingsModel settings;
    // Following the system language is the default, and it is what this process
    // started with.
    QCOMPARE(settings.language(), QStringLiteral("system"));
    QCOMPARE(settings.languageIndex(), 0);
    QCOMPARE(settings.appliedLanguage(), QStringLiteral("system"));

    settings.setLanguageIndex(2);
    QCOMPARE(settings.language(), QStringLiteral("en"));
    QCOMPARE(settings.languageIndex(), 2);
    // The running process keeps its language until it is restarted.
    QCOMPARE(settings.appliedLanguage(), QStringLiteral("system"));

    // The choice is persisted immediately, before the Save button is pressed.
    QSettings store(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("ultraview"),
                    QStringLiteral("client"));
    QCOMPARE(store.value(QStringLiteral("ui/language")).toString(), QStringLiteral("en"));

    settings.setLanguageIndex(1);
    QCOMPARE(settings.language(), QStringLiteral("vi"));
    QCOMPARE(settings.languageIndex(), 1);

    // Reset restores the system default.
    settings.resetToDefaults();
    QCOMPARE(settings.language(), QStringLiteral("system"));
    QCOMPARE(settings.languageIndex(), 0);
}

QTEST_MAIN(TranslationTest)
#include "tst_translation.moc"
