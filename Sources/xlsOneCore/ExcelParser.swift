import Foundation
import CoreXLSX

/// Excel 文件解析器
public actor ExcelParser {
    
    public init() {}
    
    /// 解析单个 Excel 文件
    public func parse(filePath: String) throws -> [[CellData]] {
        guard let file = XLSXFile(filepath: filePath) else {
            throw ParserError.fileNotFound
        }
        
        let workbooks = try file.parseWorkbooks()
        guard let workbook = workbooks.first else {
            throw ParserError.noWorksheet
        }
        
        // 默认取第一个工作表
        let worksheetInfo = try file.parseWorksheetPathsAndNames(workbook: workbook).first
        guard let (_, worksheetPath) = worksheetInfo else {
            throw ParserError.noWorksheet
        }
        guard let worksheet = try? file.parseWorksheet(at: worksheetPath) else {
            throw ParserError.noWorksheet
        }
        
        // 解析 shared strings（如果有）
        let sharedStrings = try? file.parseSharedStrings()
        
        return parseWorksheet(worksheet, sharedStrings: sharedStrings)
    }
    
    /// 解析所有工作表
    public func parseAllSheets(filePath: String) throws -> [String: [[CellData]]] {
        guard let file = XLSXFile(filepath: filePath) else {
            throw ParserError.fileNotFound
        }
        
        let workbooks = try file.parseWorkbooks()
        guard let workbook = workbooks.first else {
            throw ParserError.noWorksheet
        }
        
        let sharedStrings = try? file.parseSharedStrings()
        let sheets = try file.parseWorksheetPathsAndNames(workbook: workbook)
        
        var result: [String: [[CellData]]] = [:]
        
        for sheet in sheets {
            let name = sheet.name ?? "Sheet"
            let path = sheet.path
            if let worksheet = try? file.parseWorksheet(at: path) {
                result[name] = parseWorksheet(worksheet, sharedStrings: sharedStrings)
            }
        }
        
        return result
    }
    
    /// 获取单元格值
    private func getCellValue(cell: Cell, sharedStrings: SharedStrings?) -> String? {
        // 如果有共享字符串表，优先从那里获取
        if let strings = sharedStrings, let value = cell.value {
            if cell.type?.rawValue == "s" {
                // 这是一个字符串索引
                if let index = Int(value), index < strings.items.count {
                    return strings.items[index].text
                }
            }
        }
        
        // 直接使用值
        return cell.value
    }
    
    /// 将列引用转换为索引 (A=1, B=2, ..., Z=26, AA=27, ...)
    private func columnReferenceToInt(_ column: ColumnReference) -> Int {
        let value = column.value
        var result = 0
        
        for char in value.utf8 {
            result = result * 26 + Int(char - 64)  // 'A' = 65
        }
        
        return result
    }
    
    /// 解析多个 Excel 文件
    public func parseMultiple(filePaths: [String]) throws -> [[[CellData]]] {
        var allData: [[[CellData]]] = []
        
        for filePath in filePaths {
            let data = try parse(filePath: filePath)
            allData.append(data)
        }
        
        return allData
    }
    
    /// 识别数据类型
    private func recognizeDataType(rawValue: String?) -> CellDataType {
        guard let value = rawValue, !value.isEmpty else {
            return .empty
        }
        
        // 百分比
        if value.hasSuffix("%") {
            return .percentage
        }
        
        // 货币符号
        let currencySymbols = ["¥", "$", "€", "£", "₩", "₹"]
        for symbol in currencySymbols {
            if value.contains(symbol) {
                return .currency
            }
        }
        
        // 编码类型 (包含字母前缀 + 数字)
        if value.range(of: "^[A-Za-z]+-\\d+$", options: .regularExpression) != nil ||
           value.range(of: "^[A-Za-z]+\\d+$", options: .regularExpression) != nil ||
           value.range(of: "^订单\\d+$", options: .regularExpression) != nil ||
           value.range(of: "^(?i)SKU-?\\d+$", options: .regularExpression) != nil {
            return .code
        }
        
        // 纯数字
        if Double(value) != nil {
            return .numeric
        }
        
        return .text
    }
    
    /// 提取数值
    private func extractNumericValue(rawValue: String?, type: CellDataType) -> Double? {
        guard let value = rawValue else { return nil }
        
        switch type {
        case .numeric:
            return Double(value)
        case .currency:
            // 移除货币符号
            let cleaned = value.replacingOccurrences(of: "[^0-9.]", with: "", options: .regularExpression)
            return Double(cleaned)
        case .percentage:
            // 移除百分号并转换为小数
            let cleaned = value.replacingOccurrences(of: "%", with: "", options: .regularExpression)
            if let num = Double(cleaned) {
                return num / 100.0
            }
            return nil
        default:
            return nil
        }
    }
    
    /// 获取工作表名称列表
    public func getSheetNames(filePath: String) throws -> [String] {
        guard let file = XLSXFile(filepath: filePath) else {
            throw ParserError.fileNotFound
        }
        
        let workbooks = try file.parseWorkbooks()
        guard let workbook = workbooks.first else {
            throw ParserError.noWorksheet
        }
        
        return try file.parseWorksheetPathsAndNames(workbook: workbook).map { $0.name ?? "Sheet" }
    }
    
    private func parseWorksheet(_ worksheet: Worksheet, sharedStrings: SharedStrings?) -> [[CellData]] {
        // 暂存为稀疏结构，便于补齐空单元格
        var rowMaps: [Int: [Int: CellData]] = [:]
        var maxRowIndex = -1
        var maxColIndex = -1
        
        // 遍历所有行
        if let rows = worksheet.data?.rows {
            for row in rows {
                let rowIndex = Int(row.reference) - 1  // UInt to Int, 0-based
                maxRowIndex = max(maxRowIndex, rowIndex)
                
                var rowMap = rowMaps[rowIndex] ?? [:]
                
                for cell in row.cells {
                    let colIndex = columnReferenceToInt(cell.reference.column) - 1  // 0-based
                    maxColIndex = max(maxColIndex, colIndex)
                    let position = CellPosition(row: rowIndex, column: colIndex)
                    
                    // 获取原始值
                    let rawValue = getCellValue(cell: cell, sharedStrings: sharedStrings)
                    
                    // 识别数据类型
                    let cellType = recognizeDataType(rawValue: rawValue)
                    
                    // 提取数值
                    let numericValue = extractNumericValue(rawValue: rawValue, type: cellType)
                    
                    let cellData = CellData(
                        position: position,
                        rawValue: rawValue,
                        type: cellType,
                        numericValue: numericValue
                    )
                    
                    rowMap[colIndex] = cellData
                }
                
                if !rowMap.isEmpty {
                    rowMaps[rowIndex] = rowMap
                }
            }
        }
        
        // 组装为密集矩阵，补齐空单元格，保证位置对齐
        var result: [[CellData]] = []
        if maxRowIndex >= 0 && maxColIndex >= 0 {
            for rowIndex in 0...maxRowIndex {
                let rowMap = rowMaps[rowIndex] ?? [:]
                var rowData: [CellData] = []
                rowData.reserveCapacity(maxColIndex + 1)
                
                for colIndex in 0...maxColIndex {
                    if let cellData = rowMap[colIndex] {
                        rowData.append(cellData)
                    } else {
                        let position = CellPosition(row: rowIndex, column: colIndex)
                        let emptyCell = CellData(
                            position: position,
                            rawValue: nil,
                            type: .empty,
                            numericValue: nil
                        )
                        rowData.append(emptyCell)
                    }
                }
                
                result.append(rowData)
            }
        }
        
        return result
    }
}

/// 解析错误
public enum ParserError: Error, LocalizedError {
    case fileNotFound
    case noWorksheet
    case invalidData
    
    public var errorDescription: String? {
        switch self {
        case .fileNotFound:
            return "文件未找到"
        case .noWorksheet:
            return "没有工作表"
        case .invalidData:
            return "无效的数据"
        }
    }
}
