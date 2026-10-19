#include "security/CredentialStore.h"

#include "core/ErrorTypes.h"

#if defined(ULTRAVIEW_HAVE_LIBSECRET)
#pragma push_macro("signals")
#undef signals
#include <glib.h>
#include <libsecret/secret.h>
#pragma pop_macro("signals")

namespace {
const SecretSchema *passwordSchema()
{
    static const SecretSchema schema = {
        "net.ultraviewer.Client.Password",
        SECRET_SCHEMA_NONE,
        {
            {"partner", SECRET_SCHEMA_ATTRIBUTE_STRING},
            {nullptr, static_cast<SecretSchemaAttributeType>(0)},
        },
        0, 0, 0, 0, 0, 0, 0, 0
    };
    return &schema;
}
} // namespace
#endif

namespace ultraview::security {

CredentialStore::CredentialStore(QObject *parent)
    : QObject(parent)
{
}

bool CredentialStore::isPasswordStorageAvailable()
{
#if defined(ULTRAVIEW_HAVE_LIBSECRET)
    return true;
#else
    return false;
#endif
}

QString CredentialStore::backendName()
{
#if defined(ULTRAVIEW_HAVE_LIBSECRET)
    return QStringLiteral("libsecret (GNOME keyring / KWallet)");
#else
    return QStringLiteral("unavailable (build without libsecret)");
#endif
}

bool CredentialStore::storePassword(const QString &partnerId, const QString &password, QString *error)
{
    if (partnerId.isEmpty()) {
        if (error)
            *error = QStringLiteral("empty partner id");
        return false;
    }
#if defined(ULTRAVIEW_HAVE_LIBSECRET)
    GError *glibError = nullptr;
    const QByteArray partner = partnerId.toUtf8();
    const QByteArray secret = password.toUtf8();
    const bool ok = secret_password_store_sync(passwordSchema(), SECRET_COLLECTION_DEFAULT,
                                               "UltraViewer partner password", secret.constData(),
                                               nullptr, &glibError,
                                               "partner", partner.constData(), nullptr) != FALSE;
    if (!ok) {
        if (error)
            *error = QStringLiteral("the secret service rejected the request");
        if (glibError)
            g_error_free(glibError);
        return false;
    }
    return true;
#else
    Q_UNUSED(password);
    if (error) {
        *error = QStringLiteral(
            "no platform secret service is available in this build; the password was not stored");
    }
    return false;
#endif
}

std::optional<QString> CredentialStore::password(const QString &partnerId, QString *error) const
{
    if (partnerId.isEmpty())
        return std::nullopt;
#if defined(ULTRAVIEW_HAVE_LIBSECRET)
    GError *glibError = nullptr;
    const QByteArray partner = partnerId.toUtf8();
    gchar *secret = secret_password_lookup_sync(passwordSchema(), nullptr, &glibError,
                                                "partner", partner.constData(), nullptr);
    if (glibError) {
        if (error)
            *error = QStringLiteral("the secret service could not be queried");
        g_error_free(glibError);
        return std::nullopt;
    }
    if (!secret)
        return std::nullopt;
    const QString value = QString::fromUtf8(secret);
    secret_password_free(secret);
    return value;
#else
    Q_UNUSED(error);
    return std::nullopt;
#endif
}

bool CredentialStore::forgetPassword(const QString &partnerId, QString *error)
{
#if defined(ULTRAVIEW_HAVE_LIBSECRET)
    GError *glibError = nullptr;
    const QByteArray partner = partnerId.toUtf8();
    const bool ok = secret_password_clear_sync(passwordSchema(), nullptr, &glibError,
                                               "partner", partner.constData(), nullptr) != FALSE;
    if (!ok) {
        if (error)
            *error = QStringLiteral("the secret service could not remove the entry");
        if (glibError)
            g_error_free(glibError);
        return false;
    }
    return true;
#else
    Q_UNUSED(partnerId);
    if (error)
        *error = QStringLiteral("no platform secret service is available in this build");
    return false;
#endif
}

bool CredentialStore::forgetAll(QString *error)
{
    // Entries are looked up individually, so "forget all" is driven by the caller
    // that owns the recent-connection list.
    Q_UNUSED(error);
    return true;
}

void CredentialStore::setSessionPassword(const QString &partnerId, const QString &password)
{
    if (partnerId.isEmpty())
        return;
    if (password.isEmpty())
        m_sessionPasswords.remove(partnerId);
    else
        m_sessionPasswords.insert(partnerId, password);
}

std::optional<QString> CredentialStore::sessionPassword(const QString &partnerId) const
{
    const auto found = m_sessionPasswords.constFind(partnerId);
    if (found == m_sessionPasswords.constEnd())
        return std::nullopt;
    return *found;
}

void CredentialStore::clearSessionPasswords()
{
    m_sessionPasswords.clear();
}

} // namespace ultraview::security
