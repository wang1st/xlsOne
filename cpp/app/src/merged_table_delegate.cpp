#include "merged_table_delegate.hpp"

#include "merged_table_model.hpp"
#include "ui_theme.hpp"
#include "xlsone/core/models.hpp"

#include <QApplication>
#include <QPainter>
#include <QTableView>

MergedTableDelegate::MergedTableDelegate(QObject* parent) : QStyledItemDelegate(parent)
{
}

void MergedTableDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const auto& t = xlsone::ui::theme();
    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);

    const auto* table = qobject_cast<const QTableView*>(parent());
    const QModelIndex current = table == nullptr ? QModelIndex() : table->currentIndex();
    const bool sameRow = current.isValid() && current.row() == index.row();
    const bool sameColumn = current.isValid() && current.column() == index.column();
    const bool selected = option.state.testFlag(QStyle::State_Selected);
    const auto kind = static_cast<xlsone::CellKind>(index.data(MergedTableModel::CellKindRole).toInt());
    const bool overridden = index.data(MergedTableModel::OverriddenRole).toBool();

    QColor background = Qt::white;
    if (kind == xlsone::CellKind::Sum) {
        background = t.sumSoft;
    }
    if (sameRow || sameColumn) {
        background = QColor(
            (background.red() * 4 + t.accentSoft.red()) / 5,
            (background.green() * 4 + t.accentSoft.green()) / 5,
            (background.blue() * 4 + t.accentSoft.blue()) / 5
        );
    }
    if (selected) {
        background = t.accentSoft;
    }

    painter->save();
    painter->fillRect(option.rect, background);
    painter->setPen(QPen(t.borderSoft, 1));
    painter->drawRect(option.rect.adjusted(0, 0, -1, -1));

    QRect textRect = option.rect.adjusted(8, 0, -8, 0);
    painter->setPen(kind == xlsone::CellKind::Sum ? QColor(30, 95, 191) : t.text);
    painter->drawText(textRect, opt.displayAlignment, opt.text);

    if (overridden) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(t.adjusted);
        painter->drawEllipse(QRect(option.rect.right() - 10, option.rect.top() + 5, 5, 5));
    }

    if (selected) {
        painter->setPen(QPen(t.accent, 2));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(option.rect.adjusted(1, 1, -2, -2));
    }
    painter->restore();
}
