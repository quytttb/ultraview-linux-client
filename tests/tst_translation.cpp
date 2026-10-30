#include <QCoreApplication>
#include <QCryptographicHash>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTranslator>

#include "core/Language.h"
#include "decoder/ZstdDecoder.h"
#include "models/ConnectionViewModel.h"
#include "models/HostViewModel.h"
#include "models/SettingsModel.h"
#include "platform/GlobalMenu.h"
#include <zstd.h>

class TranslationTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void vietnamese();
    void languageCodes();
    void languageSelection();
    void connectDoesNotRequireProfile();
    void bundledDictionary();

private:
    QTemporaryDir m_configDir;
};

void TranslationTest::initTestCase()
{
    QVERIFY(m_configDir.isValid());
    // QSettings may cache the XDG location on first use, before languageSelection.
    // Set its path explicitly before any test creates a settings-backed model.
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_configDir.path());
}

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
    using ultraview::localization::preferredUiLanguage;
    QCOMPARE(preferredUiLanguage({QStringLiteral("en-US"), QStringLiteral("vi-VN")}), QStringLiteral("en"));
    QCOMPARE(preferredUiLanguage({QStringLiteral("vi-VN"), QStringLiteral("en-US")}), QStringLiteral("vi"));
    QCOMPARE(preferredUiLanguage({QStringLiteral("fr-FR")}), QStringLiteral("en"));

    // English is the source language, so no catalog is installed for it.
    QTranslator english;
    QVERIFY(!ultraview::localization::installTranslator(english, QStringLiteral("en")));
}

void TranslationTest::languageSelection()
{
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

void TranslationTest::connectDoesNotRequireProfile()
{
    ultraview::models::ConnectionViewModel controller;
    controller.setPartnerId(QStringLiteral("123456789"));
    QVERIFY(controller.settingsObject()->registrationProfilePath().isEmpty());
    QVERIFY(controller.canConnect());
}

void TranslationTest::bundledDictionary()
{
    ultraview::models::SettingsModel settings;
    settings.resetToDefaults();
    QString error;
    const QByteArray dictionary = settings.loadDictionary(&error);
    QVERIFY2(!dictionary.isEmpty(), qPrintable(error));
    QCOMPARE(QCryptographicHash::hash(dictionary, QCryptographicHash::Sha256).toHex(),
             QByteArray("9aa45789d9ff9d329a623a685d57b67e33c777019de9ce1f273f21860e4bfe6b"));
    const QByteArray pixels(4096, '\x42');
    QByteArray compressed(static_cast<qsizetype>(ZSTD_compressBound(pixels.size())), Qt::Uninitialized);
    ZSTD_CCtx *context = ZSTD_createCCtx();
    QVERIFY(context);
    const size_t size = ZSTD_compress_usingDict(context, compressed.data(), compressed.size(),
                                                pixels.constData(), pixels.size(),
                                                dictionary.constData(), dictionary.size(), 3);
    ZSTD_freeCCtx(context);
    QVERIFY(!ZSTD_isError(size));
    compressed.resize(static_cast<qsizetype>(size));
    ultraview::decoder::ZstdDecoder decoder;
    decoder.setDictionary(dictionary);
    QCOMPARE(decoder.decompress(compressed, pixels.size()), pixels);
    settings.setDictionaryPath(QStringLiteral("/nonexistent/dictionary"));
    QVERIFY(settings.loadDictionary(&error).isEmpty());
    QVERIFY(!error.isEmpty());
}

QTEST_MAIN(TranslationTest)
#include "tst_translation.moc"
