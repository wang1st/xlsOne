import XCTest
import xlsOneCore
@testable import xlsOne

@MainActor
final class AppViewModelSelectionTests: XCTestCase {
    func testSwitchToSkippedSheetClearsMergeEditingContext() {
        let viewModel = AppViewModel()
        viewModel.validationReport = WorkbookValidationReport(
            readiness: .ready,
            templateFile: nil,
            files: [],
            commonSheetNames: ["可合并表"],
            skippedSheetNames: ["已跳过表"]
        )
        viewModel.availableSheets = ["可合并表"]
        viewModel.mergedResult = MergedResult(
            sheetName: "可合并表",
            rows: [[MergedCell(type: .label)]],
            sourceFiles: ["a.xlsx"]
        )
        viewModel.selectedCell = CellPosition(row: 0, col: 0)
        viewModel.anomalyQueue = [
            CellAnomalyItem(
                sheetName: "可合并表",
                position: CellPosition(row: 0, col: 0),
                cellReference: "A1",
                displayValue: "值",
                summary: "已人工修正"
            )
        ]

        viewModel.switchToSkippedSheet("已跳过表")

        XCTAssertEqual(viewModel.selectedSheetName, "已跳过表")
        XCTAssertEqual(viewModel.selectedSheetStructureStatus, "已跳过")
        XCTAssertNil(viewModel.currentSheet)
        XCTAssertNil(viewModel.mergedResult)
        XCTAssertNil(viewModel.selectedCell)
        XCTAssertTrue(viewModel.anomalyQueue.isEmpty)
    }

    func testSelectedSheetStructureStatusTracksSelectionKind() {
        let viewModel = AppViewModel()

        viewModel.selectedSheetSelection = .mergeable("汇总表")
        XCTAssertEqual(viewModel.selectedSheetName, "汇总表")
        XCTAssertEqual(viewModel.selectedSheetStructureStatus, "可合并")

        viewModel.selectedSheetSelection = .skipped("异常表")
        XCTAssertEqual(viewModel.selectedSheetName, "异常表")
        XCTAssertEqual(viewModel.selectedSheetStructureStatus, "已跳过")
        XCTAssertNil(viewModel.currentSheet)
    }
}
