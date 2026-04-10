import Foundation

/// Excel 导出器
public struct ExcelExporter {

    public init() {}

    /// 导出合并结果为CSV格式（临时方案，因为生成真正的XLSX较复杂）
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

    /// 导出为简单的HTML表格
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
                let cellClass: String
                switch cell.type {
                case .sum: cellClass = "numeric"
                case .mixed: cellClass = "mixed"
                default: cellClass = "label"
                }

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
