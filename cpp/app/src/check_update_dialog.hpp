#pragma once

#include <QDialog>
#include <QString>

namespace xlsone { struct UpdateInfo; }

class QLabel;
class QPushButton;
class QTextEdit;
class QProgressBar;
class QWidget;

class CheckUpdateDialog final : public QDialog {
    Q_OBJECT

public:
    explicit CheckUpdateDialog(QWidget* parent = nullptr);

    void startChecking();
    void showNoUpdate(const QString& currentVersion);
    void showUpdateAvailable(const xlsone::UpdateInfo& info);
    void showError(const QString& message);

signals:
    void retryRequested();
    void downloadRequested(const QString& url);

private:
    void clearContent();
    void setButtons(QWidget* primary, QWidget* secondary = nullptr);
    void setTitle(const QString& text);

    QLabel* titleLabel_ = nullptr;
    QWidget* contentWidget_ = nullptr;
    QWidget* buttonWidget_ = nullptr;
    QString currentDownloadUrl_;
};
