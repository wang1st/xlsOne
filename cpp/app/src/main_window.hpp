#pragma once

#include "diagnostics_view.hpp"
#include "empty_workspace_view.hpp"
#include "inspector_panel.hpp"
#include "merged_table_model.hpp"
#include "sheet_strip.hpp"
#include "workspace_chrome.hpp"
#include "xlsone/core/excel_parser.hpp"
#include "xlsone/core/merger.hpp"
#include "xlsone/core/schema_repository.hpp"
#include "xlsone/core/validator.hpp"

#include <QLabel>
#include <QMainWindow>
#include <QModelIndex>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableView>
#include <QWidget>
#include <optional>
#include "xlsone/core/update_checker.hpp"

class QDragEnterEvent;
class QDragLeaveEvent;
class QDropEvent;
class QShowEvent;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void showEvent(QShowEvent* event) override;

private slots:
    void openFiles();
    void appendFiles();
    void reloadFiles();
    void clearWorkspace();
    void exportResult();
    void markSelectedAsLabel();
    void markSelectedAsSum();
    void restoreAutomaticDecisionForSelection();
    void saveCurrentSchema();
    void manageSchemas();
    void undoLastOverride();
    void clearOverrides();
    void jumpToNextAnomaly();
    void jumpToPreviousAnomaly();
    void selectSheet(const QString& sheetName, bool mergeable);
    void inspectCell(const QModelIndex& index);
    void checkForUpdates();

private:
    void buildUi();
    void loadFiles(const QStringList& paths, bool append);
    void recomputeWorkspace();
    void showResult(const xlsone::MergedResult& result);
    void showSkippedSheet(const QString& sheetName);
    void showValidationSummary();
    void updateDiagnostics();
    void updateChromeState();
    void updateSheetStrip();
    void updateCorrectionBar();
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
    bool hasRestorableOverride(int row, int column, const QString& sheetName) const;

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
    QString selectedSheetName_;
    bool selectedSheetMergeable_ = true;

    WorkspaceChrome* chrome_ = nullptr;
    QStackedWidget* contentStack_ = nullptr;
    EmptyWorkspaceView* emptyView_ = nullptr;
    QWidget* workspaceView_ = nullptr;
    SheetStrip* sheetStrip_ = nullptr;
    QStackedWidget* workspaceStack_ = nullptr;
    QTableView* table_ = nullptr;
    MergedTableModel* tableModel_ = nullptr;
    InspectorPanel* inspector_ = nullptr;
    DiagnosticsView* diagnostics_ = nullptr;
    QWidget* correctionBar_ = nullptr;
    QLabel* correctionLabel_ = nullptr;
    QPushButton* undoButton_ = nullptr;
    QPushButton* clearOverridesButton_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    xlsone::UpdateChecker* updateChecker_ = nullptr;
    bool firstShow_ = true;
};
