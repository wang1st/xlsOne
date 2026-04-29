#include "diagnostics_view.hpp"

#include "ui_theme.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace {

QString severityName(xlsone::ValidationSeverity severity)
{
    return severity == xlsone::ValidationSeverity::Blocking ? QObject::tr("阻断") : QObject::tr("警告");
}

QString readinessName(xlsone::MergeReadiness readiness)
{
    return readiness == xlsone::MergeReadiness::Ready ? QObject::tr("可合并") : QObject::tr("阻断");
}

QString statusName(xlsone::FileValidationStatus status)
{
    switch (status) {
    case xlsone::FileValidationStatus::Included: return QObject::tr("参与");
    case xlsone::FileValidationStatus::Warning: return QObject::tr("警告");
    case xlsone::FileValidationStatus::Blocked: return QObject::tr("阻断");
    }
    return QObject::tr("未知");
}

} // namespace

DiagnosticsView::DiagnosticsView(QWidget* parent) : QScrollArea(parent)
{
    setObjectName(QStringLiteral("diagnosticsView"));
    setFrameShape(QFrame::NoFrame);
    setWidgetResizable(true);
    content_ = new QWidget(this);
    layout_ = new QVBoxLayout(content_);
    layout_->setContentsMargins(16, 16, 16, 16);
    layout_->setSpacing(10);
    layout_->addStretch(1);
    setWidget(content_);
    setStyleSheet(QStringLiteral(
        "QScrollArea#diagnosticsView { background: #f6f7fa; border: none; }"
        "QWidget[diagnosticsCard=\"true\"] { background: white; border: 1px solid #e8ebf0; border-radius: 12px; }"
        "QLabel[diagnosticsTitle=\"true\"] { font-weight: 700; color: #1f2328; }"
        "QLabel[diagnosticsBody=\"true\"] { color: #646d7a; }"
    ));
    showEmpty();
}

void DiagnosticsView::showEmpty()
{
    clearCards();
    layout_->addWidget(makeCard(tr("暂无诊断"), tr("导入文件后会在这里显示结构校验和跳过原因。"), xlsone::ui::theme().accent));
    layout_->addStretch(1);
}

void DiagnosticsView::setReport(const xlsone::WorkbookValidationReport& report)
{
    clearCards();
    QString summary = tr("状态: %1\n可合并工作表: %2\n跳过工作表: %3")
        .arg(
            readinessName(report.readiness),
            report.commonSheetNames.isEmpty() ? tr("无") : report.commonSheetNames.join(QStringLiteral(", ")),
            report.skippedSheetNames.isEmpty() ? tr("无") : report.skippedSheetNames.join(QStringLiteral(", "))
        );
    layout_->addWidget(makeCard(tr("工作区结构"), summary, report.readiness == xlsone::MergeReadiness::Ready
        ? xlsone::ui::theme().adjusted
        : QColor(220, 93, 75)));

    for (const auto& file : report.files) {
        QStringList lines;
        lines << tr("状态: %1%2").arg(statusName(file.status), file.isTemplate ? tr(" / 模板") : QString());
        for (const auto& issue : file.issues) {
            lines << tr("[%1] %2").arg(severityName(issue.severity), issue.message);
        }
        layout_->addWidget(makeCard(file.filename, lines.join(QLatin1Char('\n')), file.status == xlsone::FileValidationStatus::Blocked
            ? QColor(220, 93, 75)
            : file.status == xlsone::FileValidationStatus::Warning ? QColor(207, 142, 39) : xlsone::ui::theme().adjusted));
    }

    if (!report.skippedSheetIssues.empty()) {
        QStringList lines;
        for (const auto& issue : report.skippedSheetIssues) {
            lines << tr("%1 / %2: %3").arg(issue.fileName, issue.sheetName, issue.message);
        }
        layout_->addWidget(makeCard(tr("工作表问题"), lines.join(QLatin1Char('\n')), QColor(207, 142, 39)));
    }
    layout_->addStretch(1);
}

void DiagnosticsView::showSkippedSheet(const xlsone::WorkbookValidationReport& report, const QString& sheetName)
{
    clearCards();
    QStringList lines;
    for (const auto& issue : report.skippedSheetIssues) {
        if (issue.sheetName == sheetName) {
            lines << tr("%1: %2").arg(issue.fileName, issue.message);
        }
    }
    if (lines.isEmpty()) {
        lines << tr("该工作表未参与合并，暂无更详细的结构差异。");
    }
    layout_->addWidget(makeCard(sheetName, lines.join(QLatin1Char('\n')), QColor(207, 142, 39)));
    layout_->addStretch(1);
}

QWidget* DiagnosticsView::makeCard(const QString& title, const QString& body, const QColor& accent)
{
    auto* card = new QWidget(content_);
    card->setProperty("diagnosticsCard", true);
    auto* outerLayout = new QHBoxLayout(card);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    auto* accentStrip = new QWidget(card);
    accentStrip->setFixedWidth(4);
    accentStrip->setStyleSheet(QStringLiteral("background: %1; border-top-left-radius: 12px; border-bottom-left-radius: 12px;").arg(accent.name()));
    outerLayout->addWidget(accentStrip);

    auto* bodyWidget = new QWidget(card);
    auto* layout = new QVBoxLayout(bodyWidget);
    layout->setContentsMargins(12, 12, 14, 12);
    layout->setSpacing(7);
    auto* titleLabel = new QLabel(title, bodyWidget);
    titleLabel->setProperty("diagnosticsTitle", true);
    auto* bodyLabel = new QLabel(body, bodyWidget);
    bodyLabel->setProperty("diagnosticsBody", true);
    bodyLabel->setWordWrap(true);
    bodyLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(titleLabel);
    layout->addWidget(bodyLabel);
    outerLayout->addWidget(bodyWidget, 1);
    return card;
}

void DiagnosticsView::clearCards()
{
    while (auto* item = layout_->takeAt(0)) {
        if (auto* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
}
