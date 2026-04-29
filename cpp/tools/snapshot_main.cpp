#include "xlsone/core/excel_parser.hpp"
#include "xlsone/core/exporter.hpp"
#include "xlsone/core/merger.hpp"
#include "xlsone/core/schema_repository.hpp"
#include "xlsone/core/validator.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QTemporaryDir>
#include <QTextStream>

#include <algorithm>
#include <array>
#include <iostream>

namespace {

void insertIfPresent(QJsonObject& object, const QString& key, const std::optional<QString>& value)
{
    if (value.has_value()) {
        object.insert(key, *value);
    }
}

void insertIfPresent(QJsonObject& object, const QString& key, const std::optional<double>& value)
{
    if (value.has_value()) {
        object.insert(key, *value);
    }
}

QString readinessName(xlsone::MergeReadiness readiness)
{
    return readiness == xlsone::MergeReadiness::Ready ? QStringLiteral("ready") : QStringLiteral("blocked");
}

QString severityName(xlsone::ValidationSeverity severity)
{
    return severity == xlsone::ValidationSeverity::Blocking ? QStringLiteral("blocking") : QStringLiteral("warning");
}

QString issueCodeName(xlsone::ValidationIssueCode code)
{
    switch (code) {
    case xlsone::ValidationIssueCode::ParseFailure: return QStringLiteral("parseFailure");
    case xlsone::ValidationIssueCode::EmptyWorkbook: return QStringLiteral("emptyWorkbook");
    case xlsone::ValidationIssueCode::MissingSheet: return QStringLiteral("missingSheet");
    case xlsone::ValidationIssueCode::ExtraSheet: return QStringLiteral("extraSheet");
    case xlsone::ValidationIssueCode::RowCountMismatch: return QStringLiteral("rowCountMismatch");
    case xlsone::ValidationIssueCode::ColumnCountMismatch: return QStringLiteral("columnCountMismatch");
    case xlsone::ValidationIssueCode::HeaderMismatch: return QStringLiteral("headerMismatch");
    case xlsone::ValidationIssueCode::LayoutMismatch: return QStringLiteral("layoutMismatch");
    }
    return QStringLiteral("layoutMismatch");
}

QString fileStatusName(xlsone::FileValidationStatus status)
{
    switch (status) {
    case xlsone::FileValidationStatus::Included: return QStringLiteral("included");
    case xlsone::FileValidationStatus::Warning: return QStringLiteral("warning");
    case xlsone::FileValidationStatus::Blocked: return QStringLiteral("blocked");
    }
    return QStringLiteral("warning");
}

QString schemaMatchKindName(xlsone::SchemaMatchKind kind)
{
    switch (kind) {
    case xlsone::SchemaMatchKind::None: return QStringLiteral("none");
    case xlsone::SchemaMatchKind::Exact: return QStringLiteral("exact");
    case xlsone::SchemaMatchKind::Ambiguous: return QStringLiteral("ambiguous");
    case xlsone::SchemaMatchKind::Similar: return QStringLiteral("similar");
    }
    return QStringLiteral("none");
}

QJsonObject snapshotIssue(const xlsone::ExcelParseFailure& failure)
{
    QJsonObject object;
    object.insert(QStringLiteral("severity"), QStringLiteral("warning"));
    object.insert(QStringLiteral("code"), QStringLiteral("parseFailure"));
    object.insert(QStringLiteral("fileName"), QFileInfo(failure.path).fileName());
    object.insert(QStringLiteral("filePath"), failure.path);
    object.insert(QStringLiteral("message"), QStringLiteral("解析失败: %1").arg(failure.message));
    return object;
}

QJsonObject snapshotIssue(const xlsone::ValidationIssue& issue)
{
    QJsonObject object;
    object.insert(QStringLiteral("severity"), severityName(issue.severity));
    object.insert(QStringLiteral("code"), issueCodeName(issue.code));
    object.insert(QStringLiteral("fileName"), issue.fileName);
    object.insert(QStringLiteral("filePath"), issue.filePath);
    if (!issue.sheetName.isEmpty()) {
        object.insert(QStringLiteral("sheetName"), issue.sheetName);
    }
    object.insert(QStringLiteral("message"), issue.message);
    return object;
}

QJsonObject snapshotCell(const xlsone::CellData& cell, int row, int column)
{
    QJsonObject object;
    object.insert(QStringLiteral("row"), row);
    object.insert(QStringLiteral("column"), column);
    object.insert(QStringLiteral("value"), cell.value);
    if (cell.rawValue.has_value() && !cell.rawValue->isEmpty()) {
        object.insert(QStringLiteral("rawValue"), *cell.rawValue);
    }
    insertIfPresent(object, QStringLiteral("numericValue"), cell.numericValue);
    insertIfPresent(object, QStringLiteral("formatCode"), cell.formatCode);
    object.insert(QStringLiteral("isDate"), cell.isDate);
    return object;
}

QJsonObject snapshotSheet(const xlsone::SheetData& sheet)
{
    int columnCount = 0;
    QJsonArray cells;
    for (int row = 0; row < static_cast<int>(sheet.rows.size()); ++row) {
        const auto& rowData = sheet.rows[static_cast<size_t>(row)];
        columnCount = std::max(columnCount, static_cast<int>(rowData.size()));
        for (int column = 0; column < static_cast<int>(rowData.size()); ++column) {
            cells.append(snapshotCell(rowData[static_cast<size_t>(column)], row, column));
        }
    }

    QJsonObject object;
    object.insert(QStringLiteral("name"), sheet.name);
    object.insert(QStringLiteral("rowCount"), static_cast<int>(sheet.rows.size()));
    object.insert(QStringLiteral("columnCount"), columnCount);
    object.insert(QStringLiteral("cells"), cells);
    return object;
}

QJsonObject snapshotWorkbook(
    const xlsone::ExcelFile& workbook,
    const QString& filenameOverride = {},
    const QString& filepathOverride = {}
)
{
    QJsonArray sheets;
    for (const auto& sheet : workbook.sheets) {
        sheets.append(snapshotSheet(sheet));
    }

    QJsonObject object;
    object.insert(QStringLiteral("filename"), filenameOverride.isEmpty() ? workbook.filename : filenameOverride);
    object.insert(QStringLiteral("filepath"), filepathOverride.isEmpty() ? workbook.filepath : filepathOverride);
    object.insert(QStringLiteral("sheets"), sheets);
    return object;
}

QJsonObject snapshotSheetFingerprint(const xlsone::SheetRuleFingerprint& fingerprint)
{
    QJsonObject object;
    object.insert(QStringLiteral("sheetName"), fingerprint.sheetName);
    object.insert(QStringLiteral("rowCount"), fingerprint.rowCount);
    object.insert(QStringLiteral("columnCount"), fingerprint.columnCount);
    object.insert(QStringLiteral("layoutHash"), fingerprint.layoutHash);
    object.insert(QStringLiteral("formatHash"), fingerprint.formatHash);
    return object;
}

QJsonObject snapshotWorkbookFingerprint(const xlsone::WorkbookFingerprint& fingerprint)
{
    QJsonArray sheetFingerprints;
    for (const auto& sheet : fingerprint.sheetFingerprints) {
        sheetFingerprints.append(snapshotSheetFingerprint(sheet));
    }

    QJsonObject object;
    object.insert(QStringLiteral("sheetNames"), QJsonArray::fromStringList(fingerprint.sheetNames));
    object.insert(QStringLiteral("sheetFingerprints"), sheetFingerprints);
    return object;
}

QJsonObject snapshotSchemaMatchResult(const xlsone::SchemaMatchResult& result)
{
    QStringList names;
    for (const auto& candidate : result.candidates) {
        names.append(candidate.schema.name);
    }
    names.sort();

    QJsonObject object;
    object.insert(QStringLiteral("kind"), schemaMatchKindName(result.kind));
    object.insert(QStringLiteral("names"), QJsonArray::fromStringList(names));
    return object;
}

QJsonObject snapshotValidation(const xlsone::WorkbookValidationReport& report)
{
    QJsonArray files;
    for (const auto& file : report.files) {
        QJsonArray issues;
        for (const auto& issue : file.issues) {
            issues.append(snapshotIssue(issue));
        }

        QJsonObject object;
        object.insert(QStringLiteral("filename"), file.filename);
        object.insert(QStringLiteral("filepath"), file.filepath);
        object.insert(QStringLiteral("status"), fileStatusName(file.status));
        object.insert(QStringLiteral("isTemplate"), file.isTemplate);
        object.insert(QStringLiteral("issues"), issues);
        files.append(object);
    }

    QJsonArray skippedIssues;
    for (const auto& issue : report.skippedSheetIssues) {
        skippedIssues.append(snapshotIssue(issue));
    }

    QJsonObject object;
    object.insert(QStringLiteral("readiness"), readinessName(report.readiness));
    object.insert(QStringLiteral("commonSheetNames"), QJsonArray::fromStringList(report.commonSheetNames));
    object.insert(QStringLiteral("skippedSheetNames"), QJsonArray::fromStringList(report.skippedSheetNames));
    object.insert(QStringLiteral("files"), files);
    object.insert(QStringLiteral("skippedSheetIssues"), skippedIssues);
    return object;
}

QJsonValue schemaMatchProbeSnapshot(
    const std::vector<xlsone::ExcelFile>& files,
    const QStringList& sheetNames
)
{
    if (files.empty() || sheetNames.isEmpty()) {
        return QJsonValue::Null;
    }

    const auto fingerprint = xlsone::fingerprintFor(files, sheetNames);

    xlsone::MergeSchema exactRule;
    exactRule.id = QUuid(QStringLiteral("{11111111-1111-1111-1111-111111111111}"));
    exactRule.name = QStringLiteral("snapshot-exact");
    exactRule.fingerprint = fingerprint;

    xlsone::MergeSchema ambiguousRule;
    ambiguousRule.id = QUuid(QStringLiteral("{22222222-2222-2222-2222-222222222222}"));
    ambiguousRule.name = QStringLiteral("snapshot-ambiguous");
    ambiguousRule.fingerprint = fingerprint;

    QJsonObject object;
    object.insert(QStringLiteral("workbookFingerprint"), snapshotWorkbookFingerprint(fingerprint));
    object.insert(QStringLiteral("selfSimilarity"), xlsone::SchemaMatcher::calculateSimilarity(fingerprint, fingerprint));
    object.insert(QStringLiteral("exactMatch"), snapshotSchemaMatchResult(xlsone::SchemaMatcher::match(fingerprint, {exactRule})));
    object.insert(
        QStringLiteral("ambiguousMatch"),
        snapshotSchemaMatchResult(xlsone::SchemaMatcher::match(fingerprint, {exactRule, ambiguousRule}))
    );
    return object;
}

QJsonObject snapshotSource(const xlsone::CellSourceEntry& source)
{
    QJsonObject object;
    object.insert(QStringLiteral("filename"), source.filename);
    object.insert(QStringLiteral("filepath"), source.filepath);
    object.insert(QStringLiteral("value"), source.value);
    if (source.rawValue.has_value() && !source.rawValue->isEmpty()) {
        object.insert(QStringLiteral("rawValue"), *source.rawValue);
    }
    object.insert(
        QStringLiteral("state"),
        source.state == xlsone::CellSourceState::Value
            ? QStringLiteral("value")
            : source.state == xlsone::CellSourceState::Empty ? QStringLiteral("empty") : QStringLiteral("missing")
    );
    return object;
}

QJsonObject snapshotMergedCell(const xlsone::MergedCell& cell, int row, int column)
{
    QJsonArray sources;
    for (const auto& source : cell.sources) {
        sources.append(snapshotSource(source));
    }

    QJsonObject object;
    object.insert(QStringLiteral("row"), row);
    object.insert(QStringLiteral("column"), column);
    object.insert(QStringLiteral("type"), xlsone::cellKindName(cell.type.kind));
    object.insert(QStringLiteral("displayValue"), cell.displayValue);
    if (cell.type.kind == xlsone::CellKind::Sum) {
        object.insert(QStringLiteral("sum"), cell.type.sum);
    } else if (cell.type.kind == xlsone::CellKind::Mixed) {
        object.insert(QStringLiteral("mixedCount"), cell.type.mixedCount);
    } else if (cell.type.kind == xlsone::CellKind::Single) {
        object.insert(QStringLiteral("singleValue"), cell.type.singleValue);
    }
    object.insert(QStringLiteral("isOverridden"), cell.isOverridden);
    insertIfPresent(object, QStringLiteral("formatCode"), cell.formatCode);
    object.insert(QStringLiteral("decisionReasons"), QJsonArray::fromStringList(cell.decision.decisionReasons));
    object.insert(QStringLiteral("isSuspicious"), cell.decision.isSuspicious);
    object.insert(QStringLiteral("sources"), sources);
    return object;
}

QJsonObject snapshotMergedResult(const xlsone::MergedResult& result)
{
    int columnCount = 0;
    QJsonArray cells;
    for (int row = 0; row < static_cast<int>(result.rows.size()); ++row) {
        const auto& rowData = result.rows[static_cast<size_t>(row)];
        columnCount = std::max(columnCount, static_cast<int>(rowData.size()));
        for (int column = 0; column < static_cast<int>(rowData.size()); ++column) {
            cells.append(snapshotMergedCell(rowData[static_cast<size_t>(column)], row, column));
        }
    }

    QJsonObject object;
    object.insert(QStringLiteral("sheetName"), result.sheetName);
    object.insert(QStringLiteral("rowCount"), static_cast<int>(result.rows.size()));
    object.insert(QStringLiteral("columnCount"), columnCount);
    object.insert(QStringLiteral("sourceFiles"), QJsonArray::fromStringList(result.sourceFiles));
    object.insert(QStringLiteral("cells"), cells);
    return object;
}

QJsonValue schemaProbeSnapshot(const std::vector<xlsone::MergedResult>& mergedResults)
{
    if (mergedResults.empty()) {
        return QJsonValue::Null;
    }

    const auto& result = mergedResults.front();
    const std::array overrideTypes = {
        xlsone::SchemaCellOverrideType::Label,
        xlsone::SchemaCellOverrideType::Sum,
        xlsone::SchemaCellOverrideType::Mixed,
    };

    xlsone::MergeSchema schema;
    schema.id = QUuid::createUuid();
    schema.name = QStringLiteral("snapshot-probe");

    for (int row = 0; row < static_cast<int>(result.rows.size()); ++row) {
        const auto& rowData = result.rows[static_cast<size_t>(row)];
        for (int column = 0; column < static_cast<int>(rowData.size()); ++column) {
            const auto& cell = rowData[static_cast<size_t>(column)];
            const bool hasValueSource = std::any_of(cell.sources.begin(), cell.sources.end(), [](const auto& source) {
                return source.state == xlsone::CellSourceState::Value;
            });
            if (!hasValueSource) {
                continue;
            }

            xlsone::SchemaCellOverride override;
            override.position = {row, column};
            override.type = overrideTypes[schema.overrides.size()];
            override.sheetName = result.sheetName;
            schema.overrides.push_back(std::move(override));

            if (schema.overrides.size() == overrideTypes.size()) {
                return snapshotMergedResult(xlsone::applySchema(schema, result));
            }
        }
    }

    if (schema.overrides.empty()) {
        return QJsonValue::Null;
    }
    return snapshotMergedResult(xlsone::applySchema(schema, result));
}

QJsonValue exportParseBackSnapshot(
    const xlsone::WorkbookValidationReport& report,
    const std::vector<xlsone::MergedResult>& mergedResults
)
{
    if (report.readiness != xlsone::MergeReadiness::Ready || mergedResults.empty()) {
        return QJsonValue::Null;
    }

    QString templatePath;
    for (const auto& file : report.files) {
        if (file.isTemplate && file.status == xlsone::FileValidationStatus::Included) {
            templatePath = file.filepath;
            break;
        }
    }
    if (templatePath.isEmpty() || QFileInfo(templatePath).suffix().toLower() != QStringLiteral("xlsx")) {
        return QJsonValue::Null;
    }

    try {
        QTemporaryDir temp;
        if (!temp.isValid()) {
            return QJsonValue::Null;
        }
        const auto outputPath = temp.filePath(QStringLiteral("exported.xlsx"));
        xlsone::TemplateWorkbookExporter().exportWorkbook(templatePath, mergedResults, outputPath);
        const auto parsed = xlsone::ExcelParser().parseFile(outputPath);
        return snapshotWorkbook(parsed, QStringLiteral("exported.xlsx"), QStringLiteral("/snapshot/exported.xlsx"));
    } catch (const std::exception&) {
        return QJsonValue::Null;
    }
}

QJsonObject makeSnapshot(const QStringList& paths)
{
    const auto parsed = xlsone::ExcelParser().parseFiles(paths);
    const auto outcome = xlsone::WorkbookValidator().validate(parsed.files, parsed.failures);

    QJsonArray failures;
    for (const auto& failure : parsed.failures) {
        failures.append(snapshotIssue(failure));
    }

    QJsonArray workbooks;
    for (const auto& workbook : parsed.files) {
        workbooks.append(snapshotWorkbook(workbook));
    }

    QJsonArray mergedResults;
    std::vector<xlsone::MergedResult> mergedResultValues;
    xlsone::SimpleMerger merger;
    for (const auto& sheetName : outcome.report.commonSheetNames) {
        auto result = merger.merge(outcome.mergeableFiles, sheetName);
        mergedResults.append(snapshotMergedResult(result));
        mergedResultValues.push_back(std::move(result));
    }

    QJsonObject object;
    object.insert(QStringLiteral("snapshotVersion"), 4);
    object.insert(QStringLiteral("engine"), QStringLiteral("cpp"));
    object.insert(QStringLiteral("schemaMode"), QStringLiteral("disabled"));
    object.insert(QStringLiteral("inputs"), QJsonArray::fromStringList(paths));
    object.insert(QStringLiteral("parseFailures"), failures);
    object.insert(QStringLiteral("workbooks"), workbooks);
    object.insert(QStringLiteral("validation"), snapshotValidation(outcome.report));
    object.insert(QStringLiteral("mergedResults"), mergedResults);
    object.insert(QStringLiteral("schemaProbe"), schemaProbeSnapshot(mergedResultValues));
    object.insert(QStringLiteral("schemaMatchProbe"), schemaMatchProbeSnapshot(outcome.mergeableFiles, outcome.report.commonSheetNames));
    object.insert(QStringLiteral("exportParseBack"), exportParseBackSnapshot(outcome.report, mergedResultValues));
    return object;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments().mid(1);

    QString outputPath;
    QStringList paths;
    for (int index = 0; index < arguments.size();) {
        const QString argument = arguments[index];
        if (argument == QStringLiteral("--output") || argument == QStringLiteral("-o")) {
            if (index + 1 >= arguments.size()) {
                std::cerr << "xlsone_snapshot: missing value for " << argument.toStdString() << '\n';
                return 2;
            }
            outputPath = arguments[index + 1];
            index += 2;
        } else if (argument == QStringLiteral("--help") || argument == QStringLiteral("-h")) {
            std::cout << "Usage: xlsone_snapshot [--output snapshot.json] <workbook> [workbook...]\n";
            return 0;
        } else {
            paths.append(argument);
            ++index;
        }
    }

    if (paths.isEmpty()) {
        std::cerr << "xlsone_snapshot: at least one workbook path is required\n";
        return 2;
    }

    try {
        const QJsonDocument document(makeSnapshot(paths));
        const QByteArray data = document.toJson(QJsonDocument::Indented);
        if (outputPath.isEmpty()) {
            QTextStream(stdout) << QString::fromUtf8(data);
        } else {
            QFile file(outputPath);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                std::cerr << "xlsone_snapshot: cannot write " << outputPath.toStdString() << ": " << file.errorString().toStdString() << '\n';
                return 2;
            }
            file.write(data);
        }
    } catch (const std::exception& error) {
        std::cerr << "xlsone_snapshot: " << error.what() << '\n';
        return 2;
    }

    return 0;
}
