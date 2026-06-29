#pragma once

#include "xlsone/core/validator.hpp"

#include <QScrollArea>
#include <QVBoxLayout>

class DiagnosticsView final : public QScrollArea {
    Q_OBJECT

public:
    explicit DiagnosticsView(QWidget* parent = nullptr);

    void showEmpty();
    void setReport(const xlsone::WorkbookValidationReport& report);
    void showSkippedSheet(const xlsone::WorkbookValidationReport& report, const QString& sheetName);

private:
    QWidget* makeCard(const QString& title, const QString& body, const QColor& accent);
    void clearCards();

    QWidget* content_ = nullptr;
    QVBoxLayout* layout_ = nullptr;
};
