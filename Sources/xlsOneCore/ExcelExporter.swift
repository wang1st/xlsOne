import Foundation

/// Excel 导出器
public struct ExcelExporter {

    public init() {}

    /// 导出合并结果为CSV格式
    public func exportToCSV(result: MergedResult) -> String {
        var csv = ""

        for row in result.rows {
            let rowValues = row.map { $0.displayValue }
            // 处理包含逗号的值
            let escapedValues = rowValues.map { value -> String in
                if value.contains(",") || value.contains("\"") || value.contains("\n") {
                    let escaped = value.replacingOccurrences(of: "\"", with: "\"\"")
                    return "\"\(escaped)\""
                }
                return value
            }
            csv += escapedValues.joined(separator: ",") + "\n"
        }

        return csv
    }

    /// 保存CSV到文件
    public func saveCSV(result: MergedResult, to path: String) throws {
        let csv = exportToCSV(result: result)
        try csv.write(toFile: path, atomically: true, encoding: .utf8)
    }

    /// 导出为HTML表格（复刻原始Excel格式）
    public func exportToHTML(result: MergedResult) -> String {
        var html = """
        <!DOCTYPE html>
        <html>
        <head>
            <meta charset="UTF-8">
            <title>\(result.sheetName) - 汇总结果</title>
            <style>
                body { font-family: -apple-system, BlinkMacSystemFont, sans-serif; margin: 20px; }
                h1 { font-size: 18px; margin-bottom: 10px; }
                .info { color: #666; margin-bottom: 20px; font-size: 12px; }
                table { border-collapse: collapse; width: 100%; }
                th, td { border: 1px solid #ccc; padding: 8px; text-align: left; font-size: 13px; }
                th { background-color: #f5f5f5; font-weight: 600; }
                tr:nth-child(even) { background-color: #fafafa; }
                .numeric { text-align: right; }
                .currency { text-align: right; font-family: 'SF Mono', monospace; }
                .percentage { text-align: right; }
                .date { text-align: center; }
                .label { color: #333; }
                .mixed { color: #999; font-style: italic; }
            </style>
        </head>
        <body>
            <h1>工作表: \(result.sheetName)</h1>
            <div class="info">来源文件: \(result.sourceFiles.joined(separator: ", "))</div>
            <table>
        """

        for (rowIdx, row) in result.rows.enumerated() {
            html += "        <tr>\n"
            for cell in row {
                let cellClass = determineCellClass(cell)
                let tag = rowIdx == 0 ? "th" : "td"
                html += "            <\(tag) class=\"\(cellClass)\">\(escapeHTML(cell.displayValue))</\(tag)>\n"
            }
            html += "        </tr>\n"
        }

        html += """
            </table>
        </body>
        </html>
        """

        return html
    }

    /// 根据单元格类型和格式码确定CSS类
    private func determineCellClass(_ cell: MergedCell) -> String {
        // 优先根据 formatCode 判断
        if let formatCode = cell.formatCode {
            if formatCode.contains("¥") || formatCode.contains("$") || formatCode.contains("\\¥") {
                return "currency"
            }
            if formatCode.contains("%") {
                return "percentage"
            }
            if formatCode.contains("yyyy") || formatCode.contains("MM") || formatCode.contains("dd") {
                return "date"
            }
            if formatCode.contains("#,##0") || formatCode.contains("0.00") || formatCode.contains("0.0") {
                return "numeric"
            }
        }

        // 根据类型判断
        switch cell.type {
        case .sum:
            return "numeric"
        case .mixed:
            return "mixed"
        default:
            return "label"
        }
    }

    /// 保存HTML到文件
    public func saveHTML(result: MergedResult, to path: String) throws {
        let html = exportToHTML(result: result)
        try html.write(toFile: path, atomically: true, encoding: .utf8)
    }

    /// HTML转义
    private func escapeHTML(_ text: String) -> String {
        return text
            .replacingOccurrences(of: "&", with: "&amp;")
            .replacingOccurrences(of: "<", with: "&lt;")
            .replacingOccurrences(of: ">", with: "&gt;")
            .replacingOccurrences(of: "\"", with: "&quot;")
    }
}
