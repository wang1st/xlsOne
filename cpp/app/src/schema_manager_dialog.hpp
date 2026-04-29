#pragma once

#include "xlsone/core/schema_repository.hpp"

#include <QDialog>

#include <optional>

class QListWidget;
class QPushButton;
class QTextEdit;

class SchemaManagerDialog final : public QDialog {
    Q_OBJECT

public:
    explicit SchemaManagerDialog(const xlsone::SchemaRepository& repository, QWidget* parent = nullptr);

    std::optional<xlsone::MergeSchema> selectedSchema() const;

private slots:
    void updateDetails();
    void deleteSelected();
    void applySelected();
    void importSchema();
    void exportSelected();

private:
    void refresh();
    QString describeSchema(const xlsone::MergeSchema& schema) const;

    const xlsone::SchemaRepository& repository_;
    std::vector<xlsone::MergeSchema> schemas_;
    std::optional<xlsone::MergeSchema> selectedSchema_;

    QListWidget* list_ = nullptr;
    QTextEdit* details_ = nullptr;
    QPushButton* applyButton_ = nullptr;
    QPushButton* deleteButton_ = nullptr;
    QPushButton* importButton_ = nullptr;
    QPushButton* exportButton_ = nullptr;
};
