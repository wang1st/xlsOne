#include "sheet_strip.hpp"

#include <QHBoxLayout>
#include <QPushButton>
#include <QScrollBar>

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

    setStyleSheet(QStringLiteral(
        "QWidget#sheetStrip { background: white; border-bottom: 1px solid #dce0e8; }"
        "QPushButton[sheetCapsule=\"true\"] {"
        " border: 1px solid #dce0e8; border-radius: 14px; background: #f8f9fb;"
        " padding: 6px 12px; color: #596375; font-weight: 600;"
        "}"
        "QPushButton[sheetCapsule=\"true\"]:checked {"
        " background: #e8f0ff; border-color: rgba(42,117,255,0.38); color: #1f2328;"
        "}"
        "QPushButton[sheetCapsule=\"true\"][mergeable=\"false\"] {"
        " background: #fff7e5; border-color: #f1d8a8; color: #7a5b19;"
        "}"
        "QPushButton[sheetCapsule=\"true\"][mergeable=\"false\"]:checked {"
        " background: #fff1cf; border-color: #e4bd62;"
        "}"
    ));
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
        button->setProperty("sheetCapsule", true);
        button->setProperty("mergeable", item.mergeable);
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
