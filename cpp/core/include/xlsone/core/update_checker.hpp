#pragma once

#include <QMetaType>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace xlsone {

struct UpdateInfo {
    QString latestVersion;
    QString changelog;
    QString downloadUrl;
};

class UpdateChecker final : public QObject {
    Q_OBJECT

public:
    explicit UpdateChecker(QObject* parent = nullptr);
    ~UpdateChecker() override;

    QString currentVersion() const;
    void checkForUpdates(const QString& apiUrl);

    static int compareVersions(const QString& lhs, const QString& rhs);
    static UpdateInfo parseUpdateInfo(const QByteArray& json);
    static QString platformKey();
    static QString architectureKey();

signals:
    void updateAvailable(const UpdateInfo& info);
    void noUpdateAvailable();
    void checkError(const QString& message);

private slots:
    void onReplyFinished(QNetworkReply* reply);

private:
    QNetworkAccessManager* networkManager_ = nullptr;
};

} // namespace xlsone

Q_DECLARE_METATYPE(xlsone::UpdateInfo)
