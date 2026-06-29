import XCTest
@testable import xlsOneCore

final class WorkbookValidatorTests: XCTestCase {
    private let validator = WorkbookValidator()

    func testValidatorReadyWhenSheetsShareEffectiveDimensionsAndParseFailureIsWarning() {
        let fileA = makeFile(
            name: "a.xlsx",
            sheets: [
                SheetData(name: "Sheet1", rows: sampleRows(header: "金额")),
                SheetData(name: "Sheet2", rows: sampleRows(header: "代码"))
            ]
        )
        let fileB = makeFile(
            name: "b.xlsx",
            sheets: [
                SheetData(name: "Sheet1", rows: sampleRows(header: "本年金额")),
                SheetData(name: "Sheet2", rows: sampleRows(header: "主体代码"))
            ]
        )

        let outcome = validator.validate(
            files: [fileA, fileB],
            parseFailures: [ExcelParseFailure(path: "/tmp/bad.xlsx", message: "damaged workbook")]
        )

        XCTAssertEqual(outcome.report.readiness, .ready)
        XCTAssertEqual(outcome.report.commonSheetNames, ["Sheet1", "Sheet2"])
        XCTAssertEqual(outcome.report.skippedSheetNames, [])
        XCTAssertEqual(outcome.mergeableFiles.map(\.filename), ["a.xlsx", "b.xlsx"])
        XCTAssertEqual(outcome.report.warningFiles.count, 1)
    }

    func testValidatorSkipsOnlyMismatchedSheetAndKeepsOtherSheetsMergeable() {
        let fileA = makeFile(
            name: "a.xlsx",
            sheets: [
                SheetData(name: "Sheet1", rows: sampleRows(header: "金额")),
                SheetData(name: "Sheet2", rows: sampleRows(header: "代码"))
            ]
        )
        let fileB = makeFile(
            name: "b.xlsx",
            sheets: [
                SheetData(name: "Sheet1", rows: sampleRows(header: "本年金额")),
                SheetData(name: "Sheet2", rows: sampleRows(header: "代码") + [[CellData(value: "额外"), CellData(value: "1")]])
            ]
        )

        let outcome = validator.validate(files: [fileA, fileB])

        XCTAssertEqual(outcome.report.readiness, .ready)
        XCTAssertEqual(outcome.report.commonSheetNames, ["Sheet1"])
        XCTAssertEqual(outcome.report.skippedSheetNames, ["Sheet2"])
        XCTAssertTrue(outcome.report.skippedSheetIssues.contains { $0.sheetName == "Sheet2" && $0.code == .rowCountMismatch })
        XCTAssertTrue(outcome.report.skippedSheetIssues.contains { $0.message.contains("多数文件为") })
        XCTAssertEqual(outcome.mergeableFiles.map(\.filename), ["a.xlsx", "b.xlsx"])
    }

    func testValidatorTrimsTrailingBlankRowsAndColumns() {
        let compactRows = [
            [CellData(value: "表头"), CellData(value: "金额"), CellData(value: "")],
            [CellData(value: "合计"), CellData(value: "100"), CellData(value: "")]
        ]
        let paddedRows = [
            [CellData(value: "表头"), CellData(value: "金额"), CellData(value: ""), CellData(value: "")],
            [CellData(value: "合计"), CellData(value: "100"), CellData(value: ""), CellData(value: "")],
            [CellData(value: ""), CellData(value: ""), CellData(value: ""), CellData(value: "")]
        ]

        let fileA = makeFile(
            name: "a.xlsx",
            sheets: [SheetData(name: "Sheet1", rows: compactRows)]
        )
        let fileB = makeFile(
            name: "b.xlsx",
            sheets: [SheetData(name: "Sheet1", rows: paddedRows)]
        )

        let outcome = validator.validate(files: [fileA, fileB])

        XCTAssertEqual(outcome.report.readiness, .ready)
        XCTAssertEqual(outcome.report.commonSheetNames, ["Sheet1"])
        XCTAssertEqual(outcome.report.skippedSheetNames, [])
    }

    func testValidatorChoosesMostCompleteRepresentativeTemplateNotFirstFile() {
        let sparse = makeFile(
            name: "first.xlsx",
            sheets: [
                SheetData(name: "Sheet1", rows: sampleRows(header: "金额")),
                SheetData(name: "Sheet2", rows: [
                    [CellData(value: "科目"), CellData(value: "代码")],
                    [CellData(value: "一般公共服务"), CellData(value: "100")],
                    [CellData(value: "农林水"), CellData(value: "")]
                ])
            ]
        )
        let richer = makeFile(
            name: "second.xlsx",
            sheets: [
                SheetData(name: "Sheet1", rows: sampleRows(header: "本年金额")),
                SheetData(name: "Sheet2", rows: [
                    [CellData(value: "科目"), CellData(value: "代码")],
                    [CellData(value: "一般公共服务"), CellData(value: "100")],
                    [CellData(value: "农林水"), CellData(value: "200")]
                ])
            ]
        )

        let outcome = validator.validate(files: [sparse, richer])

        XCTAssertEqual(outcome.report.templateFile?.filename, "second.xlsx")
        XCTAssertEqual(outcome.mergeableFiles.first?.filename, "second.xlsx")
    }

    func testValidatorBlocksWhenNoSheetCanParticipate() {
        let fileA = makeFile(
            name: "a.xlsx",
            sheets: [SheetData(name: "Sheet1", rows: sampleRows(header: "金额"))]
        )
        let fileB = makeFile(
            name: "b.xlsx",
            sheets: [SheetData(name: "Sheet2", rows: sampleRows(header: "代码"))]
        )

        let outcome = validator.validate(files: [fileA, fileB])

        XCTAssertEqual(outcome.report.readiness, .blocked)
        XCTAssertEqual(outcome.report.commonSheetNames, [])
        XCTAssertEqual(outcome.report.skippedSheetNames, ["Sheet1", "Sheet2"])
        XCTAssertEqual(outcome.mergeableFiles.count, 0)
    }

    func testMergedCellSourcesPreserveOrderAndState() {
        let merged = MergedCell.from(
            cells: [
                CellMergeInput(filename: "a.xlsx", filepath: "/tmp/a.xlsx", cell: CellData(value: "100")),
                CellMergeInput(filename: "b.xlsx", filepath: "/tmp/b.xlsx", cell: CellData(value: "")),
                CellMergeInput(filename: "c.xlsx", filepath: "/tmp/c.xlsx", cell: nil)
            ],
            neighborContext: NeighborContext(numericTendency: 1, labelTendency: 0),
            row: 1,
            col: 1
        )

        XCTAssertEqual(merged.sources.map(\.filename), ["a.xlsx", "b.xlsx", "c.xlsx"])
        XCTAssertEqual(merged.sources.map(\.state), [.value, .empty, .missing])
    }

    private func makeFile(name: String, sheets: [SheetData]) -> ExcelFile {
        ExcelFile(filename: name, filepath: "/tmp/\(name)", sheets: sheets)
    }

    private func sampleRows(header: String) -> [[CellData]] {
        [
            [CellData(value: "科目"), CellData(value: header)],
            [CellData(value: "一般公共服务"), CellData(value: "100")]
        ]
    }
}
