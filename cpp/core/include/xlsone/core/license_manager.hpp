#pragma once

#include <climits>

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QMetaType>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace xlsone {

enum class LicenseState {
    Unactivated,
    Activated,
    Expired,
    Trial
};

enum class LicensePlan {
    Trial,
    PersonalLifetime,
    PersonalYearly,
    Enterprise10
};

/// Features that can be gated by license state.
enum class LicenseFeature {
    ImportFiles,      ///< Importing / appending workbooks
    BatchExport,      ///< Exporting merged results
    NoExportWatermark ///< Exporting without a watermark
};

struct LicenseInfo {
    QString keyId;
    LicensePlan plan = LicensePlan::PersonalLifetime;
    QString deviceHash;
    QDateTime issuedAt;
    QDateTime expiresAt; // null/invalid means never
    QByteArray rawPayload; // canonical JSON that was signed
};

struct ActivationResult {
    bool success = false;
    bool trial = false;
    QString errorMessage;
};

class LicenseManager final : public QObject {
    Q_OBJECT

public:
    explicit LicenseManager(QObject* parent = nullptr);
    ~LicenseManager() override;

    /// Current license state.
    LicenseState state() const;

    /// Current license info (populated after successful applyLicenseFile).
    LicenseInfo currentInfo() const;

    /// Whether the current license grants full functionality.
    bool isFullyLicensed() const;

    /// Whether the user is in the post-expiration grace period.
    bool isInGracePeriod() const;

    /// Check whether a specific feature is allowed in the current state.
    bool canUse(LicenseFeature feature) const;

    /// Maximum number of files allowed in a single import batch.
    /// Returns INT_MAX when unlimited.
    int maxImportFiles() const;

    /// Watermark text to embed in exported files when restricted.
    /// Returns an empty string when no watermark should be applied.
    QString exportWatermarkText() const;

    /// Human-readable message explaining the current restriction.
    QString restrictionMessage() const;

    /// Request a signed 14-day trial license from the activation server.
    void requestTrial(const QString& deviceFingerprint);

    /// Check trial status. Returns remaining days, or -1 if not in trial / expired.
    int checkTrial() const;

    /// Remaining local grace days after a signed license expires.
    int graceRemainingDays() const;

    /// Compute a stable device fingerprint for this machine.
    /// On Windows this uses WMIC + MachineGuid; elsewhere it falls back to
    /// QSysInfo::machineUniqueId(). The result is cached for the process lifetime.
    static QString deviceFingerprint();

    /// Activation API base URL (region-dependent). International builds resolve
    /// to https://api.xlsone.com; domestic builds (XLSONE_ACTIVATION_BASE_URL)
    /// resolve to https://api.z-pulse.cn. Used by the offline-activation dialog
    /// to open the matching /offline page.
    static QString activationBaseUrl();

    /// Clear the cached device fingerprint (rarely needed, e.g. after major hardware change).
    static void clearDeviceFingerprintCache();

    /// Verify a signed license file (online or offline) for the current device.
    /// If the license is valid it is persisted and the state becomes Activated.
    bool applyLicenseFile(const QByteArray& licenseData, const QString& deviceFingerprint,
                          LicenseInfo* info = nullptr, QString* errorMessage = nullptr);

    /// Request activation with a key. Result delivered via activationFinished.
    void activate(const QString& key, const QString& deviceFingerprint);

    /// Import and validate an offline license file for the current device.
    bool importOfflineLicenseFile(const QString& path, const QString& deviceFingerprint,
                                  QString* errorMessage = nullptr);

#ifdef XLSONE_BUILD_TESTS
    /// Test seam: force a license state without signature verification.
    /// Only available when the test target is built with XLSONE_BUILD_TESTS.
    void applyTestLicense(const LicenseInfo& info, LicenseState state)
    {
        currentInfo_ = info;
        setState(state);
    }
#endif

signals:
    void stateChanged();
    void activationFinished(const ActivationResult& result);

private slots:
    void onActivationReply(QNetworkReply* reply);

private:
    void setState(LicenseState s);
    void loadPersistedState();
    bool loadOrCreateLinuxDefaultLicense();
    static bool verifyEd25519Signature(const QByteArray& message, const QByteArray& signature);
    static bool checkDeviceHash(const QJsonObject& licenseObj, const QString& actualFingerprint);

    /// Parse and verify a stored license without checking expiry.
    /// Used to preserve Expired state for valid-but-expired licenses.
    static bool parseVerifiedLicenseInfo(const QByteArray& licenseData,
                                         const QString& fingerprint,
                                         LicenseInfo* info);

    QNetworkAccessManager* networkManager_ = nullptr;
    LicenseState state_ = LicenseState::Unactivated;
    LicenseInfo currentInfo_;
};

} // namespace xlsone

Q_DECLARE_METATYPE(xlsone::ActivationResult)
