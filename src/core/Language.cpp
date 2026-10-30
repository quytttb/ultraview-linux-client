#include "core/Language.h"

#include <QLocale>
#include <QSettings>
#include <QTranslator>

namespace ultraview::localization {
namespace {

QSettings languageStore()
{
    // Same store as models::SettingsModel so main.cpp can read the choice before
    // the view models exist.
    return QSettings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("ultraview"),
                     QStringLiteral("client"));
}

bool loadCatalog(QTranslator &translator, const QLocale &locale)
{
    return translator.load(locale, QStringLiteral("ultraview"), QStringLiteral("_"),
                           QStringLiteral(":/i18n"));
}

} // namespace

QString normalizeLanguageCode(const QString &code)
{
    const QString lower = code.trimmed().toLower();
    if (lower == QLatin1String(kEnglishLanguage) || lower.startsWith(QLatin1String("en_"))
        || lower.startsWith(QLatin1String("en-")))
        return QString::fromLatin1(kEnglishLanguage);
    if (lower == QLatin1String(kVietnameseLanguage) || lower.startsWith(QLatin1String("vi_"))
        || lower.startsWith(QLatin1String("vi-")))
        return QString::fromLatin1(kVietnameseLanguage);
    return QString::fromLatin1(kSystemLanguage);
}

QString storedLanguageCode()
{
    return normalizeLanguageCode(languageStore().value(QStringLiteral("ui/language"),
                                                        QString::fromLatin1(kDefaultLanguage))
                                     .toString());
}

QString preferredUiLanguage(const QStringList &languages)
{
    for (const QString &language : languages) {
        const QString normalized = normalizeLanguageCode(language);
        if (normalized != QLatin1String(kSystemLanguage))
            return normalized;
    }
    return QString::fromLatin1(kEnglishLanguage);
}

bool installTranslator(QTranslator &translator, const QString &code)
{
    const QString normalized = normalizeLanguageCode(code);
    if (normalized == QLatin1String(kEnglishLanguage))
        return false; // English is the source language: nothing to install.
    if (normalized == QLatin1String(kVietnameseLanguage))
        return loadCatalog(translator, QLocale(QString::fromLatin1(kVietnameseLanguage)));
    // Use the desktop's UI language preferences, not its date/number locale.
    if (preferredUiLanguage(QLocale::system().uiLanguages()) == QLatin1String(kVietnameseLanguage))
        return loadCatalog(translator, QLocale(QString::fromLatin1(kVietnameseLanguage)));
    return false;
}

} // namespace ultraview::localization
