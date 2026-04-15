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
}
