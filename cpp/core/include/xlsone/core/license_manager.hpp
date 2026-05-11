#pragma once

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

    /// Request activation with a key. Result delivered via activationFinished.
    void activate(const QString& key, const QString& deviceId);

    /// Import and validate an offline license file for the current device.
    bool importOfflineLicenseFile(const QString& path, const QString& deviceId, QString* errorMessage = nullptr);

signals:
    void stateChanged();
    void activationFinished(const ActivationResult& result);

private slots:
    void onActivationReply(QNetworkReply* reply);

private:
    void setState(LicenseState s);
    void loadPersistedState();

    QNetworkAccessManager* networkManager_ = nullptr;
    LicenseState state_ = LicenseState::Unactivated;
};

} // namespace xlsone

Q_DECLARE_METATYPE(xlsone::ActivationResult)
