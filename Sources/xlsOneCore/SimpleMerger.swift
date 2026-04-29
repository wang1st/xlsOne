import Foundation

/// 简单的合并引擎 - 基于多因子动态单元格类型判定
/// 核心公式: f(自身格式40%, 垂直穿透一致性30%, 左邻列语义20%, 上下文邻居10%)
public struct SimpleMerger {

    public init() {}

    /// 合并多个文件的指定工作表
    public func merge(files: [ExcelFile], sheetName: String) -> MergedResult {
        var sheetDataList: [(filename: String, filepath: String, sheet: SheetData)] = []

        for file in files {
            if let sheet = file.sheets.first(where: { $0.name == sheetName }) {
                sheetDataList.append((filename: file.filename, filepath: file.filepath, sheet: sheet))
            }
        }

        guard !sheetDataList.isEmpty else {
            return MergedResult(sheetName: sheetName, rows: [], sourceFiles: files.map { $0.filename })
        }

        let maxRows = sheetDataList.map { $0.sheet.rows.count }.max() ?? 0
        let maxCols = sheetDataList.flatMap { $0.sheet.rows.map { $0.count } }.max() ?? 0
        let columnMetricTendencies = (0..<maxCols).map {
            buildColumnMetricTendency(sheetDataList: sheetDataList, colIdx: $0)
        }

        var mergedRows: [[MergedCell]] = []

        for rowIdx in 0..<maxRows {
            var mergedRow: [MergedCell] = []

            for colIdx in 0..<maxCols {
                // 收集当前位置的所有单元格
                var cellData: [CellMergeInput] = []
                for (filename, filepath, sheet) in sheetDataList {
                    let cell = sheet.cellAt(row: rowIdx, col: colIdx)
                    cellData.append(CellMergeInput(filename: filename, filepath: filepath, cell: cell))
                }

                // 收集左邻列的所有单元格
                var leftCellData: [CellMergeInput] = []
                if colIdx > 0 {
                    for (filename, filepath, sheet) in sheetDataList {
                        let cell = sheet.cellAt(row: rowIdx, col: colIdx - 1)
                        leftCellData.append(CellMergeInput(filename: filename, filepath: filepath, cell: cell))
                    }
                }

                // 构建邻居上下文
                let neighborContext = buildNeighborContext(
                    sheetDataList: sheetDataList,
                    rowIdx: rowIdx,
                    colIdx: colIdx,
                    columnMetricTendency: columnMetricTendencies[colIdx]
                )

                let mergedCell = MergedCell.from(
                    cells: cellData,
                    leftCells: leftCellData,
                    neighborContext: neighborContext,
                    row: rowIdx,
                    col: colIdx
                )
                mergedRow.append(mergedCell)
            }

            mergedRows.append(mergedRow)
        }

        return MergedResult(
            sheetName: sheetName,
            rows: mergedRows,
            sourceFiles: sheetDataList.map { $0.filename }
        )
    }

    /// 合并所有文件的第一个工作表
    public func mergeFirstSheets(from files: [ExcelFile]) -> MergedResult {
        var sheetDataList: [(filename: String, filepath: String, sheet: SheetData)] = []

        for file in files {
            if let firstSheet = file.sheets.first {
                sheetDataList.append((filename: file.filename, filepath: file.filepath, sheet: firstSheet))
            }
        }

        guard !sheetDataList.isEmpty else {
            return MergedResult(sheetName: "Sheet1", rows: [], sourceFiles: files.map { $0.filename })
        }

        let sheetName = sheetDataList.first?.sheet.name ?? "Sheet1"
        let maxRows = sheetDataList.map { $0.sheet.rows.count }.max() ?? 0
        let maxCols = sheetDataList.flatMap { $0.sheet.rows.map { $0.count } }.max() ?? 0
        let columnMetricTendencies = (0..<maxCols).map {
            buildColumnMetricTendency(sheetDataList: sheetDataList, colIdx: $0)
        }

        var mergedRows: [[MergedCell]] = []

        for rowIdx in 0..<maxRows {
            var mergedRow: [MergedCell] = []

            for colIdx in 0..<maxCols {
                var cellData: [CellMergeInput] = []
                for (filename, filepath, sheet) in sheetDataList {
                    let cell = sheet.cellAt(row: rowIdx, col: colIdx)
                    cellData.append(CellMergeInput(filename: filename, filepath: filepath, cell: cell))
                }

                var leftCellData: [CellMergeInput] = []
                if colIdx > 0 {
                    for (filename, filepath, sheet) in sheetDataList {
                        let cell = sheet.cellAt(row: rowIdx, col: colIdx - 1)
                        leftCellData.append(CellMergeInput(filename: filename, filepath: filepath, cell: cell))
                    }
                }

                let neighborContext = buildNeighborContext(
                    sheetDataList: sheetDataList,
                    rowIdx: rowIdx,
                    colIdx: colIdx,
                    columnMetricTendency: columnMetricTendencies[colIdx]
                )

                let mergedCell = MergedCell.from(
                    cells: cellData,
                    leftCells: leftCellData,
                    neighborContext: neighborContext,
                    row: rowIdx,
                    col: colIdx
                )
                mergedRow.append(mergedCell)
            }

            mergedRows.append(mergedRow)
        }

        return MergedResult(
            sheetName: sheetName,
            rows: mergedRows,
            sourceFiles: sheetDataList.map { $0.filename }
        )
    }

    /// 获取所有可用的工作表名称
    public func availableSheetNames(from files: [ExcelFile]) -> [String] {
        guard !files.isEmpty else { return [] }

        var allNames: Set<String> = []
        for file in files {
            for sheet in file.sheets {
                allNames.insert(sheet.name)
            }
        }

        var orderedNames: [String] = []
        var seenNames: Set<String> = []

        for sheet in files[0].sheets {
            if allNames.contains(sheet.name) && !seenNames.contains(sheet.name) {
                orderedNames.append(sheet.name)
                seenNames.insert(sheet.name)
            }
        }

        let remainingNames = allNames.subtracting(seenNames).sorted()
        orderedNames.append(contentsOf: remainingNames)

        return orderedNames
    }

    // MARK: - 邻居上下文

    private func buildColumnMetricTendency(
        sheetDataList: [(filename: String, filepath: String, sheet: SheetData)],
        colIdx: Int
    ) -> Double {
        var labeledColumns = 0
        var metricAnchors = 0

        for (_, _, sheet) in sheetDataList {
            for rowIdx in 0..<sheet.rows.count {
                guard let cell = sheet.cellAt(row: rowIdx, col: colIdx),
                      !cell.value.isEmpty,
                      cell.numericValue != nil else {
                    continue
                }

                if let label = nearestLabelForFirstNumeric(sheet: sheet, rowIdx: rowIdx, colIdx: colIdx) {
                    labeledColumns += 1
                    if isMetricAnchor(label) {
                        metricAnchors += 1
                    }
                }
                break
            }
        }

        guard labeledColumns > 0 else { return 0 }
        return Double(metricAnchors) / Double(labeledColumns)
    }

    private func nearestLabelForFirstNumeric(sheet: SheetData, rowIdx: Int, colIdx: Int) -> CellData? {
        if colIdx > 0 {
            for leftCol in stride(from: colIdx - 1, through: 0, by: -1) {
                if let cell = sheet.cellAt(row: rowIdx, col: leftCol),
                   !cell.value.isEmpty,
                   cell.numericValue == nil {
                    return cell
                }
            }
        }

        let maxDistance = max(sheet.rows.count, colIdx + 1)
        for distance in 1...maxDistance {
            let aboveRow = rowIdx - distance
            if aboveRow >= 0,
               let cell = sheet.cellAt(row: aboveRow, col: colIdx),
               !cell.value.isEmpty,
               cell.numericValue == nil {
                return cell
            }
            if aboveRow >= 0, colIdx > 0,
               let cell = sheet.cellAt(row: aboveRow, col: colIdx - 1),
               !cell.value.isEmpty,
               cell.numericValue == nil {
                return cell
            }
        }
        return nil
    }

    private func isMetricAnchor(_ cell: CellData) -> Bool {
        if matchesAnySemantic(cell.value, patterns: Self.codeAnchorPatterns) {
            return false
        }
        return matchesAnySemantic(cell.value, patterns: Self.metricAnchorPatterns)
    }

    private func matchesAnySemantic(_ text: String, patterns: [String]) -> Bool {
        let cleaned = cleanSemanticText(text)
        return patterns.contains { cleaned.contains($0.lowercased()) }
    }

    private func cleanSemanticText(_ text: String) -> String {
        var cleaned = text.trimmingCharacters(in: .whitespacesAndNewlines)
        let trailingPunctuations = CharacterSet(charactersIn: "：:：、。．;；/／-—")
        while let last = cleaned.last,
              String(last).rangeOfCharacter(from: trailingPunctuations) != nil {
            cleaned.removeLast()
        }
        return cleaned.lowercased()
    }

    private static let metricAnchorPatterns = [
        "合计", "总计", "小计",
        "金额", "数额", "额度",
        "数量", "单价", "总价",
        "价格", "数值", "预算",
        "收入", "支出", "成本",
        "费用", "利润", "执行",
        "决算", "款", "税金",
        "人数", "人口", "户数",
        "家数", "个数", "人员",
        "编制", "职工",
        "数", "额", "值", "量", "价"
    ]

    private static let codeAnchorPatterns = [
        "代码", "编码", "编号",
        "序号", "号码", "证号",
        "区划", "邮编", "邮政编码",
        "身份证", "电话", "传真",
        "期间", "年月", "年份",
        "日期", "时间", "学号",
        "工号", "账号", "户号",
        "卡号", "单号", "订单号",
        "票号", "发票号", "批号",
        "条码", "档案号", "许可证号"
    ]

    private func buildNeighborContext(
        sheetDataList: [(filename: String, filepath: String, sheet: SheetData)],
        rowIdx: Int,
        colIdx: Int,
        columnMetricTendency: Double
    ) -> NeighborContext {
        var numericScore = 0.0
        var labelScore = 0.0
        var totalWeight = 0.0

        let maxRows = sheetDataList.map { $0.sheet.rows.count }.max() ?? 0

        // 上3行
        for offset in 1...3 {
            let targetRow = rowIdx - offset
            guard targetRow >= 0 else { continue }
            let weight = 1.0 / Double(offset)

            var rowCells: [CellData?] = []
            for (_, _, sheet) in sheetDataList {
                rowCells.append(sheet.cellAt(row: targetRow, col: colIdx))
            }

            let profile = FormatProfile(cells: rowCells)
            applyNeighborScore(
                profile: profile,
                weight: weight,
                numericScore: &numericScore,
                labelScore: &labelScore,
                totalWeight: &totalWeight
            )
        }

        // 下3行
        for offset in 1...3 {
            let targetRow = rowIdx + offset
            guard targetRow < maxRows else { continue }
            let weight = 1.0 / Double(offset)

            var rowCells: [CellData?] = []
            for (_, _, sheet) in sheetDataList {
                rowCells.append(sheet.cellAt(row: targetRow, col: colIdx))
            }

            let profile = FormatProfile(cells: rowCells)
            applyNeighborScore(
                profile: profile,
                weight: weight,
                numericScore: &numericScore,
                labelScore: &labelScore,
                totalWeight: &totalWeight
            )
        }

        return NeighborContext(
            numericTendency: totalWeight > 0 ? numericScore / totalWeight : 0.0,
            labelTendency: totalWeight > 0 ? labelScore / totalWeight : 0.0,
            columnMetricTendency: columnMetricTendency
        )
    }

    private func applyNeighborScore(
        profile: FormatProfile,
        weight: Double,
        numericScore: inout Double,
        labelScore: inout Double,
        totalWeight: inout Double
    ) {
        if let dominant = profile.dominantFingerprint {
            switch dominant {
            case .strongNumeric, .integerWide:
                numericScore += weight
            case .chineseText, .alphaText, .date:
                labelScore += weight
            default:
                break
            }
        }
        totalWeight += weight
    }
}

// MARK: - 格式指纹系统

/// 单元格格式指纹
public enum FormatFingerprint: Equatable {
    case strongNumeric      // 带小数点/货币符号/千分位
    case integerWide        // 整数，位数>3或非统一长度
    case integerCode        // 整数，统一长度（编码特征）
    case chineseText        // 中文文本
    case alphaText          // 英文字母
    case date               // 日期
    case dashMarker         // —、/、-、NA
    case empty              // 空值
    case mixed              // 混合格式

    var isNumeric: Bool {
        switch self {
        case .strongNumeric, .integerWide, .integerCode:
            return true
        default:
            return false
        }
    }

    var isStrongNumeric: Bool {
        switch self {
        case .strongNumeric:
            return true
        default:
            return false
        }
    }
}

/// 格式分析结果
public struct FormatProfile {
    let fingerprints: [FormatFingerprint: Int]
    let totalSamples: Int
    let dominantFingerprint: FormatFingerprint?
    let dominantRatio: Double

    init(cells: [CellData?]) {
        let nonEmpty = cells.filter { $0 != nil && !($0?.value.isEmpty ?? true) }
        var counts: [FormatFingerprint: Int] = [:]
        var order: [FormatFingerprint] = []
        for cell in nonEmpty {
            let fp = FormatProfile.fingerprint(for: cell)
            if counts[fp] == nil {
                order.append(fp)
            }
            counts[fp, default: 0] += 1
        }
        self.fingerprints = counts
        self.totalSamples = nonEmpty.count

        var dominant: FormatFingerprint?
        var dominantCount = 0
        for fp in order {
            let count = counts[fp] ?? 0
            if dominant == nil || count > dominantCount {
                dominant = fp
                dominantCount = count
            }
        }

        if let dominant {
            self.dominantFingerprint = dominant
            self.dominantRatio = Double(dominantCount) / max(Double(nonEmpty.count), 1)
        } else {
            self.dominantFingerprint = nil
            self.dominantRatio = 0
        }
    }

    static func fingerprint(for cell: CellData?) -> FormatFingerprint {
        guard let cell = cell, !cell.value.isEmpty else { return .empty }
        let text = cell.value.trimmingCharacters(in: .whitespacesAndNewlines)

        // 占位符
        let markers = ["—", "-", "/", "NA", "N/A", "无", "null", "NULL", "~"]
        if markers.contains(text) || text == " " {
            return .dashMarker
        }

        // 日期
        if cell.isDate { return .date }

        // 数值判断 - 优先使用原始数值
        if cell.numericValue != nil {
            guard let num = cell.numericValue else { return .mixed }

            // 真正带小数点（非 .0 整数）
            if num != floor(num) { return .strongNumeric }

            // 检查显示格式：若 formatCode 为纯整数格式，即使底层存储带 .0 也不视为强数值
            let isIntegerFormat: Bool
            if let formatCode = cell.formatCode {
                isIntegerFormat = !formatCode.contains(".") &&
                                  !formatCode.contains("¥") &&
                                  !formatCode.contains("\\¥") &&
                                  !formatCode.contains("[$¥]") &&
                                  !formatCode.contains("$") &&
                                  !formatCode.contains("%")
            } else {
                isIntegerFormat = false
            }

            let isScientific = text.range(of: "[eE][+-]?\\d+", options: .regularExpression) != nil
            if !isScientific && (text.contains(".") || text.contains(",") || text.contains("，")) {
                if !isIntegerFormat {
                    return .strongNumeric
                }
            }

            // 对整数格式做规范化（去掉 .0 后缀），以便正确判定编码长度
            let fingerprintText = (isIntegerFormat && text.hasSuffix(".0"))
                ? String(text.dropLast(2))
                : text

            // 统一长度纯数字（编码候选）- 常见编码长度
            let digits = fingerprintText.filter { $0.isNumber }
            let codeLengths = [6, 9, 11, 12, 15, 18]
            if digits.count >= 2 && digits.count == fingerprintText.count && codeLengths.contains(digits.count) {
                return .integerCode
            }

            // 格式码为 @（文本格式）的纯数字，视为编码（如科目代码 101、10101）
            if let formatCode = cell.formatCode, formatCode == "@",
               digits.count >= 2 && digits.count == fingerprintText.count {
                return .integerCode
            }

            return .integerWide
        }

        // 中文字符
        if text.range(of: "\\p{Han}", options: .regularExpression) != nil {
            return .chineseText
        }

        // 英文字母
        if text.allSatisfy({ $0.isLetter || $0.isWhitespace }) {
            return .alphaText
        }

        return .mixed
    }
}

/// 邻居上下文
public struct NeighborContext {
    public let numericTendency: Double  // 0.0 ~ 1.0
    public let labelTendency: Double    // 0.0 ~ 1.0
    public let columnMetricTendency: Double // 0.0 ~ 1.0

    public init(
        numericTendency: Double = 0.0,
        labelTendency: Double = 0.0,
        columnMetricTendency: Double = 0.0
    ) {
        self.numericTendency = numericTendency
        self.labelTendency = labelTendency
        self.columnMetricTendency = columnMetricTendency
    }
}

/// 抽样工具
public func sampleFiles<T>(from items: [T]) -> [T] {
    let count = items.count
    if count <= 10 {
        return items
    } else if count <= 50 {
        var result: [T] = []
        result.append(contentsOf: items.prefix(3))
        let midStart = max(3, (count - 4) / 2)
        result.append(contentsOf: items[midStart..<min(midStart + 4, count - 3)])
        result.append(contentsOf: items.suffix(3))
        return Array(result.prefix(10))
    } else {
        var result: [T] = []
        result.append(contentsOf: items.prefix(5))
        let mid1 = count / 4
        result.append(contentsOf: items[mid1..<min(mid1 + 3, count)])
        let mid2 = count / 2
        result.append(contentsOf: items[mid2..<min(mid2 + 3, count)])
        let mid3 = count * 3 / 4
        result.append(contentsOf: items[mid3..<min(mid3 + 3, count)])
        result.append(contentsOf: items.suffix(4))
        return Array(result.prefix(15))
    }
}
