import Foundation
import ZIPFoundation

/// Excel 样式解析器 - 用于解析日期格式等
public struct ExcelStylesParser {

    /// 数字格式定义
    private struct NumberFormat {
        let numFmtId: Int
        let formatCode: String
    }

    /// 样式定义
    private struct CellFormat {
        let numFmtId: Int
    }

    private var numberFormats: [Int: String] = [:]  // numFmtId -> formatCode
    private var cellFormats: [Int: Int] = [:]       // styleIndex -> numFmtId

    public init() {}

    /// 从 xlsx 文件解析样式
    public mutating func parseStyles(from xlsxPath: String) throws {
        let archive = try Archive(url: URL(fileURLWithPath: xlsxPath), accessMode: .read)

        guard let entry = archive["xl/styles.xml"] else {
            return
        }

        var data = Data()
        _ = try archive.extract(entry) { chunk in
            data.append(chunk)
        }

        let xmlString = String(data: data, encoding: .utf8) ?? ""

        // 解析自定义数字格式
        parseNumberFormats(xmlString)

        // 解析单元格格式映射
        parseCellFormats(xmlString)
    }

    /// 解析 numFmts 部分
    private mutating func parseNumberFormats(_ xmlString: String) {
        // 匹配 <numFmt numFmtId="165" formatCode="yyyy-MM-dd"/>
        let pattern = #"<numFmt[^>]+numFmtId=\"(\d+)\"[^>]+formatCode=\"([^\"]+)\""#

        if let regex = try? NSRegularExpression(pattern: pattern, options: [.dotMatchesLineSeparators]) {
            let range = NSRange(xmlString.startIndex..., in: xmlString)
            let matches = regex.matches(in: xmlString, options: [], range: range)

            for match in matches {
                if let idRange = Range(match.range(at: 1), in: xmlString),
                   let codeRange = Range(match.range(at: 2), in: xmlString) {
                    let id = Int(String(xmlString[idRange])) ?? 0
                    let code = String(xmlString[codeRange])
                    numberFormats[id] = code
                }
            }
        }

        // 添加内置日期格式
        // 内置格式ID: 14-22 通常是日期/时间格式
        for id in [14, 15, 16, 17, 18, 19, 20, 21, 22] {
            if numberFormats[id] == nil {
                numberFormats[id] = "date"
            }
        }
    }

    /// 解析 cellXfs 部分（样式到数字格式的映射）
    private mutating func parseCellFormats(_ xmlString: String) {
        // 查找 cellXfs 部分
        guard let cellXfsStart = xmlString.range(of: "<cellXfs"),
              let cellXfsEnd = xmlString.range(of: "</cellXfs>") else {
            return
        }

        let cellXfsContent = String(xmlString[cellXfsStart.lowerBound..<cellXfsEnd.upperBound])

        // 匹配 <xf numFmtId="..." ... />
        let pattern = #"<xf[^>]+numFmtId=\"(\d+)\""#

        if let regex = try? NSRegularExpression(pattern: pattern, options: []) {
            let range = NSRange(cellXfsContent.startIndex..., in: cellXfsContent)
            let matches = regex.matches(in: cellXfsContent, options: [], range: range)

            for (index, match) in matches.enumerated() {
                if let idRange = Range(match.range(at: 1), in: cellXfsContent) {
                    let numFmtId = Int(String(cellXfsContent[idRange])) ?? 0
                    cellFormats[index] = numFmtId
                }
            }
        }
    }

    /// 检查是否为日期格式
    public func isDateFormat(styleIndex: Int?) -> Bool {
        guard let styleIndex = styleIndex else {
            return false
        }

        guard let numFmtId = cellFormats[styleIndex] else {
            return false
        }

        // 内置日期/时间格式 ID
        if (14...22).contains(numFmtId) || (45...47).contains(numFmtId) {
            return true
        }

        // 检查自定义格式
        if let formatCode = numberFormats[numFmtId] {
            return isDateFormatCode(formatCode)
        }

        return false
    }

    /// 获取格式代码
    public func getFormatCode(styleIndex: Int?) -> String? {
        guard let styleIndex = styleIndex else { return nil }
        guard let numFmtId = cellFormats[styleIndex] else { return nil }

        if let formatCode = numberFormats[numFmtId] {
            return formatCode
        }

        return builtInFormatCode(for: numFmtId)
    }

    /// 获取格式信息（用于调试）
    public func getFormatInfo(styleIndex: Int?) -> String {
        guard let styleIndex = styleIndex else {
            return "styleIndex is nil"
        }

        guard let numFmtId = cellFormats[styleIndex] else {
            return "styleIndex \(styleIndex) -> no numFmtId found. Available: \(cellFormats.keys.sorted())"
        }

        // 检查自定义格式
        if let formatCode = numberFormats[numFmtId] {
            return "styleIndex \(styleIndex) -> numFmtId \(numFmtId) -> formatCode: \(formatCode)"
        } else if let builtIn = builtInFormatCode(for: numFmtId) {
            return "styleIndex \(styleIndex) -> numFmtId \(numFmtId) -> built-in: \(builtIn)"
        } else {
            return "styleIndex \(styleIndex) -> numFmtId \(numFmtId) -> unknown format"
        }
    }

    /// Excel 内置格式代码映射
    private func builtInFormatCode(for numFmtId: Int) -> String? {
        switch numFmtId {
        case 0: return "General"
        case 1: return "0"
        case 2: return "0.00"
        case 3: return "#,##0"
        case 4: return "#,##0.00"
        case 9: return "0%"
        case 10: return "0.00%"
        case 11: return "0.00E+00"
        case 12: return "# ?/?"
        case 13: return "# ??/??"
        case 14...22: return "yyyy-MM-dd"
        case 37: return "#,##0 ;(#,##0)"
        case 38: return "#,##0 ;[Red](#,##0)"
        case 39: return "#,##0.00;(#,##0.00)"
        case 40: return "#,##0.00;[Red](#,##0.00)"
        case 45: return "mm:ss"
        case 46: return "[h]:mm:ss"
        case 47: return "mmss.0"
        case 48: return "##0.0E+0"
        case 49: return "@"
        default: return nil
        }
    }

    /// 检查 formatCode 是否为日期格式
    private func isDateFormatCode(_ code: String) -> Bool {
        let dateMarkers = ["y", "m", "d", "h", "s", "Y", "M", "D", "H", "S"]
        return dateMarkers.contains { code.contains($0) }
    }

    /// 将 Excel 日期数值转换为日期字符串
    public func formatDate(_ numericValue: Double, styleIndex: Int?) -> String? {
        guard isDateFormat(styleIndex: styleIndex) else {
            return nil
        }

        // Excel 日期基准：1899-12-30
        // 但 Excel 有一个已知 bug：将 1900 年当作闰年
        // 所以实际上是 1900-01-00 作为基准
        let excelEpoch = Date(timeIntervalSince1970: -2209161600) // 1900-01-01

        // 调整：Excel 的 1 表示 1900-01-01
        // 但由于 1900 被错误当作闰年，需要减 2 天
        let days = numericValue - 2

        var components = DateComponents()
        components.day = Int(days)

        let calendar = Calendar.current
        if let date = calendar.date(byAdding: components, to: excelEpoch) {
            let formatter = DateFormatter()
            formatter.dateFormat = "yyyy-MM-dd"
            return formatter.string(from: date)
        }

        return nil
    }
}
