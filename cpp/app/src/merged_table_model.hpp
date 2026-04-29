#pragma once

#include "xlsone/core/models.hpp"

#include <QAbstractTableModel>

class MergedTableModel final : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Role {
        CellKindRole = Qt::UserRole + 1,
        OverriddenRole,
        SuspiciousRole,
    };

    explicit MergedTableModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void setResult(xlsone::MergedResult result);
    const xlsone::MergedCell* cellAt(int row, int column) const;

private:
    xlsone::MergedResult result_;
};
