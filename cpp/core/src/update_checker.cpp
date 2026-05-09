#include "xlsone/core/update_checker.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QVersionNumber>

namespace xlsone {

UpdateChecker::UpdateChecker(QObject* parent)
    : QObject(parent)
    , networkManager_(new QNetworkAccessManager(this))
{
    connect(networkManager_, &QNetworkAccessManager::finished,
            this, &UpdateChecker::onReplyFinished);
}

UpdateChecker::~UpdateChecker() = default;

QString UpdateChecker::currentVersion() const
{
    return QStringLiteral("%1.%2.%3")
        .arg(XLSONE_VERSION_MAJOR)
        .arg(XLSONE_VERSION_MINOR)
        .arg(XLSONE_VERSION_PATCH);
}

void UpdateChecker::checkForUpdates(const QString& apiUrl)
{
    QNetworkRequest request{QUrl(apiUrl)};
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));

    auto* reply = networkManager_->get(request);

    auto* timer = new QTimer(reply);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    timer->start(5000);

    ++pendingRequests_;
}

void UpdateChecker::onReplyFinished(QNetworkReply* reply)
{
    reply->deleteLater();
    --pendingRequests_;

    if (reply->error() != QNetworkReply::NoError) {
        return;
    }

    const int statusCode = reply->attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (statusCode != 200) {
        return;
    }

    const QByteArray body = reply->readAll();
    const UpdateInfo info = parseUpdateInfo(body);

    if (info.latestVersion.isEmpty() || info.downloadUrl.isEmpty()) {
        return;
    }

    if (compareVersions(info.latestVersion, currentVersion()) > 0) {
        emit updateAvailable(info);
    } else {
        emit noUpdateAvailable();
    }
}

int UpdateChecker::compareVersions(const QString& lhs, const QString& rhs)
{
    return QVersionNumber::compare(
        QVersionNumber::fromString(lhs),
        QVersionNumber::fromString(rhs));
}

UpdateInfo UpdateChecker::parseUpdateInfo(const QByteArray& json)
{
    UpdateInfo info;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject()) {
        return info;
    }

    const QJsonObject root = doc.object();
    info.latestVersion = root.value(QStringLiteral("latest_version")).toString();
    info.changelog = root.value(QStringLiteral("changelog")).toString();

    const QJsonObject downloads = root.value(QStringLiteral("downloads")).toObject();
    const QString key = platformKey();
    info.downloadUrl = downloads.value(key).toString();

    return info;
}

QString UpdateChecker::platformKey()
{
#if defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#elif defined(Q_OS_WIN)
    return QStringLiteral("windows");
#else
    return QStringLiteral("linux");
#endif
}

} // namespace xlsone
