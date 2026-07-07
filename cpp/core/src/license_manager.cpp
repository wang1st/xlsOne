#include "xlsone/core/license_manager.hpp"

#include "ed25519/monocypher-ed25519.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSettings>
#include <QSet>
#include <QSysInfo>
#include <QTimer>
#include <algorithm>

namespace xlsone {

static constexpr int kTrialDurationDays = 14;
static const QString kTrialStartKey = QStringLiteral("license/trialStart");
static const QString kTokenKey = QStringLiteral("license/token");
static const QString kOfflineTokenKey = QStringLiteral("license/offline");

// Process-level cache for the device fingerprint to avoid repeated WMIC/PowerShell calls.
static QString g_deviceFingerprintCache;

// Ed25519 public key for verifying license signatures (32 bytes).
// This key must match the private key used by the activation server.
//
// Generated key pair (keep the seed secret on the server; do NOT reuse the old
// example seed that was once committed to the repo — it is publicly known and
// would let anyone forge licenses). Regenerate and set ED25519_PRIVATE_KEY on the
// Worker, then update this public key to match.
static constexpr uint8_t kLicensePublicKey[32] = {
    0xa2, 0x07, 0x19, 0xb2, 0x5b, 0x53, 0x36, 0xd7,
    0xf4, 0xdb, 0xd4, 0x2d, 0xc2, 0x40, 0xf3, 0xab,
    0x88, 0x93, 0x77, 0x7f, 0xa0, 0xdc, 0x9a, 0x4d,
    0x99, 0x45, 0xef, 0x6e, 0xd8, 0x41, 0x9e, 0x32
};

namespace {

QByteArray base64UrlDecodeBytes(const QString& base64url)
{
    QString s = base64url;
    s.replace(QLatin1Char('-'), QLatin1Char('+'))
     .replace(QLatin1Char('_'), QLatin1Char('/'));
    while (s.length() % 4) {
        s.append(QLatin1Char('='));
    }
    return QByteArray::fromBase64(s.toUtf8());
}

QString bytesToHex(const QByteArray& bytes)
{
    return QString::fromLatin1(bytes.toHex());
}

QByteArray sha256(const QByteArray& data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256);
}

LicensePlan planFromString(const QString& plan)
{
    if (plan == QStringLiteral("enterprise_10")) {
        return LicensePlan::Enterprise10;
    }
    if (plan == QStringLiteral("personal_yearly")) {
        return LicensePlan::PersonalYearly;
    }
    if (plan == QStringLiteral("trial")) {
        return LicensePlan::Trial;
    }
    return LicensePlan::PersonalLifetime;
}

#if defined(Q_OS_WIN)
QString runPowerShellCommand(const QString& script)
{
    QProcess process;
    process.start(QStringLiteral("powershell.exe"),
                  QStringList{ QStringLiteral("-NoProfile"),
                               QStringLiteral("-Command"),
                               script });
    if (!process.waitForFinished(5000)) {
        process.kill();
        return QString();
    }
    QString output = QString::fromLocal8Bit(process.readAllStandardOutput());
    return output.trimmed();
}

QString runWmicCommand(const QString& className, const QString& property)
{
    QProcess process;
    process.start(QStringLiteral("wmic"),
                  QStringList{ className, QStringLiteral("get"), property });
    if (!process.waitForFinished(5000)) {
        process.kill();
        return QString();
    }
    QString output = QString::fromLocal8Bit(process.readAllStandardOutput());
    // WMIC output has a header line followed by the value
    QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (lines.size() >= 2) {
        return lines.at(1).trimmed();
    }
    return QString();
}

QString readMachineGuid()
{
    QSettings settings(QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Cryptography"),
                       QSettings::NativeFormat);
    return settings.value(QStringLiteral("MachineGuid")).toString().trimmed();
}
#endif // Q_OS_WIN

QString normalizeHardwareValue(const QString& value)
{
    QString cleaned = value.toUpper().trimmed();
    // Remove common placeholder/noise strings
    static const QStringList placeholders = {
        QStringLiteral("TO BE FILLED BY O.E.M."),
        QStringLiteral("NONE"),
        QStringLiteral("NOT AVAILABLE"),
        QStringLiteral("BASEBOARD SERIAL NUMBER"),
        QStringLiteral("SERIALNUMBER"),
    };
    for (const QString& p : placeholders) {
        if (cleaned == p) {
            return QString();
        }
    }
    return cleaned;
}

struct HardwareComponents {
    QString baseboardSerial;
    QString cpuId;
    QString diskSerial;
    QString machineGuid;

    QStringList nonEmptyValues() const
    {
        QStringList values;
        if (!baseboardSerial.isEmpty()) values << baseboardSerial;
        if (!cpuId.isEmpty()) values << cpuId;
        if (!diskSerial.isEmpty()) values << diskSerial;
        if (!machineGuid.isEmpty()) values << machineGuid;
        return values;
    }

    QStringList componentHashes() const
    {
        QStringList hashes;
        if (!baseboardSerial.isEmpty())
            hashes << bytesToHex(sha256(baseboardSerial.toUtf8()));
        if (!cpuId.isEmpty())
            hashes << bytesToHex(sha256(cpuId.toUtf8()));
        if (!diskSerial.isEmpty())
            hashes << bytesToHex(sha256(diskSerial.toUtf8()));
        if (!machineGuid.isEmpty())
            hashes << bytesToHex(sha256(machineGuid.toUtf8()));
        return hashes;
    }

    QString fullHash() const
    {
        QStringList values = nonEmptyValues();
        std::sort(values.begin(), values.end());
        if (values.isEmpty()) {
            return QString();
        }
        return bytesToHex(sha256(values.join(QLatin1Char('|')).toUtf8()));
    }
};

HardwareComponents collectWindowsComponents()
{
    HardwareComponents comp;
#if defined(Q_OS_WIN)
    comp.baseboardSerial = normalizeHardwareValue(
        runPowerShellCommand(QStringLiteral("(Get-CimInstance Win32_BaseBoard).SerialNumber")));
    if (comp.baseboardSerial.isEmpty()) {
        comp.baseboardSerial = normalizeHardwareValue(runWmicCommand(QStringLiteral("baseboard"),
                                                                      QStringLiteral("serialnumber")));
    }

    comp.cpuId = normalizeHardwareValue(
        runPowerShellCommand(QStringLiteral("(Get-CimInstance Win32_Processor).ProcessorId")));
    if (comp.cpuId.isEmpty()) {
        comp.cpuId = normalizeHardwareValue(runWmicCommand(QStringLiteral("cpu"),
                                                            QStringLiteral("processorid")));
    }

    comp.diskSerial = normalizeHardwareValue(
        runPowerShellCommand(QStringLiteral("(Get-CimInstance Win32_DiskDrive).SerialNumber")));
    if (comp.diskSerial.isEmpty()) {
        comp.diskSerial = normalizeHardwareValue(runWmicCommand(QStringLiteral("diskdrive"),
                                                                 QStringLiteral("serialnumber")));
    }

    comp.machineGuid = normalizeHardwareValue(readMachineGuid());
#endif
    return comp;
}

} // anonymous namespace

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

QString LicenseManager::deviceFingerprint()
{
    if (!g_deviceFingerprintCache.isEmpty()) {
        return g_deviceFingerprintCache;
    }

#if defined(Q_OS_WIN)
    const HardwareComponents comp = collectWindowsComponents();
    const QString full = comp.fullHash();
    if (!full.isEmpty()) {
        g_deviceFingerprintCache = full;
        return g_deviceFingerprintCache;
    }
#endif

    // Fallback to Qt's machine unique id (may be empty on some platforms).
    const QByteArray id = QSysInfo::machineUniqueId();
    if (!id.isEmpty()) {
        g_deviceFingerprintCache = bytesToHex(sha256(id));
        return g_deviceFingerprintCache;
    }
    return QString();
}

void LicenseManager::clearDeviceFingerprintCache()
{
    g_deviceFingerprintCache.clear();
}

bool LicenseManager::applyLicenseFile(const QByteArray& licenseData,
                                      const QString& deviceFingerprint,
                                      LicenseInfo* info,
                                      QString* errorMessage)
{
    auto fail = [&](const QString& message) {
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    };

    const QJsonDocument doc = QJsonDocument::fromJson(licenseData);
    if (!doc.isObject()) {
        return fail(QStringLiteral("授权文件格式无效"));
    }

    const QJsonObject obj = doc.object();
    const QString signatureBase64 = obj.value(QStringLiteral("signature")).toString();
    if (signatureBase64.isEmpty()) {
        return fail(QStringLiteral("授权文件缺少签名"));
    }

    // Build canonical payload (JSON object without signature, compact).
    QJsonObject payloadObj = obj;
    payloadObj.remove(QStringLiteral("signature"));
    const QByteArray payloadJson = QJsonDocument(payloadObj).toJson(QJsonDocument::Compact);

    // Verify Ed25519 signature.
    const QByteArray signature = base64UrlDecodeBytes(signatureBase64);
    if (signature.size() != 64) {
        return fail(QStringLiteral("授权签名格式无效"));
    }
    if (!verifyEd25519Signature(payloadJson, signature)) {
        return fail(QStringLiteral("授权签名验证失败"));
    }

    // Check device binding.
    const QString storedFullHash = obj.value(QStringLiteral("device_hash")).toString();
    if (!storedFullHash.isEmpty() && !checkDeviceHash(obj, deviceFingerprint)) {
        return fail(QStringLiteral("授权文件与当前设备不匹配"));
    }

    // Check expiry.
    const qint64 expiresAt = static_cast<qint64>(obj.value(QStringLiteral("expires_at")).toDouble());
    if (expiresAt > 0) {
        const QDateTime expiry = QDateTime::fromSecsSinceEpoch(expiresAt);
        if (expiry <= QDateTime::currentDateTimeUtc()) {
            return fail(QStringLiteral("授权已过期"));
        }
    }

    if (info) {
        info->keyId = obj.value(QStringLiteral("key_id")).toString();
        info->plan = planFromString(obj.value(QStringLiteral("plan")).toString());
        info->deviceHash = storedFullHash;
        info->issuedAt = QDateTime::fromSecsSinceEpoch(
            static_cast<qint64>(obj.value(QStringLiteral("issued_at")).toDouble()));
        info->expiresAt = expiresAt > 0
            ? QDateTime::fromSecsSinceEpoch(expiresAt)
            : QDateTime();
        info->rawPayload = payloadJson;
    }

    // Persist the validated license.
    QSettings settings;
    settings.setValue(kTokenKey, QString::fromUtf8(licenseData));
    settings.setValue(kOfflineTokenKey, QString::fromUtf8(licenseData));
    setState(LicenseState::Activated);
    return true;
}

void LicenseManager::activate(const QString& key, const QString& deviceFingerprint)
{
    QJsonObject body;
    body[QStringLiteral("key")] = key;
    body[QStringLiteral("device_hash")] = deviceFingerprint;
    body[QStringLiteral("device_name")] = QSysInfo::machineHostName();

#if defined(Q_OS_WIN)
    const HardwareComponents comp = collectWindowsComponents();
    QJsonArray components;
    for (const QString& hash : comp.componentHashes()) {
        components.append(hash);
    }
    body[QStringLiteral("device_components")] = components;
#endif

    QNetworkRequest request{QUrl(QStringLiteral("https://api.xlsone.com/api/activate/windows"))};
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QVariant(QStringLiteral("application/json")));
    request.setRawHeader("Accept", "application/json");

    QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    auto* reply = networkManager_->post(request, payload);

    auto* timer = new QTimer(reply);
    timer->setSingleShot(true);
    reply->setProperty("_timer", QVariant::fromValue(timer));
    connect(timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    timer->start(10000);
}

bool LicenseManager::importOfflineLicenseFile(const QString& path,
                                              const QString& deviceFingerprint,
                                              QString* errorMessage)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法读取授权文件");
        }
        return false;
    }

    const QByteArray data = file.readAll();
    file.close();

    return applyLicenseFile(data, deviceFingerprint, nullptr, errorMessage);
}

bool LicenseManager::verifyEd25519Signature(const QByteArray& message,
                                            const QByteArray& signature) const
{
    if (signature.size() != 64) {
        return false;
    }
    return crypto_ed25519_check(
        reinterpret_cast<const uint8_t*>(signature.constData()),
        kLicensePublicKey,
        reinterpret_cast<const uint8_t*>(message.constData()),
        static_cast<size_t>(message.size())) == 0;
}

bool LicenseManager::checkDeviceHash(const QJsonObject& licenseObj,
                                     const QString& actualFingerprint) const
{
    const QString expectedHash = licenseObj.value(QStringLiteral("device_hash")).toString();
    if (expectedHash.isEmpty()) {
        return true;
    }

    // Exact match first.
    if (expectedHash == actualFingerprint) {
        return true;
    }

    // Partial matching: allow hardware changes if enough components still match.
    const QJsonArray storedComponentsArray = licenseObj.value(QStringLiteral("device_components")).toArray();
    if (storedComponentsArray.isEmpty()) {
        return false;
    }

    QSet<QString> storedHashes;
    for (const QJsonValue& value : storedComponentsArray) {
        const QString hash = value.toString();
        if (!hash.isEmpty()) {
            storedHashes.insert(hash);
        }
    }
    if (storedHashes.isEmpty()) {
        return false;
    }

    const int storedCount = storedHashes.size();
    const int threshold = std::max(2, (storedCount * 2 + 2) / 3); // ceil(2/3)

#if defined(Q_OS_WIN)
    const HardwareComponents comp = collectWindowsComponents();
    const QStringList currentHashes = comp.componentHashes();
#else
    const QStringList currentHashes{ actualFingerprint };
#endif

    int matchCount = 0;
    for (const QString& hash : currentHashes) {
        if (storedHashes.contains(hash)) {
            ++matchCount;
        }
    }

    return matchCount >= threshold;
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
        result.errorMessage = QStringLiteral("网络连接失败，请检查网络");
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
                result.errorMessage = QStringLiteral("激活码不存在");
            else if (error == QStringLiteral("KEY_REVOKED"))
                result.errorMessage = QStringLiteral("激活码已被吊销");
            else if (error == QStringLiteral("DEVICE_LIMIT"))
                result.errorMessage = QStringLiteral("已达到最大设备数限制");
            else if (error == QStringLiteral("DEVICE_MISMATCH"))
                result.errorMessage = QStringLiteral("授权文件与当前设备不匹配");
            else if (error == QStringLiteral("RATE_LIMITED"))
                result.errorMessage = QStringLiteral("请求过于频繁，请稍后再试");
            else
                result.errorMessage = obj.value(QStringLiteral("message")).toString(
                    QStringLiteral("激活失败"));
        } else {
            result.errorMessage = QStringLiteral("激活失败: HTTP %1").arg(statusCode);
        }
        emit activationFinished(result);
        return;
    }

    const QJsonObject obj = doc.object();
    const QJsonObject licenseObj = obj.value(QStringLiteral("license")).toObject();
    if (licenseObj.isEmpty()) {
        result.success = false;
        result.errorMessage = QStringLiteral("服务器响应异常");
        emit activationFinished(result);
        return;
    }

    const QByteArray licenseData = QJsonDocument(licenseObj).toJson(QJsonDocument::Compact);
    const QString fingerprint = deviceFingerprint();
    QString verifyError;
    if (!applyLicenseFile(licenseData, fingerprint, nullptr, &verifyError)) {
        result.success = false;
        result.errorMessage = verifyError;
        emit activationFinished(result);
        return;
    }

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

    // Check for existing signed license file
    const QString licenseText = settings.value(kTokenKey).toString();
    if (!licenseText.isEmpty()) {
        const QByteArray licenseData = licenseText.toUtf8();
        const QString fingerprint = deviceFingerprint();
        LicenseInfo info;
        if (applyLicenseFile(licenseData, fingerprint, &info, nullptr)) {
            setState(LicenseState::Activated);
            return;
        }
        // Invalid or expired stored license: clear it.
        settings.remove(kTokenKey);
        settings.remove(kOfflineTokenKey);
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
