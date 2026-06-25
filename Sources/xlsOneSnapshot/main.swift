import Foundation
import Darwin
import xlsOneCore

struct SnapshotCell: Codable {
    let row: Int
    let column: Int
    let value: String
    let rawValue: String?
    let numericValue: Double?
    let formatCode: String?
    let isDate: Bool
}

struct SnapshotSheet: Codable {
    let name: String
    let rowCount: Int
    let columnCount: Int
    let cells: [SnapshotCell]
}

struct SnapshotWorkbook: Codable {
    let filename: String
    let filepath: String
    let sheets: [SnapshotSheet]
}

struct SnapshotIssue: Codable {
    let severity: String
    let code: String
    let fileName: String
    let filePath: String
    let sheetName: String?
    let message: String
}

struct SnapshotFileReport: Codable {
    let filename: String
    let filepath: String
    let status: String
    let isTemplate: Bool
    let issues: [SnapshotIssue]
}

struct SnapshotValidation: Codable {
    let readiness: String
    let commonSheetNames: [String]
    let skippedSheetNames: [String]
    let files: [SnapshotFileReport]
    let skippedSheetIssues: [SnapshotIssue]
}

struct SnapshotSource: Codable {
    let filename: String
    let filepath: String
    let value: String
    let rawValue: String?
    let state: String
}

struct SnapshotMergedCell: Codable {
    let row: Int
    let column: Int
    let type: String
    let displayValue: String
    let sum: Double?
    let mixedCount: Int?
    let singleValue: String?
    let isOverridden: Bool
    let formatCode: String?
    let decisionReasons: [String]
    let isSuspicious: Bool
    let sources: [SnapshotSource]
}

struct SnapshotMergedResult: Codable {
    let sheetName: String
    let rowCount: Int
    let columnCount: Int
    let sourceFiles: [String]
    let cells: [SnapshotMergedCell]
}

struct SnapshotSheetFingerprint: Codable {
    let sheetName: String
    let rowCount: Int
    let columnCount: Int
    let layoutHash: String
    let formatHash: String
}

struct SnapshotWorkbookFingerprint: Codable {
    let sheetNames: [String]
    let sheetFingerprints: [SnapshotSheetFingerprint]
}

struct SnapshotSchemaMatchResult: Codable {
    let kind: String
    let names: [String]
}

struct SnapshotSchemaMatchProbe: Codable {
    let workbookFingerprint: SnapshotWorkbookFingerprint
    let selfSimilarity: Double
    let exactMatch: SnapshotSchemaMatchResult
    let ambiguousMatch: SnapshotSchemaMatchResult
}

struct SnapshotDocument: Codable {
    let snapshotVersion: Int
    let engine: String
    let schemaMode: String
    let inputs: [String]
    let parseFailures: [SnapshotIssue]
    let workbooks: [SnapshotWorkbook]
    let validation: SnapshotValidation
    let mergedResults: [SnapshotMergedResult]
    let schemaProbe: SnapshotMergedResult?
    let schemaMatchProbe: SnapshotSchemaMatchProbe?
    let exportParseBack: SnapshotWorkbook?
}

@main
struct SnapshotCommand {
    static func main() async {
        do {
            let options = try parseArguments(Array(CommandLine.arguments.dropFirst()))
            let document = await makeSnapshot(paths: options.paths)
            let encoder = JSONEncoder()
            encoder.outputFormatting = [.prettyPrinted, .sortedKeys, .withoutEscapingSlashes]
            let data = try encoder.encode(document)

            if let output = options.output {
                try data.write(to: URL(fileURLWithPath: output), options: .atomic)
            } else {
                FileHandle.standardOutput.write(data)
                FileHandle.standardOutput.write(Data("\n".utf8))
            }
        } catch {
            FileHandle.standardError.write(Data("xlsOneSnapshot: \(error.localizedDescription)\n".utf8))
            exit(2)
        }
    }

    private static func parseArguments(_ arguments: [String]) throws -> (output: String?, paths: [String]) {
        var output: String?
        var paths: [String] = []
        var index = 0
        while index < arguments.count {
            let argument = arguments[index]
            if argument == "--output" || argument == "-o" {
                guard index + 1 < arguments.count else {
                    throw SnapshotError.message("missing value for \(argument)")
                }
                output = arguments[index + 1]
                index += 2
            } else if argument == "--help" || argument == "-h" {
                print("Usage: xlsOneSnapshot [--output snapshot.json] <workbook> [workbook...]")
                exit(0)
            } else {
                paths.append(argument)
                index += 1
            }
        }
        if paths.isEmpty {
            throw SnapshotError.message("at least one workbook path is required")
        }
        return (output, paths)
    }

    private static func makeSnapshot(paths: [String]) async -> SnapshotDocument {
        let batch = await ExcelParser().parseFilesWithDiagnostics(at: paths)
        let outcome = WorkbookValidator().validate(files: batch.files, parseFailures: batch.failures)
        let merger = SimpleMerger()
        let mergedResults = outcome.report.commonSheetNames.map {
            merger.merge(files: outcome.mergeableFiles, sheetName: $0)
        }
        let schemaProbe = makeSchemaProbeSnapshot(mergedResults: mergedResults)
        let schemaMatchProbe = makeSchemaMatchProbeSnapshot(
            files: outcome.mergeableFiles,
            sheetNames: outcome.report.commonSheetNames
        )
        let exportParseBack = try? await makeExportParseBackSnapshot(
            report: outcome.report,
            mergedResults: mergedResults
        )

        return SnapshotDocument(
            snapshotVersion: 4,
            engine: "swift",
            schemaMode: "disabled",
            inputs: paths,
            parseFailures: batch.failures.map(snapshotIssue),
            workbooks: batch.files.map { snapshotWorkbook($0) },
            validation: snapshotValidation(outcome.report),
            mergedResults: mergedResults.map(snapshotMergedResult),
            schemaProbe: schemaProbe,
            schemaMatchProbe: schemaMatchProbe,
            exportParseBack: exportParseBack
        )
    }

    private static func makeSchemaProbeSnapshot(mergedResults: [MergedResult]) -> SnapshotMergedResult? {
        guard let result = mergedResults.first else {
            return nil
        }

        let overrideTypes: [CellOverrideType] = [.label, .sum, .mixed]
        var overrides: [CellTypeOverride] = []

        cellLoop: for (rowIndex, row) in result.rows.enumerated() {
            for (columnIndex, cell) in row.enumerated() {
                guard cell.sources.contains(where: { $0.state == .value }) else {
                    continue
                }
                overrides.append(CellTypeOverride(
                    sheetName: result.sheetName,
                    rowIndex: rowIndex,
                    colIndex: columnIndex,
                    cellType: overrideTypes[overrides.count],
                    userNote: "snapshot-probe"
                ))
                if overrides.count == overrideTypes.count {
                    break cellLoop
                }
            }
        }

        guard !overrides.isEmpty else {
            return nil
        }

        let probed = SmartMerger().applyOverrides(to: result, overrides: overrides)
        return snapshotMergedResult(probed)
    }

    private static func makeSchemaMatchProbeSnapshot(
        files: [ExcelFile],
        sheetNames: [String]
    ) -> SnapshotSchemaMatchProbe? {
        guard !files.isEmpty, !sheetNames.isEmpty else {
            return nil
        }

        let workbookFingerprint = FingerprintGenerator.generateWorkbook(
            from: files,
            sheetNames: sheetNames
        )
        let legacyFingerprint = FingerprintGenerator.generateWorkspaceLegacyFingerprint(
            from: files,
            sheetNames: sheetNames
        )

        let exactRule = MergeSchema(
            id: UUID(uuidString: "11111111-1111-1111-1111-111111111111")!,
            name: "snapshot-exact",
            fingerprint: legacyFingerprint,
            workbookFingerprint: workbookFingerprint,
            cellOverrides: []
        )
        let ambiguousRule = MergeSchema(
            id: UUID(uuidString: "22222222-2222-2222-2222-222222222222")!,
            name: "snapshot-ambiguous",
            fingerprint: legacyFingerprint,
            workbookFingerprint: workbookFingerprint,
            cellOverrides: []
        )

        let exactMatch = SchemaMatcher.match(
            workbookFingerprint: workbookFingerprint,
            against: [exactRule]
        )
        let ambiguousMatch = SchemaMatcher.match(
            workbookFingerprint: workbookFingerprint,
            against: [exactRule, ambiguousRule]
        )

        return SnapshotSchemaMatchProbe(
            workbookFingerprint: snapshotWorkbookFingerprint(workbookFingerprint),
            selfSimilarity: SchemaMatcher.calculateWorkbookSimilarity(workbookFingerprint, workbookFingerprint),
            exactMatch: snapshotSchemaMatchResult(exactMatch),
            ambiguousMatch: snapshotSchemaMatchResult(ambiguousMatch)
        )
    }

    private static func makeExportParseBackSnapshot(
        report: WorkbookValidationReport,
        mergedResults: [MergedResult]
    ) async throws -> SnapshotWorkbook? {
        guard report.readiness == .ready,
              let templatePath = report.templateFile?.filepath,
              URL(fileURLWithPath: templatePath).pathExtension.lowercased() == "xlsx",
              !mergedResults.isEmpty else {
            return nil
        }

        let outputURL = FileManager.default.temporaryDirectory
            .appendingPathComponent("xlsone-snapshot-export-\(UUID().uuidString).xlsx")
        defer {
            try? FileManager.default.removeItem(at: outputURL)
        }

        try TemplateWorkbookExporter().exportWorkbook(
            templatePath: templatePath,
            results: mergedResults,
            to: outputURL.path
        )
        let parsed = try await ExcelParser().parseFile(at: outputURL.path)
        return snapshotWorkbook(parsed, filename: "exported.xlsx", filepath: "/snapshot/exported.xlsx")
    }

    private static func snapshotWorkbook(
        _ workbook: ExcelFile,
        filename: String? = nil,
        filepath: String? = nil
    ) -> SnapshotWorkbook {
        SnapshotWorkbook(
            filename: filename ?? workbook.filename,
            filepath: filepath ?? workbook.filepath,
            sheets: workbook.sheets.map(snapshotSheet)
        )
    }

    private static func snapshotSheet(_ sheet: SheetData) -> SnapshotSheet {
        let columnCount = sheet.rows.map(\.count).max() ?? 0
        var cells: [SnapshotCell] = []
        for (rowIndex, row) in sheet.rows.enumerated() {
            for (columnIndex, cell) in row.enumerated() {
                cells.append(SnapshotCell(
                    row: rowIndex,
                    column: columnIndex,
                    value: cell.value,
                    rawValue: cell.rawValue,
                    numericValue: cell.numericValue,
                    formatCode: cell.formatCode,
                    isDate: cell.isDate
                ))
            }
        }
        return SnapshotSheet(
            name: sheet.name,
            rowCount: sheet.rows.count,
            columnCount: columnCount,
            cells: cells
        )
    }

    private static func snapshotIssue(_ failure: ExcelParseFailure) -> SnapshotIssue {
        SnapshotIssue(
            severity: "warning",
            code: "parseFailure",
            fileName: URL(fileURLWithPath: failure.path).lastPathComponent,
            filePath: failure.path,
            sheetName: nil,
            message: "解析失败: \(failure.message)"
        )
    }

    private static func snapshotIssue(_ issue: ValidationIssue) -> SnapshotIssue {
        SnapshotIssue(
            severity: issue.severity.rawValue,
            code: issue.code.rawValue,
            fileName: issue.fileName,
            filePath: issue.filePath,
            sheetName: issue.sheetName,
            message: issue.message
        )
    }

    private static func snapshotValidation(_ report: WorkbookValidationReport) -> SnapshotValidation {
        SnapshotValidation(
            readiness: report.readiness.rawValue,
            commonSheetNames: report.commonSheetNames,
            skippedSheetNames: report.skippedSheetNames,
            files: report.files.map {
                SnapshotFileReport(
                    filename: $0.filename,
                    filepath: $0.filepath,
                    status: $0.status.rawValue,
                    isTemplate: $0.isTemplate,
                    issues: $0.issues.map(snapshotIssue)
                )
            },
            skippedSheetIssues: report.skippedSheetIssues.map(snapshotIssue)
        )
    }

    private static func snapshotWorkbookFingerprint(
        _ fingerprint: WorkbookRuleFingerprint
    ) -> SnapshotWorkbookFingerprint {
        SnapshotWorkbookFingerprint(
            sheetNames: fingerprint.sheetNames,
            sheetFingerprints: fingerprint.sheetFingerprints.map {
                SnapshotSheetFingerprint(
                    sheetName: $0.sheetName,
                    rowCount: $0.rowCount,
                    columnCount: $0.columnCount,
                    layoutHash: $0.layoutHash,
                    formatHash: $0.formatHash
                )
            }
        )
    }

    private static func snapshotSchemaMatchResult(_ result: SchemaMatchResult) -> SnapshotSchemaMatchResult {
        let kind: String
        let names: [String]
        switch result {
        case .none:
            kind = "none"
            names = []
        case .exact(let schema):
            kind = "exact"
            names = [schema.name]
        case .ambiguous(let candidates):
            kind = "ambiguous"
            names = candidates.map(\.schema.name).sorted()
        case .similar(let candidates):
            kind = "similar"
            names = candidates.map(\.schema.name).sorted()
        }
        return SnapshotSchemaMatchResult(kind: kind, names: names)
    }

    private static func snapshotMergedResult(_ result: MergedResult) -> SnapshotMergedResult {
        let columnCount = result.rows.map(\.count).max() ?? 0
        var cells: [SnapshotMergedCell] = []
        for (rowIndex, row) in result.rows.enumerated() {
            for (columnIndex, cell) in row.enumerated() {
                cells.append(snapshotMergedCell(cell, row: rowIndex, column: columnIndex))
            }
        }
        return SnapshotMergedResult(
            sheetName: result.sheetName,
            rowCount: result.rows.count,
            columnCount: columnCount,
            sourceFiles: result.sourceFiles,
            cells: cells
        )
    }

    private static func snapshotMergedCell(_ cell: MergedCell, row: Int, column: Int) -> SnapshotMergedCell {
        let type: String
        let sum: Double?
        let mixedCount: Int?
        let singleValue: String?
        switch cell.type {
        case .label:
            type = "label"
            sum = nil
            mixedCount = nil
            singleValue = nil
        case .sum(let value):
            type = "sum"
            sum = value
            mixedCount = nil
            singleValue = nil
        case .mixed(let count):
            type = "mixed"
            sum = nil
            mixedCount = count
            singleValue = nil
        case .single(let value):
            type = "single"
            sum = nil
            mixedCount = nil
            singleValue = value
        }

        return SnapshotMergedCell(
            row: row,
            column: column,
            type: type,
            displayValue: cell.displayValue,
            sum: sum,
            mixedCount: mixedCount,
            singleValue: singleValue,
            isOverridden: cell.isOverridden,
            formatCode: cell.formatCode,
            decisionReasons: cell.decision.decisionReasons,
            isSuspicious: cell.decision.isSuspicious,
            sources: cell.sources.map {
                SnapshotSource(
                    filename: $0.filename,
                    filepath: $0.filepath,
                    value: $0.value,
                    rawValue: $0.rawValue,
                    state: $0.state.rawValue
                )
            }
        )
    }
}

enum SnapshotError: LocalizedError {
    case message(String)

    var errorDescription: String? {
        switch self {
        case .message(let message):
            return message
        }
    }
}
