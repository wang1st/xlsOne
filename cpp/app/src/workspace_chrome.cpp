#include "workspace_chrome.hpp"

#include "ui_theme.hpp"

#include <QApplication>
#include <QHBoxLayout>
#include <QStyle>

namespace {

QPushButton* makeUtilityButton(QWidget* parent, const QString& text, QStyle::StandardPixmap icon)
{
    auto* button = new QPushButton(text, parent);
    button->setIcon(qApp->style()->standardIcon(icon));
    button->setCursor(Qt::PointingHandCursor);
    button->setObjectName(QStringLiteral("workspaceUtilityButton"));
    button->setToolTip(text);
    return button;
}

} // namespace

WorkspaceChrome::WorkspaceChrome(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("workspaceChrome"));
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(14, 10, 14, 10);
    root->setSpacing(10);

    auto* group = new QWidget(this);
    group->setObjectName(QStringLiteral("workspaceButtonGroup"));
    auto* groupLayout = new QHBoxLayout(group);
    groupLayout->setContentsMargins(4, 4, 4, 4);
    groupLayout->setSpacing(2);

    appendButton_ = makeUtilityButton(group, tr("追加"), QStyle::SP_FileDialogNewFolder);
    reloadButton_ = makeUtilityButton(group, tr("刷新"), QStyle::SP_BrowserReload);
    clearButton_ = makeUtilityButton(group, tr("清空"), QStyle::SP_DialogCloseButton);
    groupLayout->addWidget(appendButton_);
    groupLayout->addWidget(reloadButton_);
    groupLayout->addWidget(clearButton_);

    exportButton_ = new QPushButton(tr("导出 XLSX"), this);
    exportButton_->setIcon(qApp->style()->standardIcon(QStyle::SP_DialogSaveButton));
    exportButton_->setCursor(Qt::PointingHandCursor);
    exportButton_->setObjectName(QStringLiteral("workspacePrimaryButton"));
    exportButton_->setToolTip(tr("导出同构汇总 Excel"));

    root->addWidget(group);
    root->addStretch(1);
    root->addWidget(exportButton_);

    connect(appendButton_, &QPushButton::clicked, this, &WorkspaceChrome::appendRequested);
    connect(reloadButton_, &QPushButton::clicked, this, &WorkspaceChrome::reloadRequested);
    connect(clearButton_, &QPushButton::clicked, this, &WorkspaceChrome::clearRequested);
    connect(exportButton_, &QPushButton::clicked, this, &WorkspaceChrome::exportRequested);

    const auto& t = xlsone::ui::theme();
    setStyleSheet(QStringLiteral(
        "QWidget#workspaceChrome { background: %1; border-bottom: 1px solid %2; }"
        "QWidget#workspaceButtonGroup { background: %3; border: 1px solid %2; border-radius: 12px; }"
        "QPushButton#workspaceUtilityButton { border: none; border-radius: 9px; padding: 7px 10px; font-weight: 500; color: %4; }"
        "QPushButton#workspaceUtilityButton:hover { background: %5; }"
        "QPushButton#workspacePrimaryButton { background: %6; color: white; border: 1px solid %7; border-radius: 10px; padding: 8px 14px; font-weight: 700; }"
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
        .arg(t.isDark ? QColor(50, 52, 62).name() : QStringLiteral("#e2e6ef"))
        .arg(t.textMuted.name()));
    setWorkspaceState(false, false);
}

void WorkspaceChrome::setWorkspaceState(bool hasWorkspace, bool canExport)
{
    appendButton_->setVisible(hasWorkspace);
    reloadButton_->setVisible(hasWorkspace);
    clearButton_->setVisible(hasWorkspace);
    exportButton_->setVisible(hasWorkspace);
    exportButton_->setEnabled(canExport);
}
