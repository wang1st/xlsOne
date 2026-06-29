#pragma once

#include <QStyledItemDelegate>

class MergedTableDelegate final : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit MergedTableDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};
