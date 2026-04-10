import Foundation
import CoreXLSX

/// Excel 文件解析器
public struct ExcelParser {

    public init() {}

    /// 解析单个Excel文件
    /// - Parameters:
    ///   - path: 文件路径
    /// - Returns: 解析后的Excel文件数据
    public func parseFile(at path: String) async throws -> ExcelFile {
        let url = URL(fileURLWithPath: path)
        let filename = url.lastPathComponent

        // 检查文件是否存在
        guard FileManager.default.fileExists(atPath: path) else {
            throw ParserError.fileNotFound(path)
        }

        // 解析XLSX文件
        guard let xlsxFile = XLSXFile(filepath: path) else {
            throw ParserError.cannotOpenFile(path)
        }

        // 获取所有工作表
        var sheets: [SheetData] = []

        // 解析共享字符串（如果有的话）
        let sharedStrings = try? xlsxFile.parseSharedStrings()

        // 获取工作簿信息
        let workbooks = try xlsxFile.parseWorkbooks()
        var sheetNames: [String: String] = [:]

        for workbook in workbooks {
            for sheet in workbook.sheets.items {
                // 使用id作为key，name作为value
                sheetNames[sheet.id] = sheet.name
            }
        }

        // 解析工作表路径
        let worksheetPaths = try xlsxFile.parseWorksheetPaths()

        for (index, sheetPath) in worksheetPaths.enumerated() {
            guard let worksheet = try? xlsxFile.parseWorksheet(at: sheetPath) else { continue }

            // 获取工作表名称 - 使用workbook中的顺序
            let name = sheetNames["\(index + 1)"] ?? "Sheet\(sheets.count + 1)"

            // 解析行数据
            var rows: [[CellData]] = []

            for row in worksheet.data?.rows ?? [] {
                var rowData: [CellData] = []

                for cell in row.cells {
                    let value = extractValue(from: cell, sharedStrings: sharedStrings)
                    rowData.append(CellData(value: value))
                }

                // 只添加非空行
                if !rowData.isEmpty {
                    rows.append(rowData)
                }
            }

            sheets.append(SheetData(name: name, rows: rows))
        }

        return ExcelFile(filename: filename, filepath: path, sheets: sheets)
    }

    /// 解析多个Excel文件
    public func parseFiles(at paths: [String]) async throws -> [ExcelFile] {
        var files: [ExcelFile] = []
        var errors: [(String, Error)] = []

        for path in paths {
            do {
                let file = try await parseFile(at: path)
                files.append(file)
            } catch {
                errors.append((path, error))
            }
        }

        // 如果所有文件都解析失败，抛出错误
        if files.isEmpty && !errors.isEmpty {
            throw ParserError.allFilesFailed(errors)
        }

        // 记录错误但不中断
        if !errors.isEmpty {
            print("Warning: Failed to parse \(errors.count) file(s):")
            for (path, error) in errors {
                print("  - \(path): \(error)")
            }
        }

        return files
    }

    /// 从单元格提取值
    private func extractValue(from cell: Cell, sharedStrings: SharedStrings?) -> String {
        // 优先使用字符串值
        if let sharedStrings = sharedStrings {
            if let stringValue = cell.stringValue(sharedStrings) {
                return stringValue
            }
        }

        // 检查是否是数值
        if let numericValue = cell.numericValue {
            // 保留原始格式，不自动转换
            return String(numericValue)
        }

        // 检查日期
        if let dateValue = cell.dateValue {
            let formatter = DateFormatter()
            formatter.dateStyle = .short
            return formatter.string(from: dateValue)
        }

        // 返回原始值
        return cell.value ?? ""
    }

    /// 获取所有工作表名称（从多个文件中合并）
    public func collectSheetNames(from files: [ExcelFile]) -> [String] {
        var names: Set<String> = []
        for file in files {
            for sheet in file.sheets {
                names.insert(sheet.name)
            }
        }
        return Array(names).sorted()
    }
}

/// 解析错误
public enum ParserError: Error, CustomStringConvertible {
    case fileNotFound(String)
    case cannotOpenFile(String)
    case invalidFormat(String)
    case allFilesFailed([(String, Error)])

    public var description: String {
        switch self {
        case .fileNotFound(let path):
            return "文件未找到: \(path)"
        case .cannotOpenFile(let path):
            return "无法打开文件: \(path)"
        case .invalidFormat(let reason):
            return "格式错误: \(reason)"
        case .allFilesFailed(let errors):
            return "所有文件解析失败:\n" + errors.map { "  - \($0.0): \($0.1)" }.joined(separator: "\n")
        }
    }
}

/// CoreXLSX Cell 扩展
private extension Cell {
    var numericValue: Double? {
        guard let value = value else { return nil }
        return Double(value)
    }

    var dateValue: Date? {
        // XLSX 中的日期是以天数存储的，从1899-12-30开始
        guard let numericValue = numericValue else { return nil }
        let date = Date(timeIntervalSinceReferenceDate: (numericValue - 10957) * 86400)
        return date
    }
}
