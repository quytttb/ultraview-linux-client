#pragma once
//
// Persisted client settings.
//
// Everything the user can configure lives in `QSettings` (INI on Linux) *except*
// passwords: those follow `security::CredentialStore` and are only persisted when
// a platform secret service is available, which is reported to the UI through
// `passwordPersistenceAvailable`.
//
// The defaults are the recovered production values: the reference endpoints, full
// TLS verification, the reference frame limit and the reference cache/block
// parameters.  Endpoint overrides exist because offline tests and private
// deployments need them; they are never applied silently (the UI shows when an
// override is active).
//
#include <QByteArray>
#include <QObject>
#include <QString>

#include "network/ConnectionManager.h"
#include "protocol/PacketCodec.h"
#include "security/CredentialStore.h"
#include "security/TlsPolicy.h"

namespace ultraview::models {

class SettingsModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString registrationProfilePath READ registrationProfilePath WRITE setRegistrationProfilePath NOTIFY registrationProfilePathChanged)
    Q_PROPERTY(QString partnerTemplatePath READ partnerTemplatePath WRITE setPartnerTemplatePath NOTIFY partnerTemplatePathChanged)
    Q_PROPERTY(QString dictionaryPath READ dictionaryPath WRITE setDictionaryPath NOTIFY dictionaryPathChanged)
    Q_PROPERTY(int tlsMode READ tlsMode WRITE setTlsMode NOTIFY tlsModeChanged)
    Q_PROPERTY(bool certificateVerification READ certificateVerification NOTIFY tlsModeChanged)
    Q_PROPERTY(QString tlsWarning READ tlsWarning NOTIFY tlsModeChanged)
    Q_PROPERTY(bool passwordPersistenceAvailable READ passwordPersistenceAvailable CONSTANT)
    Q_PROPERTY(bool rememberPassword READ rememberPassword WRITE setRememberPassword NOTIFY rememberPasswordChanged)
    Q_PROPERTY(int timeoutMs READ timeoutMs WRITE setTimeoutMs NOTIFY limitsChanged)
    Q_PROPERTY(int maxFrameBytes READ maxFrameBytes WRITE setMaxFrameBytes NOTIFY limitsChanged)
    Q_PROPERTY(int jpegBlockSize READ jpegBlockSize WRITE setJpegBlockSize NOTIFY decoderChanged)
    Q_PROPERTY(int cacheLimit READ cacheLimit WRITE setCacheLimit NOTIFY decoderChanged)
    Q_PROPERTY(int fillMode READ fillMode WRITE setFillMode NOTIFY viewChanged)
    Q_PROPERTY(bool smoothScaling READ smoothScaling WRITE setSmoothScaling NOTIFY viewChanged)
    Q_PROPERTY(bool hideLocalPointer READ hideLocalPointer WRITE setHideLocalPointer NOTIFY viewChanged)
    Q_PROPERTY(int viewportWidth READ viewportWidth WRITE setViewportWidth NOTIFY viewChanged)
    Q_PROPERTY(int viewportHeight READ viewportHeight WRITE setViewportHeight NOTIFY viewChanged)
    Q_PROPERTY(QString registrationHostOverride READ registrationHostOverride WRITE setRegistrationHostOverride NOTIFY endpointsChanged)
    Q_PROPERTY(int registrationPortOverride READ registrationPortOverride WRITE setRegistrationPortOverride NOTIFY endpointsChanged)
    Q_PROPERTY(QString registrationHttpHostOverride READ registrationHttpHostOverride WRITE setRegistrationHttpHostOverride NOTIFY endpointsChanged)
    Q_PROPERTY(QString registrationPeerNameOverride READ registrationPeerNameOverride WRITE setRegistrationPeerNameOverride NOTIFY endpointsChanged)
    Q_PROPERTY(QString relayHostOverride READ relayHostOverride WRITE setRelayHostOverride NOTIFY endpointsChanged)
    Q_PROPERTY(int relayPortOverride READ relayPortOverride WRITE setRelayPortOverride NOTIFY endpointsChanged)
    Q_PROPERTY(QString relayHttpHostOverride READ relayHttpHostOverride WRITE setRelayHttpHostOverride NOTIFY endpointsChanged)
    Q_PROPERTY(QString relayPeerNameOverride READ relayPeerNameOverride WRITE setRelayPeerNameOverride NOTIFY endpointsChanged)
    Q_PROPERTY(bool endpointOverrideActive READ endpointOverrideActive NOTIFY endpointsChanged)

public:
    explicit SettingsModel(QObject *parent = nullptr);
    ~SettingsModel() override;

    // --- files --------------------------------------------------------------
    QString registrationProfilePath() const { return m_registrationProfilePath; }
    void setRegistrationProfilePath(const QString &path);
    QString partnerTemplatePath() const { return m_partnerTemplatePath; }
    void setPartnerTemplatePath(const QString &path);
    QString dictionaryPath() const { return m_dictionaryPath; }
    void setDictionaryPath(const QString &path);

    // --- security -----------------------------------------------------------
    int tlsMode() const { return static_cast<int>(m_tlsMode); }
    void setTlsMode(int mode);
    bool certificateVerification() const;
    QString tlsWarning() const { return tlsPolicy().warningText(); }
    security::TlsPolicy tlsPolicy() const;
    /// User-facing name of the active TLS mode.
    Q_INVOKABLE QString tlsModeName() const;

    bool passwordPersistenceAvailable() const { return security::CredentialStore::isPasswordStorageAvailable(); }
    bool rememberPassword() const { return m_rememberPassword && security::CredentialStore::isPasswordStorageAvailable(); }
    void setRememberPassword(bool remember);

    // --- limits -------------------------------------------------------------
    int timeoutMs() const { return m_timeoutMs; }
    void setTimeoutMs(int value);
    int maxFrameBytes() const { return m_maxFrameBytes; }
    void setMaxFrameBytes(int value);

    // --- decoder ------------------------------------------------------------
    int jpegBlockSize() const { return m_jpegBlockSize; }
    void setJpegBlockSize(int value);
    int cacheLimit() const { return m_cacheLimit; }
    void setCacheLimit(int value);

    // --- view ---------------------------------------------------------------
    int fillMode() const { return m_fillMode; }
    void setFillMode(int mode);
    bool smoothScaling() const { return m_smoothScaling; }
    void setSmoothScaling(bool smooth);
    bool hideLocalPointer() const { return m_hideLocalPointer; }
    void setHideLocalPointer(bool hide);
    int viewportWidth() const { return m_viewportWidth; }
    void setViewportWidth(int value);
    int viewportHeight() const { return m_viewportHeight; }
    void setViewportHeight(int value);

    // --- endpoint overrides -------------------------------------------------
    QString registrationHostOverride() const { return m_registrationHostOverride; }
    void setRegistrationHostOverride(const QString &value);
    int registrationPortOverride() const { return m_registrationPortOverride; }
    void setRegistrationPortOverride(int value);
    QString registrationHttpHostOverride() const { return m_registrationHttpHostOverride; }
    void setRegistrationHttpHostOverride(const QString &value);
    QString registrationPeerNameOverride() const { return m_registrationPeerNameOverride; }
    void setRegistrationPeerNameOverride(const QString &value);
    QString relayHostOverride() const { return m_relayHostOverride; }
    void setRelayHostOverride(const QString &value);
    int relayPortOverride() const { return m_relayPortOverride; }
    void setRelayPortOverride(int value);
    QString relayHttpHostOverride() const { return m_relayHttpHostOverride; }
    void setRelayHttpHostOverride(const QString &value);
    /// Certificate name for the relay channel; empty means the registration
    /// certificate name. The route hostname is an HTTP header, not a TLS name.
    QString relayPeerNameOverride() const { return m_relayPeerNameOverride; }
    void setRelayPeerNameOverride(const QString &value);
    /// True when at least one endpoint override is set (the UI warns).
    bool endpointOverrideActive() const;

    /// Reads the screen dictionary (8bit.dat).  Empty string on success, error
    /// text otherwise.  The file is *user supplied*; the project never ships it.
    QByteArray loadDictionary(QString *error) const;
    Q_INVOKABLE QString dictionaryStatus() const;
    /// The decoder options derived from the settings.
    decoder::ScreenDecoder::Options decoderOptions() const;

    /// Persist the current values.
    Q_INVOKABLE void save();
    Q_INVOKABLE void reload();
    Q_INVOKABLE void resetToDefaults();

signals:
    void registrationProfilePathChanged();
    void partnerTemplatePathChanged();
    void dictionaryPathChanged();
    void tlsModeChanged();
    void rememberPasswordChanged();
    void limitsChanged();
    void decoderChanged();
    void viewChanged();
    void endpointsChanged();

private:
    void load();

    QString m_registrationProfilePath;
    QString m_partnerTemplatePath;
    QString m_dictionaryPath;
    security::TlsPolicy::Mode m_tlsMode = security::TlsPolicy::Mode::VerifyPeer;
    bool m_rememberPassword = false;
    int m_timeoutMs = 8000;
    int m_maxFrameBytes = static_cast<int>(protocol::kDefaultMaxFrameBytes);
    int m_jpegBlockSize = 32;
    int m_cacheLimit = decoder::TileCache::kDefaultLimit;
    int m_fillMode = 0;
    bool m_smoothScaling = true;
    bool m_hideLocalPointer = false;
    int m_viewportWidth = 1280;
    int m_viewportHeight = 720;
    QString m_registrationHostOverride;
    int m_registrationPortOverride = 0;
    QString m_registrationHttpHostOverride;
    QString m_registrationPeerNameOverride;
    QString m_relayHostOverride;
    int m_relayPortOverride = 0;
    QString m_relayHttpHostOverride;
    QString m_relayPeerNameOverride;
};

} // namespace ultraview::models
