#include "workspace_chrome.hpp"

#include "symbol_icons.hpp"
#include "ui_theme.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QDateTime>

namespace {

QPushButton* makeUtilityButton(QWidget* parent, const QString& text, xlsone::ui::SymbolIcon icon, const QColor& iconColor)
{
    auto* button = new QPushButton(text, parent);
    button->setIcon(xlsone::ui::makeSymbolIcon(icon, iconColor));
    button->setIconSize(QSize(16, 16));
    button->setCursor(Qt::PointingHandCursor);
    button->setObjectName(QStringLiteral("workspaceUtilityButton"));
    button->setToolTip(text);
    return button;
}

} // namespace

WorkspaceChrome::WorkspaceChrome(xlsone::LicenseManager* licenseManager,
                                 QWidget* parent)
    : QWidget(parent)
    , licenseManager_(licenseManager)
{
    buildUi();

    if (licenseManager_ != nullptr) {
        connect(licenseManager_, &xlsone::LicenseManager::stateChanged,
                this, &WorkspaceChrome::updateLicenseStatus);
    }
    updateLicenseStatus();
    setWorkspaceState(false, false);
}

void WorkspaceChrome::buildUi()
{
    setObjectName(QStringLiteral("workspaceChrome"));
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(14, 10, 14, 10);
    root->setSpacing(16);

    const auto& t = xlsone::ui::theme();

    // Utility button group
    buttonGroup_ = new QWidget(this);
    buttonGroup_->setObjectName(QStringLiteral("workspaceButtonGroup"));
    auto* groupLayout = new QHBoxLayout(buttonGroup_);
    groupLayout->setContentsMargins(4, 4, 4, 4);
    groupLayout->setSpacing(2);

    appendButton_ = makeUtilityButton(buttonGroup_, tr("追加"), xlsone::ui::SymbolIcon::Plus, t.textMuted);
    reloadButton_ = makeUtilityButton(buttonGroup_, tr("刷新"), xlsone::ui::SymbolIcon::Refresh, t.textMuted);
    clearButton_ = makeUtilityButton(buttonGroup_, tr("清空"), xlsone::ui::SymbolIcon::Xmark, t.textMuted);
    groupLayout->addWidget(appendButton_);
    groupLayout->addWidget(reloadButton_);
    groupLayout->addWidget(clearButton_);

    root->addWidget(buttonGroup_);
    root->addStretch(1);

    // License status button
    licenseButton_ = new QPushButton(this);
    licenseButton_->setCursor(Qt::PointingHandCursor);
    licenseButton_->setObjectName(QStringLiteral("workspaceLicenseButton"));
    licenseButton_->setToolTip(tr("查看许可证"));
    root->addWidget(licenseButton_);

    // Export button
    exportButton_ = new QPushButton(tr("导出 XLSX"), this);
    exportButton_->setIcon(xlsone::ui::makeSymbolIcon(xlsone::ui::SymbolIcon::Export, Qt::white));
    exportButton_->setIconSize(QSize(16, 16));
    exportButton_->setCursor(Qt::PointingHandCursor);
    exportButton_->setObjectName(QStringLiteral("workspacePrimaryButton"));
    exportButton_->setToolTip(tr("导出同构汇总 Excel"));

    root->addWidget(exportButton_);

    connect(appendButton_, &QPushButton::clicked, this, &WorkspaceChrome::appendRequested);
    connect(reloadButton_, &QPushButton::clicked, this, &WorkspaceChrome::reloadRequested);
    connect(clearButton_, &QPushButton::clicked, this, &WorkspaceChrome::clearRequested);
    connect(exportButton_, &QPushButton::clicked, this, &WorkspaceChrome::exportRequested);
    connect(licenseButton_, &QPushButton::clicked, this, &WorkspaceChrome::licenseRequested);

    setStyleSheet(QStringLiteral(
        "QWidget#workspaceChrome { background: %1; border-bottom: 1px solid %2; }"
        "QWidget#workspaceButtonGroup { background: %3; border: 1px solid %2; border-radius: 12px; }"
        "QPushButton#workspaceUtilityButton { border: none; border-radius: 8px; padding: 7px 10px; font-weight: 500; color: %4; }"
        "QPushButton#workspaceUtilityButton:hover { background: %5; }"
        "QPushButton#workspaceLicenseButton { background: transparent; border: 1px solid %2; border-radius: 999px; padding: 5px 10px; font-size: 11px; font-weight: 600; }"
        "QPushButton#workspaceLicenseButton:hover { background: %5; }"
        "QPushButton#workspacePrimaryButton { background: %6; color: white; border: 1px solid %7; border-radius: 8px; padding: 8px 14px; font-weight: 600; }"
        "QPushButton#workspacePrimaryButton:hover { background: %8; }"
        "QPushButton#workspacePrimaryButton:disabled { background: %9; color: %10; border-color: %2; }"
    )
        .arg(t.bg0.name())
        .arg(t.border.name())
        .arg(t.bg1.name())
        .arg(t.textMuted.name())
        .arg(t.isDark ? QStringLiteral("rgba(255,255,255,0.06)") : QStringLiteral("rgba(0,0,0,0.06)"))
        .arg(t.accent.name())
        .arg(t.isDark ? QStringLiteral("rgba(82,148,255,0.30)") : QStringLiteral("rgba(42,117,255,0.30)"))
        .arg(t.isDark ? QStringLiteral("#1e5fd6") : QStringLiteral("#2368e8"))
        .arg(t.isDark ? QStringLiteral("#2c2e36") : QStringLiteral("#e2e6ef"))
        .arg(t.textDisabled.name()));
}

void WorkspaceChrome::setWorkspaceState(bool hasWorkspace, bool canExport)
{
    buttonGroup_->setVisible(hasWorkspace);
    appendButton_->setVisible(hasWorkspace);
    reloadButton_->setVisible(hasWorkspace);
    clearButton_->setVisible(hasWorkspace);
    exportButton_->setVisible(hasWorkspace);
    exportButton_->setEnabled(canExport);
}

void WorkspaceChrome::updateLicenseStatus()
{
    const auto& t = xlsone::ui::theme();
    if (licenseManager_ == nullptr) {
        licenseButton_->setText(QString());
        licenseButton_->setStyleSheet(QStringLiteral("color: %1;").arg(t.textMuted.name()));
        return;
    }

    QColor stateColor;
    QString text;
    switch (licenseManager_->state()) {
    case xlsone::LicenseState::Activated: {
        stateColor = t.success;
        const auto info = licenseManager_->currentInfo();
        const int graceDays = licenseManager_->graceRemainingDays();
        if (graceDays > 0) {
            stateColor = t.warning;
            text = tr("宽限期 · 剩余 %1 天").arg(graceDays);
        } else if (info.plan == xlsone::LicensePlan::PersonalYearly && info.expiresAt.isValid()) {
            const int remainingDays = QDateTime::currentDateTime().daysTo(info.expiresAt);
            if (remainingDays > 0) {
                text = tr("已激活 · 剩余 %1 天").arg(remainingDays);
            } else {
                text = tr("已激活");
            }
        } else {
            text = tr("已激活");
        }
        break;
    }
    case xlsone::LicenseState::Trial: {
        stateColor = t.info;
        const int remaining = licenseManager_->checkTrial();
        const int graceDays = licenseManager_->graceRemainingDays();
        if (remaining > 0) {
            text = tr("试用期 · 剩余 %1 天").arg(remaining);
        } else if (graceDays > 0) {
            stateColor = t.warning;
            text = tr("试用宽限 · 剩余 %1 天").arg(graceDays);
        } else {
            text = tr("试用期");
        }
        break;
    }
    case xlsone::LicenseState::Expired:
        stateColor = t.error;
        text = tr("已过期");
        break;
    case xlsone::LicenseState::Unactivated:
    default:
        stateColor = t.warning;
        text = tr("未授权 · 功能受限");
        break;
    }

    licenseButton_->setText(text);
    if (licenseManager_->isFullyLicensed()) {
        licenseButton_->setToolTip(tr("许可证有效"));
    } else {
        licenseButton_->setToolTip(licenseManager_->restrictionMessage());
    }
    licenseButton_->setStyleSheet(QStringLiteral(
        "QPushButton#workspaceLicenseButton { color: %1; background: transparent; border: 1px solid %2; border-radius: 999px; padding: 5px 10px; font-size: 11px; font-weight: 600; }"
        "QPushButton#workspaceLicenseButton:hover { background: %3; }"
    )
        .arg(stateColor.name())
        .arg(stateColor.name())
        .arg(t.isDark ? QStringLiteral("rgba(255,255,255,0.06)") : QStringLiteral("rgba(0,0,0,0.06)")));
}
