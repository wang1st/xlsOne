#pragma once

#include "xlsone/core/models.hpp"

#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

class InspectorPanel final : public QScrollArea {
    Q_OBJECT

public:
    explicit InspectorPanel(QWidget* parent = nullptr);

    void showPlaceholder(const QString& text);
    void showCell(const QString& reference, const xlsone::MergedCell& cell, bool canRestoreAutomatic);

signals:
    void markLabelRequested();
    void markSumRequested();
    void restoreAutomaticRequested();

private:
    QWidget* makeCard();
    QLabel* makeMutedLabel(const QString& text);
    QString typeText(xlsone::CellKind kind) const;
    QString stateText(xlsone::CellSourceState state) const;

    QWidget* content_ = nullptr;
    QVBoxLayout* layout_ = nullptr;
    QWidget* sourceBody_ = nullptr;
    QToolButton* sourceToggle_ = nullptr;
};
