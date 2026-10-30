#pragma once
//
// Interface language selection.
//
// The source strings are English; only the Vietnamese catalog ships with the
// application.  A stored code selects what the translator does at startup:
//
//   "system"  follow the desktop locale, English when no catalog matches
//   "vi"      force the Vietnamese catalog
//   "en"      force English by not installing any catalog
//
#include <QString>
#include <QStringList>

class QTranslator;

namespace ultraview::localization {

inline constexpr auto kSystemLanguage = "system";
inline constexpr auto kVietnameseLanguage = "vi";
inline constexpr auto kEnglishLanguage = "en";

/// Code used when the user never chose a language: follow the desktop locale,
/// which is English unless the system language is Vietnamese.
inline constexpr auto kDefaultLanguage = kSystemLanguage;

/// Maps arbitrary stored/system values onto "system", "vi" or "en".  Unknown
/// values fall back to "system".
QString normalizeLanguageCode(const QString &code);

/// Choose the first supported UI language, or English if none is available.
QString preferredUiLanguage(const QStringList &languages);

/// Language code persisted in `QSettings` (`ui/language`).
QString storedLanguageCode();

/// Installs a catalog for `code` into `translator`.  Returns true when a catalog
/// was loaded; false means English (no translation is installed).
bool installTranslator(QTranslator &translator, const QString &code = storedLanguageCode());

} // namespace ultraview::localization
