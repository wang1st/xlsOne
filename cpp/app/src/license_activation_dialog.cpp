#include "license_activation_dialog.hpp"
#include "dialog_utils.hpp"
#include "ui_theme.hpp"
#include "xlsone/core/license_manager.hpp"

#include <QApplication>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSysInfo>
#include <QVBoxLayout>

namespace {

QLineEdit* makeKeyPartInput(QWidget* parent)
{
    auto* edit = new QLineEdit(parent);
    edit->setMaxLength(4);
    edit->setAlignment(Qt::AlignCenter);
    edit->setFixedSize(QSize(64, 48));
    edit->setAttribute(Qt::WA_MacShowFocusRect, false);
    return edit;
}

QString styleForKeyInput(const xlsone::ui::Theme& t, bool focused)
{
    return QStringLiteral(
        "QLineEdit {"
        " font-size: 15px; font-family: monospace;"
        " background: %1; color: %2;"
        " border: 1px solid %3; border-radius: 8px;"
        " padding: 0;"
        "}"
        "QLineEdit:focus { border: 1.5px solid %4; }"
    )
        .arg(t.surface.name())
        .arg(t.text.name())
        .arg(focused ? t.accent.name() : t.border.name())
        .arg(t.accent.name());
}

} // namespace

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
    setWindowTitle(tr("激活 表表归一"));
    setMinimumSize(680, 480);
    resize(720, 520);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    const auto& t = xlsone::ui::theme();

    auto* root = new QHBoxLayout(this);
    root->setSpacing(0);
    root->setContentsMargins(0, 0, 0, 0);

    // Brand panel
    auto* brandPanel = new QWidget(this);
    brandPanel->setFixedWidth(280);
    buildBrandPanel(brandPanel);
    root->addWidget(brandPanel);

    // Separator
    auto* separator = new QFrame(this);
    separator->setFrameShape(QFrame::VLine);
    separator->setStyleSheet(QStringLiteral("color: %1").arg(t.border.name()));
    root->addWidget(separator);

    // Form panel
    auto* formPanel = new QWidget(this);
    buildFormPanel(formPanel);
    root->addWidget(formPanel, 1);

    setStyleSheet(QStringLiteral(
        "LicenseActivationDialog { background: %1; }"
    ).arg(t.bg0.name()));
}

void LicenseActivationDialog::buildBrandPanel(QWidget* container)
{
    const auto& t = xlsone::ui::theme();

    auto* layout = new QVBoxLayout(container);
    layout->setSpacing(16);
    layout->setContentsMargins(32, 32, 32, 32);
    container->setStyleSheet(QStringLiteral("background: %1").arg(t.accentSoft.name()));

    layout->addStretch();

    // Icon
    auto* iconLabel = new QLabel(container);
    iconLabel->setFixedSize(56, 56);
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setStyleSheet(QStringLiteral(
        "background: %1; border-radius: 14px; color: white; font-size: 28px; font-weight: 600;"
    ).arg(t.accent.name()));
    iconLabel->setText(QStringLiteral("表"));
    iconLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(iconLabel, 0, Qt::AlignCenter);

    // Title
    auto* titleLabel = new QLabel(tr("表表归一"), container);
    titleLabel->setAlignment(Qt::AlignCenter);
    QFont titleFont = font();
    titleFont.setPointSize(18);
    titleFont.setWeight(QFont::DemiBold);
    titleLabel->setFont(titleFont);
    titleLabel->setStyleSheet(QStringLiteral("color: %1").arg(t.text.name()));
    layout->addWidget(titleLabel);

    // Subtitle
    auto* subtitleLabel = new QLabel(tr("多单位 Excel 报表一键汇总"), container);
    subtitleLabel->setAlignment(Qt::AlignCenter);
    subtitleLabel->setWordWrap(true);
    subtitleLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 13px;").arg(t.textMuted.name()));
    layout->addWidget(subtitleLabel);

    layout->addStretch();

    // Feature pills
    auto* pillLayout = new QHBoxLayout();
    pillLayout->setSpacing(8);
    auto makePill = [&](const QString& text) {
        auto* label = new QLabel(text, container);
        label->setStyleSheet(QStringLiteral(
            "background: %1; color: %2; border-radius: 12px; padding: 4px 10px; font-size: 11px;"
        ).arg(t.surface.name()).arg(t.textMuted.name()));
        return label;
    };
    pillLayout->addWidget(makePill(tr("快速")));
    pillLayout->addWidget(makePill(tr("安全")));
    pillLayout->addWidget(makePill(tr("原生")));
    pillLayout->addStretch();
    layout->addLayout(pillLayout);
}

void LicenseActivationDialog::buildFormPanel(QWidget* container)
{
    const auto& t = xlsone::ui::theme();

    auto* root = new QVBoxLayout(container);
    root->setSpacing(0);
    root->setContentsMargins(36, 32, 36, 24);
    container->setStyleSheet(QStringLiteral("background: %1").arg(t.bg0.name()));

    // Header
    auto* headerTitle = new QLabel(tr("激活 表表归一"), container);
    QFont headerFont = font();
    headerFont.setPointSize(17);
    headerFont.setWeight(QFont::DemiBold);
    headerTitle->setFont(headerFont);
    headerTitle->setStyleSheet(QStringLiteral("color: %1").arg(t.text.name()));
    root->addWidget(headerTitle);

    auto* headerSubtitle = new QLabel(
        licenseManager_->state() == xlsone::LicenseState::Expired
            ? tr("您的许可证已过期，请续费获取新的激活码")
            : tr("输入激活码以解锁全部功能"),
        container);
    headerSubtitle->setWordWrap(true);
    headerSubtitle->setStyleSheet(QStringLiteral("color: %1; font-size: 13px; margin-top: 4px;").arg(t.textMuted.name()));
    root->addWidget(headerSubtitle);

    root->addSpacing(28);

    // Key input
    auto* keyLabel = new QLabel(tr("激活码"), container);
    keyLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 13px; font-weight: 500;").arg(t.text.name()));
    root->addWidget(keyLabel);

    root->addSpacing(8);

    auto* keyRow = new QHBoxLayout();
    keyRow->setSpacing(8);
    keyRow->addStretch();
    for (int i = 0; i < 4; ++i) {
        auto* edit = makeKeyPartInput(container);
        edit->setStyleSheet(styleForKeyInput(t, false));
        connect(edit, &QLineEdit::textChanged, this, [this, i] { onKeyPartChanged(i); });
        connect(edit, &QLineEdit::textEdited, this, [this, i] { onKeyPartChanged(i); });
        keyParts_.append(edit);
        keyRow->addWidget(edit);
        if (i < 3) {
            auto* dash = new QLabel(QStringLiteral("-"), container);
            dash->setStyleSheet(QStringLiteral("color: %1; font-size: 15px; font-family: monospace;").arg(t.textMuted.name()));
            keyRow->addWidget(dash);
        }
    }
    keyRow->addStretch();
    root->addLayout(keyRow);

    root->addSpacing(8);

    // Message area (fixed height)
    messageLabel_ = new QLabel(container);
    messageLabel_->setMinimumHeight(22);
    messageLabel_->setWordWrap(true);
    messageLabel_->setStyleSheet(QStringLiteral("font-size: 12px; padding-left: 2px;"));
    messageLabel_->hide();
    root->addWidget(messageLabel_);

    root->addSpacing(20);

    // Activate button
    activateButton_ = new QPushButton(tr("激活"), container);
    activateButton_->setEnabled(false);
    activateButton_->setCursor(Qt::PointingHandCursor);
    activateButton_->setMinimumHeight(44);
    activateButton_->setStyleSheet(QStringLiteral(
        "QPushButton {"
        " font-size: 15px; font-weight: 600; color: white;"
        " background-color: %1; border: none; border-radius: 8px;"
        " padding: 12px 0;"
        "}"
        "QPushButton:disabled { background-color: %2; color: %3; }"
        "QPushButton:hover:!disabled { background-color: %4; }"
    )
        .arg(t.accent.name())
        .arg(t.isDark ? QStringLiteral("#2c2e36") : QStringLiteral("#e2e6ef"))
        .arg(t.textDisabled.name())
        .arg(t.isDark ? QStringLiteral("#1e5fd6") : QStringLiteral("#2368e8")));
    connect(activateButton_, &QPushButton::clicked,
            this, &LicenseActivationDialog::onActivateClicked);
    root->addWidget(activateButton_);

    root->addSpacing(12);

    // Trial + Purchase
    auto* linkRow = new QHBoxLayout;
    linkRow->setSpacing(16);

    trialButton_ = new QPushButton(tr("免费试用 14 天"), container);
    trialButton_->setFlat(true);
    trialButton_->setCursor(Qt::PointingHandCursor);
    trialButton_->setStyleSheet(QStringLiteral(
        "QPushButton { color: %1; font-size: 13px; border: none; background: transparent; }"
        "QPushButton:hover { text-decoration: underline; }"
    ).arg(t.accent.name()));
    connect(trialButton_, &QPushButton::clicked,
            this, &LicenseActivationDialog::onTrialClicked);
    linkRow->addWidget(trialButton_);

    linkRow->addStretch();
    root->addLayout(linkRow);

    root->addSpacing(16);

    // Offline activation
    offlineToggle_ = new QPushButton(tr("离线激活"), container);
    offlineToggle_->setFlat(true);
    offlineToggle_->setCursor(Qt::PointingHandCursor);
    offlineToggle_->setStyleSheet(QStringLiteral(
        "QPushButton { color: %1; font-size: 12px; border: none; background: transparent; }"
        "QPushButton:hover { text-decoration: underline; }"
    ).arg(t.textMuted.name()));
    connect(offlineToggle_, &QPushButton::clicked,
            this, &LicenseActivationDialog::toggleOfflineInfo);
    root->addWidget(offlineToggle_, 0, Qt::AlignLeft);

    // Offline info (hidden initially)
    offlineInfo_ = new QWidget(container);
    auto* offlineLayout = new QVBoxLayout(offlineInfo_);
    offlineLayout->setSpacing(8);
    offlineLayout->setContentsMargins(12, 12, 12, 12);
    offlineInfo_->setStyleSheet(QStringLiteral(
        "background: %1; border-radius: 8px;"
    ).arg(t.surface.name()));

    auto addStep = [&](int number, const QString& text) {
        auto* row = new QHBoxLayout();
        row->setSpacing(8);
        auto* badge = new QLabel(QString::number(number), offlineInfo_);
        badge->setFixedSize(20, 20);
        badge->setAlignment(Qt::AlignCenter);
        badge->setStyleSheet(QStringLiteral(
            "background: %1; color: white; border-radius: 10px; font-size: 11px; font-weight: 600;"
        ).arg(t.accent.name()));
        row->addWidget(badge);
        auto* label = new QLabel(text, offlineInfo_);
        label->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;").arg(t.textMuted.name()));
        label->setWordWrap(true);
        row->addWidget(label, 1);
        offlineLayout->addLayout(row);
    };

    addStep(1, tr("联系 831261@qq.com 获取离线授权页面地址"));
    addStep(2, tr("输入购买邮箱和本机设备码"));
    addStep(3, tr("下载授权文件并导入本程序"));

    auto* importButton = new QPushButton(tr("导入授权文件..."), offlineInfo_);
    importButton->setCursor(Qt::PointingHandCursor);
    importButton->setMinimumHeight(34);
    importButton->setStyleSheet(QStringLiteral(
        "QPushButton {"
        " color: %1; background: %2; border: 1px solid %3;"
        " border-radius: 6px; font-size: 12px;"
        "}"
        "QPushButton:hover { background: %4; }"
    )
        .arg(t.text.name())
        .arg(t.surface.name())
        .arg(t.border.name())
        .arg(t.bg2.name()));
    connect(importButton, &QPushButton::clicked,
            this, &LicenseActivationDialog::onImportLicenseClicked);
    offlineLayout->addSpacing(4);
    offlineLayout->addWidget(importButton);

    offlineInfo_->hide();
    root->addWidget(offlineInfo_);

    root->addStretch();
}

void LicenseActivationDialog::onKeyPartChanged(int index)
{
    setMessage(QString(), false);

    QLineEdit* edit = keyParts_[index];
    QString text = edit->text().toUpper();
    QString cleaned;
    for (const QChar& c : text) {
        if (c.isLetterOrNumber()) {
            cleaned.append(c);
        }
    }

    if (cleaned.length() > 4) {
        QString overflow = cleaned.mid(4);
        cleaned = cleaned.left(4);

        // Distribute overflow to next fields
        int current = index + 1;
        while (!overflow.isEmpty() && current < 4) {
            QString part = overflow.left(4 - keyParts_[current]->text().length());
            keyParts_[current]->setText(keyParts_[current]->text() + part);
            overflow.remove(0, part.length());
            ++current;
        }
    }

    if (cleaned != edit->text()) {
        edit->setText(cleaned);
    }

    updateActivateButton();

    if (cleaned.length() == 4 && index < 3) {
        focusNextPart(index);
    }
}

void LicenseActivationDialog::onKeyPartBackspace(int index)
{
    if (keyParts_[index]->text().isEmpty() && index > 0) {
        focusPreviousPart(index);
    }
}

void LicenseActivationDialog::focusNextPart(int fromIndex)
{
    if (fromIndex + 1 < keyParts_.size()) {
        keyParts_[fromIndex + 1]->setFocus();
        keyParts_[fromIndex + 1]->selectAll();
    }
}

void LicenseActivationDialog::focusPreviousPart(int fromIndex)
{
    if (fromIndex > 0) {
        keyParts_[fromIndex - 1]->setFocus();
        keyParts_[fromIndex - 1]->setCursorPosition(keyParts_[fromIndex - 1]->text().length());
    }
}

void LicenseActivationDialog::updateActivateButton()
{
    bool complete = true;
    for (const auto* edit : keyParts_) {
        if (edit->text().length() != 4) {
            complete = false;
            break;
        }
    }
    activateButton_->setEnabled(complete);
}

void LicenseActivationDialog::setMessage(const QString& message, bool error)
{
    const auto& t = xlsone::ui::theme();
    if (message.isEmpty()) {
        messageLabel_->hide();
        messageLabel_->clear();
        return;
    }
    messageLabel_->setText(message);
    messageLabel_->setStyleSheet(QStringLiteral(
        "font-size: 12px; padding-left: 2px; color: %1;"
    ).arg(error ? t.error.name() : t.success.name()));
    messageLabel_->show();
}

void LicenseActivationDialog::toggleOfflineInfo()
{
    showOffline_ = !showOffline_;
    offlineInfo_->setVisible(showOffline_);
    offlineToggle_->setText(showOffline_ ? tr("隐藏离线激活") : tr("离线激活"));
}

void LicenseActivationDialog::onActivateClicked()
{
    QString key;
    for (const auto* edit : keyParts_) {
        key += edit->text();
    }
    if (key.length() != 16) return;

    activateButton_->setEnabled(false);
    activateButton_->setText(tr("验证中..."));
    setMessage(QString(), false);

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
        setMessage(errorMessage, true);
        return;
    }

    setMessage(tr("授权文件已导入"), false);
    accept();
}

void LicenseActivationDialog::onTrialClicked()
{
    licenseManager_->startTrial();
    accept();
}

void LicenseActivationDialog::onActivationFinished(const xlsone::ActivationResult& result)
{
    updateActivateButton();
    activateButton_->setText(tr("激活"));

    if (result.success) {
        accept();
    } else {
        setMessage(result.errorMessage, true);
    }
}
