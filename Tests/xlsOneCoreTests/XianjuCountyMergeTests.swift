import XCTest
@testable import xlsOneCore

/// 仙居县财务报表合并测试
/// 验证符合 PRD 中描述的仙居县测试场景
final class XianjuCountyMergeTests: XCTestCase {

    // MARK: - 场景1: 仙居县乡镇决算报表
    /// 5个乡镇的决算报表
    /// B列：科目代码（如"201"、"301"）→ 不可聚合，显示原值
    /// D列：金额（如"1000"、"2000"）→ 可聚合，显示总和

    func testXianjuCountyReport_BColumn_CodesAreLabels() {
        // 模拟B2单元格：所有文件都是科目代码 "201"
        let cells = [
            (filename: "官路镇.xlsx", cell: CellData(value: "201")),
            (filename: "湫山乡.xlsx", cell: CellData(value: "201")),
            (filename: "白塔镇.xlsx", cell: CellData(value: "201")),
            (filename: "溪港乡.xlsx", cell: CellData(value: "201")),
            (filename: "埠头镇.xlsx", cell: CellData(value: "201"))
        ]

        let merged = MergedCell.from(
            cells: cells,
            leftCells: [],
            neighborContext: NeighborContext(numericTendency: 0, labelTendency: 0),
            row: 1,
            col: 1
        )

        // 所有值相同 → 标签型
        XCTAssertEqual(merged.type, .label)
        XCTAssertEqual(merged.displayValue, "201")
    }

    func testXianjuCountyReport_DColumn_AmountsAreSummed() {
        // 模拟D2单元格：金额列，不同值（带小数 = strongNumeric）
        let cells = [
            (filename: "官路镇.xlsx", cell: CellData(value: "1500.50")),
            (filename: "湫山乡.xlsx", cell: CellData(value: "2000.00")),
            (filename: "白塔镇.xlsx", cell: CellData(value: "1,250.50")),
            (filename: "溪港乡.xlsx", cell: CellData(value: "1800")),
            (filename: "埠头镇.xlsx", cell: CellData(value: "2,000"))
        ]

        let merged = MergedCell.from(
            cells: cells,
            leftCells: [],
            neighborContext: NeighborContext(numericTendency: 0, labelTendency: 0),
            row: 1,
            col: 3
        )

        // 带小数的数值属于 strongNumeric，直接求和
        if case .sum(let total) = merged.type {
            XCTAssertEqual(total, 8551, accuracy: 0.01)
        } else {
            XCTFail("Expected sum type, got \(merged.type)")
        }
        XCTAssertEqual(merged.displayValue, "8551")
    }

    // MARK: - 场景2: 不同区域编码
    /// 不同文件的区域编码不同（如 331024001 vs 331024002）
    /// 结果：显示标签（编码不可累加）

    func testXianjuCounty_DifferentRegionCodes_ShowLabels() {
        let cells = [
            (filename: "官路镇.xlsx", cell: CellData(value: "331024001")),
            (filename: "湫山乡.xlsx", cell: CellData(value: "331024002")),
            (filename: "白塔镇.xlsx", cell: CellData(value: "331024003")),
            (filename: "溪港乡.xlsx", cell: CellData(value: "331024004"))
        ]

        let merged = MergedCell.from(
            cells: cells,
            leftCells: [],
            neighborContext: NeighborContext(numericTendency: 0, labelTendency: 0),
            row: 1,
            col: 1
        )

        // 9位区域编码，长度一致且有公共前缀 → 标签型
        XCTAssertEqual(merged.type, .label)
        XCTAssertEqual(merged.displayValue, "33102400_")
    }

    // MARK: - 场景3: 金额汇总
    /// 文件1的D2="1000"
    /// 文件2的D2="2000"
    /// 文件3的D2="1500"
    /// 结果：显示"4500"

    func testXianjuCounty_AmountSum_4500() {
        let cells = [
            (filename: "乡镇1.xlsx", cell: CellData(value: "1000")),
            (filename: "乡镇2.xlsx", cell: CellData(value: "2000")),
            (filename: "乡镇3.xlsx", cell: CellData(value: "1500"))
        ]

        let merged = MergedCell.from(
            cells: cells,
            leftCells: [],
            neighborContext: NeighborContext(numericTendency: 0, labelTendency: 0),
            row: 1,
            col: 3
        )

        if case .sum(let total) = merged.type {
            XCTAssertEqual(total, 4500)
        } else {
            XCTFail("Expected sum type, got \(merged.type)")
        }
        XCTAssertEqual(merged.displayValue, "4500")
    }

    // MARK: - 千分位格式测试

    func testXianjuCounty_ThousandSeparator_US() {
        // 美式格式：1,234.56
        let cell = CellData(value: "1,234.56")
        XCTAssertEqual(cell.numericValue, 1234.56)
    }

    func testXianjuCounty_ThousandSeparator_European() {
        // 欧式格式：1.234,56
        let cell = CellData(value: "1.234,56")
        XCTAssertEqual(cell.numericValue, 1234.56)
    }

    func testXianjuCounty_ThousandSeparator_MixedSum() {
        // 混合格式求和（带小数 = strongNumeric）
        let cells = [
            (filename: "乡镇1.xlsx", cell: CellData(value: "1,000.50")),
            (filename: "乡镇2.xlsx", cell: CellData(value: "2.000,50")),
            (filename: "乡镇3.xlsx", cell: CellData(value: "500.00"))
        ]

        let merged = MergedCell.from(
            cells: cells,
            leftCells: [],
            neighborContext: NeighborContext(numericTendency: 0, labelTendency: 0),
            row: 1,
            col: 3
        )

        // 1000.50 + 2000.50 + 500 = 3501
        if case .sum(let total) = merged.type {
            XCTAssertEqual(total, 3501, accuracy: 0.01)
        } else {
            XCTFail("Expected sum type")
        }
    }

    // MARK: - 完整报表合并测试

    func testXianjuCounty_FullReportMerge() {
        // 创建一个模拟的仙居县报表结构
        let headerRow = [
            CellData(value: "科目编码"),
            CellData(value: "科目名称"),
            CellData(value: "金额")
        ]

        // 官路镇数据
        let guanluSheet = SheetData(
            name: "报表主体",
            rows: [
                headerRow,
                [CellData(value: "201"), CellData(value: "一般公共服务"), CellData(value: "1000")],
                [CellData(value: "301"), CellData(value: "教育支出"), CellData(value: "2000")]
            ]
        )

        // 湫山乡数据
        let qiushanSheet = SheetData(
            name: "报表主体",
            rows: [
                headerRow,
                [CellData(value: "201"), CellData(value: "一般公共服务"), CellData(value: "1500")],
                [CellData(value: "301"), CellData(value: "教育支出"), CellData(value: "2500")]
            ]
        )

        let file1 = ExcelFile(filename: "官路镇.xlsx", filepath: "/tmp/官路镇.xlsx", sheets: [guanluSheet])
        let file2 = ExcelFile(filename: "湫山乡.xlsx", filepath: "/tmp/湫山乡.xlsx", sheets: [qiushanSheet])

        let merger = SimpleMerger()
        let result = merger.merge(files: [file1, file2], sheetName: "报表主体")

        XCTAssertEqual(result.sheetName, "报表主体")
        XCTAssertEqual(result.sourceFiles.count, 2)

        // 验证表头（第一行）- 应该是标签型
        XCTAssertEqual(result.rows[0][0].type, .label)
        XCTAssertEqual(result.rows[0][1].type, .label)

        // 验证科目编码列 - 所有文件相同，应该是标签型
        XCTAssertEqual(result.rows[1][0].type, .label)
        XCTAssertEqual(result.rows[1][0].displayValue, "201")

        // 验证科目名称列 - 所有文件相同，应该是标签型
        XCTAssertEqual(result.rows[1][1].type, .label)
        XCTAssertEqual(result.rows[1][1].displayValue, "一般公共服务")

        // 验证金额列 - 值不同且是数值，左邻是"一般公共服务"（含"名称"关键词增强数值倾向的抵消较弱）
        // 实际：1000/1500 是 integerWide + 垂直穿透一致性高 → sum
        if case .sum(let total) = result.rows[1][2].type {
            XCTAssertEqual(total, 2500)
        } else {
            XCTFail("Expected sum type for amount column, got \(result.rows[1][2].type)")
        }
        XCTAssertEqual(result.rows[1][2].displayValue, "2500")

        if case .sum(let total) = result.rows[2][2].type {
            XCTAssertEqual(total, 4500)
        } else {
            XCTFail("Expected sum type for amount column, got \(result.rows[2][2].type)")
        }
        XCTAssertEqual(result.rows[2][2].displayValue, "4500")
    }

    // MARK: - 穿透查阅测试

    func testXianjuCounty_DrillDown() {
        let cells = [
            (filename: "官路镇.xlsx", cell: CellData(value: "1500")),
            (filename: "湫山乡.xlsx", cell: CellData(value: "2000")),
            (filename: "白塔镇.xlsx", cell: CellData(value: "1200"))
        ]

        let merged = MergedCell.from(
            cells: cells,
            leftCells: [],
            neighborContext: NeighborContext(numericTendency: 0, labelTendency: 0),
            row: 1,
            col: 3
        )

        // 验证来源值映射
        XCTAssertEqual(merged.sourceValues["官路镇.xlsx"], "1500")
        XCTAssertEqual(merged.sourceValues["湫山乡.xlsx"], "2000")
        XCTAssertEqual(merged.sourceValues["白塔镇.xlsx"], "1200")

        // 验证显示值
        if case .sum(let total) = merged.type {
            XCTAssertEqual(total, 4700)
        } else {
            XCTFail("Expected sum type, got \(merged.type)")
        }
        XCTAssertEqual(merged.displayValue, "4700")
    }
}
