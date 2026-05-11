#include "xlsone/core/license_manager.hpp"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QSysInfo>
#include <QTimer>

namespace xlsone {

static constexpr int kTrialDurationDays = 14;
static const QString kTrialStartKey = QStringLiteral("license/trialStart");
static const QString kTokenKey = QStringLiteral("license/token");
static const QString kOfflineTokenKey = QStringLiteral("license/offline");

// ========== LicenseManager ==========

LicenseManager::LicenseManager(QObject* parent)
    : QObject(parent)
    , networkManager_(new QNetworkAccessManager(this))
{
    connect(networkManager_, &QNetworkAccessManager::finished,
            this, &LicenseManager::onActivationReply);
    loadPersistedState();
}

LicenseManager::~LicenseManager() = default;

bool LicenseManager::isFreePlatform()
{
#if defined(Q_OS_LINUX) && (defined(__aarch64__) || defined(__arm64__))
    return true;
#else
    return false;
#endif
}

LicenseState LicenseManager::state() const
{
    return state_;
}

int LicenseManager::startTrial()
{
    QSettings settings;
    settings.setValue(kTrialStartKey, QDateTime::currentDateTime());

    const int remaining = kTrialDurationDays;
    setState(LicenseState::Trial);
    return remaining;
}

int LicenseManager::checkTrial() const
{
    QSettings settings;
    const QDateTime start = settings.value(kTrialStartKey).toDateTime();
    if (!start.isValid()) {
        return -1;
    }

    const qint64 elapsed = start.daysTo(QDateTime::currentDateTime());
    const int remaining = kTrialDurationDays - static_cast<int>(elapsed);
    return remaining > 0 ? remaining : -1;
}

void LicenseManager::activate(const QString& key, const QString& deviceId)
{
    QJsonObject body;
    body[QStringLiteral("key")] = key;
    body[QStringLiteral("device_id")] = deviceId;
    body[QStringLiteral("device_name")] = QSysInfo::machineHostName();

    QNetworkRequest request{QUrl(QStringLiteral("https://api.xlsone.com/api/activate"))};
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QVariant(QStringLiteral("application/json")));
    request.setRawHeader("Accept", "application/json");

    QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    auto* reply = networkManager_->post(request, payload);

    auto* timer = new QTimer(reply);
    timer->setSingleShot(true);
    reply->setProperty("_timer", QVariant::fromValue(timer));
    connect(timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    timer->start(5000);
}

bool LicenseManager::importOfflineLicenseFile(const QString& path, const QString& deviceId, QString* errorMessage)
{
    auto fail = [&](const QString& message) {
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    };

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return fail(QStringLiteral("无法读取授权文件"));
    }

    const QString token = QString::fromUtf8(file.readAll()).trimmed();
    file.close();

    if (token.isEmpty()) {
        return fail(QStringLiteral("授权文件为空"));
    }

    const auto parts = token.split(QLatin1Char('.'));
    if (parts.size() < 2) {
        return fail(QStringLiteral("授权文件格式无效"));
    }

    QString payload = parts[1];
    payload.replace(QLatin1Char('-'), QLatin1Char('+')).replace(QLatin1Char('_'), QLatin1Char('/'));
    while (payload.length() % 4 != 0) {
        payload += QLatin1Char('=');
    }

    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromBase64(payload.toUtf8()));
    if (!doc.isObject()) {
        return fail(QStringLiteral("授权文件格式无效"));
    }

    const QJsonObject obj = doc.object();
    const QString dev = obj.value(QStringLiteral("dev")).toString();
    if (!dev.isEmpty() && dev != deviceId) {
        return fail(QStringLiteral("授权文件与当前设备不匹配"));
    }

    QSettings settings;
    settings.setValue(kTokenKey, token);
    settings.setValue(kOfflineTokenKey, token);
    setState(LicenseState::Activated);
    return true;
}

void LicenseManager::onActivationReply(QNetworkReply* reply)
{
    reply->deleteLater();

    if (auto* timer = qvariant_cast<QTimer*>(reply->property("_timer"))) {
        timer->stop();
    }

    ActivationResult result;

    if (reply->error() != QNetworkReply::NoError) {
        result.success = false;
        result.errorMessage = QStringLiteral("\u7f51\u7edc\u8fde\u63a5\u5931\u8d25\uff0c\u8bf7\u68c0\u67e5\u7f51\u7edc");
        emit activationFinished(result);
        return;
    }

    const int statusCode = reply->attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    const QJsonDocument doc = QJsonDocument::fromJson(body);

    if (statusCode != 200 || !doc.isObject()) {
        result.success = false;
        if (doc.isObject()) {
            const QJsonObject obj = doc.object();
            const QString error = obj.value(QStringLiteral("error")).toString();
            if (error == QStringLiteral("KEY_NOT_FOUND"))
                result.errorMessage = QStringLiteral("\u6fc0\u6d3b\u7801\u4e0d\u5b58\u5728");
            else if (error == QStringLiteral("KEY_REVOKED"))
                result.errorMessage = QStringLiteral("\u6fc0\u6d3b\u7801\u5df2\u88ab\u540a\u9500");
            else if (error == QStringLiteral("DEVICE_LIMIT"))
                result.errorMessage = QStringLiteral("\u5df2\u8fbe\u5230\u6700\u5927\u8bbe\u5907\u6570\u9650\u5236");
            else if (error == QStringLiteral("RATE_LIMITED"))
                result.errorMessage = QStringLiteral("\u8bf7\u6c42\u8fc7\u4e8e\u9891\u7e41\uff0c\u8bf7\u7a0d\u540e\u518d\u8bd5");
            else
                result.errorMessage = obj.value(QStringLiteral("message")).toString(
                    QStringLiteral("\u6fc0\u6d3b\u5931\u8d25"));
        } else {
            result.errorMessage = QStringLiteral("\u6fc0\u6d3b\u5931\u8d25: HTTP %1").arg(statusCode);
        }
        emit activationFinished(result);
        return;
    }

    const QJsonObject obj = doc.object();
    const QString token = obj.value(QStringLiteral("license_token")).toString();
    if (token.isEmpty()) {
        result.success = false;
        result.errorMessage = QStringLiteral("\u670d\u52a1\u5668\u54cd\u5e94\u5f02\u5e38");
        emit activationFinished(result);
        return;
    }

    QSettings settings;
    settings.setValue(kTokenKey, token);

    setState(LicenseState::Activated);
    result.success = true;
    emit activationFinished(result);
}

void LicenseManager::setState(LicenseState s)
{
    if (state_ != s) {
        state_ = s;
        emit stateChanged();
    }
}

void LicenseManager::loadPersistedState()
{
    // Domestic ARM64 Linux OS (UOS/Kylin/Phytium/Kunpeng) — always free
    if (isFreePlatform()) {
        setState(LicenseState::Activated);
        return;
    }

    QSettings settings;

    // Check for existing activation token
    const QString token = settings.value(kTokenKey).toString();
    if (!token.isEmpty()) {
        setState(LicenseState::Activated);
        return;
    }

    // Check trial status
    const int remaining = checkTrial();
    if (remaining > 0) {
        setState(LicenseState::Trial);
        return;
    }

    setState(LicenseState::Unactivated);
}

} // namespace xlsone
