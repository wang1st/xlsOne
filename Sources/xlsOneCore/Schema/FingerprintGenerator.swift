import Foundation
import CryptoKit

/// 文件指纹生成器
public struct FingerprintGenerator {

    /// 从 ExcelFile 生成指纹
    /// - Parameters:
    ///   - file: Excel 文件
    ///   - fileNamePattern: 可选的文件名匹配模式
    /// - Returns: 文件指纹
    public static func generate(from file: ExcelFile, fileNamePattern: String? = nil) -> FileFingerprint {
        // 使用第一个工作表作为主要特征源
        guard let sheet = file.sheets.first else {
            return FileFingerprint(
                sheetName: "",
                rowCount: 0,
                colCount: 0,
                headerHash: "",
                sampleDataHash: "",
                fileNamePattern: fileNamePattern
            )
        }

        let headerHash = generateHeaderHash(sheet: sheet)
        let sampleDataHash = generateSampleDataHash(sheet: sheet)
        let maxCols = sheet.rows.map { $0.count }.max() ?? 0

        return FileFingerprint(
            sheetName: sheet.name,
            rowCount: sheet.rows.count,
            colCount: maxCols,
            headerHash: headerHash,
            sampleDataHash: sampleDataHash,
            fileNamePattern: fileNamePattern
        )
    }

    /// 从指定工作表生成兼容旧规则的单表指纹
    public static func generate(
        from file: ExcelFile,
        sheetName: String,
        fileNamePattern: String? = nil
    ) -> FileFingerprint {
        guard let sheet = file.sheets.first(where: { $0.name == sheetName }) else {
            return FileFingerprint(
                sheetName: sheetName,
                rowCount: 0,
                colCount: 0,
                headerHash: "",
                sampleDataHash: "",
                fileNamePattern: fileNamePattern
            )
        }

        let dimensions = effectiveDimensions(for: sheet)
        let sheetFingerprint = generateSheetRuleFingerprint(
            sheet: sheet,
            effectiveRowCount: dimensions.rows,
            effectiveColumnCount: dimensions.cols
        )

        return FileFingerprint(
            schemaVersion: 2,
            sheetName: sheet.name,
            rowCount: sheetFingerprint.rowCount,
            colCount: sheetFingerprint.columnCount,
            headerHash: sheetFingerprint.layoutHash,
            sampleDataHash: sheetFingerprint.formatHash,
            fileNamePattern: fileNamePattern
        )
    }

    /// 从一组同构 Excel 的可合并工作表生成工作区级规则指纹
    public static func generateWorkbook(
        from files: [ExcelFile],
        sheetNames: [String]
    ) -> WorkbookRuleFingerprint {
        let fingerprints = sheetNames.map { sheetName in
            generateConsensusSheetFingerprint(from: files, sheetName: sheetName)
        }

        return WorkbookRuleFingerprint(sheetFingerprints: fingerprints)
    }

    /// 为新规则提供兼容旧字段的代表性指纹
    public static func generateWorkspaceLegacyFingerprint(
        from files: [ExcelFile],
        sheetNames: [String]
    ) -> FileFingerprint {
        guard let firstSheetName = sheetNames.first,
              let file = files.first else {
            return FileFingerprint(
                schemaVersion: 2,
                sheetName: "",
                rowCount: 0,
                colCount: 0,
                headerHash: "",
                sampleDataHash: ""
            )
        }

        return generate(from: file, sheetName: firstSheetName)
    }

    /// 生成表头哈希
    /// 结合工作表名称、第一行（列头）和第一列（行标签）
    private static func generateHeaderHash(sheet: SheetData) -> String {
        var components: [String] = []

        // 添加工作表名称
        components.append(sheet.name)

        // 添加第一行内容（列头）- 最多前 10 列
        if let firstRow = sheet.rows.first {
            let limit = min(firstRow.count, 10)
            for i in 0..<limit {
                let value = firstRow[i].value.trimmingCharacters(in: .whitespaces)
                // 标准化：移除空格、转为小写
                components.append(normalize(value))
            }
        }

        // 添加第一列内容（行标签）- 最多前 20 行
        let rowLimit = min(sheet.rows.count, 20)
        for i in 0..<rowLimit {
            if let firstCell = sheet.rows[i].first {
                let value = firstCell.value.trimmingCharacters(in: .whitespaces)
                components.append(normalize(value))
            }
        }

        return hash(components.joined(separator: "|"))
    }

    /// 生成样本数据哈希
    /// 选取表格四角和中心点的单元格值
    private static func generateSampleDataHash(sheet: SheetData) -> String {
        let maxRows = sheet.rows.count
        guard maxRows > 0 else { return "" }

        let maxCols = sheet.rows.map { $0.count }.max() ?? 0
        guard maxCols > 0 else { return "" }

        // 选取关键点
        var samplePoints: [(row: Int, col: Int)] = [
            (0, 0),                                    // 左上
            (0, max(0, maxCols - 1)),                  // 右上
            (max(0, maxRows - 1), 0),                  // 左下
            (max(0, maxRows - 1), max(0, maxCols - 1)) // 右下
        ]

        // 添加中心点
        if maxRows > 2 && maxCols > 2 {
            samplePoints.append((maxRows / 2, maxCols / 2))
        }

        // 收集样本值
        var sampleValues: [String] = []
        for point in samplePoints {
            if point.row < maxRows,
               point.col < sheet.rows[point.row].count {
                let value = sheet.rows[point.row][point.col].value
                sampleValues.append(normalize(value))
            }
        }

        return hash(sampleValues.joined(separator: "|"))
    }

    /// 标准化字符串（用于更稳定的匹配）
    private static func normalize(_ text: String) -> String {
        return text
            .lowercased()
            .trimmingCharacters(in: .whitespaces)
            .replacingOccurrences(of: "\\s+", with: "", options: .regularExpression)
    }

    /// 计算字符串哈希（使用 SHA256 的前 16 位）
    private static func hash(_ text: String) -> String {
        let data = Data(text.utf8)
        let hash = SHA256.hash(data: data)
        // 取前 8 字节（16 个十六进制字符）作为哈希值
        return hash.prefix(8).map { String(format: "%02x", $0) }.joined()
    }

    private static func generateConsensusSheetFingerprint(
        from files: [ExcelFile],
        sheetName: String
    ) -> SheetRuleFingerprint {
        let sheets = files.compactMap { file in
            file.sheets.first(where: { $0.name == sheetName })
        }

        let dimensions = chooseDominantDimensions(
            from: sheets.map(effectiveDimensions)
        )

        guard let representativeSheet = sheets.first else {
            return SheetRuleFingerprint(
                sheetName: sheetName,
                rowCount: 0,
                columnCount: 0,
                layoutHash: "",
                formatHash: ""
            )
        }

        return generateSheetRuleFingerprint(
            sheet: representativeSheet,
            effectiveRowCount: dimensions.rows,
            effectiveColumnCount: dimensions.cols
        )
    }

    private static func generateSheetRuleFingerprint(
        sheet: SheetData,
        effectiveRowCount: Int,
        effectiveColumnCount: Int
    ) -> SheetRuleFingerprint {
        var layoutComponents: [String] = [sheet.name, "\(effectiveRowCount)x\(effectiveColumnCount)"]
        var formatComponents: [String] = []

        for rowIndex in 0..<effectiveRowCount {
            for columnIndex in 0..<effectiveColumnCount {
                let cell = sheet.cellAt(row: rowIndex, col: columnIndex)
                let isEmpty = cell?.value.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty ?? true
                layoutComponents.append(isEmpty ? "0" : "1")
                formatComponents.append(structuralToken(for: cell))
            }
        }

        return SheetRuleFingerprint(
            sheetName: sheet.name,
            rowCount: effectiveRowCount,
            columnCount: effectiveColumnCount,
            layoutHash: hash(layoutComponents.joined(separator: "|")),
            formatHash: hash(formatComponents.joined(separator: "|"))
        )
    }

    private static func structuralToken(for cell: CellData?) -> String {
        guard let cell else { return "E" }
        let value = cell.value.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !value.isEmpty else { return "E" }
        if cell.isDate { return "D" }
        if cell.numericValue != nil { return "N" }
        if value.range(of: #"\p{Han}"#, options: .regularExpression) != nil { return "C" }
        if value.range(of: #"[A-Za-z]"#, options: .regularExpression) != nil { return "A" }
        return "T"
    }

    private static func effectiveDimensions(for sheet: SheetData) -> SheetDimensions {
        var lastNonEmptyRow = -1
        var lastNonEmptyColumn = -1

        for (rowIndex, row) in sheet.rows.enumerated() {
            for (columnIndex, cell) in row.enumerated()
            where !cell.value.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
                lastNonEmptyRow = max(lastNonEmptyRow, rowIndex)
                lastNonEmptyColumn = max(lastNonEmptyColumn, columnIndex)
            }
        }

        return SheetDimensions(
            rows: max(lastNonEmptyRow + 1, 0),
            cols: max(lastNonEmptyColumn + 1, 0)
        )
    }

    private static func chooseDominantDimensions(from dimensions: [SheetDimensions]) -> SheetDimensions {
        guard !dimensions.isEmpty else { return SheetDimensions(rows: 0, cols: 0) }

        return Dictionary(grouping: dimensions.enumerated(), by: \.element)
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
