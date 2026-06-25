#include "license_activation_dialog.hpp"
#include "dialog_utils.hpp"
#include "xlsone/core/license_manager.hpp"

#include <QDesktopServices>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSysInfo>
#include <QUrl>
#include <QVBoxLayout>

LicenseActivationDialog::LicenseActivationDialog(xlsone::LicenseManager* mgr,
                                                 QWidget* parent)
    : QDialog(parent)
    , licenseManager_(mgr)
{
    buildUi();

    connect(licenseManager_, &xlsone::LicenseManager::activationFinished,
            this, &LicenseActivationDialog::onActivationFinished);
}

void LicenseActivationDialog::buildUi()
{
    setWindowTitle(QStringLiteral("激活 表表归一"));
    setFixedSize(420, 580);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto* root = new QVBoxLayout(this);
    root->setSpacing(0);
    root->setContentsMargins(48, 32, 48, 20);

    // Icon
    auto* iconLabel = new QLabel(QStringLiteral("\xF0\x9F\x94\x91"));
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setStyleSheet("font-size: 40px;");
    root->addWidget(iconLabel);

    root->addSpacing(12);

    // Title
    auto* titleLabel = new QLabel(QStringLiteral("激活 表表归一"));
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet("font-size: 18px; font-weight: 600;");
    root->addWidget(titleLabel);

    root->addSpacing(4);

    // Subtitle (dynamic)
    auto* subtitleLabel = new QLabel(
        licenseManager_->state() == xlsone::LicenseState::Expired
            ? QStringLiteral("您的许可证已过期，请续费获取新的激活码")
            : QStringLiteral("输入激活码以解锁全部功能"));
    subtitleLabel->setAlignment(Qt::AlignCenter);
    subtitleLabel->setWordWrap(true);
    subtitleLabel->setStyleSheet("color: #8e8e93; font-size: 13px;");
    root->addWidget(subtitleLabel);

    root->addSpacing(24);

    // Key label
    auto* keyLabel = new QLabel(QStringLiteral("激活码"));
    keyLabel->setStyleSheet("font-size: 13px; font-weight: 500;");
    root->addWidget(keyLabel);

    root->addSpacing(8);

    // Key input
    keyInput_ = new QLineEdit;
    keyInput_->setPlaceholderText(QStringLiteral("XXXX-XXXX-XXXX"));
    keyInput_->setMaxLength(14);
    keyInput_->setAlignment(Qt::AlignCenter);
    keyInput_->setStyleSheet(
        "QLineEdit {"
        "  font-size: 15px; font-family: monospace;"
        "  padding: 12px; border: 1px solid #ccc; border-radius: 8px;"
        "  background: %1;"
        "}"
        "QLineEdit:focus { border-color: #007AFF; }");
    root->addWidget(keyInput_);

    // Auto-format
    connect(keyInput_, &QLineEdit::textChanged, this, [this](const QString& text) {
        QString cleaned = text.toUpper().remove('-');
        if (cleaned.length() > 12) cleaned = cleaned.left(12);

        QString formatted;
        for (int i = 0; i < cleaned.length(); ++i) {
            if (i > 0 && i % 4 == 0) formatted += '-';
            formatted += cleaned[i];
        }

        if (formatted != text) {
            keyInput_->blockSignals(true);
            keyInput_->setText(formatted);
            keyInput_->blockSignals(false);
        }

        activateButton_->setEnabled(formatted.length() == 14);
    });

    root->addSpacing(8);

    // Error
    errorLabel_ = new QLabel;
    errorLabel_->setStyleSheet("color: #FF3B30; font-size: 12px; padding-left: 2px;");
    errorLabel_->setWordWrap(true);
    errorLabel_->hide();
    root->addWidget(errorLabel_);

    root->addSpacing(20);

    // Activate button
    activateButton_ = new QPushButton(QStringLiteral("激活"));
    activateButton_->setEnabled(false);
    activateButton_->setCursor(Qt::PointingHandCursor);
    activateButton_->setMinimumHeight(44);
    activateButton_->setStyleSheet(
        "QPushButton {"
        "  font-size: 15px; font-weight: 600; color: white;"
        "  background-color: #007AFF; border: none; border-radius: 8px;"
        "  padding: 12px 0;"
        "}"
        "QPushButton:disabled { background-color: #d1d1d6; color: #98989d; }"
        "QPushButton:hover:!disabled { background-color: #0066DD; }");
    connect(activateButton_, &QPushButton::clicked,
            this, &LicenseActivationDialog::onActivateClicked);
    root->addWidget(activateButton_);

    root->addSpacing(12);

    // Trial + Purchase
    auto* linkRow = new QHBoxLayout;
    linkRow->setSpacing(24);

    trialButton_ = new QPushButton(QStringLiteral("免费试用 14 天"));
    trialButton_->setFlat(true);
    trialButton_->setCursor(Qt::PointingHandCursor);
    trialButton_->setStyleSheet(
        "QPushButton { color: #007AFF; font-size: 13px; border: none; background: transparent; }"
        "QPushButton:hover { text-decoration: underline; }");
    connect(trialButton_, &QPushButton::clicked,
            this, &LicenseActivationDialog::onTrialClicked);
    linkRow->addWidget(trialButton_);

    auto* buyButton = new QPushButton(QStringLiteral("购买激活码 →"));
    buyButton->setFlat(true);
    buyButton->setCursor(Qt::PointingHandCursor);
    buyButton->setStyleSheet(
        "QPushButton { color: #007AFF; font-size: 13px; border: none; background: transparent; }"
        "QPushButton:hover { text-decoration: underline; }");
    linkRow->addWidget(buyButton);
    connect(buyButton, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://z-pulse.cn")));
    });
    linkRow->addStretch();
    root->addLayout(linkRow);

    root->addSpacing(12);

    // Offline activation
    offlineToggle_ = new QPushButton(QStringLiteral("\xE2\x9C\x93 离线激活"));
    offlineToggle_->setFlat(true);
    offlineToggle_->setCursor(Qt::PointingHandCursor);
    offlineToggle_->setStyleSheet(
        "QPushButton { color: #8e8e93; font-size: 11px; border: none; background: transparent; }"
        "QPushButton:hover { text-decoration: underline; }");
    connect(offlineToggle_, &QPushButton::clicked, this, [this] {
        showOffline_ = !showOffline_;
        offlineInfo_->setVisible(showOffline_);
    });
    root->addWidget(offlineToggle_);

    // Offline info (hidden initially)
    offlineInfo_ = new QWidget;
    offlineInfo_->setStyleSheet("background: %1; border-radius: 8px; padding: 12px;");
    auto* offlineLayout = new QVBoxLayout(offlineInfo_);
    offlineLayout->setSpacing(4);
    offlineLayout->setContentsMargins(12, 12, 12, 12);

    auto* offlineTitle = new QLabel(QStringLiteral("离线激活方式："));
    offlineTitle->setStyleSheet("font-size: 11px; font-weight: 500;");
    offlineLayout->addWidget(offlineTitle);

    auto* step1 = new QLabel(QStringLiteral("1. 在联网电脑上访问 z-pulse.cn/offline"));
    step1->setStyleSheet("color: #8e8e93; font-size: 11px;");
    offlineLayout->addWidget(step1);

    auto* step2 = new QLabel(QStringLiteral("2. 输入购买邮箱和本机设备码"));
    step2->setStyleSheet("color: #8e8e93; font-size: 11px;");
    offlineLayout->addWidget(step2);

    auto* step3 = new QLabel(QStringLiteral("3. 下载授权文件并导入本程序"));
    step3->setStyleSheet("color: #8e8e93; font-size: 11px;");
    offlineLayout->addWidget(step3);

    auto* importButton = new QPushButton(QStringLiteral("导入授权文件..."));
    importButton->setCursor(Qt::PointingHandCursor);
    importButton->setMinimumHeight(32);
    importButton->setStyleSheet(
        "QPushButton {"
        "  color: #1d1d1f; background: white; border: 1px solid #d1d1d6;"
        "  border-radius: 6px; font-size: 12px;"
        "}"
        "QPushButton:hover { background: #f5f5f7; }");
    connect(importButton, &QPushButton::clicked,
            this, &LicenseActivationDialog::onImportLicenseClicked);
    offlineLayout->addSpacing(6);
    offlineLayout->addWidget(importButton);

    offlineInfo_->hide();
    root->addWidget(offlineInfo_);

    root->addStretch();
}

void LicenseActivationDialog::onActivateClicked()
{
    const QString key = keyInput_->text().remove('-').toUpper();
    if (key.length() != 12) return;

    activateButton_->setEnabled(false);
    activateButton_->setText(QStringLiteral("验证中..."));
    errorLabel_->hide();

    const QString deviceId = QSysInfo::machineUniqueId();
    licenseManager_->activate(key, deviceId);
}

void LicenseActivationDialog::onImportLicenseClicked()
{
    const QString path = xlsone::ui::getOpenFileNameCentered(
        this,
        tr("导入授权文件"),
        QString(),
        tr("授权文件 (*.license *.txt);;所有文件 (*)")
    );
    if (path.isEmpty()) {
        return;
    }

    QString errorMessage;
    const QString deviceId = QSysInfo::machineUniqueId();
    if (!licenseManager_->importOfflineLicenseFile(path, deviceId, &errorMessage)) {
        xlsone::ui::showWarning(this, tr("导入失败"), errorMessage);
        return;
    }

    xlsone::ui::showInformation(this, tr("导入成功"), tr("授权文件已导入"));
    accept();
}

void LicenseActivationDialog::onTrialClicked()
{
    licenseManager_->startTrial();
    accept();
}

void LicenseActivationDialog::onActivationFinished(const xlsone::ActivationResult& result)
{
    activateButton_->setEnabled(true);
    activateButton_->setText(QStringLiteral("激活"));

    if (result.success) {
        accept();
    } else {
        errorLabel_->setText(QStringLiteral("⚠ ") + result.errorMessage);
        errorLabel_->show();
    }
}
