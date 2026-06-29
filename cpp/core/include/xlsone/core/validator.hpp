#pragma once

#include "xlsone/core/models.hpp"

#include <QStringList>
#include <vector>

namespace xlsone {

enum class MergeReadiness {
    Ready,
    Blocked,
};

enum class ValidationSeverity {
    Warning,
    Blocking,
};

enum class ValidationIssueCode {
    ParseFailure,
    EmptyWorkbook,
    MissingSheet,
    ExtraSheet,
    RowCountMismatch,
    ColumnCountMismatch,
    HeaderMismatch,
    LayoutMismatch,
};

enum class FileValidationStatus {
    Included,
    Warning,
    Blocked,
};

struct ExcelParseFailure {
    QString path;
    QString message;
};

struct ValidationIssue {
    ValidationSeverity severity = ValidationSeverity::Warning;
    ValidationIssueCode code = ValidationIssueCode::LayoutMismatch;
    QString fileName;
    QString filePath;
    QString sheetName;
    QString message;
};

struct SheetValidationReport {
    QString sheetName;
    MergeReadiness readiness = MergeReadiness::Ready;
    std::vector<ValidationIssue> issues;
    int templateRowCount = 0;
    int templateColumnCount = 0;
    int candidateRowCount = 0;
    int candidateColumnCount = 0;
};

struct FileValidationReport {
    QString filename;
    QString filepath;
    FileValidationStatus status = FileValidationStatus::Included;
    bool isTemplate = false;
    std::vector<ValidationIssue> issues;
    std::vector<SheetValidationReport> sheetReports;
};

struct WorkbookValidationReport {
    MergeReadiness readiness = MergeReadiness::Blocked;
    std::vector<FileValidationReport> files;
    QStringList commonSheetNames;
    QStringList skippedSheetNames;
    std::vector<ValidationIssue> skippedSheetIssues;
};

struct WorkbookValidationOutcome {
    WorkbookValidationReport report;
    std::vector<ExcelFile> mergeableFiles;
};

class WorkbookValidator {
public:
    WorkbookValidationOutcome validate(
        const std::vector<ExcelFile>& files,
        const std::vector<ExcelParseFailure>& parseFailures = {}
    ) const;
};

} // namespace xlsone

