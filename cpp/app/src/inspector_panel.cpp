#include "inspector_panel.hpp"

#include "ui_theme.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVariant>

InspectorPanel::InspectorPanel(QWidget* parent) : QScrollArea(parent)
{
    setObjectName(QStringLiteral("inspectorPanel"));
    setFrameShape(QFrame::NoFrame);
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    content_ = new QWidget(this);
    layout_ = new QVBoxLayout(content_);
    layout_->setContentsMargins(14, 14, 14, 14);
    layout_->setSpacing(12);
    layout_->addStretch(1);
    setWidget(content_);

    const auto& t = xlsone::ui::theme();
    setStyleSheet(QStringLiteral(
        "QScrollArea#inspectorPanel { background: %1; border-left: 1px solid %2; }"
        "QWidget[inspectorCard=\"true\"] { background: %3; border: 1px solid %4; border-radius: 12px; }"
        "QLabel[muted=\"true\"] { color: %5; }"
        "QPushButton[overrideButton=\"true\"] { border-radius: 8px; padding: 6px 10px; font-weight: 600; }"
        "QPushButton[overrideButton=\"label\"] { background: %6; color: %7; border: 1px solid %8; }"
        "QPushButton[overrideButton=\"sum\"] { background: %9; color: %10; border: 1px solid %11; }"
        "QPushButton[restoreButton=\"true\"] { color: %5; text-align: left; border: none; padding: 4px 0; }"
    )
        .arg(t.bg0.name())
        .arg(t.border.name())
        .arg(t.bg1.name())
        .arg(t.borderSoft.name())
        .arg(t.textMuted.name())
        .arg(t.labelBg.name())
        .arg(t.labelFg.name())
        .arg(t.labelBorder.name())
        .arg(t.sumBg.name())
        .arg(t.sumFg.name())
        .arg(t.sumBorder.name()));
    showPlaceholder(tr("选择单元格后查看结果与来源。"));
}

void InspectorPanel::showPlaceholder(const QString& text)
{
    while (auto* item = layout_->takeAt(0)) {
        if (auto* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }

    auto* card = makeCard();
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(14, 14, 14, 14);
    auto* label = makeMutedLabel(text);
    label->setWordWrap(true);
    cardLayout->addWidget(label);
    layout_->addWidget(card);
    layout_->addStretch(1);
}

void InspectorPanel::showCell(const QString& reference, const xlsone::MergedCell& cell, bool canRestoreAutomatic)
{
    while (auto* item = layout_->takeAt(0)) {
        if (auto* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }

    auto* detailCard = makeCard();
    auto* detailLayout = new QVBoxLayout(detailCard);
    detailLayout->setContentsMargins(14, 14, 14, 14);
    detailLayout->setSpacing(10);

    auto* referenceLabel = makeMutedLabel(reference);
    detailLayout->addWidget(referenceLabel);

    auto* valueLabel = new QLabel(cell.displayValue.isEmpty() ? tr("空值") : cell.displayValue, detailCard);
    QFont valueFont = valueLabel->font();
    valueFont.setPointSize(18);
    valueFont.setWeight(QFont::DemiBold);
    valueFont.setFamily(QStringLiteral("monospace"));
    valueLabel->setFont(valueFont);
    valueLabel->setWordWrap(true);
    detailLayout->addWidget(valueLabel);

    const QString summary = cell.isOverridden
        ? tr("当前按%1显示").arg(typeText(cell.type.kind))
        : cell.decision.decisionReasons.isEmpty() ? tr("自动判断为%1").arg(typeText(cell.type.kind)) : cell.decision.decisionReasons.first();
    auto* summaryLabel = makeMutedLabel(summary);
    summaryLabel->setWordWrap(true);
    detailLayout->addWidget(summaryLabel);

    auto* buttons = new QHBoxLayout;
    auto* labelButton = new QPushButton(tr("标签"), detailCard);
    labelButton->setProperty("overrideButton", QVariant(QStringLiteral("label")));
    auto* sumButton = new QPushButton(tr("求和"), detailCard);
    sumButton->setProperty("overrideButton", QVariant(QStringLiteral("sum")));
    buttons->addWidget(labelButton);
    buttons->addWidget(sumButton);
    buttons->addStretch(1);
    detailLayout->addLayout(buttons);
    connect(labelButton, &QPushButton::clicked, this, &InspectorPanel::markLabelRequested);
    connect(sumButton, &QPushButton::clicked, this, &InspectorPanel::markSumRequested);

    if (canRestoreAutomatic) {
        auto* restoreButton = new QPushButton(tr("恢复自动判断"), detailCard);
        restoreButton->setProperty("restoreButton", QVariant(true));
        restoreButton->setCursor(Qt::PointingHandCursor);
        detailLayout->addWidget(restoreButton);
        connect(restoreButton, &QPushButton::clicked, this, &InspectorPanel::restoreAutomaticRequested);
    }

    layout_->addWidget(detailCard);

    auto* sourceCard = makeCard();
    auto* sourceLayout = new QVBoxLayout(sourceCard);
    sourceLayout->setContentsMargins(14, 12, 14, 12);
    sourceLayout->setSpacing(8);

    sourceToggle_ = new QToolButton(sourceCard);
    sourceToggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    sourceToggle_->setArrowType(sourceExpanded_ ? Qt::DownArrow : Qt::RightArrow);
    sourceToggle_->setText(tr("来源明细 %1 个").arg(static_cast<int>(cell.sources.size())));
    sourceToggle_->setCheckable(true);
    sourceToggle_->setChecked(sourceExpanded_);
    sourceLayout->addWidget(sourceToggle_);

    sourceBody_ = new QWidget(sourceCard);
    auto* sourceBodyLayout = new QVBoxLayout(sourceBody_);
    sourceBodyLayout->setContentsMargins(0, 0, 0, 0);
    sourceBodyLayout->setSpacing(0);
    for (const auto& source : cell.sources) {
        auto* row = new QWidget(sourceBody_);
        auto* rowLayout = new QVBoxLayout(row);
        rowLayout->setContentsMargins(0, 8, 0, 8);
        rowLayout->setSpacing(4);
        auto* title = new QLabel(source.state == xlsone::CellSourceState::Value
            ? source.filename
            : tr("%1 / %2").arg(source.filename, stateText(source.state)), row);
        title->setWordWrap(true);
        auto* value = makeMutedLabel(source.state == xlsone::CellSourceState::Value
            ? source.value
            : stateText(source.state));
        value->setWordWrap(true);
        rowLayout->addWidget(title);
        rowLayout->addWidget(value);
        sourceBodyLayout->addWidget(row);
    }
    sourceBody_->setVisible(sourceExpanded_);
    sourceLayout->addWidget(sourceBody_);
    connect(sourceToggle_, &QToolButton::toggled, this, [this](bool checked) {
        sourceExpanded_ = checked;
        sourceToggle_->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow);
        sourceBody_->setVisible(checked);
    });
    layout_->addWidget(sourceCard);
    layout_->addStretch(1);
}

QWidget* InspectorPanel::makeCard()
{
    auto* card = new QWidget(content_);
    card->setProperty("inspectorCard", QVariant(true));
    return card;
}

QLabel* InspectorPanel::makeMutedLabel(const QString& text)
{
    auto* label = new QLabel(text, content_);
    label->setProperty("muted", QVariant(true));
    return label;
}

QString InspectorPanel::typeText(xlsone::CellKind kind) const
{
    switch (kind) {
    case xlsone::CellKind::Sum: return tr("求和");
    case xlsone::CellKind::Mixed: return tr("混合");
    case xlsone::CellKind::Single: return tr("单值");
    case xlsone::CellKind::Label: return tr("标签");
    }
    return tr("标签");
}

QString InspectorPanel::stateText(xlsone::CellSourceState state) const
{
    switch (state) {
    case xlsone::CellSourceState::Value: return {};
    case xlsone::CellSourceState::Empty: return tr("空值");
    case xlsone::CellSourceState::Missing: return tr("缺失");
    }
    return {};
}
