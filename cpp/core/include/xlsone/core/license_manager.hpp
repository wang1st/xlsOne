#pragma once

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
    QString errorMessage;
};

class LicenseManager final : public QObject {
    Q_OBJECT

public:
    explicit LicenseManager(QObject* parent = nullptr);
    ~LicenseManager() override;

    /// True when running on domestic ARM64 Linux (UOS/Kylin/Phytium/Kunpeng) — always free.
    static bool isFreePlatform();

    /// Current license state.
    LicenseState state() const;

    /// Start a 14-day trial. Returns remaining days.
    int startTrial();

    /// Check trial status. Returns remaining days, or -1 if not in trial / expired.
    int checkTrial() const;

    /// Compute a stable device fingerprint for this machine.
    /// On Windows this uses WMIC + MachineGuid; elsewhere it falls back to
    /// QSysInfo::machineUniqueId(). The result is cached for the process lifetime.
    static QString deviceFingerprint();

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

signals:
    void stateChanged();
    void activationFinished(const ActivationResult& result);

private slots:
    void onActivationReply(QNetworkReply* reply);

private:
    void setState(LicenseState s);
    void loadPersistedState();
    bool verifyEd25519Signature(const QByteArray& message, const QByteArray& signature) const;
    bool checkDeviceHash(const QJsonObject& licenseObj, const QString& actualFingerprint) const;

    QNetworkAccessManager* networkManager_ = nullptr;
    LicenseState state_ = LicenseState::Unactivated;
};

} // namespace xlsone

Q_DECLARE_METATYPE(xlsone::ActivationResult)
