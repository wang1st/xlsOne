import Foundation

public enum MergeReadiness: String, Codable, Equatable, Sendable {
    case ready
    case blocked
}

public enum ValidationSeverity: String, Codable, Equatable, Sendable {
    case warning
    case blocking
}

public enum ValidationIssueCode: String, Codable, Equatable, Sendable {
    case parseFailure
    case emptyWorkbook
    case sheetCountMismatch
    case sheetOrderMismatch
    case missingSheet
    case extraSheet
    case rowCountMismatch
    case columnCountMismatch
    case headerMismatch
    case layoutMismatch
}

public enum FileValidationStatus: String, Codable, Equatable, Sendable {
    case included
    case warning
    case blocked
}

public struct ValidationIssue: Identifiable, Codable, Equatable, Sendable {
    public let id: String
    public let severity: ValidationSeverity
    public let code: ValidationIssueCode
    public let fileName: String
    public let filePath: String
    public let sheetName: String?
    public let message: String

    public init(
        severity: ValidationSeverity,
        code: ValidationIssueCode,
        fileName: String,
        filePath: String,
        sheetName: String? = nil,
        message: String
    ) {
        self.severity = severity
        self.code = code
        self.fileName = fileName
        self.filePath = filePath
        self.sheetName = sheetName
        self.message = message
        self.id = [
            severity.rawValue,
            code.rawValue,
            fileName,
            filePath,
            sheetName ?? "",
            message
        ].joined(separator: "|")
    }
}

public struct SheetValidationReport: Codable, Equatable, Sendable {
    public let sheetName: String
    public let readiness: MergeReadiness
    public let issues: [ValidationIssue]
    public let templateRowCount: Int
    public let templateColumnCount: Int
    public let candidateRowCount: Int
    public let candidateColumnCount: Int

    public init(
        sheetName: String,
        readiness: MergeReadiness,
        issues: [ValidationIssue],
        templateRowCount: Int,
        templateColumnCount: Int,
        candidateRowCount: Int,
        candidateColumnCount: Int
    ) {
        self.sheetName = sheetName
        self.readiness = readiness
        self.issues = issues
        self.templateRowCount = templateRowCount
        self.templateColumnCount = templateColumnCount
        self.candidateRowCount = candidateRowCount
        self.candidateColumnCount = candidateColumnCount
    }
}

public struct FileValidationReport: Codable, Equatable, Sendable {
    public let filename: String
    public let filepath: String
    public let status: FileValidationStatus
    public let isTemplate: Bool
    public let issues: [ValidationIssue]
    public let sheetReports: [SheetValidationReport]

    public init(
        filename: String,
        filepath: String,
        status: FileValidationStatus,
        isTemplate: Bool,
        issues: [ValidationIssue],
        sheetReports: [SheetValidationReport]
    ) {
        self.filename = filename
        self.filepath = filepath
        self.status = status
        self.isTemplate = isTemplate
        self.issues = issues
        self.sheetReports = sheetReports
    }
}

public struct WorkbookValidationReport: Codable, Equatable, Sendable {
    public let readiness: MergeReadiness
    public let templateFile: FileValidationReport?
    public let files: [FileValidationReport]
    public let commonSheetNames: [String]
    public let skippedSheetNames: [String]
    public let skippedSheetIssues: [ValidationIssue]

    public init(
        readiness: MergeReadiness,
        templateFile: FileValidationReport?,
        files: [FileValidationReport],
        commonSheetNames: [String],
        skippedSheetNames: [String] = [],
        skippedSheetIssues: [ValidationIssue] = []
    ) {
        self.readiness = readiness
        self.templateFile = templateFile
        self.files = files
        self.commonSheetNames = commonSheetNames
        self.skippedSheetNames = skippedSheetNames
        self.skippedSheetIssues = skippedSheetIssues
    }

    public var includedFiles: [FileValidationReport] {
        files.filter { $0.status == .included }
    }

    public var blockedFiles: [FileValidationReport] {
        files.filter { $0.status == .blocked }
    }

    public var warningFiles: [FileValidationReport] {
        files.filter { $0.status == .warning }
    }

    public var skippedSheetCount: Int {
        skippedSheetNames.count
    }
}

public struct WorkbookValidationOutcome: Sendable {
    public let report: WorkbookValidationReport
    public let mergeableFiles: [ExcelFile]

    public init(report: WorkbookValidationReport, mergeableFiles: [ExcelFile]) {
        self.report = report
        self.mergeableFiles = mergeableFiles
    }
}

public struct WorkbookValidator {
    public init() {}

    public func validate(
        files: [ExcelFile],
        parseFailures: [ExcelParseFailure] = []
    ) -> WorkbookValidationOutcome {
        var reports: [FileValidationReport] = parseFailures.map { failure in
            FileValidationReport(
                filename: URL(fileURLWithPath: failure.path).lastPathComponent,
                filepath: failure.path,
                status: .warning,
                isTemplate: false,
                issues: [
                    ValidationIssue(
                        severity: .warning,
                        code: .parseFailure,
                        fileName: URL(fileURLWithPath: failure.path).lastPathComponent,
                        filePath: failure.path,
                        message: LocaleManager.loc("解析失败: \(failure.message)")
                    )
                ],
                sheetReports: []
            )
        }

        let candidateFiles = files.filter { !$0.sheets.isEmpty }

        for emptyFile in files where emptyFile.sheets.isEmpty {
            reports.append(
                FileValidationReport(
                    filename: emptyFile.filename,
                    filepath: emptyFile.filepath,
                    status: .warning,
                    isTemplate: false,
                    issues: [
                        ValidationIssue(
                            severity: .warning,
                            code: .emptyWorkbook,
                            fileName: emptyFile.filename,
                            filePath: emptyFile.filepath,
                            message: LocaleManager.loc("工作簿中没有可用工作表，已跳过")
                        )
                    ],
                    sheetReports: []
                )
            )
        }

        guard !candidateFiles.isEmpty else {
            let report = WorkbookValidationReport(
                readiness: .blocked,
                templateFile: nil,
                files: reports,
                commonSheetNames: []
            )
            return WorkbookValidationOutcome(report: report, mergeableFiles: [])
        }

        let preliminaryTemplate = chooseRepresentativeTemplate(
            from: candidateFiles,
            mergeableSheetNames: nil
        )
        let orderedSheetNames = orderedSheetNames(
            from: candidateFiles,
            preferredFile: preliminaryTemplate
        )

        var mergeableSheetNames: [String] = []
        var skippedSheetNames: [String] = []
        var skippedSheetIssues: [ValidationIssue] = []
        var referenceDimensionsBySheet: [String: SheetDimensions] = [:]

        for sheetName in orderedSheetNames {
            let entries = candidateFiles.map { file in
                SheetPresence(file: file, sheet: file.sheet(named: sheetName))
            }

            let presentEntries = entries.compactMap { entry -> (ExcelFile, SheetData)? in
                guard let sheet = entry.sheet else { return nil }
                return (entry.file, sheet)
            }

            if presentEntries.count != candidateFiles.count {
                skippedSheetNames.append(sheetName)
                for entry in entries where entry.sheet == nil {
                    skippedSheetIssues.append(
                        ValidationIssue(
                            severity: .warning,
                            code: .missingSheet,
                            fileName: entry.file.filename,
                            filePath: entry.file.filepath,
                            sheetName: sheetName,
                            message: LocaleManager.loc("工作表“\(sheetName)”未在所有文件中同时出现，已从本次汇总中排除")
                        )
                    )
                }
                continue
            }

            let dimensionsByFile = presentEntries.map { file, sheet in
                (file: file, dimensions: effectiveDimensions(for: sheet))
            }
            let dominantDimensions = chooseDominantDimensions(
                from: dimensionsByFile.map(\.dimensions)
            )
            let distinctDimensions = Set(dimensionsByFile.map(\.dimensions))

            if distinctDimensions.count == 1 {
                mergeableSheetNames.append(sheetName)
                referenceDimensionsBySheet[sheetName] = dominantDimensions
            } else {
                skippedSheetNames.append(sheetName)
                for item in dimensionsByFile where item.dimensions != dominantDimensions {
                    if item.dimensions.rows != dominantDimensions.rows {
                        skippedSheetIssues.append(
                            ValidationIssue(
                                severity: .warning,
                                code: .rowCountMismatch,
                                fileName: item.file.filename,
                                filePath: item.file.filepath,
                                sheetName: sheetName,
                                message: LocaleManager.loc("工作表“\(sheetName)”有效行数不一致（忽略尾部空白后：多数文件为 \(dominantDimensions.rows) 行，当前文件为 \(item.dimensions.rows) 行），已从本次汇总中排除")
                            )
                        )
                    }

                    if item.dimensions.cols != dominantDimensions.cols {
                        skippedSheetIssues.append(
                            ValidationIssue(
                                severity: .warning,
                                code: .columnCountMismatch,
                                fileName: item.file.filename,
                                filePath: item.file.filepath,
                                sheetName: sheetName,
                                message: LocaleManager.loc("工作表“\(sheetName)”有效列数不一致（忽略尾部空白后：多数文件为 \(dominantDimensions.cols) 列，当前文件为 \(item.dimensions.cols) 列），已从本次汇总中排除")
                            )
                        )
                    }
                }
            }
        }

        let template = chooseRepresentativeTemplate(
            from: candidateFiles,
            mergeableSheetNames: mergeableSheetNames
        )
        let orderedMergeableSheetNames = reorder(
            sheetNames: mergeableSheetNames,
            preferredFile: template,
            fallbackOrder: orderedSheetNames
        )
        let orderedSkippedSheetNames = reorder(
            sheetNames: skippedSheetNames,
            preferredFile: template,
            fallbackOrder: orderedSheetNames
        )

        let orderedMergeableFiles: [ExcelFile]
        if let template {
            orderedMergeableFiles = [template] + candidateFiles.filter { $0.filepath != template.filepath }
        } else {
            orderedMergeableFiles = candidateFiles
        }

        for file in orderedMergeableFiles {
            let sheetReports = orderedSheetNames.map { sheetName -> SheetValidationReport in
                let referenceDimensions = referenceDimensionsBySheet[sheetName] ?? SheetDimensions(rows: 0, cols: 0)
                let candidateDimensions = file.sheet(named: sheetName).map(effectiveDimensions) ?? SheetDimensions(rows: 0, cols: 0)
                let issues = skippedSheetIssues.filter {
                    $0.filePath == file.filepath && $0.sheetName == sheetName
                }

                return SheetValidationReport(
                    sheetName: sheetName,
                    readiness: orderedMergeableSheetNames.contains(sheetName) ? .ready : .blocked,
                    issues: issues,
                    templateRowCount: referenceDimensions.rows,
                    templateColumnCount: referenceDimensions.cols,
                    candidateRowCount: candidateDimensions.rows,
                    candidateColumnCount: candidateDimensions.cols
                )
            }

            reports.append(
                FileValidationReport(
                    filename: file.filename,
                    filepath: file.filepath,
                    status: .included,
                    isTemplate: file.filepath == template?.filepath,
                    issues: [],
                    sheetReports: sheetReports
                )
            )
        }

        reports.sort { lhs, rhs in
            if lhs.isTemplate != rhs.isTemplate {
                return lhs.isTemplate && !rhs.isTemplate
            }
            if lhs.status != rhs.status {
                return lhs.status.sortRank < rhs.status.sortRank
            }
            return lhs.filename.localizedStandardCompare(rhs.filename) == .orderedAscending
        }

        let readiness: MergeReadiness = orderedMergeableSheetNames.isEmpty ? .blocked : .ready
        let templateReport = reports.first(where: { $0.isTemplate })
        let report = WorkbookValidationReport(
            readiness: readiness,
            templateFile: templateReport,
            files: reports,
            commonSheetNames: readiness == .ready ? orderedMergeableSheetNames : [],
            skippedSheetNames: orderedSkippedSheetNames,
            skippedSheetIssues: skippedSheetIssues
        )

        return WorkbookValidationOutcome(
            report: report,
            mergeableFiles: readiness == .ready ? orderedMergeableFiles : []
        )
    }

    private func chooseRepresentativeTemplate(
        from files: [ExcelFile],
        mergeableSheetNames: [String]?
    ) -> ExcelFile? {
        let relevantSheetNames = Set(mergeableSheetNames ?? files.flatMap { $0.sheets.map(\.name) })
        return files.enumerated().max { lhs, rhs in
            let lhsScore = templateScore(for: lhs.element, relevantSheetNames: relevantSheetNames)
            let rhsScore = templateScore(for: rhs.element, relevantSheetNames: relevantSheetNames)
            if lhsScore != rhsScore {
                return lhsScore < rhsScore
            }
            return lhs.offset > rhs.offset
        }?.element
    }

    private func templateScore(
        for file: ExcelFile,
        relevantSheetNames: Set<String>
    ) -> TemplateScore {
        let relevantSheets = file.sheets.filter { relevantSheetNames.contains($0.name) }
        let totalDimensions = relevantSheets.reduce((rows: 0, cols: 0, nonEmpty: 0)) { partial, sheet in
            let dimensions = effectiveDimensions(for: sheet)
            return (
                rows: partial.rows + dimensions.rows,
                cols: partial.cols + dimensions.cols,
                nonEmpty: partial.nonEmpty + nonEmptyCellCount(in: sheet)
            )
        }

        return TemplateScore(
            sheetCount: relevantSheets.count,
            totalRows: totalDimensions.rows,
            totalColumns: totalDimensions.cols,
            nonEmptyCellCount: totalDimensions.nonEmpty
        )
    }

    private func orderedSheetNames(
        from files: [ExcelFile],
        preferredFile: ExcelFile?
    ) -> [String] {
        var names: [String] = []
        var seen: Set<String> = []

        let orderedFiles = (preferredFile.map { [$0] } ?? []) + files.filter { $0.filepath != preferredFile?.filepath }
        for file in orderedFiles {
            for sheet in file.sheets where !seen.contains(sheet.name) {
                seen.insert(sheet.name)
                names.append(sheet.name)
            }
        }
        return names
    }

    private func reorder(
        sheetNames: [String],
        preferredFile: ExcelFile?,
        fallbackOrder: [String]
    ) -> [String] {
        let targetSet = Set(sheetNames)
        var ordered: [String] = []
        var seen: Set<String> = []

        if let preferredFile {
            for sheet in preferredFile.sheets where targetSet.contains(sheet.name) && !seen.contains(sheet.name) {
                seen.insert(sheet.name)
                ordered.append(sheet.name)
            }
        }

        for sheetName in fallbackOrder where targetSet.contains(sheetName) && !seen.contains(sheetName) {
            seen.insert(sheetName)
            ordered.append(sheetName)
        }

        return ordered
    }

    private func chooseDominantDimensions(from dimensions: [SheetDimensions]) -> SheetDimensions {
        let indexedDimensions = Array(dimensions.enumerated())
        return Dictionary(grouping: indexedDimensions, by: \.element)
            .values
            .max { lhs, rhs in
                if lhs.count != rhs.count {
                    return lhs.count < rhs.count
                }

                let lhsDimensions = lhs[0].element
                let rhsDimensions = rhs[0].element
                if lhsDimensions != rhsDimensions {
                    return lhsDimensions < rhsDimensions
                }

                let lhsFirstIndex = lhs.map(\.offset).min() ?? .max
                let rhsFirstIndex = rhs.map(\.offset).min() ?? .max
                return lhsFirstIndex > rhsFirstIndex
            }?
            .first?
            .element ?? SheetDimensions(rows: 0, cols: 0)
    }

    private func effectiveDimensions(for sheet: SheetData) -> SheetDimensions {
        var lastNonEmptyRow = -1
        var lastNonEmptyColumn = -1

        for (rowIndex, row) in sheet.rows.enumerated() {
            for (columnIndex, cell) in row.enumerated() where !cell.value.isEmpty {
                lastNonEmptyRow = max(lastNonEmptyRow, rowIndex)
                lastNonEmptyColumn = max(lastNonEmptyColumn, columnIndex)
            }
        }

        return SheetDimensions(
            rows: max(lastNonEmptyRow + 1, 0),
            cols: max(lastNonEmptyColumn + 1, 0)
        )
    }

    private func nonEmptyCellCount(in sheet: SheetData) -> Int {
        sheet.rows.reduce(0) { count, row in
            count + row.filter { !$0.value.isEmpty }.count
        }
    }
}

private struct SheetPresence {
    let file: ExcelFile
    let sheet: SheetData?
}

private struct SheetDimensions: Hashable, Comparable {
    let rows: Int
    let cols: Int

    static func < (lhs: SheetDimensions, rhs: SheetDimensions) -> Bool {
        if lhs.rows != rhs.rows {
            return lhs.rows < rhs.rows
        }
        return lhs.cols < rhs.cols
    }
}

private struct TemplateScore: Comparable {
    let sheetCount: Int
    let totalRows: Int
    let totalColumns: Int
    let nonEmptyCellCount: Int

    static func < (lhs: TemplateScore, rhs: TemplateScore) -> Bool {
        if lhs.sheetCount != rhs.sheetCount {
            return lhs.sheetCount < rhs.sheetCount
        }
        if lhs.totalRows != rhs.totalRows {
            return lhs.totalRows < rhs.totalRows
        }
        if lhs.totalColumns != rhs.totalColumns {
            return lhs.totalColumns < rhs.totalColumns
        }
        return lhs.nonEmptyCellCount < rhs.nonEmptyCellCount
    }
}

private extension ExcelFile {
    func sheet(named sheetName: String) -> SheetData? {
        sheets.first(where: { $0.name == sheetName })
    }
}

private extension FileValidationStatus {
    var sortRank: Int {
        switch self {
        case .included: return 0
        case .warning: return 1
        case .blocked: return 2
        }
    }
}
