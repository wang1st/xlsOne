import Foundation
import CoreXLSX
import ZIPFoundation

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

        // 获取工作簿信息和关系映射
        let workbooks = try xlsxFile.parseWorkbooks()
        let relationships = try parseWorkbookRelationships(from: path)

        // 构建 sheetId 到 sheet 名称的映射
        // CoreXLSX 的 sheet.id 实际上是 sheetId (如 "1", "2", "3")
        var sheetNameById: [String: String] = [:]
        for workbook in workbooks {
            for sheet in workbook.sheets.items {
                sheetNameById[sheet.id] = sheet.name
            }
        }

        // 构建 r:id 到 sheetId 的映射（通过解析 workbook.xml 的 r:id 属性）
        // 我们需要从原始 XML 中提取 r:id
        let ridToSheetId = try parseRIdToSheetId(from: path, workbooks: workbooks)

        // 构建 worksheet 路径到 sheet 名称的映射
        var sheetNameByPath: [String: String] = [:]
        for (rId, targetPath) in relationships {
            if let sheetId = ridToSheetId[rId],
               let sheetName = sheetNameById[sheetId] {
                let fullPath = "xl/\(targetPath)"
                sheetNameByPath[fullPath] = sheetName
            }
        }

        // 解析工作表路径
        let worksheetPaths = try xlsxFile.parseWorksheetPaths()
        print("DEBUG: worksheetPaths = \(worksheetPaths)")

        for sheetPath in worksheetPaths {
            guard let worksheet = try? xlsxFile.parseWorksheet(at: sheetPath) else { continue }

            // 获取工作表名称 - 通过路径查找
            let name = sheetNameByPath[sheetPath] ?? extractSheetNameFromPath(sheetPath)

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

        // 按原始顺序排序 sheets（根据 sheet 名称中的数字或原始索引）
        sheets = sortSheetsByOriginalOrder(sheets: sheets, workbooks: workbooks)

        return ExcelFile(filename: filename, filepath: path, sheets: sheets)
    }

    /// 解析 workbook.xml 获取 r:id 到 sheetId 的映射
    private func parseRIdToSheetId(from xlsxPath: String, workbooks: [Workbook]) throws -> [String: String] {
        let archive = try Archive(url: URL(fileURLWithPath: xlsxPath), accessMode: .read)

        guard let entry = archive["xl/workbook.xml"] else {
            return [:]
        }

        var data = Data()
        _ = try archive.extract(entry) { chunk in
            data.append(chunk)
        }

        let xmlString = String(data: data, encoding: .utf8) ?? ""

        // 使用正则表达式提取 sheet 的 sheetId 和 r:id
        var ridToSheetId: [String: String] = [:]

        // 匹配 <sheet 标签，提取 sheetId 和 r:id
        // 格式: <sheet name="..." sheetId="1" r:id="rId3" ...>
        let sheetPattern = #"<sheet[^>]+sheetId=\"(\d+)\"[^>]*r:id=\"([^\"]+)\"|"#
            + #"<sheet[^>]+r:id=\"([^\"]+)\"[^>]*sheetId=\"(\d+)\""#

        if let regex = try? NSRegularExpression(pattern: sheetPattern, options: [.dotMatchesLineSeparators]) {
            let range = NSRange(xmlString.startIndex..., in: xmlString)
            let matches = regex.matches(in: xmlString, options: [], range: range)

            for match in matches {
                // 检查哪种模式匹配
                if let sheetIdRange = Range(match.range(at: 1), in: xmlString),
                   let rIdRange = Range(match.range(at: 2), in: xmlString) {
                    // 第一种模式: sheetId 在前
                    let sheetId = String(xmlString[sheetIdRange])
                    let rId = String(xmlString[rIdRange])
                    ridToSheetId[rId] = sheetId
                } else if let rIdRange = Range(match.range(at: 3), in: xmlString),
                          let sheetIdRange = Range(match.range(at: 4), in: xmlString) {
                    // 第二种模式: r:id 在前
                    let rId = String(xmlString[rIdRange])
                    let sheetId = String(xmlString[sheetIdRange])
                    ridToSheetId[rId] = sheetId
                }
            }
        }

        return ridToSheetId
    }

    /// 解析 workbook.xml.rels 文件获取关系映射
    private func parseWorkbookRelationships(from xlsxPath: String) throws -> [(rId: String, target: String)] {
        let archive = try Archive(url: URL(fileURLWithPath: xlsxPath), accessMode: .read)

        guard let entry = archive["xl/_rels/workbook.xml.rels"] else {
            return []
        }

        var data = Data()
        _ = try archive.extract(entry) { chunk in
            data.append(chunk)
        }

        let xmlString = String(data: data, encoding: .utf8) ?? ""

        // 简单解析 XML
        var relationships: [(String, String)] = []

        // 使用正则表达式提取 Relationship
        let pattern = #"<Relationship[^>]+Id="([^"]+)"[^>]+Target="([^"]+)""#
        if let regex = try? NSRegularExpression(pattern: pattern, options: []) {
            let range = NSRange(xmlString.startIndex..., in: xmlString)
            let matches = regex.matches(in: xmlString, options: [], range: range)

            for match in matches {
                if let idRange = Range(match.range(at: 1), in: xmlString),
                   let targetRange = Range(match.range(at: 2), in: xmlString) {
                    let rId = String(xmlString[idRange])
                    let target = String(xmlString[targetRange])
                    relationships.append((rId, target))
                }
            }
        }

        return relationships
    }

    /// 从路径提取 sheet 名称（备用方法）
    private func extractSheetNameFromPath(_ path: String) -> String {
        // 从 "xl/worksheets/sheet1.xml" 提取 "sheet1"
        let components = path.components(separatedBy: "/")
        if let last = components.last {
            return last.replacingOccurrences(of: ".xml", with: "")
        }
        return path
    }

    /// 按原始顺序排序 sheets
    private func sortSheetsByOriginalOrder(
        sheets: [SheetData],
        workbooks: [Workbook]
    ) -> [SheetData] {
        // 构建 sheet 名称到原始顺序的映射（通过 sheetId）
        var orderByName: [String: Int] = [:]

        for (index, sheet) in workbooks.first?.sheets.items.enumerated() ?? [].enumerated() {
            if let name = sheet.name {
                orderByName[name] = index
            }
        }

        // 按原始顺序排序
        return sheets.sorted { sheet1, sheet2 in
            let order1 = orderByName[sheet1.name] ?? Int.max
            let order2 = orderByName[sheet2.name] ?? Int.max
            return order1 < order2
        }
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
