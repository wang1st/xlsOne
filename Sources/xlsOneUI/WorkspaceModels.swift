import Foundation
import xlsOneCore

enum WorkspacePhase: Equatable {
    case idle
    case validating
    case blocked
    case ready
}

enum WorkspaceSheetSelection: Equatable {
    case mergeable(String)
    case skipped(String)

    var sheetName: String {
        switch self {
        case .mergeable(let sheetName), .skipped(let sheetName):
            return sheetName
        }
    }
}

struct CellAnomalyItem: Identifiable, Hashable {
    let sheetName: String
    let position: CellPosition
    let cellReference: String
    let displayValue: String
    let summary: String

    var id: String {
        "\(sheetName)|\(cellReference)"
    }
}

struct SourceInspectionOverview: Equatable {
    let sourceCount: Int
    let valueCount: Int
    let emptyCount: Int
    let missingCount: Int
    let distinctValueCount: Int

    var summaryText: String {
        var segments = ["\(sourceCount) 个来源"]

        if emptyCount > 0 {
            segments.append("\(emptyCount) 个空值")
        }
        if missingCount > 0 {
            segments.append("\(missingCount) 个缺失")
        }
        if distinctValueCount > 1 {
            segments.append("内容不完全一致")
        }

        return segments.joined(separator: "，")
    }
}

enum SheetOverviewStatus: String, Hashable {
    case mergeable
    case skipped
}

enum CellCorrectionState: Equatable {
    case none
    case manual
    case rule
}

enum WorkspaceRuleState: Equatable {
    case none
    case applied(name: String, correctionCount: Int)
    case ambiguous(count: Int)
    case similar(count: Int)

    var message: String? {
        switch self {
        case .none:
            return nil
        case .applied(let name, let correctionCount):
            return correctionCount > 0 ? "已应用修正规则：\(name)" : "已匹配修正规则：\(name)"
        case .ambiguous(let count):
            return "检测到 \(count) 组修正规则，未自动应用"
        case .similar(let count):
            return "发现 \(count) 组相近修正规则，未自动应用"
        }
    }

    var isAttentionNeeded: Bool {
        switch self {
        case .ambiguous, .similar:
            return true
        case .none, .applied:
            return false
        }
    }
}

struct SheetOverviewItem: Identifiable, Hashable {
    let sheetName: String
    let status: SheetOverviewStatus
    let participatingFileCount: Int
    let totalFileCount: Int
    let effectiveRowCount: Int
    let effectiveColumnCount: Int
    let anomalyCount: Int
    let reasonSummary: String?
    let detailMessages: [String]

    var id: String {
        sheetName
    }
}

enum SkippedSheetGroupKind: Hashable {
    case dimensions(rows: Int, cols: Int)
    case missing
}

struct SkippedSheetGroup: Identifiable, Hashable {
    let kind: SkippedSheetGroupKind
    let title: String
    let detail: String
    let fileCount: Int
    let filenames: [String]
    let isDominant: Bool

    var id: String {
        switch kind {
        case .dimensions(let rows, let cols):
            return "dimensions-\(rows)-\(cols)"
        case .missing:
            return "missing"
        }
    }
}

struct SkippedSheetConsensus: Hashable {
    let sheetName: String
    let comparedFileCount: Int
    let groupCount: Int
    let summary: String
    let dominantGroupDescription: String?
    let groups: [SkippedSheetGroup]
}

enum WorkspaceDiagnostics {
    static func workspaceSummary(report: WorkbookValidationReport) -> String {
        var segments: [String] = [
            "\(report.includedFiles.count) 个文件参与",
            "\(report.commonSheetNames.count) 张可合并"
        ]

        if report.skippedSheetCount > 0 {
            segments.append("\(report.skippedSheetCount) 张跳过")
        }

        return segments.joined(separator: " · ")
    }

    static func decisionSummary(for cell: MergedCell) -> String? {
        let reasons = cell.decision.decisionReasons
            .map { $0.trimmingCharacters(in: .whitespacesAndNewlines) }
            .filter { !$0.isEmpty }

        guard let decisiveReason = reasons.last else {
            return nil
        }

        return decisiveReason
    }

    static func buildAnomalyQueue(for result: MergedResult) -> [CellAnomalyItem] {
        var items: [CellAnomalyItem] = []

        for (rowIndex, row) in result.rows.enumerated() {
            for (columnIndex, cell) in row.enumerated() {
                let summary = anomalySummary(for: cell)
                guard let summary else { continue }

                items.append(
                    CellAnomalyItem(
                        sheetName: result.sheetName,
                        position: CellPosition(row: rowIndex, col: columnIndex),
                        cellReference: cellReference(row: rowIndex, col: columnIndex),
                        displayValue: cell.displayValue,
                        summary: summary
                    )
                )
            }
        }

        return items
    }

    static func cellReference(row: Int, col: Int) -> String {
        "\(columnLetters(col))\(row + 1)"
    }

    static func columnLetters(_ col: Int) -> String {
        var result = ""
        var number = col
        repeat {
            result = String(UnicodeScalar(65 + (number % 26))!) + result
            number = number / 26 - 1
        } while number >= 0
        return result
    }

    static func anomalySummary(for cell: MergedCell) -> String? {
        return nil
    }

    static func buildSourceInspectionOverview(for sources: [CellSourceEntry]) -> SourceInspectionOverview {
        let sourceCount = sources.count
        let valueSources = sources.filter { $0.state == .value }
        let valueCount = valueSources.count
        let emptyCount = sources.filter { $0.state == .empty }.count
        let missingCount = sources.filter { $0.state == .missing }.count
        let distinctValueCount = Set(valueSources.map(\.value)).count

        return SourceInspectionOverview(
            sourceCount: sourceCount,
            valueCount: valueCount,
            emptyCount: emptyCount,
            missingCount: missingCount,
            distinctValueCount: distinctValueCount
        )
    }

    static func compactSourceNames(for sources: [CellSourceEntry]) -> [String] {
        let stems = sources.map { fileStem(from: $0.filename) }
        guard stems.count > 1 else { return stems }

        let sharedSuffix = trimDelimiters(longestCommonSuffix(stems))
        guard sharedSuffix.count >= 4 else { return stems }

        let compacted = stems.map { stem in
            guard stem.hasSuffix(sharedSuffix), stem.count > sharedSuffix.count else {
                return stem
            }

            let trimmed = trimDelimiters(String(stem.dropLast(sharedSuffix.count)))
            return trimmed.isEmpty ? stem : trimmed
        }

        return Set(compacted).count == compacted.count ? compacted : stems
    }

    static func buildSheetOverview(
        report: WorkbookValidationReport,
        anomalyItems: [CellAnomalyItem]
    ) -> [SheetOverviewItem] {
        let totalFileCount = report.files.count
        let anomalyCountBySheet = Dictionary(
            anomalyItems.map { ($0.sheetName, 1) },
            uniquingKeysWith: +
        )
        let orderedSheetNames = report.commonSheetNames + report.skippedSheetNames.filter {
            !report.commonSheetNames.contains($0)
        }

        return orderedSheetNames.compactMap { sheetName in
            let sheetReports = report.files.compactMap { fileReport in
                fileReport.sheetReports.first(where: { $0.sheetName == sheetName })
            }
            guard let referenceReport = sheetReports.first(where: {
                $0.templateRowCount > 0 || $0.templateColumnCount > 0
            }) ?? sheetReports.first else {
                return nil
            }

            let participatingFileCount = sheetReports.filter {
                $0.candidateRowCount == referenceReport.templateRowCount &&
                $0.candidateColumnCount == referenceReport.templateColumnCount &&
                (referenceReport.templateRowCount > 0 || referenceReport.templateColumnCount > 0)
            }.count

            let isMergeable = report.commonSheetNames.contains(sheetName)
            let detailMessages = uniqueMessages(
                report.skippedSheetIssues
                    .filter { $0.sheetName == sheetName }
                    .map(\.message)
            )

            return SheetOverviewItem(
                sheetName: sheetName,
                status: isMergeable ? .mergeable : .skipped,
                participatingFileCount: isMergeable ? report.includedFiles.count : participatingFileCount,
                totalFileCount: totalFileCount,
                effectiveRowCount: referenceReport.templateRowCount,
                effectiveColumnCount: referenceReport.templateColumnCount,
                anomalyCount: anomalyCountBySheet[sheetName, default: 0],
                reasonSummary: isMergeable ? nil : skippedReasonSummary(for: report.skippedSheetIssues, sheetName: sheetName),
                detailMessages: detailMessages
            )
        }
    }

    static func buildSkippedSheetConsensus(
        report: WorkbookValidationReport,
        sheetName: String
    ) -> SkippedSheetConsensus? {
        let comparableFiles = report.files.filter { !$0.sheetReports.isEmpty }
        guard !comparableFiles.isEmpty else { return nil }

        let entries = comparableFiles.compactMap { fileReport -> (String, SkippedSheetGroupKind)? in
            guard let sheetReport = fileReport.sheetReports.first(where: { $0.sheetName == sheetName }) else {
                return nil
            }

            let isMissing = sheetReport.issues.contains(where: { $0.code == .missingSheet })
            let kind: SkippedSheetGroupKind = isMissing
                ? .missing
                : .dimensions(rows: sheetReport.candidateRowCount, cols: sheetReport.candidateColumnCount)

            return (fileReport.filename, kind)
        }

        guard !entries.isEmpty else { return nil }

        let groupedEntries = Dictionary(grouping: entries, by: \.1)
        let dominantKind = groupedEntries.max { lhs, rhs in
            if lhs.value.count != rhs.value.count {
                return lhs.value.count < rhs.value.count
            }
            return groupSortRank(lhs.key) > groupSortRank(rhs.key)
        }?.key

        let groups = groupedEntries
            .map { kind, groupEntries in
                SkippedSheetGroup(
                    kind: kind,
                    title: groupTitle(for: kind, index: 0),
                    detail: groupDetail(for: kind),
                    fileCount: groupEntries.count,
                    filenames: groupEntries.map(\.0).sorted { $0.localizedStandardCompare($1) == .orderedAscending },
                    isDominant: kind == dominantKind
                )
            }
            .sorted { lhs, rhs in
                if lhs.fileCount != rhs.fileCount {
                    return lhs.fileCount > rhs.fileCount
                }
                return groupSortRank(lhs.kind) < groupSortRank(rhs.kind)
            }
            .enumerated()
            .map { index, group in
                SkippedSheetGroup(
                    kind: group.kind,
                    title: groupTitle(for: group.kind, index: index),
                    detail: group.detail,
                    fileCount: group.fileCount,
                    filenames: group.filenames,
                    isDominant: group.isDominant
                )
            }

        let dominantDescription = groups.first(where: { $0.isDominant }).map {
            "\($0.detail)（\($0.fileCount) 个文件）"
        }

        let summary: String
        if groups.contains(where: { if case .missing = $0.kind { return true } else { return false } }) {
            summary = "结构未达成一致，且部分文件缺少该工作表，已从本次汇总中排除。"
        } else {
            summary = "有效尺寸未达成一致，已从本次汇总中排除。"
        }

        return SkippedSheetConsensus(
            sheetName: sheetName,
            comparedFileCount: comparableFiles.count,
            groupCount: groups.count,
            summary: summary,
            dominantGroupDescription: dominantDescription,
            groups: groups
        )
    }

    private static func skippedReasonSummary(
        for issues: [ValidationIssue],
        sheetName: String
    ) -> String? {
        let codes = Set(
            issues
                .filter { $0.sheetName == sheetName }
                .map(\.code)
        )

        if codes.contains(.missingSheet) {
            return "部分文件缺少该工作表"
        }
        if codes.contains(.rowCountMismatch) && codes.contains(.columnCountMismatch) {
            return "有效行列数不一致"
        }
        if codes.contains(.rowCountMismatch) {
            return "有效行数不一致"
        }
        if codes.contains(.columnCountMismatch) {
            return "有效列数不一致"
        }
        return issues.first(where: { $0.sheetName == sheetName })?.message
    }

    private static func uniqueMessages(_ messages: [String]) -> [String] {
        var seen: Set<String> = []
        var result: [String] = []
        for message in messages where !seen.contains(message) {
            seen.insert(message)
            result.append(message)
        }
        return result
    }

    private static func groupSortRank(_ kind: SkippedSheetGroupKind) -> Int {
        switch kind {
        case .dimensions:
            return 0
        case .missing:
            return 1
        }
    }

    private static func groupTitle(for kind: SkippedSheetGroupKind, index: Int) -> String {
        switch kind {
        case .dimensions:
            return "结构组 \(columnLetters(index))"
        case .missing:
            return "缺失组"
        }
    }

    private static func groupDetail(for kind: SkippedSheetGroupKind) -> String {
        switch kind {
        case .dimensions(let rows, let cols):
            return "有效尺寸 \(rows) 行 × \(cols) 列"
        case .missing:
            return "未包含该工作表"
        }
    }

    private static func fileStem(from filename: String) -> String {
        URL(fileURLWithPath: filename).deletingPathExtension().lastPathComponent
    }

    private static func longestCommonSuffix(_ values: [String]) -> String {
        guard var suffix = values.first else { return "" }

        for value in values.dropFirst() {
            while !value.hasSuffix(suffix) && !suffix.isEmpty {
                suffix.removeFirst()
            }
            if suffix.isEmpty {
                return ""
            }
        }

        return suffix
    }

    private static func trimDelimiters(_ text: String) -> String {
        let delimiterCharacterSet = CharacterSet(charactersIn: "-_()[]{}（）【】<>《》,.，。/\\| ")
        return text.trimmingCharacters(in: delimiterCharacterSet.union(.whitespacesAndNewlines))
    }
}

enum ExportNaming {
    static func suggestedWorkbookName(from filenames: [String]) -> String {
        let stems = filenames
            .map(fileStem(from:))
            .map(normalizeCandidate(_:))
            .filter { !$0.isEmpty }

        guard !stems.isEmpty else {
            return "汇总结果"
        }

        let baseName: String
        if stems.count == 1 {
            baseName = stems[0]
        } else {
            let prefix = trimDelimiters(longestCommonPrefix(stems))
            let tokenPhrase = trimDelimiters(mostRepeatedTokenPhrase(stems))

            if prefix.count >= 4 {
                baseName = prefix
            } else if tokenPhrase.count >= 2 {
                baseName = tokenPhrase
            } else if prefix.count >= 2 {
                baseName = prefix
            } else {
                baseName = "汇总结果"
            }
        }

        let sanitized = sanitizeFileName(baseName)
        guard !sanitized.isEmpty else {
            return "汇总结果"
        }
        if sanitized.hasSuffix("汇总") {
            return sanitized
        }
        return "\(sanitized)_汇总"
    }

    private static func fileStem(from filename: String) -> String {
        URL(fileURLWithPath: filename).deletingPathExtension().lastPathComponent
    }

    private static func normalizeCandidate(_ text: String) -> String {
        text
            .replacingOccurrences(of: #"\s+"#, with: " ", options: .regularExpression)
            .trimmingCharacters(in: .whitespacesAndNewlines)
    }

    private static func longestCommonPrefix(_ values: [String]) -> String {
        guard var prefix = values.first else { return "" }
        for value in values.dropFirst() {
            while !value.hasPrefix(prefix) && !prefix.isEmpty {
                prefix.removeLast()
            }
            if prefix.isEmpty {
                return ""
            }
        }
        return prefix
    }

    private static func mostRepeatedTokenPhrase(_ values: [String]) -> String {
        guard let first = values.first else { return "" }
        let tokenLists = values.map { Array(Set(tokenize($0))) }
        var frequency: [String: Int] = [:]
        for tokenList in tokenLists {
            for token in tokenList where token.count >= 2 {
                frequency[token, default: 0] += 1
            }
        }

        let repeatedTokens = frequency
            .filter { $0.value >= 2 }
            .map(\.key)

        guard !repeatedTokens.isEmpty else {
            return ""
        }

        let maxFrequency = repeatedTokens.map { frequency[$0, default: 0] }.max() ?? 0
        let topTokens = tokenize(first).filter { token in
            frequency[token, default: 0] == maxFrequency
        }

        guard !topTokens.isEmpty else {
            return repeatedTokens
                .sorted {
                    let lhsFrequency = frequency[$0, default: 0]
                    let rhsFrequency = frequency[$1, default: 0]
                    if lhsFrequency != rhsFrequency {
                        return lhsFrequency > rhsFrequency
                    }
                    return $0.count > $1.count
                }
                .prefix(2)
                .joined()
        }

        return topTokens.joined()
    }

    private static func tokenize(_ text: String) -> [String] {
        let delimiterCharacters = Set("-_()[]{}（）【】<>《》,.，。/\\| ")
        var tokens: [String] = []
        var current = ""
        var lastWasDigit: Bool?

        for character in text {
            if character.isWhitespace || delimiterCharacters.contains(character) {
                if !current.isEmpty {
                    tokens.append(current)
                    current = ""
                }
                lastWasDigit = nil
                continue
            }

            let isDigit = character.isNumber
            if let lastWasDigit, lastWasDigit != isDigit, !current.isEmpty {
                tokens.append(current)
                current = ""
            }

            current.append(character)
            lastWasDigit = isDigit
        }

        if !current.isEmpty {
            tokens.append(current)
        }

        return tokens
    }

    private static func trimDelimiters(_ text: String) -> String {
        let delimiterCharacterSet = CharacterSet(charactersIn: "-_()[]{}（）【】<>《》,.，。/\\| ")
        return text.trimmingCharacters(in: delimiterCharacterSet.union(.whitespacesAndNewlines))
    }

    private static func sanitizeFileName(_ text: String) -> String {
        let illegalCharacters = CharacterSet(charactersIn: ":*?\"<>|/")
        let components = text.components(separatedBy: illegalCharacters)
        return components
            .joined(separator: "_")
            .replacingOccurrences(of: #"\s+"#, with: " ", options: .regularExpression)
            .trimmingCharacters(in: .whitespacesAndNewlines)
    }
}

struct ToolbarPresentation: Equatable {
    let importTitle: String
    let appendEnabled: Bool
    let importIsProminent: Bool
    let exportIsProminent: Bool
}

enum WorkspaceToolbarBuilder {
    static func buildPresentation(
        selectedFileCount: Int,
        canExport: Bool
    ) -> ToolbarPresentation {
        ToolbarPresentation(
            importTitle: "导入文件",
            appendEnabled: selectedFileCount > 0,
            importIsProminent: selectedFileCount == 0,
            exportIsProminent: canExport
        )
    }
}
