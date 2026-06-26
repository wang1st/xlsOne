#include "help_dialog.hpp"

#include "dialog_utils.hpp"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QSplitter>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QFont>
#include <QPushButton>

HelpDialog::HelpDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("快速参考指南"));
    setMinimumSize(900, 640);
    resize(1000, 700);

    buildUi();
}

void HelpDialog::buildUi()
{
    // ── Tree (sidebar) ──
    tree_ = new QTreeWidget(this);
    tree_->setHeaderHidden(true);
    tree_->setRootIsDecorated(false);
    tree_->setIndentation(16);
    tree_->setIconSize(QSize(18, 18));
    tree_->setMinimumWidth(200);
    tree_->setMaximumWidth(260);
    tree_->setFont(QFont(tree_->font().family(), tree_->font().pointSize()));

    // Group headers and their topic children
    // ── Getting Started ──
    auto* gsHeader = new QTreeWidgetItem(tree_, {tr("入门指南")});
    gsHeader->setFlags(gsHeader->flags() & ~Qt::ItemIsSelectable);
    gsHeader->setData(0, Qt::FontRole, QFont(tree_->font().family(), tree_->font().pointSize(), QFont::Bold));
    gsHeader->setData(0, Qt::ForegroundRole, QColor(0x59, 0x63, 0x6e));

    addTopic(tr("快速开始"), QStringLiteral("quickstart"), gsHeader);

    // ── Core Workflow ──
    auto* wfHeader = new QTreeWidgetItem(tree_, {tr("核心工作流")});
    wfHeader->setFlags(wfHeader->flags() & ~Qt::ItemIsSelectable);
    wfHeader->setData(0, Qt::FontRole, QFont(tree_->font().family(), tree_->font().pointSize(), QFont::Bold));
    wfHeader->setData(0, Qt::ForegroundRole, QColor(0x59, 0x63, 0x6e));

    addTopic(tr("导入文件"),   QStringLiteral("import"),     wfHeader);
    addTopic(tr("查看汇总"),    QStringLiteral("merge"),      wfHeader);
    addTopic(tr("工作表状态"),  QStringLiteral("sheetstatus"), wfHeader);
    addTopic(tr("穿透查阅"),    QStringLiteral("drilldown"),   wfHeader);
    addTopic(tr("导出结果"),    QStringLiteral("export"),     wfHeader);

    // ── Advanced ──
    auto* advHeader = new QTreeWidgetItem(tree_, {tr("高级功能")});
    advHeader->setFlags(advHeader->flags() & ~Qt::ItemIsSelectable);
    advHeader->setData(0, Qt::FontRole, QFont(tree_->font().family(), tree_->font().pointSize(), QFont::Bold));
    advHeader->setData(0, Qt::ForegroundRole, QColor(0x59, 0x63, 0x6e));

    addTopic(tr("单元格修正"),   QStringLiteral("correction"), advHeader);
    addTopic(tr("修正规则"),   QStringLiteral("schema"),     advHeader);

    // ── Reference ──
    auto* refHeader = new QTreeWidgetItem(tree_, {tr("参考信息")});
    refHeader->setFlags(refHeader->flags() & ~Qt::ItemIsSelectable);
    refHeader->setData(0, Qt::FontRole, QFont(tree_->font().family(), tree_->font().pointSize(), QFont::Bold));
    refHeader->setData(0, Qt::ForegroundRole, QColor(0x59, 0x63, 0x6e));

    addTopic(tr("快捷键"),          QStringLiteral("shortcuts"), refHeader);
    addTopic(tr("常见问题"),        QStringLiteral("faq"),       refHeader);
    addTopic(tr("联系方式"),         QStringLiteral("support"),   refHeader);

    // Expand all groups by default
    tree_->expandAll();

    // ── Text Browser (content) ──
    browser_ = new QTextBrowser(this);
    browser_->setOpenExternalLinks(true);
    browser_->setSource(QUrl(QStringLiteral("qrc:/help/index.html")));

    // ── Splitter ──
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(tree_);
    splitter->addWidget(browser_);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({220, 780});

    // ── Button Box ──
    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, Qt::Horizontal, this);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::close);

    // ── Layout ──
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 12);
    mainLayout->setSpacing(12);
    mainLayout->addWidget(splitter, 1);
    mainLayout->addWidget(buttonBox, 0);
    setLayout(mainLayout);

    // ── Connections ──
    connect(tree_, &QTreeWidget::itemClicked, this, &HelpDialog::onItemClicked);
}

void HelpDialog::addTopic(const QString& title, const QString& anchor, QTreeWidgetItem* parent)
{
    auto* item = new QTreeWidgetItem();
    item->setText(0, title);
    item->setData(0, Qt::UserRole, anchor);
    if (parent != nullptr) {
        parent->addChild(item);
    } else {
        tree_->addTopLevelItem(item);
    }
}

void HelpDialog::onItemClicked(QTreeWidgetItem* item, int /*column*/)
{
    const QString anchor = item->data(0, Qt::UserRole).toString();
    if (anchor.isEmpty()) {
        return;
    }
    browser_->scrollToAnchor(QStringLiteral("topic-") + anchor);
}
