import XCTest
@testable import xlsOneCore

final class TemplateWorkbookExporterTests: XCTestCase {
    func testTemplateExporterProducesReadableWorkbook() async throws {
        let samplePath = "/Users/ethan/xlsOne/仙居县/仙居县人民政府安洲街道办事处2025乡镇报表主体信息表.xlsx"
        let parser = ExcelParser()
        let sourceFile = try await parser.parseFile(at: samplePath)

        let results = sourceFile.sheets.map { sheet in
            MergedResult(
                sheetName: sheet.name,
                rows: sheet.rows.map { row in
                    row.map { cell in
                        let type: MergedCell.CellType = .single(cell.value)
                        return MergedCell(
                            type: type,
                            displayValue: cell.value,
                            sources: [
                                CellSourceEntry(
                                    filename: sourceFile.filename,
                                    filepath: sourceFile.filepath,
                                    value: cell.value,
                                    rawValue: cell.rawValue,
                                    state: cell.value.isEmpty ? .empty : .value
                                )
                            ],
                            formatCode: cell.formatCode,
                            decision: MergedCellDecision(
                                autoDetectedType: type,
                                confidence: 1.0,
                                decisionReasons: ["test fixture"],
                                isSuspicious: false
                            )
                        )
                    }
                },
                sourceFiles: [sourceFile.filename]
            )
        }

        let outputURL = URL(fileURLWithPath: NSTemporaryDirectory())
            .appendingPathComponent("xlsone-export-\(UUID().uuidString).xlsx")
        defer {
            try? FileManager.default.removeItem(at: outputURL)
        }

        let exporter = TemplateWorkbookExporter()
        try exporter.exportWorkbook(
            templatePath: samplePath,
            results: results,
            to: outputURL.path
        )

        let exported = try await parser.parseFile(at: outputURL.path)
        XCTAssertEqual(exported.sheets.map(\.name), sourceFile.sheets.map(\.name))
        XCTAssertEqual(exported.sheets.first?.rows.count, sourceFile.sheets.first?.rows.count)

        let sourcePreview = Array(sourceFile.sheets.first?.rows.prefix(20) ?? [])
        let exportedPreview = Array(exported.sheets.first?.rows.prefix(20) ?? [])
        XCTAssertEqual(
            visibleMatrix(from: exportedPreview),
            visibleMatrix(from: sourcePreview)
        )
        XCTAssertEqual(
            formatMatrix(from: exportedPreview),
            formatMatrix(from: sourcePreview)
        )

        XCTAssertEqual(exported.sheets.first?.rows[1][1].value, "乡镇报表主体信息表")
        XCTAssertEqual(exported.sheets.first?.rows[13][2].value, "2025-06-16")
        XCTAssertEqual(exported.sheets.first?.rows[13][2].formatCode, "yyyy-MM-dd")
    }

    func testTemplateExporterWritesNumericSumsAsNumbers() async throws {
        let samplePath = "/Users/ethan/xlsOne/仙居县/仙居县人民政府安洲街道办事处2025乡镇报表主体信息表.xlsx"
        let parser = ExcelParser()
        let sourceFile = try await parser.parseFile(at: samplePath)

        let results = sourceFile.sheets.map { sheet in
            let rows = sheet.rows.enumerated().map { rowIndex, row in
                row.enumerated().map { columnIndex, cell in
                    if sheet.name == "乡镇财政基本情况表", rowIndex == 4, columnIndex == 2 {
                        return MergedCell(
                            type: .sum(1234),
                            displayValue: MergedCell.formatNumber(1234, formatCode: cell.formatCode),
                            sources: [
                                CellSourceEntry(
                                    filename: sourceFile.filename,
                                    filepath: sourceFile.filepath,
                                    value: "1234",
                                    rawValue: "1234",
                                    state: .value
                                )
                            ],
                            formatCode: cell.formatCode,
                            decision: MergedCellDecision(
                                autoDetectedType: .sum(1234),
                                confidence: 1.0,
                                decisionReasons: ["test sum export"],
                                isSuspicious: false
                            )
                        )
                    }

                    let type: MergedCell.CellType = .single(cell.value)
                    return MergedCell(
                        type: type,
                        displayValue: cell.value,
                        sources: [
                            CellSourceEntry(
                                filename: sourceFile.filename,
                                filepath: sourceFile.filepath,
                                value: cell.value,
                                rawValue: cell.rawValue,
                                state: cell.value.isEmpty ? .empty : .value
                            )
                        ],
                        formatCode: cell.formatCode,
                        decision: MergedCellDecision(
                            autoDetectedType: type,
                            confidence: 1.0,
                            decisionReasons: ["test fixture"],
                            isSuspicious: false
                        )
                    )
                }
            }

            return MergedResult(
                sheetName: sheet.name,
                rows: rows,
                sourceFiles: [sourceFile.filename]
            )
        }

        let outputURL = URL(fileURLWithPath: NSTemporaryDirectory())
            .appendingPathComponent("xlsone-export-sum-\(UUID().uuidString).xlsx")
        defer {
            try? FileManager.default.removeItem(at: outputURL)
        }

        try TemplateWorkbookExporter().exportWorkbook(
            templatePath: samplePath,
            results: results,
            to: outputURL.path
        )

        let exported = try await parser.parseFile(at: outputURL.path)
        let targetCell = try XCTUnwrap(
            exported.sheets.first(where: { $0.name == "乡镇财政基本情况表" })?.rows[4][2]
        )
        XCTAssertEqual(targetCell.value, "1234")
        XCTAssertEqual(targetCell.numericValue, 1234)
    }
}

private func visibleMatrix(from rows: [[CellData]]) -> [[String]] {
    rows.map { row in
        row.map(\.value)
    }
}

private func formatMatrix(from rows: [[CellData]]) -> [[String?]] {
    rows.map { row in
        row.map(\.formatCode)
    }
}
