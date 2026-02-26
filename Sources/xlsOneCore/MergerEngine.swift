import Foundation

/// 汇总策略配置
public struct MergeStrategy: Codable, Sendable {
    public var numericStrategy: NumericMergeStrategy
    public var textStrategy: TextMergeStrategy
    public var codeStrategy: CodeMergeStrategy
    public var emptyAsZero: Bool
    
    public init(
        numericStrategy: NumericMergeStrategy = .sum,
        textStrategy: TextMergeStrategy = .majority,
        codeStrategy: CodeMergeStrategy = .placeholder,
        emptyAsZero: Bool = false
    ) {
        self.numericStrategy = numericStrategy
        self.textStrategy = textStrategy
        self.codeStrategy = codeStrategy
        self.emptyAsZero = emptyAsZero
    }
}

/// 数值汇总策略
public enum NumericMergeStrategy: String, Codable, Sendable {
    case sum      // 相加
    case average  // 平均值
    case max      // 最大值
    case min      // 最小值
    case first    // 保留第一个
    case last     // 保留最后一个
}

/// 文本汇总策略
public enum TextMergeStrategy: String, Codable, Sendable {
    case majority   // 取多数
    case first      // 保留第一个
    case last       // 保留最后一个
    case concat     // 拼接
    case empty      // 空
}

/// 编码汇总策略
public enum CodeMergeStrategy: String, Codable, Sendable {
    case placeholder  // 生成占位符 (订单001 + 订单002 → 订单000)
    case first        // 保留第一个
    case last         // 保留最后一个
    case empty        // 空
}

/// Excel 汇总引擎
public struct MergerEngine {
    public let strategy: MergeStrategy
    
    public init(strategy: MergeStrategy = MergeStrategy()) {
        self.strategy = strategy
    }
    
    /// 汇总多个表格
    public func merge(dataSets: [[[CellData]]]) -> (cells: [[MergedCell]], statistics: MergeStatistics) {
        // 防御性检查：过滤空数据集
        let validDataSets = dataSets.filter { !$0.isEmpty }
        
        guard !validDataSets.isEmpty else {
            return (cells: [], statistics: MergeStatistics(
                totalFiles: validDataSets.count, totalCells: 0,
                numericCells: 0, textCells: 0,
                codeCells: 0, emptyCells: 0, conflictCells: 0
            ))
        }
        
        guard !validDataSets.isEmpty else {
            return (cells: [], statistics: MergeStatistics(
                totalFiles: 0, totalCells: 0,
                numericCells: 0, textCells: 0,
                codeCells: 0, emptyCells: 0, conflictCells: 0
            ))
        }
        
        // 验证所有数据集结构一致
        guard validateStructure(validDataSets) else {
            // 结构不一致时返回空结果而不是崩溃
            return (cells: [], statistics: MergeStatistics(
                totalFiles: validDataSets.count, totalCells: 0,
                numericCells: 0, textCells: 0,
                codeCells: 0, emptyCells: 0, conflictCells: 0
            ))
        }
        
        var mergedCells: [[MergedCell]] = []
        var stats = MergeStatistics(
            totalFiles: validDataSets.count,
            totalCells: 0,
            numericCells: 0,
            textCells: 0,
            codeCells: 0,
            emptyCells: 0,
            conflictCells: 0
        )
        
        // 获取最大行数和列数
        let maxRows = validDataSets.map { $0.count }.max() ?? 0
        let maxCols = validDataSets.flatMap { $0.map { $0.count } }.max() ?? 0
        
        for row in 0..<maxRows {
            var mergedRow: [MergedCell] = []
            
            for col in 0..<maxCols {
                let position = CellPosition(row: row, column: col)
                
                // 收集该位置的所有数据
                var cellDataList: [CellData] = []
                for dataSet in dataSets {
                    if row < dataSet.count && col < dataSet[row].count {
                        cellDataList.append(dataSet[row][col])
                    }
                }
                
                // 汇总该单元格
                let mergedCell = mergeCell(data: cellDataList, position: position)
                mergedRow.append(mergedCell)
                
                // 统计
                stats.totalCells += 1
                switch mergedCell.conflictMarkers {
                case let markers where markers.hasNumeric: stats.numericCells += 1
                case let markers where markers.hasText: stats.textCells += 1
                case let markers where markers.hasCode: stats.codeCells += 1
                case let markers where markers.hasEmpty: stats.emptyCells += 1
                default: break
                }
            }
            
            mergedCells.append(mergedRow)
        }
        
        return (mergedCells, stats)
    }
    
    /// 汇总单个单元格
    private func mergeCell(data: [CellData], position: CellPosition) -> MergedCell {
        let nonEmpty = data.filter { !$0.isEmpty }
        let empty = data.filter { $0.isEmpty }
        
        let hasNumeric = nonEmpty.contains { $0.type == .numeric || $0.type == .currency || $0.type == .percentage }
        let hasText = nonEmpty.contains { $0.type == .text }
        let hasCode = nonEmpty.contains { $0.type == .code }
        let hasEmpty = !empty.isEmpty
        
        let conflictMarkers = MergedCell.ConflictMarkers(
            hasNumeric: hasNumeric,
            hasText: hasText,
            hasCode: hasCode,
            hasEmpty: hasEmpty
        )
        
        // 如果全为空
        if nonEmpty.isEmpty {
            return MergedCell(
                position: position,
                resultType: .empty,
                resultValue: nil,
                numericValue: nil,
                sourceValues: [],
                conflictMarkers: conflictMarkers
            )
        }
        
        // 如果只有数值
        if !hasNumeric, !hasText, !hasCode {
            return mergeNumeric(data: nonEmpty, position: position, conflictMarkers: conflictMarkers)
        }
        
        // 如果只有编码
        if hasCode && !hasNumeric && !hasText {
            return mergeCode(data: nonEmpty, position: position, conflictMarkers: conflictMarkers)
        }
        
        // 如果只有文本
        if hasText && !hasNumeric && !hasCode {
            return mergeText(data: nonEmpty, position: position, conflictMarkers: conflictMarkers)
        }
        
        // 混合类型：数值 + 文本/编码
        return mergeMixed(data: nonEmpty, position: position, conflictMarkers: conflictMarkers)
    }
    
    /// 汇总数值
    private func mergeNumeric(data: [CellData], position: CellPosition, conflictMarkers: MergedCell.ConflictMarkers) -> MergedCell {
        let values = data.compactMap { $0.numericValue }
        let rawValues = data.map { $0.rawValue ?? "" }
        
        var resultValue: String
        var numericValue: Double?
        
        switch strategy.numericStrategy {
        case .sum:
            numericValue = values.reduce(0, +)
            resultValue = String(format: "%.2f", numericValue!)
            
        case .average:
            numericValue = values.reduce(0, +) / Double(values.count)
            resultValue = String(format: "%.2f", numericValue!)
            
        case .max:
            numericValue = values.max()
            resultValue = String(format: "%.2f", numericValue!)
            
        case .min:
            numericValue = values.min()
            resultValue = String(format: "%.2f", numericValue!)
            
        case .first:
            numericValue = data.first?.numericValue
            resultValue = data.first?.rawValue ?? ""
            
        case .last:
            numericValue = data.last?.numericValue
            resultValue = data.last?.rawValue ?? ""
        }
        
        return MergedCell(
            position: position,
            resultType: .numericSum,
            resultValue: resultValue,
            numericValue: numericValue,
            sourceValues: rawValues,
            conflictMarkers: conflictMarkers
        )
    }
    
    /// 汇总编码
    private func mergeCode(data: [CellData], position: CellPosition, conflictMarkers: MergedCell.ConflictMarkers) -> MergedCell {
        let rawValues = data.map { $0.rawValue ?? "" }
        
        let resultValue: String
        let resultType: MergeResultType
        
        switch strategy.codeStrategy {
        case .placeholder:
            // 生成占位符 (订单001 + 订单002 → 订单000)
            resultValue = generatePlaceholder(values: rawValues)
            resultType = .codePlaceholder
            
        case .first:
            resultValue = rawValues.first ?? ""
            resultType = .firstValue
            
        case .last:
            resultValue = rawValues.last ?? ""
            resultType = .lastValue
            
        case .empty:
            resultValue = ""
            resultType = .empty
        }
        
        return MergedCell(
            position: position,
            resultType: resultType,
            resultValue: resultValue,
            numericValue: nil,
            sourceValues: rawValues,
            conflictMarkers: conflictMarkers
        )
    }
    
    /// 汇总文本
    private func mergeText(data: [CellData], position: CellPosition, conflictMarkers: MergedCell.ConflictMarkers) -> MergedCell {
        let rawValues = data.map { $0.rawValue ?? "" }
        
        let resultValue: String
        let resultType: MergeResultType
        
        switch strategy.textStrategy {
        case .majority:
            // 取多数
            let counts = Dictionary(grouping: rawValues, by: { $0 }).mapValues { $0.count }
            if let majority = counts.max(by: { $0.value < $1.value })?.key {
                resultValue = majority
                resultType = .textMajority
            } else {
                resultValue = rawValues.first ?? ""
                resultType = .firstValue
            }
            
        case .first:
            resultValue = rawValues.first ?? ""
            resultType = .firstValue
            
        case .last:
            resultValue = rawValues.last ?? ""
            resultType = .lastValue
            
        case .concat:
            resultValue = rawValues.joined(separator: ", ")
            resultType = .textConcat
            
        case .empty:
            resultValue = ""
            resultType = .empty
        }
        
        return MergedCell(
            position: position,
            resultType: resultType,
            resultValue: resultValue,
            numericValue: nil,
            sourceValues: rawValues,
            conflictMarkers: conflictMarkers
        )
    }
    
    /// 汇总混合类型
    private func mergeMixed(data: [CellData], position: CellPosition, conflictMarkers: MergedCell.ConflictMarkers) -> MergedCell {
        // 混合类型：数值 + 文本/编码
        // 默认策略：只汇总数值，文本/编码取第一个
        let numericData = data.filter { $0.type == .numeric || $0.type == .currency || $0.type == .percentage }
        let textData = data.filter { $0.type == .text || $0.type == .code }
        
        let numericValues = numericData.compactMap { $0.numericValue }
        let rawValues = data.map { $0.rawValue ?? "" }
        
        var resultValue: String
        var numericValue: Double?
        
        // 汇总数值
        if !numericValues.isEmpty {
            numericValue = numericValues.reduce(0, +)
            resultValue = String(format: "%.2f", numericValue!)
        } else {
            numericValue = nil
            resultValue = textData.first?.rawValue ?? ""
        }
        
        return MergedCell(
            position: position,
            resultType: .numericSum,
            resultValue: resultValue,
            numericValue: numericValue,
            sourceValues: rawValues,
            conflictMarkers: conflictMarkers
        )
    }
    
    /// 生成编码占位符
    private func generatePlaceholder(values: [String]) -> String {
        guard !values.isEmpty else { return "" }
        
        // 提取前缀和数字部分
        var prefix = ""
        var numbers: [Int] = []
        
        for value in values {
            let numberPart = value.replacingOccurrences(of: "[^0-9]", with: "", options: .regularExpression)
            let letterPart = value.replacingOccurrences(of: "[0-9]", with: "", options: .regularExpression)
            
            prefix = letterPart
            if let num = Int(numberPart) {
                numbers.append(num)
            }
        }
        
        // 生成占位符
        let sum = numbers.reduce(0, +)
        let placeholderNum = String(format: "%03d", sum)
        
        return "\(prefix)\(placeholderNum)"
    }
    
    /// 验证数据结构一致性
    private func validateStructure(_ dataSets: [[[CellData]]]) -> Bool {
        guard dataSets.count > 1 else { return true }
        
        let referenceRows = dataSets[0].count
        let referenceCols = dataSets[0].map { $0.count }.max() ?? 0
        
        for dataSet in dataSets.dropFirst() {
            if dataSet.count != referenceRows {
                return false
            }
            
            for row in dataSet {
                if row.count != referenceCols {
                    return false
                }
            }
        }
        
        return true
    }
}
