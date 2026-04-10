import Foundation

/// 表示一个单元格的数据
public struct CellData: Equatable, Sendable {
    public let value: String
    public let rawValue: String?

    public init(value: String, rawValue: String? = nil) {
        self.value = value.trimmingCharacters(in: .whitespacesAndNewlines)
        self.rawValue = rawValue
    }

    /// 判断是否可能是数值（金额）
    /// 科目代码（如201、301）不应被视为金额
    public var isNumeric: Bool {
        return numericValue != nil
    }

    /// 解析数值（支持千分位和欧式格式）
    /// 返回 nil 如果值看起来像科目代码（3位纯数字）
    public var numericValue: Double? {
        let trimmed = value.trimmingCharacters(in: .whitespacesAndNewlines)

        // 排除纯3位数字（如201、301等科目代码）
        // 但保留有千分位、小数点或超过3位的数字
        if trimmed.count == 3,
           trimmed.allSatisfy({ $0.isNumber }) {
            return nil
        }

        return Self.parseNumber(trimmed)
    }

    private static func parseNumber(_ text: String) -> Double? {
        let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)

        // 如果为空，返回 nil
        if trimmed.isEmpty {
            return nil
        }

        // 处理负数（括号格式）
        var workingText = trimmed
        var isNegative = false

        if trimmed.hasPrefix("(") && trimmed.hasSuffix(")") {
            workingText = String(trimmed.dropFirst().dropLast())
            isNegative = true
        } else if trimmed.hasPrefix("-") {
            workingText = String(trimmed.dropFirst())
            isNegative = true
        }

        // 检测格式：判断最后一个小数点或逗号的位置
        let lastComma = workingText.lastIndex(of: ",")
        let lastPeriod = workingText.lastIndex(of: ".")

        var normalizedText: String

        if let lastComma = lastComma, lastPeriod != nil {
            // 同时包含逗号和点，根据位置判断哪个是小数分隔符
            if workingText.distance(from: lastComma, to: workingText.endIndex) <= 3 {
                // 逗号后面只有1-2位数字，欧式格式 (1.234,56)
                normalizedText = workingText
                    .replacingOccurrences(of: ".", with: "")
                    .replacingOccurrences(of: ",", with: ".")
            } else {
                // 美式格式 (1,234.56)
                normalizedText = workingText.replacingOccurrences(of: ",", with: "")
            }
        } else if let lastComma = lastComma {
            // 只有逗号，检查是否是千分位
            let charsAfterComma = workingText.distance(from: lastComma, to: workingText.endIndex)
            if charsAfterComma == 3 {
                // 可能是欧式小数 (123,45)
                normalizedText = workingText.replacingOccurrences(of: ",", with: ".")
            } else {
                // 千分位分隔符，直接移除
                normalizedText = workingText.replacingOccurrences(of: ",", with: "")
            }
        } else {
            // 只有数字和可能的点（美式小数）
            normalizedText = workingText
        }

        // 尝试解析
        if let number = Double(normalizedText) {
            return isNegative ? -number : number
        }

        return nil
    }
}

/// 聚合后的单元格数据
public struct MergedCell: Equatable, Sendable {
    public enum CellType: Equatable, Sendable {
        case label           // 标签型（所有值相同）
        case sum(Double)     // 可聚合，显示总和
        case mixed(Int)      // 混合类型，显示 "X条"
        case single(String)  // 只有一个文件
    }

    public let type: CellType
    public let displayValue: String
    public let sourceValues: [String: String]  // 文件名 -> 原始值

    public init(type: CellType, sourceValues: [String: String] = [:]) {
        self.type = type
        self.sourceValues = sourceValues

        switch type {
        case .label:
            self.displayValue = sourceValues.values.first ?? ""
        case .sum(let total):
            // 格式化数值，保留两位小数，移除末尾的0
            if total == floor(total) {
                self.displayValue = String(format: "%.0f", total)
            } else {
                self.displayValue = String(format: "%.2f", total)
                    .replacingOccurrences(of: "\\.00$", with: "", options: .regularExpression)
                    .replacingOccurrences(of: "(\\d)0+$", with: "$1", options: .regularExpression)
            }
        case .mixed(let count):
            self.displayValue = "\(count)条"
        case .single(let value):
            self.displayValue = value
        }
    }

    /// 从多个单元格创建聚合单元格
    public static func from(cells: [(filename: String, cell: CellData?)]) -> MergedCell {
        // 过滤掉空值
        let validCells = cells.compactMap { tuple -> (String, CellData)? in
            guard let cell = tuple.cell, !cell.value.isEmpty else { return nil }
            return (tuple.filename, cell)
        }

        // 如果只有一个文件，直接显示
        if validCells.count == 1, let (_, cell) = validCells.first {
            return MergedCell(
                type: .single(cell.value),
                sourceValues: [validCells[0].0: cell.value]
            )
        }

        // 如果所有值都相同
        let allValues = validCells.map { $0.1.value }
        if Set(allValues).count == 1 {
            let sourceMap = Dictionary(uniqueKeysWithValues: validCells.map { ($0.0, $0.1.value) })
            return MergedCell(type: .label, sourceValues: sourceMap)
        }

        // 检查是否都是数值且不同
        let numericValues = validCells.compactMap { tuple -> (String, Double)? in
            guard let num = tuple.1.numericValue else { return nil }
            return (tuple.0, num)
        }

        // 如果都是数值，求和
        if numericValues.count == validCells.count {
            let total = numericValues.map { $0.1 }.reduce(0, +)
            let sourceMap = Dictionary(uniqueKeysWithValues: validCells.map { ($0.0, $0.1.value) })
            return MergedCell(type: .sum(total), sourceValues: sourceMap)
        }

        // 混合类型，显示条数
        let uniqueValues = Set(allValues)
        let sourceMap = Dictionary(uniqueKeysWithValues: validCells.map { ($0.0, $0.1.value) })
        return MergedCell(type: .mixed(uniqueValues.count), sourceValues: sourceMap)
    }
}

/// 表示一个工作表的数据
public struct SheetData: Sendable {
    public let name: String
    public let rows: [[CellData]]

    public init(name: String, rows: [[CellData]]) {
        self.name = name
        self.rows = rows
    }

    /// 获取单元格（行、列从0开始）
    public func cellAt(row: Int, col: Int) -> CellData? {
        guard row >= 0, row < rows.count else { return nil }
        let rowData = rows[row]
        guard col >= 0, col < rowData.count else { return nil }
        return rowData[col]
    }
}

/// 表示一个Excel文件的数据
public struct ExcelFile: Sendable {
    public let filename: String
    public let filepath: String
    public let sheets: [SheetData]

    public init(filename: String, filepath: String, sheets: [SheetData]) {
        self.filename = filename
        self.filepath = filepath
        self.sheets = sheets
    }
}

/// 汇总结果
public struct MergedResult: Sendable {
    public let sheetName: String
    public let rows: [[MergedCell]]
    public let sourceFiles: [String]

    public init(sheetName: String, rows: [[MergedCell]], sourceFiles: [String]) {
        self.sheetName = sheetName
        self.rows = rows
        self.sourceFiles = sourceFiles
    }

    /// 获取行数和列数
    public var dimensions: (rows: Int, cols: Int) {
        let maxRow = rows.count
        let maxCol = rows.map { $0.count }.max() ?? 0
        return (maxRow, maxCol)
    }
}
