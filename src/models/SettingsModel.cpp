#include "models/SettingsModel.h"

#include "core/ErrorTypes.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

namespace ultraview::models {
namespace {

constexpr auto kRegistrationProfileKey = "profiles/registration";
constexpr auto kPartnerTemplateKey = "profiles/partnerTemplate";
constexpr auto kDictionaryKey = "profiles/dictionary";
constexpr auto kTlsModeKey = "security/tlsMode";
constexpr auto kRememberPasswordKey = "security/rememberPassword";
constexpr auto kTimeoutKey = "limits/timeoutMs";
constexpr auto kMaxFrameKey = "limits/maxFrameBytes";
constexpr auto kBlockSizeKey = "decoder/jpegBlockSize";
constexpr auto kCacheLimitKey = "decoder/cacheLimit";
constexpr auto kFillModeKey = "view/fillMode";
constexpr auto kSmoothKey = "view/smoothScaling";
constexpr auto kHidePointerKey = "view/hideLocalPointer";
constexpr auto kViewportWidthKey = "view/viewportWidth";
constexpr auto kViewportHeightKey = "view/viewportHeight";
constexpr auto kRegistrationHostKey = "endpoints/registrationHost";
constexpr auto kRegistrationPortKey = "endpoints/registrationPort";
constexpr auto kRegistrationHttpHostKey = "endpoints/registrationHttpHost";
constexpr auto kRegistrationPeerKey = "endpoints/registrationPeerName";
constexpr auto kRelayHostKey = "endpoints/relayHost";
constexpr auto kRelayPortKey = "endpoints/relayPort";
constexpr auto kRelayHttpHostKey = "endpoints/relayHttpHost";
constexpr auto kRelayPeerKey = "endpoints/relayPeerName";

QSettings settings()
{
    return QSettings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("ultraview"),
                     QStringLiteral("client"));
}

} // namespace

SettingsModel::SettingsModel(QObject *parent)
    : QObject(parent)
{
    load();
}

SettingsModel::~SettingsModel() = default;

void SettingsModel::load()
{
    QSettings store = settings();
    m_registrationProfilePath = store.value(kRegistrationProfileKey).toString();
    m_partnerTemplatePath = store.value(kPartnerTemplateKey).toString();
    m_dictionaryPath = store.value(kDictionaryKey).toString();
    m_tlsMode = security::TlsPolicy::fromSettingsValue(store.value(kTlsModeKey).toString()).mode();
    m_rememberPassword = store.value(kRememberPasswordKey, false).toBool();
    m_timeoutMs = store.value(kTimeoutKey, 8000).toInt();
    m_maxFrameBytes = store.value(kMaxFrameKey, static_cast<int>(protocol::kDefaultMaxFrameBytes)).toInt();
    m_jpegBlockSize = store.value(kBlockSizeKey, 32).toInt();
    m_cacheLimit = store.value(kCacheLimitKey, decoder::TileCache::kDefaultLimit).toInt();
    m_fillMode = store.value(kFillModeKey, 0).toInt();
    m_smoothScaling = store.value(kSmoothKey, true).toBool();
    m_hideLocalPointer = store.value(kHidePointerKey, false).toBool();
    m_viewportWidth = store.value(kViewportWidthKey, 1280).toInt();
    m_viewportHeight = store.value(kViewportHeightKey, 720).toInt();
    m_registrationHostOverride = store.value(kRegistrationHostKey).toString();
    m_registrationPortOverride = store.value(kRegistrationPortKey, 0).toInt();
    m_registrationHttpHostOverride = store.value(kRegistrationHttpHostKey).toString();
    m_registrationPeerNameOverride = store.value(kRegistrationPeerKey).toString();
    m_relayHostOverride = store.value(kRelayHostKey).toString();
    m_relayPortOverride = store.value(kRelayPortKey, 0).toInt();
    m_relayHttpHostOverride = store.value(kRelayHttpHostKey).toString();
    m_relayPeerNameOverride = store.value(kRelayPeerKey).toString();
}

void SettingsModel::save()
{
    QSettings store = settings();
    store.setValue(kRegistrationProfileKey, m_registrationProfilePath);
    store.setValue(kPartnerTemplateKey, m_partnerTemplatePath);
    store.setValue(kDictionaryKey, m_dictionaryPath);
    store.setValue(kTlsModeKey, tlsPolicy().settingsValue());
    store.setValue(kRememberPasswordKey, m_rememberPassword);
    store.setValue(kTimeoutKey, m_timeoutMs);
    store.setValue(kMaxFrameKey, m_maxFrameBytes);
    store.setValue(kBlockSizeKey, m_jpegBlockSize);
    store.setValue(kCacheLimitKey, m_cacheLimit);
    store.setValue(kFillModeKey, m_fillMode);
    store.setValue(kSmoothKey, m_smoothScaling);
    store.setValue(kHidePointerKey, m_hideLocalPointer);
    store.setValue(kViewportWidthKey, m_viewportWidth);
    store.setValue(kViewportHeightKey, m_viewportHeight);
    store.setValue(kRegistrationHostKey, m_registrationHostOverride);
    store.setValue(kRegistrationPortKey, m_registrationPortOverride);
    store.setValue(kRegistrationHttpHostKey, m_registrationHttpHostOverride);
    store.setValue(kRegistrationPeerKey, m_registrationPeerNameOverride);
    store.setValue(kRelayHostKey, m_relayHostOverride);
    store.setValue(kRelayPortKey, m_relayPortOverride);
    store.setValue(kRelayHttpHostKey, m_relayHttpHostOverride);
    store.setValue(kRelayPeerKey, m_relayPeerNameOverride);
    store.sync();
}

void SettingsModel::reload()
{
    load();
    emit tlsModeChanged();
    emit rememberPasswordChanged();
    emit limitsChanged();
    emit decoderChanged();
    emit viewChanged();
    emit endpointsChanged();
    emit registrationProfilePathChanged();
    emit partnerTemplatePathChanged();
    emit dictionaryPathChanged();
}

void SettingsModel::resetToDefaults()
{
    m_registrationProfilePath.clear();
    m_partnerTemplatePath.clear();
    m_dictionaryPath.clear();
    m_tlsMode = security::TlsPolicy::Mode::VerifyPeer;
    m_rememberPassword = false;
    m_timeoutMs = 8000;
    m_maxFrameBytes = static_cast<int>(protocol::kDefaultMaxFrameBytes);
    m_jpegBlockSize = 32;
    m_cacheLimit = decoder::TileCache::kDefaultLimit;
    m_fillMode = 0;
    m_smoothScaling = true;
    m_hideLocalPointer = false;
    m_viewportWidth = 1280;
    m_viewportHeight = 720;
    m_registrationHostOverride.clear();
    m_registrationPortOverride = 0;
    m_registrationHttpHostOverride.clear();
    m_registrationPeerNameOverride.clear();
    m_relayHostOverride.clear();
    m_relayPortOverride = 0;
    m_relayHttpHostOverride.clear();
    m_relayPeerNameOverride.clear();
    save();
    reload();
}

void SettingsModel::setRegistrationProfilePath(const QString &path)
{
    if (m_registrationProfilePath == path)
        return;
    m_registrationProfilePath = path;
    emit registrationProfilePathChanged();
}

void SettingsModel::setPartnerTemplatePath(const QString &path)
{
    if (m_partnerTemplatePath == path)
        return;
    m_partnerTemplatePath = path;
    emit partnerTemplatePathChanged();
}

void SettingsModel::setDictionaryPath(const QString &path)
{
    if (m_dictionaryPath == path)
        return;
    m_dictionaryPath = path;
    emit dictionaryPathChanged();
}

void SettingsModel::setTlsMode(int mode)
{
    const auto value = static_cast<security::TlsPolicy::Mode>(mode);
    if (m_tlsMode == value)
        return;
    m_tlsMode = value;
    emit tlsModeChanged();
}

bool SettingsModel::certificateVerification() const
{
    return m_tlsMode != security::TlsPolicy::Mode::AcceptAnyCertificate;
}

security::TlsPolicy SettingsModel::tlsPolicy() const
{
    security::TlsPolicy policy(m_tlsMode);
    return policy;
}

QString SettingsModel::tlsModeName() const
{
    switch (m_tlsMode) {
    case security::TlsPolicy::Mode::VerifyPeer:
        return QStringLiteral("Verified (chain and host name)");
    case security::TlsPolicy::Mode::VerifyPeerAllowHostMismatch:
        return QStringLiteral("Verified chain, host name not checked");
    case security::TlsPolicy::Mode::AcceptAnyCertificate:
        return QStringLiteral("Any certificate accepted (insecure)");
    }
    return QStringLiteral("unknown");
}

void SettingsModel::setRememberPassword(bool remember)
{
    const bool effective = remember && security::CredentialStore::isPasswordStorageAvailable();
    if (m_rememberPassword == effective)
        return;
    m_rememberPassword = effective;
    emit rememberPasswordChanged();
}

void SettingsModel::setTimeoutMs(int value)
{
    const int clamped = std::clamp(value, 1000, 120000);
    if (m_timeoutMs == clamped)
        return;
    m_timeoutMs = clamped;
    emit limitsChanged();
}

void SettingsModel::setMaxFrameBytes(int value)
{
    const int clamped = std::clamp(value, 4096, static_cast<int>(protocol::kAbsoluteMaxFrameBytes));
    if (m_maxFrameBytes == clamped)
        return;
    m_maxFrameBytes = clamped;
    emit limitsChanged();
}

void SettingsModel::setJpegBlockSize(int value)
{
    const int clamped = std::clamp(value, 4, 256);
    if (m_jpegBlockSize == clamped)
        return;
    m_jpegBlockSize = clamped;
    emit decoderChanged();
}

void SettingsModel::setCacheLimit(int value)
{
    const int clamped = std::clamp(value, 1, 100000);
    if (m_cacheLimit == clamped)
        return;
    m_cacheLimit = clamped;
    emit decoderChanged();
}

void SettingsModel::setFillMode(int mode)
{
    const int clamped = std::clamp(mode, 0, 2);
    if (m_fillMode == clamped)
        return;
    m_fillMode = clamped;
    emit viewChanged();
}

void SettingsModel::setSmoothScaling(bool smooth)
{
    if (m_smoothScaling == smooth)
        return;
    m_smoothScaling = smooth;
    emit viewChanged();
}

void SettingsModel::setHideLocalPointer(bool hide)
{
    if (m_hideLocalPointer == hide)
        return;
    m_hideLocalPointer = hide;
    emit viewChanged();
}

void SettingsModel::setViewportWidth(int value)
{
    const int clamped = std::clamp(value, 320, 8192);
    if (m_viewportWidth == clamped)
        return;
    m_viewportWidth = clamped;
    emit viewChanged();
}

void SettingsModel::setViewportHeight(int value)
{
    const int clamped = std::clamp(value, 200, 8192);
    if (m_viewportHeight == clamped)
        return;
    m_viewportHeight = clamped;
    emit viewChanged();
}

void SettingsModel::setRegistrationHostOverride(const QString &value)
{
    if (m_registrationHostOverride == value)
        return;
    m_registrationHostOverride = value;
    emit endpointsChanged();
}

void SettingsModel::setRegistrationPortOverride(int value)
{
    const int clamped = (value <= 0 || value > 65535) ? 0 : value;
    if (m_registrationPortOverride == clamped)
        return;
    m_registrationPortOverride = clamped;
    emit endpointsChanged();
}

void SettingsModel::setRegistrationHttpHostOverride(const QString &value)
{
    if (m_registrationHttpHostOverride == value)
        return;
    m_registrationHttpHostOverride = value;
    emit endpointsChanged();
}

void SettingsModel::setRegistrationPeerNameOverride(const QString &value)
{
    if (m_registrationPeerNameOverride == value)
        return;
    m_registrationPeerNameOverride = value;
    emit endpointsChanged();
}

void SettingsModel::setRelayHostOverride(const QString &value)
{
    if (m_relayHostOverride == value)
        return;
    m_relayHostOverride = value;
    emit endpointsChanged();
}

void SettingsModel::setRelayPeerNameOverride(const QString &value)
{
    if (m_relayPeerNameOverride == value)
        return;
    m_relayPeerNameOverride = value;
    emit endpointsChanged();
}

void SettingsModel::setRelayPortOverride(int value)
{
    const int clamped = (value <= 0 || value > 65535) ? 0 : value;
    if (m_relayPortOverride == clamped)
        return;
    m_relayPortOverride = clamped;
    emit endpointsChanged();
}

void SettingsModel::setRelayHttpHostOverride(const QString &value)
{
    if (m_relayHttpHostOverride == value)
        return;
    m_relayHttpHostOverride = value;
    emit endpointsChanged();
}

bool SettingsModel::endpointOverrideActive() const
{
    return !m_registrationHostOverride.isEmpty() || m_registrationPortOverride != 0
        || !m_registrationHttpHostOverride.isEmpty() || !m_registrationPeerNameOverride.isEmpty()
        || !m_relayHostOverride.isEmpty() || m_relayPortOverride != 0
        || !m_relayHttpHostOverride.isEmpty();
}

QByteArray SettingsModel::loadDictionary(QString *error) const
{
    const auto fail = [error](const QString &message) {
        if (error != nullptr)
            *error = message;
        return QByteArray();
    };
    if (m_dictionaryPath.isEmpty())
        return fail(QStringLiteral("no screen dictionary is configured"));
    QFile file(m_dictionaryPath);
    if (!file.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("cannot read the screen dictionary: %1").arg(file.errorString()));
    const QByteArray data = file.readAll();
    if (data.isEmpty())
        return fail(QStringLiteral("the screen dictionary file is empty"));
    if (error != nullptr)
        error->clear();
    return data;
}

QString SettingsModel::dictionaryStatus() const
{
    if (m_dictionaryPath.isEmpty())
        return QStringLiteral("no dictionary configured");
    const QFileInfo info(m_dictionaryPath);
    if (!info.isReadable())
        return QStringLiteral("the configured dictionary file is not readable");
    return QStringLiteral("%1 (%2 bytes)").arg(info.fileName()).arg(info.size());
}

decoder::ScreenDecoder::Options SettingsModel::decoderOptions() const
{
    decoder::ScreenDecoder::Options options;
    options.blockSize = m_jpegBlockSize;
    options.cacheLimit = m_cacheLimit;
    return options;
}

} // namespace ultraview::models
