import XCTest
@testable import xlsOneCore

final class SimpleMergerTests: XCTestCase {

    // MARK: - CellData Tests

    func testCellDataNumericParsing() {
        // 纯数字
        XCTAssertEqual(CellData(value: "1000").numericValue, 1000)
        XCTAssertEqual(CellData(value: "1,000.50").numericValue, 1000.5)
        XCTAssertEqual(CellData(value: "1.234,56").numericValue, 1234.56)
        XCTAssertEqual(CellData(value: "1000.00").numericValue, 1000)
    }

    func testCellDataNonNumeric() {
        XCTAssertNil(CellData(value: "abc").numericValue)
        XCTAssertNil(CellData(value: "201").numericValue)  // 编码型数字
        XCTAssertNil(CellData(value: "").numericValue)
    }

    // MARK: - MergedCell Tests

    func testMergedCellLabelType() {
        // 所有值相同 → 标签型
        let cells = [
            (filename: "file1", cell: CellData(value: "201")),
            (filename: "file2", cell: CellData(value: "201")),
            (filename: "file3", cell: CellData(value: "201"))
        ]

        let merged = MergedCell.from(cells: cells)

        XCTAssertEqual(merged.type, .label)
        XCTAssertEqual(merged.displayValue, "201")
    }

    func testMergedCellSumType() {
        // 值不同且都是数值 → 求和
        let cells = [
            (filename: "file1", cell: CellData(value: "1000")),
            (filename: "file2", cell: CellData(value: "2000")),
            (filename: "file3", cell: CellData(value: "1500"))
        ]

        let merged = MergedCell.from(cells: cells)

        if case .sum(let total) = merged.type {
            XCTAssertEqual(total, 4500)
        } else {
            XCTFail("Expected sum type")
        }
        XCTAssertEqual(merged.displayValue, "4500")
    }

    func testMergedCellMixedType() {
        // 值不同且包含非数字 → 混合类型
        let cells = [
            (filename: "file1", cell: CellData(value: "201")),
            (filename: "file2", cell: CellData(value: "301")),
            (filename: "file3", cell: CellData(value: "201"))
        ]

        let merged = MergedCell.from(cells: cells)

        if case .mixed(let count) = merged.type {
            XCTAssertEqual(count, 2)  // 两个不同的值：201, 301
        } else {
            XCTFail("Expected mixed type, got \(merged.type)")
        }
        XCTAssertEqual(merged.displayValue, "2条")
    }

    func testMergedCellSingleType() {
        // 只有一个文件 → 单值
        let cells = [
            (filename: "file1", cell: CellData(value: "hello"))
        ]

        let merged = MergedCell.from(cells: cells)

        if case .single(let value) = merged.type {
            XCTAssertEqual(value, "hello")
        } else {
            XCTFail("Expected single type")
        }
        XCTAssertEqual(merged.displayValue, "hello")
    }

    func testMergedCellWithEmptyValues() {
        // 包含空值的测试
        let cells = [
            (filename: "file1", cell: CellData(value: "1000")),
            (filename: "file2", cell: CellData(value: "")),
            (filename: "file3", cell: CellData(value: "2000"))
        ]

        let merged = MergedCell.from(cells: cells)

        // 空值应该被忽略，只剩下两个数值
        if case .sum(let total) = merged.type {
            XCTAssertEqual(total, 3000)
        } else {
            XCTFail("Expected sum type, got \(merged.type)")
        }
    }

    // MARK: - SimpleMerger Tests

    func testMergeMultipleFiles() {
        // 创建测试数据 - file1和file2的值不同，以便测试聚合
        let sheet1 = SheetData(
            name: "Sheet1",
            rows: [
                [CellData(value: "科目"), CellData(value: "代码"), CellData(value: "金额")],
                [CellData(value: "1"), CellData(value: "201"), CellData(value: "1000")],
                [CellData(value: "2"), CellData(value: "301"), CellData(value: "2000")]
            ]
        )

        let sheet2 = SheetData(
            name: "Sheet1",
            rows: [
                [CellData(value: "科目"), CellData(value: "代码"), CellData(value: "金额")],
                [CellData(value: "1"), CellData(value: "201"), CellData(value: "2000")],  // 不同值
                [CellData(value: "2"), CellData(value: "301"), CellData(value: "2500")]   // 不同值
            ]
        )

        let file1 = ExcelFile(filename: "file1.xlsx", filepath: "/tmp/file1.xlsx", sheets: [sheet1])
        let file2 = ExcelFile(filename: "file2.xlsx", filepath: "/tmp/file2.xlsx", sheets: [sheet2])

        let merger = SimpleMerger()
        let result = merger.merge(files: [file1, file2], sheetName: "Sheet1")

        XCTAssertEqual(result.sheetName, "Sheet1")
        XCTAssertEqual(result.sourceFiles.count, 2)

        // 第一行是表头，应该都相同
        XCTAssertEqual(result.rows[0][0].type, .label)
        XCTAssertEqual(result.rows[0][1].type, .label)

        // 第二行代码列在两个文件中都是"201"（相同的3位数字）→ 标签（因为相同）
        XCTAssertEqual(result.rows[1][1].type, .label)
        XCTAssertEqual(result.rows[1][1].displayValue, "201")

        // 第二行金额列值不同（1000 vs 2000）且都是数值 → 求和
        XCTAssertEqual(result.rows[1][2].type, .sum(3000))
        XCTAssertEqual(result.rows[1][2].displayValue, "3000")
    }

    func testMergeEmptyFiles() {
        let merger = SimpleMerger()
        let result = merger.merge(files: [], sheetName: "Sheet1")

        XCTAssertTrue(result.rows.isEmpty)
        XCTAssertTrue(result.sourceFiles.isEmpty)
    }

    func testMergeWithDifferentSheetNames() {
        // 测试当工作表名称不匹配时
        let sheet1 = SheetData(name: "Sheet1", rows: [[CellData(value: "A")]])
        let sheet2 = SheetData(name: "Other", rows: [[CellData(value: "B")]])

        let file1 = ExcelFile(filename: "file1.xlsx", filepath: "/tmp/file1.xlsx", sheets: [sheet1])
        let file2 = ExcelFile(filename: "file2.xlsx", filepath: "/tmp/file2.xlsx", sheets: [sheet2])

        let merger = SimpleMerger()
        let result = merger.merge(files: [file1, file2], sheetName: "Sheet1")

        // 只有 file1 有 Sheet1
        XCTAssertEqual(result.sourceFiles.count, 1)
    }
}
