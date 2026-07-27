import XCTest
@testable import xlsOneCore

final class TemplateWorkbookExporterTests: XCTestCase {
    private var samplePath: String {
        URL(fileURLWithPath: #filePath)
            .deletingLastPathComponent()
            .deletingLastPathComponent()
            .deletingLastPathComponent()
            .appendingPathComponent("samples")
            .appendingPathComponent("monthly-report-sample-v1.1")
            .appendingPathComponent("01-operations-team-1-june-2026.xlsx")
            .path
    }

    func testTemplateExporterProducesReadableWorkbook() async throws {
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

        XCTAssertEqual(exported.sheets.count, 2)
        XCTAssertEqual(exported.sheets.first?.rows[1][1].value, "2026 年 6 月")
        XCTAssertEqual(exported.sheets.first?.rows[5][2].numericValue, 180_000)
    }

    func testTemplateExporterPreservesNumericCells() async throws {
        let parser = ExcelParser()
        let sourceFile = try await parser.parseFile(at: samplePath)

        let results = sourceFile.sheets.map { sheet in
            let rows = sheet.rows.map { row in
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
            exported.sheets.first?.rows[5][2]
        )
        XCTAssertEqual(targetCell.value, "180000")
        XCTAssertEqual(targetCell.numericValue, 180_000)
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
