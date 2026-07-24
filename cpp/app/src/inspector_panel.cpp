#include "inspector_panel.hpp"

#include "dialog_utils.hpp"
#include "ui_theme.hpp"

#include <QApplication>
#include <QAction>
#include <QButtonGroup>
#include <QClipboard>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMouseEvent>
#include <QProcess>
#include <QPushButton>
#include <QSizePolicy>
#include <QToolButton>
#include <QUrl>
#include <QVariant>

#include <algorithm>
#include <functional>
#include <utility>

namespace {

[[maybe_unused]] constexpr const char* inspectorTranslations[] = {
    QT_TRANSLATE_NOOP("InspectorPanel", "未知文件"),
    QT_TRANSLATE_NOOP("InspectorPanel", "复制这个来源的值"),
    QT_TRANSLATE_NOOP("InspectorPanel", "复制文件名"),
    QT_TRANSLATE_NOOP("InspectorPanel", "定位"),
    QT_TRANSLATE_NOOP("InspectorPanel", "在文件管理器中显示"),
    QT_TRANSLATE_NOOP("InspectorPanel", "已复制来源值"),
    QT_TRANSLATE_NOOP("InspectorPanel", "已复制文件名"),
    QT_TRANSLATE_NOOP("InspectorPanel", "与其他来源差异明显"),
    QT_TRANSLATE_NOOP("InspectorPanel", "同一单元格其他有效数字的中位数为 %1；此提示仅用于辅助检查，不会改变汇总结果。"),
    QT_TRANSLATE_NOOP("InspectorPanel", "找不到来源文件"),
    QT_TRANSLATE_NOOP("InspectorPanel", "无法打开来源文件"),
    QT_TRANSLATE_NOOP("InspectorPanel", "打开来源文件"),
    QT_TRANSLATE_NOOP("InspectorPanel", "缺失"),
};

QString inspectorText(const char* source)
{
    return QCoreApplication::translate("InspectorPanel", source);
}

bool revealSourceFile(const QString& filepath)
{
    const QFileInfo info(filepath);
    if (!info.exists()) {
        return false;
    }
#if defined(Q_OS_MACOS)
    return QProcess::startDetached(QStringLiteral("open"), {QStringLiteral("-R"), info.absoluteFilePath()});
#elif defined(Q_OS_WIN)
    return QProcess::startDetached(
        QStringLiteral("explorer.exe"),
        {QStringLiteral("/select,%1").arg(QDir::toNativeSeparators(info.absoluteFilePath()))}
    );
#else
    return QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
#endif
}

class CopyLabel final : public QLabel {
public:
    CopyLabel(const QString& text, std::function<void()> copy, QWidget* parent) :
        QLabel(text, parent),
        copy_(std::move(copy))
    {
        setCursor(copy_ ? Qt::PointingHandCursor : Qt::ArrowCursor);
        setProperty("sourceCopyTarget", static_cast<bool>(copy_));
    }

protected:
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (copy_ && event->button() == Qt::LeftButton && rect().contains(event->pos())) {
            copy_();
            event->accept();
            return;
        }
        QLabel::mouseReleaseEvent(event);
    }

private:
    std::function<void()> copy_;
};

class SourceRow final : public QFrame {
public:
    SourceRow(
        const xlsone::CellSourceEntry& source,
        bool isOutlier,
        std::optional<double> median,
        QWidget* parent
    ) :
        QFrame(parent),
        source_(source)
    {
        setProperty("sourceRow", true);
        setCursor(Qt::ArrowCursor);
        setFocusPolicy(Qt::StrongFocus);
        setToolTip(source_.filepath);
        setContextMenuPolicy(Qt::CustomContextMenu);

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(10, 9, 10, 9);
        root->setSpacing(5);

        auto* heading = new QHBoxLayout;
        heading->setSpacing(6);

        const bool hasFilename = !source_.filename.isEmpty();
        auto* filename = new CopyLabel(
            hasFilename ? source_.filename : inspectorText("未知文件"),
            hasFilename
                ? std::function<void()>([this] {
                    QApplication::clipboard()->setText(source_.filename);
                    xlsone::ui::showToast(window(), inspectorText("已复制文件名"));
                })
                : std::function<void()>(),
            this
        );
        filename->setProperty("sourceFilename", true);
        filename->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        filename->setToolTip(hasFilename ? inspectorText("复制文件名") : QString());
        heading->addWidget(filename, 1);

        actions_ = new QWidget(this);
        auto* actionsLayout = new QHBoxLayout(actions_);
        actionsLayout->setContentsMargins(0, 0, 0, 0);
        actionsLayout->setSpacing(2);
        addActionButton(
            actionsLayout,
            inspectorText("定位"),
            inspectorText("在文件管理器中显示"),
            [this] { reveal(); },
            !source_.filepath.isEmpty()
        );
        actions_->setVisible(false);
        heading->addWidget(actions_);
        root->addLayout(heading);

        auto* valueLine = new QHBoxLayout;
        valueLine->setSpacing(8);

        if (isOutlier && median.has_value()) {
            auto* hint = new QLabel(inspectorText("与其他来源差异明显"), this);
            hint->setProperty("outlierBadge", true);
            hint->setToolTip(
                inspectorText("同一单元格其他有效数字的中位数为 %1；此提示仅用于辅助检查，不会改变汇总结果。")
                    .arg(QLocale().toString(*median, 'g', 12))
            );
            valueLine->addWidget(hint);
        } else if (source_.state != xlsone::CellSourceState::Value) {
            auto* state = new QLabel(
                source_.state == xlsone::CellSourceState::Empty
                    ? inspectorText("空值")
                    : inspectorText("缺失"),
                this
            );
            state->setProperty(
                "sourceState",
                source_.state == xlsone::CellSourceState::Empty
                    ? QVariant(QStringLiteral("empty"))
                    : QVariant(QStringLiteral("missing"))
            );
            valueLine->addWidget(state);
        }

        valueLine->addStretch(1);

        const QString displayValue = source_.state == xlsone::CellSourceState::Value
            ? source_.value
            : QStringLiteral("—");
        const bool hasValue = source_.state == xlsone::CellSourceState::Value;
        auto* value = new CopyLabel(
            displayValue,
            hasValue
                ? std::function<void()>([this] {
                    QApplication::clipboard()->setText(source_.value);
                    xlsone::ui::showToast(window(), inspectorText("已复制来源值"));
                })
                : std::function<void()>(),
            this
        );
        value->setProperty("sourceValue", true);
        value->setProperty("sourceValueOutlier", isOutlier);
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        value->setToolTip(hasValue ? inspectorText("复制这个来源的值") : QString());
        value->setWordWrap(true);
        if (source_.numericValue.has_value()) {
            QFont valueFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
            valueFont.setPointSize(15);
            valueFont.setWeight(QFont::DemiBold);
            value->setFont(valueFont);
        }
        valueLine->addWidget(value, 1);
        root->addLayout(valueLine);

        connect(this, &QWidget::customContextMenuRequested, this, [this](const QPoint& position) {
            showContextMenu(position);
        });
    }

protected:
    bool event(QEvent* event) override
    {
        switch (event->type()) {
        case QEvent::Enter:
        case QEvent::FocusIn:
            actions_->setVisible(true);
            break;
        case QEvent::Leave:
        case QEvent::FocusOut:
            actions_->setVisible(false);
            break;
        case QEvent::MouseButtonDblClick:
            open();
            return true;
        default:
            break;
        }
        return QFrame::event(event);
    }

private:
    template<typename Callback>
    void addActionButton(
        QHBoxLayout* layout,
        const QString& text,
        const QString& tooltip,
        Callback callback,
        bool enabled
    )
    {
        auto* button = new QToolButton(actions_);
        button->setText(text);
        button->setToolTip(tooltip);
        button->setProperty("sourceAction", true);
        button->setCursor(Qt::PointingHandCursor);
        button->setEnabled(enabled);
        layout->addWidget(button);
        connect(button, &QToolButton::clicked, this, callback);
    }

    void reveal()
    {
        if (!revealSourceFile(source_.filepath)) {
            xlsone::ui::showToast(window(), inspectorText("找不到来源文件"));
        }
    }

    void open()
    {
        if (!QFileInfo::exists(source_.filepath)
            || !QDesktopServices::openUrl(QUrl::fromLocalFile(source_.filepath))) {
            xlsone::ui::showToast(window(), inspectorText("无法打开来源文件"));
        }
    }

    void showContextMenu(const QPoint& position)
    {
        QMenu menu(this);
        auto* revealAction = menu.addAction(inspectorText("在文件管理器中显示"));
        revealAction->setEnabled(!source_.filepath.isEmpty());
        connect(revealAction, &QAction::triggered, this, [this] { reveal(); });

        auto* openAction = menu.addAction(inspectorText("打开来源文件"));
        openAction->setEnabled(!source_.filepath.isEmpty());
        connect(openAction, &QAction::triggered, this, [this] { open(); });
        menu.exec(mapToGlobal(position));
    }

    xlsone::CellSourceEntry source_;
    QWidget* actions_ = nullptr;
};

} // namespace

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
        "QLabel[muted=\"true\"], QLabel[sourceSummary=\"true\"] { color: %5; }"
        "QLabel[cellReference=\"true\"] { color: %5; font-weight: 600; }"
        "QLabel[typeBadge=\"true\"] { background: %6; color: %7; border: 1px solid %8;"
        " border-radius: 7px; padding: 3px 7px; font-weight: 600; }"
        "QLabel[resultValue=\"true\"] { color: %9; font-size: 22px; font-weight: 700;"
        " padding: 4px 0; }"
        "QPushButton[aggregationChoice=\"true\"] { border-radius: 8px; padding: 7px 12px;"
        " font-weight: 600; background: %10; color: %9; border: 1px solid %4; }"
        "QPushButton[aggregationChoice=\"true\"]:hover { background: %11; }"
        "QPushButton[choiceKind=\"label\"]:checked { background: %12; color: %13; border-color: %14; }"
        "QPushButton[choiceKind=\"sum\"]:checked { background: %15; color: %16; border-color: %17; }"
        "QPushButton[restoreButton=\"true\"] { color: %5; text-align: left; border: none; padding: 4px 0; }"
        "QPushButton[restoreButton=\"true\"]:hover { color: %18; background: transparent; }"
        "QToolButton[sourceToggle=\"true\"] { border: none; padding: 2px 0; font-weight: 700; }"
        "QLabel[sourceCount=\"true\"] { background: %10; color: %5; border-radius: 8px;"
        " padding: 2px 7px; font-weight: 600; }"
        "QFrame[sourceRow=\"true\"] { background: transparent; border-top: 1px solid %19; }"
        "QFrame[sourceRow=\"true\"]:hover { background: %11; border-radius: 8px; }"
        "QLabel[sourceCopyTarget=\"true\"] { padding: 2px 4px; border-radius: 5px; }"
        "QLabel[sourceCopyTarget=\"true\"]:hover { color: %18; background: %6; }"
        "QLabel[sourceFilename=\"true\"] { color: %5; font-size: 12px; }"
        "QLabel[sourceValue=\"true\"] { color: %9; font-size: 16px; font-weight: 600; }"
        "QLabel[sourceValueOutlier=\"true\"] { color: %20; }"
        "QLabel[sourceState=\"empty\"] { background: %21; color: %20; border-radius: 7px;"
        " padding: 2px 6px; font-size: 11px; font-weight: 600; }"
        "QLabel[sourceState=\"missing\"] { background: %10; color: %5; border-radius: 7px;"
        " padding: 2px 6px; font-size: 11px; font-weight: 600; }"
        "QLabel[outlierBadge=\"true\"] { color: %20; font-size: 11px; font-weight: 600; }"
        "QToolButton[sourceAction=\"true\"] { color: %5; border: none; padding: 3px 5px;"
        " border-radius: 6px; font-size: 11px; }"
        "QToolButton[sourceAction=\"true\"]:hover { color: %18; background: %6; }"
    )
        .arg(t.bg0.name())
        .arg(t.border.name())
        .arg(t.bg1.name())
        .arg(t.borderSoft.name())
        .arg(t.textMuted.name())
        .arg(t.accentSoft.name())
        .arg(t.accent.name())
        .arg(t.isDark ? QStringLiteral("#324866") : QStringLiteral("#c8d9ff"))
        .arg(t.text.name())
        .arg(t.bg2.name())
        .arg(t.elevatedSurface.name())
        .arg(t.labelBg.name())
        .arg(t.labelFg.name())
        .arg(t.labelBorder.name())
        .arg(t.sumBg.name())
        .arg(t.sumFg.name())
        .arg(t.sumBorder.name())
        .arg(t.accent.name())
        .arg(t.borderSoft.name())
        .arg(t.warning.name())
        .arg(t.warningSoft.name()));
    showPlaceholder(tr("选择单元格后查看结果与来源。"));
}

void InspectorPanel::showPlaceholder(const QString& text)
{
    clearContent();

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
    clearContent();
    const auto overview = xlsone::analyzeCellSources(cell.sources);

    auto* detailCard = makeCard();
    auto* detailLayout = new QVBoxLayout(detailCard);
    detailLayout->setContentsMargins(14, 14, 14, 14);
    detailLayout->setSpacing(10);

    auto* heading = new QHBoxLayout;
    auto* referenceLabel = new QLabel(reference, detailCard);
    referenceLabel->setProperty("cellReference", true);
    heading->addWidget(referenceLabel);
    heading->addStretch(1);
    auto* typeBadge = new QLabel(typeText(cell.type.kind), detailCard);
    typeBadge->setProperty("typeBadge", true);
    heading->addWidget(typeBadge);
    detailLayout->addLayout(heading);

    auto* valueLabel = new QLabel(cell.displayValue.isEmpty() ? tr("空值") : cell.displayValue, detailCard);
    valueLabel->setProperty("resultValue", true);
    valueLabel->setAlignment(Qt::AlignCenter);
    valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    valueLabel->setWordWrap(true);
    if (cell.type.kind == xlsone::CellKind::Sum) {
        QFont valueFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        valueFont.setPointSize(20);
        valueFont.setWeight(QFont::Bold);
        valueLabel->setFont(valueFont);
    }
    detailLayout->addWidget(valueLabel);

    const QString summary = cell.isOverridden
        ? tr("当前按%1显示").arg(typeText(cell.type.kind))
        : cell.decision.decisionReasons.isEmpty()
            ? tr("自动判断为%1").arg(typeText(cell.type.kind))
            : cell.decision.decisionReasons.first();
    auto* summaryLabel = makeMutedLabel(summary);
    summaryLabel->setWordWrap(true);
    summaryLabel->setAlignment(Qt::AlignCenter);
    detailLayout->addWidget(summaryLabel);

    auto* buttons = new QHBoxLayout;
    auto* choices = new QButtonGroup(detailCard);
    choices->setExclusive(true);

    auto* labelButton = new QPushButton(tr("标签"), detailCard);
    labelButton->setCheckable(true);
    labelButton->setProperty("aggregationChoice", true);
    labelButton->setProperty("choiceKind", QStringLiteral("label"));
    labelButton->setChecked(cell.type.kind == xlsone::CellKind::Label);
    labelButton->setCursor(Qt::PointingHandCursor);
    choices->addButton(labelButton);

    auto* sumButton = new QPushButton(tr("求和"), detailCard);
    sumButton->setCheckable(true);
    sumButton->setProperty("aggregationChoice", true);
    sumButton->setProperty("choiceKind", QStringLiteral("sum"));
    sumButton->setChecked(cell.type.kind == xlsone::CellKind::Sum);
    sumButton->setCursor(Qt::PointingHandCursor);
    choices->addButton(sumButton);

    buttons->addStretch(1);
    buttons->addWidget(labelButton);
    buttons->addWidget(sumButton);
    buttons->addStretch(1);
    detailLayout->addLayout(buttons);
    connect(labelButton, &QPushButton::clicked, this, &InspectorPanel::markLabelRequested);
    connect(sumButton, &QPushButton::clicked, this, &InspectorPanel::markSumRequested);

    if (canRestoreAutomatic) {
        auto* restoreButton = new QPushButton(tr("恢复自动判断"), detailCard);
        restoreButton->setProperty("restoreButton", true);
        restoreButton->setCursor(Qt::PointingHandCursor);
        detailLayout->addWidget(restoreButton, 0, Qt::AlignCenter);
        connect(restoreButton, &QPushButton::clicked, this, &InspectorPanel::restoreAutomaticRequested);
    }

    layout_->addWidget(detailCard);

    auto* sourceCard = makeCard();
    auto* sourceLayout = new QVBoxLayout(sourceCard);
    sourceLayout->setContentsMargins(12, 11, 12, 12);
    sourceLayout->setSpacing(8);

    auto* sourceHeading = new QHBoxLayout;
    sourceToggle_ = new QToolButton(sourceCard);
    sourceToggle_->setProperty("sourceToggle", true);
    sourceToggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    sourceToggle_->setArrowType(sourceExpanded_ ? Qt::DownArrow : Qt::RightArrow);
    sourceToggle_->setText(tr("来源明细"));
    sourceToggle_->setCheckable(true);
    sourceToggle_->setChecked(sourceExpanded_);
    sourceToggle_->setCursor(Qt::PointingHandCursor);
    sourceHeading->addWidget(sourceToggle_);
    sourceHeading->addStretch(1);
    auto* countBadge = new QLabel(QString::number(static_cast<int>(cell.sources.size())), sourceCard);
    countBadge->setProperty("sourceCount", true);
    sourceHeading->addWidget(countBadge);
    sourceLayout->addLayout(sourceHeading);

    QStringList summaryParts;
    summaryParts << tr("%1 个有效").arg(overview.valueCount);
    if (overview.emptyCount > 0) {
        summaryParts << tr("%1 个空值").arg(overview.emptyCount);
    }
    if (overview.missingCount > 0) {
        summaryParts << tr("%1 个缺失").arg(overview.missingCount);
    }
    auto* sourceSummary = new QLabel(summaryParts.join(QStringLiteral(" · ")), sourceCard);
    sourceSummary->setProperty("sourceSummary", true);
    sourceSummary->setWordWrap(true);
    sourceLayout->addWidget(sourceSummary);

    sourceBody_ = new QWidget(sourceCard);
    auto* sourceBodyLayout = new QVBoxLayout(sourceBody_);
    sourceBodyLayout->setContentsMargins(0, 0, 0, 0);
    sourceBodyLayout->setSpacing(0);

    for (std::size_t index = 0; index < cell.sources.size(); ++index) {
        const bool isOutlier = cell.type.kind == xlsone::CellKind::Sum
            && std::find(
                overview.outlierIndexes.begin(),
                overview.outlierIndexes.end(),
                index
            ) != overview.outlierIndexes.end();
        sourceBodyLayout->addWidget(
            new SourceRow(cell.sources[index], isOutlier, overview.numericMedian, sourceBody_)
        );
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

void InspectorPanel::clearContent()
{
    sourceBody_ = nullptr;
    sourceToggle_ = nullptr;
    while (auto* item = layout_->takeAt(0)) {
        if (auto* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
}

QWidget* InspectorPanel::makeCard()
{
    auto* card = new QWidget(content_);
    card->setProperty("inspectorCard", true);
    return card;
}

QLabel* InspectorPanel::makeMutedLabel(const QString& text)
{
    auto* label = new QLabel(text, content_);
    label->setProperty("muted", true);
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
