#include "check_update_dialog.hpp"

#include "ui_theme.hpp"
#include "xlsone/core/update_checker.hpp"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTextEdit>
#include <QUrl>
#include <QVBoxLayout>

CheckUpdateDialog::CheckUpdateDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("检查更新"));
    setMinimumWidth(420);
    setModal(true);

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(20, 20, 20, 16);
    rootLayout->setSpacing(14);

    titleLabel_ = new QLabel(this);
    titleLabel_->setWordWrap(true);
    QFont titleFont = titleLabel_->font();
    titleFont.setPointSize(14);
    titleFont.setBold(true);
    titleLabel_->setFont(titleFont);
    rootLayout->addWidget(titleLabel_);

    contentWidget_ = new QWidget(this);
    auto* contentLayout = new QVBoxLayout(contentWidget_);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(10);
    contentWidget_->setLayout(contentLayout);
    rootLayout->addWidget(contentWidget_, 1);

    buttonWidget_ = new QWidget(this);
    buttonWidget_->setLayout(new QHBoxLayout(buttonWidget_));
    buttonWidget_->layout()->setContentsMargins(0, 0, 0, 0);
    buttonWidget_->layout()->setSpacing(10);
    static_cast<QHBoxLayout*>(buttonWidget_->layout())->addStretch(1);
    rootLayout->addWidget(buttonWidget_);

    startChecking();
}

static QVBoxLayout* boxLayoutOf(QWidget* widget)
{
    return qobject_cast<QVBoxLayout*>(widget->layout());
}

void CheckUpdateDialog::clearContent()
{
    QVBoxLayout* layout = boxLayoutOf(contentWidget_);
    if (layout == nullptr) {
        return;
    }
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
}

void CheckUpdateDialog::setButtons(QWidget* primary, QWidget* secondary)
{
    QLayout* layout = buttonWidget_->layout();
    if (layout == nullptr) {
        return;
    }
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
    static_cast<QHBoxLayout*>(layout)->addStretch(1);
    if (secondary != nullptr) {
        layout->addWidget(secondary);
    }
    if (primary != nullptr) {
        layout->addWidget(primary);
    }
}

void CheckUpdateDialog::setTitle(const QString& text)
{
    titleLabel_->setText(text);
}

void CheckUpdateDialog::startChecking()
{
    clearContent();
    setTitle(tr("正在检查更新..."));

    auto* progress = new QProgressBar(contentWidget_);
    progress->setRange(0, 0);
    progress->setTextVisible(false);
    progress->setFixedHeight(6);
    boxLayoutOf(contentWidget_)->addWidget(progress);

    auto* statusLabel = new QLabel(tr("请稍候，正在连接更新服务器..."), contentWidget_);
    statusLabel->setWordWrap(true);
    boxLayoutOf(contentWidget_)->addWidget(statusLabel);
    boxLayoutOf(contentWidget_)->addStretch(1);

    auto* cancelButton = new QPushButton(tr("取消"), this);
    xlsone::ui::stylePrimaryButton(cancelButton);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    setButtons(cancelButton);
}

void CheckUpdateDialog::showNoUpdate(const QString& currentVersion)
{
    clearContent();
    setTitle(tr("当前已是最新版本"));

    auto* infoLabel = new QLabel(
        tr("您当前使用的 %1 已经是最新版本，无需更新。").arg(currentVersion),
        contentWidget_);
    infoLabel->setWordWrap(true);
    boxLayoutOf(contentWidget_)->addWidget(infoLabel);
    boxLayoutOf(contentWidget_)->addStretch(1);

    auto* okButton = new QPushButton(tr("确定"), this);
    xlsone::ui::stylePrimaryButton(okButton);
    connect(okButton, &QPushButton::clicked, this, &QDialog::accept);
    setButtons(okButton);
}

void CheckUpdateDialog::showUpdateAvailable(const xlsone::UpdateInfo& info)
{
    clearContent();
    setTitle(tr("发现新版本 %1").arg(info.latestVersion));
    currentDownloadUrl_ = info.downloadUrl;

    auto* descLabel = new QLabel(tr("新版本已发布，建议您更新以获得更好的体验。"), contentWidget_);
    descLabel->setWordWrap(true);
    boxLayoutOf(contentWidget_)->addWidget(descLabel);

    if (!info.changelog.isEmpty()) {
        auto* changelogEdit = new QTextEdit(contentWidget_);
        changelogEdit->setReadOnly(true);
        changelogEdit->setPlainText(info.changelog);
        changelogEdit->setMinimumHeight(140);
        boxLayoutOf(contentWidget_)->addWidget(changelogEdit);
    }

    auto* downloadButton = new QPushButton(tr("立即下载"), this);
    xlsone::ui::stylePrimaryButton(downloadButton);
    connect(downloadButton, &QPushButton::clicked, this, [this] {
        if (!currentDownloadUrl_.isEmpty()) {
            QDesktopServices::openUrl(QUrl(currentDownloadUrl_));
        }
        emit downloadRequested(currentDownloadUrl_);
        accept();
    });

    auto* laterButton = new QPushButton(tr("稍后提醒"), this);
    xlsone::ui::stylePrimaryButton(laterButton);
    connect(laterButton, &QPushButton::clicked, this, &QDialog::reject);

    setButtons(downloadButton, laterButton);
}

void CheckUpdateDialog::showError(const QString& message)
{
    clearContent();
    setTitle(tr("检查更新失败"));

    auto* errorLabel = new QLabel(
        tr("无法连接到更新服务器，请检查网络后重试。\n\n错误信息：%1").arg(message),
        contentWidget_);
    errorLabel->setWordWrap(true);
    boxLayoutOf(contentWidget_)->addWidget(errorLabel);
    boxLayoutOf(contentWidget_)->addStretch(1);

    auto* retryButton = new QPushButton(tr("重试"), this);
    xlsone::ui::stylePrimaryButton(retryButton);
    connect(retryButton, &QPushButton::clicked, this, [this] {
        emit retryRequested();
        startChecking();
    });

    auto* closeButton = new QPushButton(tr("关闭"), this);
    xlsone::ui::stylePrimaryButton(closeButton);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);

    setButtons(retryButton, closeButton);
}
