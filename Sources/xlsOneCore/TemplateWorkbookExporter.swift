import Foundation
import ZIPFoundation

public struct TemplateWorkbookExporter {
    public init() {}

    public func exportWorkbook(
        templatePath: String,
        results: [MergedResult],
        to outputPath: String
    ) throws {
        let fileManager = FileManager.default
        let templateURL = URL(fileURLWithPath: templatePath)
        let outputURL = URL(fileURLWithPath: outputPath)
        let workingRoot = fileManager.temporaryDirectory
            .appendingPathComponent("xlsone-export-\(UUID().uuidString)", isDirectory: true)
        let extractedURL = workingRoot.appendingPathComponent("workbook", isDirectory: true)

        if fileManager.fileExists(atPath: workingRoot.path) {
            try fileManager.removeItem(at: workingRoot)
        }
        try fileManager.createDirectory(at: extractedURL, withIntermediateDirectories: true)
        defer {
            try? fileManager.removeItem(at: workingRoot)
        }

        if fileManager.fileExists(atPath: outputPath) {
            try fileManager.removeItem(at: outputURL)
        }

        try fileManager.unzipItem(at: templateURL, to: extractedURL)

        let worksheetPaths = try worksheetPathMap(in: extractedURL)
        for result in results {
            guard let worksheetPath = worksheetPaths[result.sheetName] else { continue }
            let worksheetURL = extractedURL.appendingPathComponent(worksheetPath)
            let originalData = try Data(contentsOf: worksheetURL)
            let rewrittenData = try rewriteWorksheet(data: originalData, with: result)
            try rewrittenData.write(to: worksheetURL, options: .atomic)
        }

        try fileManager.zipItem(
            at: extractedURL,
            to: outputURL,
            shouldKeepParent: false,
            compressionMethod: .deflate
        )
    }

    private func rewriteWorksheet(data: Data, with result: MergedResult) throws -> Data {
        guard var xml = String(data: data, encoding: .utf8) else {
            throw ExportError.invalidXML("worksheet")
        }

        for (rowIndex, row) in result.rows.enumerated() {
            for (columnIndex, cell) in row.enumerated() {
                let reference = cellReference(row: rowIndex, col: columnIndex)
                xml = rewriteCell(in: xml, reference: reference, with: cell)
            }
        }

        return Data(xml.utf8)
    }

    private func rewriteCell(in xml: String, reference: String, with cell: MergedCell) -> String {
        let escapedReference = NSRegularExpression.escapedPattern(for: reference)
        let patterns = [
            "<c\\b([^>]*)\\br=\"" + escapedReference + "\"([^>]*)/>",
            "<c\\b([^>/]*)\\br=\"" + escapedReference + "\"([^>/]*)>(.*?)</c>"
        ]

        for pattern in patterns {
            guard let regex = try? NSRegularExpression(pattern: pattern, options: [.dotMatchesLineSeparators]) else {
                continue
            }

            let range = NSRange(xml.startIndex..., in: xml)
            guard let match = regex.firstMatch(in: xml, options: [], range: range) else {
                continue
            }

            let attributesBefore = Range(match.range(at: 1), in: xml).map { String(xml[$0]) } ?? ""
            let attributesAfter = Range(match.range(at: 2), in: xml).map { String(xml[$0]) } ?? ""
            let attributes = sanitizedAttributes(attributesBefore + attributesAfter)
            let replacement = cellXML(reference: reference, attributes: attributes, cell: cell)
            return (xml as NSString).replacingCharacters(in: match.range, with: replacement)
        }

        return xml
    }

    private func cellXML(reference: String, attributes: String, cell: MergedCell) -> String {
        let attributes = attributes.isEmpty ? "" : " " + attributes
        switch cell.type {
        case .sum(let value):
            return #"<c r=""# + reference + #"""# + attributes + #"><v>"# + numericCellValue(value) + #"</v></c>"#
        default:
            if cell.displayValue.isEmpty {
                return #"<c r=""# + reference + #"""# + attributes + #"/>"#
            }
            return #"<c r=""# + reference + #"" t="inlineStr""# + attributes + #"><is><t>"# + escapeXML(cell.displayValue) + #"</t></is></c>"#
        }
    }

    private func sanitizedAttributes(_ raw: String) -> String {
        raw
            .replacingOccurrences(of: "/", with: "")
            .replacingOccurrences(of: #"\s+t=\"[^\"]*\""#, with: "", options: .regularExpression)
            .replacingOccurrences(of: #"\s+"#, with: " ", options: .regularExpression)
            .trimmingCharacters(in: .whitespacesAndNewlines)
    }

    private func numericCellValue(_ value: Double) -> String {
        if value == floor(value) {
            return String(format: "%.0f", value)
        }
        return String(format: "%.15g", value)
    }

    private func worksheetPathMap(in rootURL: URL) throws -> [String: String] {
        let workbookXML = try xmlString(
            at: rootURL.appendingPathComponent("xl/workbook.xml")
        )
        let relationshipsXML = try xmlString(
            at: rootURL.appendingPathComponent("xl/_rels/workbook.xml.rels")
        )

        let relationshipPattern = #"<Relationship[^>]+Id="([^"]+)"[^>]+Target="([^"]+)""#
        let sheetPattern = #"<sheet[^>]+name="([^"]+)"[^>]+r:id="([^"]+)""#

        let relationshipMatches = try extractMatches(pattern: relationshipPattern, in: relationshipsXML)
        let pathByRelationship = Dictionary(
            uniqueKeysWithValues: relationshipMatches.map { match in
                (match[0], "xl/\(match[1])")
            }
        )

        let sheetMatches = try extractMatches(pattern: sheetPattern, in: workbookXML)
        var result: [String: String] = [:]
        for match in sheetMatches {
            let sheetName = match[0]
            let relationshipID = match[1]
            if let path = pathByRelationship[relationshipID] {
                result[sheetName] = path
            }
        }
        return result
    }

    private func extractMatches(pattern: String, in text: String) throws -> [[String]] {
        let regex = try NSRegularExpression(pattern: pattern, options: [.dotMatchesLineSeparators])
        let range = NSRange(text.startIndex..., in: text)
        return regex.matches(in: text, options: [], range: range).compactMap { match in
            var parts: [String] = []
            for index in 1..<match.numberOfRanges {
                guard let partRange = Range(match.range(at: index), in: text) else { return nil }
                parts.append(String(text[partRange]))
            }
            return parts
        }
    }

    private func xmlString(at fileURL: URL) throws -> String {
        guard FileManager.default.fileExists(atPath: fileURL.path) else {
            throw ExportError.missingEntry(fileURL.path)
        }
        let entryData = try Data(contentsOf: fileURL)
        guard let string = String(data: entryData, encoding: .utf8) else {
            throw ExportError.invalidXML(fileURL.path)
        }
        return string
    }

    private func cellReference(row: Int, col: Int) -> String {
        "\(columnLetters(col))\(row + 1)"
    }

    private func columnLetters(_ col: Int) -> String {
        var result = ""
        var number = col
        repeat {
            result = String(UnicodeScalar(65 + (number % 26))!) + result
            number = number / 26 - 1
        } while number >= 0
        return result
    }

    private func escapeXML(_ text: String) -> String {
        text
            .replacingOccurrences(of: "&", with: "&amp;")
            .replacingOccurrences(of: "<", with: "&lt;")
            .replacingOccurrences(of: ">", with: "&gt;")
            .replacingOccurrences(of: "\"", with: "&quot;")
    }
}

public enum ExportError: Error {
    case cannotOpenArchive
    case missingEntry(String)
    case invalidXML(String)
}
