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

    setStyleSheet(QStringLiteral(
        "QWidget#workspaceChrome { background: #f6f7fa; border-bottom: 1px solid #dce0e8; }"
        "QWidget#workspaceButtonGroup { background: #ffffff; border: 1px solid #dce0e8; border-radius: 12px; }"
        "QPushButton#workspaceUtilityButton { border: none; border-radius: 9px; padding: 7px 10px; font-weight: 500; }"
        "QPushButton#workspaceUtilityButton:hover { background: rgba(0,0,0,0.06); }"
        "QPushButton#workspacePrimaryButton { background: #2a75ff; color: white; border: 1px solid rgba(42,117,255,0.30); border-radius: 10px; padding: 8px 14px; font-weight: 700; }"
        "QPushButton#workspacePrimaryButton:hover { background: #2368e8; }"
        "QPushButton#workspacePrimaryButton:disabled { background: #e2e6ef; color: #8b95a6; border-color: #dce0e8; }"
    ));
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
