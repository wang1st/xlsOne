import Foundation

/// 单元格位置
public struct CellPosition: Hashable, Codable, Sendable {
    public let row: Int
    public let column: Int
    
    public init(row: Int, column: Int) {
        self.row = row
        self.column = column
    }
    
    public var a1Notation: String {
        let columnLetter = columnLetter()
        return "\(columnLetter)\(row + 1)"
    }
    
    private func columnLetter() -> String {
        var col = column
        var letters = ""
        while col > 0 {
            col -= 1
            let letter = Unicode.Scalar(UInt32(65 + col % 26))!
            letters = String(letter) + letters
            col /= 26
        }
        return letters
    }
}

/// 单元格数据类型
public enum CellDataType: String, Codable, Sendable {
    case numeric      // 纯数字
    case currency     // 货币
    case percentage   // 百分比
    case text         // 文本
    case code         // 编码类型
    case empty        // 空
    case mixed        // 混合类型
}

/// 单元格数据
public struct CellData: Hashable, Codable, Sendable {
    public let position: CellPosition
    public let rawValue: String?
    public let type: CellDataType
    public let numericValue: Double?
    
    public init(position: CellPosition, rawValue: String?, type: CellDataType, numericValue: Double?) {
        self.position = position
        self.rawValue = rawValue
        self.type = type
        self.numericValue = numericValue
    }
    
    public var isEmpty: Bool {
        rawValue == nil || rawValue?.isEmpty == true
    }
}

/// 汇总结果类型
public enum MergeResultType: String, Codable, Sendable {
    case numericSum      // 数值汇总
    case codePlaceholder // 编码占位符
    case textMajority   // 文本取多数
    case textConcat     // 文本拼接
    case firstValue     // 保留第一个
    case lastValue      // 保留最后一个
    case empty          // 空
    case conflict       // 冲突需审核
}

/// 单元格汇总结果
public struct MergedCell: Hashable, Codable, Sendable {
    public let position: CellPosition
    public let resultType: MergeResultType
    public let resultValue: String?
    public let numericValue: Double?
    public let sourceValues: [String]  // 原始值列表
    public let conflictMarkers: ConflictMarkers
    
    public struct ConflictMarkers: Hashable, Codable, Sendable {
        public let hasNumeric: Bool
        public let hasText: Bool
        public let hasCode: Bool
        public let hasEmpty: Bool
        
        public var statusColor: String {
            if hasNumeric && !hasText && !hasCode { return "green" }
            if hasText || hasCode { return "yellow" }
            if conflict { return "red" }
            return "gray"
        }
        
        public var conflict: Bool {
            // 多个非空值且类型不一致
            return false
        }
    }
    
    public init(
        position: CellPosition,
        resultType: MergeResultType,
        resultValue: String?,
        numericValue: Double?,
        sourceValues: [String],
        conflictMarkers: ConflictMarkers
    ) {
        self.position = position
        self.resultType = resultType
        self.resultValue = resultValue
        self.numericValue = numericValue
        self.sourceValues = sourceValues
        self.conflictMarkers = conflictMarkers
    }
}

/// 汇总统计信息
public struct MergeStatistics: Codable, Sendable {
    public let totalFiles: Int
    public var totalCells: Int
    public var numericCells: Int
    public var textCells: Int
    public var codeCells: Int
    public var emptyCells: Int
    public var conflictCells: Int
    
    public var summary: String {
        """
        文件数量: \(totalFiles)
        总单元格: \(totalCells)
        数值单元格: \(numericCells)
        文本单元格: \(textCells)
        编码单元格: \(codeCells)
        空单元格: \(emptyCells)
        冲突单元格: \(conflictCells)
        """
    }
}
