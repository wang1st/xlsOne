#include "sheet_strip.hpp"

#include "ui_theme.hpp"

#include <QHBoxLayout>
#include <QPushButton>
#include <QScrollBar>
#include <QVariant>

namespace {

QString buttonTextFor(const SheetStripItem& item)
{
    return item.mergeable ? item.sheetName : QObject::tr("%1 / 跳过").arg(item.sheetName);
}

} // namespace

SheetStrip::SheetStrip(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("sheetStrip"));
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(10, 8, 10, 8);
    root->setSpacing(0);

    scrollArea_ = new QScrollArea(this);
    scrollArea_->setWidgetResizable(true);
    scrollArea_->setFrameShape(QFrame::NoFrame);
    scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    content_ = new QWidget(scrollArea_);
    layout_ = new QHBoxLayout(content_);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(8);
    layout_->addStretch(1);
    scrollArea_->setWidget(content_);
    root->addWidget(scrollArea_);

    const auto& t = xlsone::ui::theme();
    setStyleSheet(QStringLiteral(
        "QWidget#sheetStrip { background: %1; border-bottom: 1px solid %2; }"
        "QPushButton[sheetCapsule=\"true\"] {"
        " border: 1px solid %2; border-radius: 14px; background: %3;"
        " padding: 6px 12px; color: %4; font-weight: 600;"
        "}"
        "QPushButton[sheetCapsule=\"true\"]:checked {"
        " background: %5; border-color: %6; color: %7;"
        "}"
        "QPushButton[sheetCapsule=\"true\"][mergeable=\"false\"] {"
        " background: %8; border-color: %9; color: %10;"
        "}"
        "QPushButton[sheetCapsule=\"true\"][mergeable=\"false\"]:checked {"
        " background: %11; border-color: %12;"
        "}"
    )
        .arg(t.bg1.name())
        .arg(t.border.name())
        .arg(t.bg2.name())
        .arg(t.textMuted.name())
        .arg(t.accentSoft.name())
        .arg(t.isDark ? t.accent.name() : QStringLiteral("rgba(42,117,255,0.38)"))
        .arg(t.text.name())
        .arg(t.warningSoft.name())
        .arg(t.isDark ? QColor(180, 150, 80).name() : QStringLiteral("#f1d8a8"))
        .arg(t.isDark ? QColor(220, 190, 100).name() : QStringLiteral("#7a5b19"))
        .arg(t.isDark ? QColor(140, 110, 40).name() : QStringLiteral("#fff1cf"))
        .arg(t.isDark ? QColor(200, 160, 60).name() : QStringLiteral("#e4bd62")));
}

void SheetStrip::setItems(const QList<SheetStripItem>& items)
{
    items_ = items;
    rebuild();
}

void SheetStrip::setCurrentSheet(const QString& sheetName, bool mergeable)
{
    currentSheet_ = sheetName;
    currentMergeable_ = mergeable;
    rebuild();
}

void SheetStrip::rebuild()
{
    while (auto* item = layout_->takeAt(0)) {
        if (auto* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }

    for (const auto& item : items_) {
        auto* button = new QPushButton(buttonTextFor(item), content_);
        button->setCheckable(true);
        button->setProperty("sheetCapsule", QVariant(true));
        button->setProperty("mergeable", QVariant(item.mergeable));
        button->setToolTip(item.tooltip.isEmpty() ? item.subtitle : item.tooltip);
        button->setChecked(item.sheetName == currentSheet_ && item.mergeable == currentMergeable_);
        button->setCursor(Qt::PointingHandCursor);
        connect(button, &QPushButton::clicked, this, [this, item] {
            currentSheet_ = item.sheetName;
            currentMergeable_ = item.mergeable;
            rebuild();
            emit sheetSelected(item.sheetName, item.mergeable);
        });
        layout_->addWidget(button);
    }
    layout_->addStretch(1);
}
