#pragma once

#include "merged_table_model.hpp"
#include "xlsone/core/excel_parser.hpp"
#include "xlsone/core/merger.hpp"
#include "xlsone/core/schema_repository.hpp"
#include "xlsone/core/validator.hpp"

#include <QComboBox>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QModelIndex>
#include <QTableView>
#include <QTextEdit>
#include <optional>

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void openFiles();
    void appendFiles();
    void reloadFiles();
    void clearWorkspace();
    void exportResult();
    void markSelectedAsLabel();
    void markSelectedAsSum();
    void markSelectedAsMixed();
    void restoreAutomaticDecisionForSelection();
    void saveCurrentSchema();
    void manageSchemas();
    void undoLastOverride();
    void clearOverrides();
    void jumpToNextAnomaly();
    void jumpToPreviousAnomaly();
    void selectSheet(int index);
    void inspectCell(const QModelIndex& index);

private:
    void buildUi();
    void loadFiles(const QStringList& paths, bool append);
    void recomputeWorkspace();
    void showResult(const xlsone::MergedResult& result);
    void showValidationSummary();
    void updateDiagnostics();
    QStringList chooseInputFiles() const;
    void applyOverrideForSelection(xlsone::SchemaCellOverrideType type);
    void rebuildResultsWithCurrentOverrides();
    std::vector<xlsone::SchemaCellOverride> effectiveWorkspaceOverrides() const;
    void syncWorkspaceSchemaBase(const std::optional<xlsone::MergeSchema>& schema);
    void persistAdjustmentMemory();
    QString suggestedAdjustmentMemoryName() const;
    void jumpAcrossAnomalies(int step);
    QModelIndexList selectedCellIndexes() const;
    int currentResultIndex() const;

    struct OverrideSnapshot {
        std::vector<xlsone::SchemaCellOverride> currentOverrides;
        std::vector<xlsone::SchemaCellOverride> forgottenOverrides;
    };

    xlsone::ExcelParser parser_;
    xlsone::WorkbookValidator validator_;
    xlsone::SimpleMerger merger_;
    xlsone::SchemaRepository schemaRepository_;

    QStringList selectedPaths_;
    std::vector<xlsone::ExcelFile> parsedFiles_;
    std::vector<xlsone::ExcelParseFailure> parseFailures_;
    xlsone::WorkbookValidationOutcome validation_;
    std::vector<xlsone::MergedResult> baseResults_;
    std::vector<xlsone::MergedResult> results_;
    std::vector<xlsone::SchemaCellOverride> currentOverrides_;
    std::vector<xlsone::SchemaCellOverride> forgottenOverrides_;
    std::vector<OverrideSnapshot> overrideHistory_;
    std::vector<xlsone::SchemaCellOverride> workspaceBaseOverrides_;
    std::optional<QUuid> workspaceBaseSchemaId_;
    std::optional<QUuid> workspaceActiveSchemaId_;

    QComboBox* sheetCombo_ = nullptr;
    QTableView* table_ = nullptr;
    MergedTableModel* tableModel_ = nullptr;
    QListWidget* fileList_ = nullptr;
    QTextEdit* inspector_ = nullptr;
    QTextEdit* diagnostics_ = nullptr;
    QLabel* statusLabel_ = nullptr;
};
