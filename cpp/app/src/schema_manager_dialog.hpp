#pragma once

#include "xlsone/core/schema_repository.hpp"

#include <QDialog>

#include <optional>

class QPushButton;
class QTextEdit;

class SchemaManagerDialog final : public QDialog {
    Q_OBJECT

public:
    explicit SchemaManagerDialog(const xlsone::SchemaRepository& repository,
                                 const std::optional<xlsone::MergeSchema>& currentSchema,
                                 QWidget* parent = nullptr);

    std::optional<xlsone::MergeSchema> importedSchema() const;
    bool clearRequested() const;

private slots:
    void importSchema();
    void exportCurrent();
    void clearCurrent();

private:
    void refreshDetails();
    QString describeSchema(const xlsone::MergeSchema& schema) const;

    const xlsone::SchemaRepository& repository_;
    std::optional<xlsone::MergeSchema> currentSchema_;
    std::optional<xlsone::MergeSchema> importedSchema_;
    bool clearRequested_ = false;

    QTextEdit* details_ = nullptr;
    QPushButton* exportButton_ = nullptr;
    QPushButton* importButton_ = nullptr;
    QPushButton* clearButton_ = nullptr;
};
