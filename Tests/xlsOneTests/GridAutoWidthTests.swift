import XCTest
import xlsOneCore
@testable import xlsOneUI

final class GridAutoWidthTests: XCTestCase {
    func testAutoWidthUsesHeaderAndSampledRows() {
        let rows = [
            [cell("编码"), cell("名称"), cell("类型"), cell("金额(元)")],
            [cell("201"), cell("一般公共服务"), cell("预算内"), cell("1280.50")],
            [cell("301"), cell("教育支出"), cell("预算内"), cell("982345.75")]
        ]

        let widths = ColumnWidthCalculator.defaultWidths(for: rows)

        XCTAssertGreaterThan(try XCTUnwrap(widths[3]), try XCTUnwrap(widths[0]))
    }

    func testMissingCellsStillUseSharedWidthForSparseRows() {
        let rows = [
            [cell("编码"), cell("名称"), cell("备注")],
            [cell("201")],
            [cell("301"), cell("教育支出"), cell("这是一个更长的备注字段")]
        ]

        let widths = ColumnWidthCalculator.defaultWidths(for: rows)

        XCTAssertNotNil(widths[2])
        XCTAssertGreaterThan(try XCTUnwrap(widths[2]), try XCTUnwrap(widths[0]))
    }

    private func cell(_ value: String) -> MergedCell {
        MergedCell(type: .single(value))
    }
}
