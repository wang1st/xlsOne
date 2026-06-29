import Foundation
import CoreXLSX
import ZIPFoundation

public struct ExcelParseFailure: Sendable, Equatable {
    public let path: String
    public let message: String

    public init(path: String, message: String) {
        self.path = path
        self.message = message
    }
}

public struct ExcelParseBatchResult: Sendable {
    public let files: [ExcelFile]
    public let failures: [ExcelParseFailure]

    public init(files: [ExcelFile], failures: [ExcelParseFailure]) {
        self.files = files
        self.failures = failures
    }
}

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
        let fileExtension = url.pathExtension.lowercased()

        // 检查文件是否存在
        guard FileManager.default.fileExists(atPath: path) else {
            throw ParserError.fileNotFound(path)
        }

        if fileExtension == "xls" {
            return try BIFF8XLSParser().parseFile(at: path)
        }

        guard fileExtension == "xlsx" else {
            throw ParserError.unsupportedFileExtension(fileExtension.isEmpty ? filename : ".\(fileExtension)")
        }

        // 解析XLSX文件
        guard let xlsxFile = XLSXFile(filepath: path) else {
            throw ParserError.cannotOpenFile(path)
        }

        // 获取所有工作表
        var sheets: [SheetData] = []

        // 解析共享字符串（如果有的话）
        let sharedStrings = try? xlsxFile.parseSharedStrings()

        // 解析样式（用于日期格式识别）
        var stylesParser = ExcelStylesParser()
        try? stylesParser.parseStyles(from: path)

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

        for sheetPath in worksheetPaths {
            guard let worksheet = try? xlsxFile.parseWorksheet(at: sheetPath) else { continue }

            // 获取工作表名称 - 通过路径查找
            let name = sheetNameByPath[sheetPath] ?? extractSheetNameFromPath(sheetPath)

            // 解析行数据（使用 row.reference 对齐实际行号）
            var rows: [[CellData]] = []

            for row in worksheet.data?.rows ?? [] {
                let rowIndex = Int(row.reference) - 1 // 0-based

                // 确保 rows 数组长度足够
                while rows.count <= rowIndex {
                    rows.append([])
                }

                var rowData: [CellData] = []

                for cell in row.cells {
                    let targetCol = columnIndex(from: cell.reference.column.value) // 0-based
                    // 填充缺失的列（处理合并单元格或空列导致的缺失）
                    while rowData.count < targetCol {
                        rowData.append(CellData(value: ""))
                    }
                    let cellData = extractCellData(from: cell, sharedStrings: sharedStrings, stylesParser: stylesParser)
                    rowData.append(cellData)
                }

                rows[rowIndex] = rowData
            }

            // 统一所有行的列数，用空单元格补齐
            let maxCols = rows.map { $0.count }.max() ?? 0
            for i in 0..<rows.count {
                while rows[i].count < maxCols {
                    rows[i].append(CellData(value: ""))
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
        let batch = await parseFilesWithDiagnostics(at: paths)
        if batch.files.isEmpty && !batch.failures.isEmpty {
            throw ParserError.allFilesFailed(
                batch.failures.map { ($0.path, ParserError.invalidFormat($0.message)) }
            )
        }
        return batch.files
    }

    /// 解析多个 Excel 文件并返回诊断信息
    public func parseFilesWithDiagnostics(at paths: [String]) async -> ExcelParseBatchResult {
        var files: [ExcelFile] = []
        var errors: [ExcelParseFailure] = []

        for path in paths {
            do {
                let file = try await parseFile(at: path)
                files.append(file)
            } catch {
                errors.append(
                    ExcelParseFailure(
                        path: path,
                        message: error.localizedDescription
                    )
                )
            }
        }

        return ExcelParseBatchResult(files: files, failures: errors)
    }

    /// 将 Excel 列引用转换为 0-based 列索引
    private func columnIndex(from reference: String) -> Int {
        var result = 0
        for char in reference.uppercased() {
            guard let scalar = char.unicodeScalars.first else { continue }
            let value = Int(scalar.value) - Int(Character("A").unicodeScalars.first!.value) + 1
            result = result * 26 + value
        }
        return result - 1 // 0-based
    }

    /// 解析合并单元格引用，如 "A4:B4" → (startRow: 3, startCol: 0, endRow: 3, endCol: 1)
    private func parseMergeCellReference(_ reference: String) -> (startRow: Int, startCol: Int, endRow: Int, endCol: Int)? {
        let parts = reference.split(separator: ":")
        guard parts.count == 2 else { return nil }

        guard let start = parseCellCoordinate(String(parts[0])),
              let end = parseCellCoordinate(String(parts[1])) else { return nil }

        return (startRow: start.row, startCol: start.col, endRow: end.row, endCol: end.col)
    }

    /// 解析单元格坐标，如 "A4" → (row: 3, col: 0)
    private func parseCellCoordinate(_ coordinate: String) -> (row: Int, col: Int)? {
        let chars = Array(coordinate)
        var colStr = ""
        var rowStr = ""

        for char in chars {
            if char.isLetter {
                colStr.append(String(char).uppercased())
            } else if char.isNumber {
                rowStr.append(char)
            }
        }

        guard !colStr.isEmpty, !rowStr.isEmpty,
              let row = Int(rowStr), row > 0 else { return nil }

        let col = columnIndex(from: colStr) // 0-based
        return (row: row - 1, col: col)
    }

    /// 从单元格提取完整数据（包含值和格式信息）
    private func extractCellData(from cell: Cell, sharedStrings: SharedStrings?, stylesParser: ExcelStylesParser?) -> CellData {
        let styleIndex = cell.styleIndex
        let rawValue = cell.value
        let isDate = stylesParser?.isDateFormat(styleIndex: styleIndex) ?? false
        let formatCode = stylesParser?.getFormatCode(styleIndex: styleIndex)

        // 如果是共享字符串类型
        if cell.type == .sharedString, let sharedStrings = sharedStrings {
            if let stringValue = cell.stringValue(sharedStrings) {
                return CellData(
                    value: stringValue,
                    rawValue: rawValue,
                    numericValue: nil,
                    formatCode: formatCode,
                    isDate: false
                )
            }
        }

        // 如果是数值类型（包括日期和普通数字）
        if cell.type == .number || cell.type == nil {
            if let numericValue = cell.numericValue {
                // 检查是否是日期格式
                if isDate, let dateString = stylesParser?.formatDate(numericValue, styleIndex: styleIndex) {
                    return CellData(
                        value: dateString,
                        rawValue: rawValue,
                        numericValue: numericValue,
                        formatCode: formatCode,
                        isDate: true
                    )
                }
                // 不是日期，优先使用 Excel 原始存储值作为显示文本
                // 避免 Double 转 String 产生的 .0 后缀（如 331024000 -> 331024000.0）
                var displayValue: String
                if let rawValue = rawValue, !rawValue.isEmpty {
                    // 科学计数法需要重新格式化（如 3.31024105E8 -> 331024105）
                    let isScientific = rawValue.range(of: "[eE][+-]?\\d+", options: .regularExpression) != nil
                    if isScientific {
                        displayValue = String(format: "%.0f", numericValue)
                    } else {
                        displayValue = rawValue
                    }
                } else {
                    displayValue = String(numericValue)
                }

                // 根据格式码修正显示：纯整数格式（不含小数点、货币、百分比）去掉无意义的 .0 后缀
                if let formatCode = formatCode {
                    let isIntegerFormat = !formatCode.contains(".") &&
                                          !formatCode.contains("¥") &&
                                          !formatCode.contains("\\¥") &&
                                          !formatCode.contains("[$¥]") &&
                                          !formatCode.contains("$") &&
                                          !formatCode.contains("%")
                    if isIntegerFormat && displayValue.hasSuffix(".0") {
                        displayValue = String(displayValue.dropLast(2))
                    }
                }
                return CellData(
                    value: displayValue,
                    rawValue: rawValue,
                    numericValue: numericValue,
                    formatCode: formatCode,
                    isDate: false
                )
            }
        }

        // 其他情况返回原始值或inline字符串
        let displayValue = cell.value ?? cell.inlineString?.text ?? ""
        return CellData(
            value: displayValue,
            rawValue: rawValue,
            numericValue: nil,
            formatCode: formatCode,
            isDate: false
        )
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
public enum ParserError: Error, CustomStringConvertible, LocalizedError {
    case fileNotFound(String)
    case cannotOpenFile(String)
    case invalidFormat(String)
    case allFilesFailed([(String, Error)])
    case unsupportedFileExtension(String)

    public var description: String {
        switch self {
        case .fileNotFound(let path):
            return LocaleManager.loc("文件未找到: \(path)")
        case .cannotOpenFile(let path):
            return LocaleManager.loc("无法打开文件: \(path)")
        case .invalidFormat(let reason):
            return LocaleManager.loc("格式错误: \(reason)")
        case .allFilesFailed(let errors):
            return LocaleManager.loc("所有文件解析失败:\n") + errors.map { "  - \($0.0): \($0.1)" }.joined(separator: "\n")
        case .unsupportedFileExtension(let ext):
            return LocaleManager.loc("暂不支持的文件类型: \(ext)。请选择 .xlsx 或 .xls 文件。")
        }
    }

    public var errorDescription: String? {
        description
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
