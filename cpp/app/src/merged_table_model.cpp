#include "merged_table_model.hpp"

#include <QBrush>

MergedTableModel::MergedTableModel(QObject* parent) : QAbstractTableModel(parent)
{
}

int MergedTableModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(result_.rows.size());
}

int MergedTableModel::columnCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    int columns = 0;
    for (const auto& row : result_.rows) {
        columns = std::max(columns, static_cast<int>(row.size()));
    }
    return columns;
}

QVariant MergedTableModel::data(const QModelIndex& index, int role) const
{
    const auto* cell = cellAt(index.row(), index.column());
    if (cell == nullptr) {
        return {};
    }

    if (role == Qt::DisplayRole) {
        return cell->displayValue;
    }
    if (role == Qt::ToolTipRole) {
        return cell->decision.decisionReasons.join(QStringLiteral("\n"));
    }
    if (role == Qt::BackgroundRole && cell->type.kind == xlsone::CellKind::Mixed) {
        return QBrush(QColor(255, 244, 214));
    }
    if (role == Qt::TextAlignmentRole) {
        return Qt::AlignCenter;
    }
    return {};
}

QVariant MergedTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole) {
        return {};
    }
    if (orientation == Qt::Horizontal) {
        return xlsone::columnLetters(section);
    }
    return section + 1;
}

void MergedTableModel::setResult(xlsone::MergedResult result)
{
    beginResetModel();
    result_ = std::move(result);
    endResetModel();
}

const xlsone::MergedCell* MergedTableModel::cellAt(int row, int column) const
{
    if (row < 0 || column < 0 || row >= static_cast<int>(result_.rows.size())) {
        return nullptr;
    }
    const auto& rowData = result_.rows[static_cast<size_t>(row)];
    if (column >= static_cast<int>(rowData.size())) {
        return nullptr;
    }
    return &rowData[static_cast<size_t>(column)];
}

