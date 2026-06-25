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

        let merged = MergedCell.from(
            cells: cells,
            leftCells: [],
            neighborContext: NeighborContext(numericTendency: 0, labelTendency: 0),
            row: 1,
            col: 1
        )

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

        let merged = MergedCell.from(
            cells: cells,
            leftCells: [],
            neighborContext: NeighborContext(numericTendency: 0, labelTendency: 0),
            row: 1,
            col: 1
        )

        if case .sum(let total) = merged.type {
            XCTAssertEqual(total, 4500)
        } else {
            XCTFail("Expected sum type")
        }
        XCTAssertEqual(merged.displayValue, "4500")
    }

    func testMergedCellMixedType() {
        // 9位区域编码，长度一致且有公共前缀 → 标签型（编码不可累加）
        let cells = [
            (filename: "file1", cell: CellData(value: "331024001")),
            (filename: "file2", cell: CellData(value: "331024002")),
            (filename: "file3", cell: CellData(value: "331024003"))
        ]

        let merged = MergedCell.from(
            cells: cells,
            leftCells: [],
            neighborContext: NeighborContext(numericTendency: 0, labelTendency: 0),
            row: 1,
            col: 1
        )

        // 9位编码，长度一致且有公共前缀 → 标签
        XCTAssertEqual(merged.type, .label)
        XCTAssertEqual(merged.displayValue, "33102400_")
    }

    func testMergedCellMaKeywordWithEqualLengthIntegersAreLabels() {
        // 左邻含"码"字（如"验证码"），当前格是统一长度整数 → 标签（编码）
        let cells = [
            (filename: "file1", cell: CellData(value: "123456")),
            (filename: "file2", cell: CellData(value: "654321")),
            (filename: "file3", cell: CellData(value: "111111"))
        ]

        let merged = MergedCell.from(
            cells: cells,
            leftCells: [(filename: "file1", cell: CellData(value: "验证码"))],
            neighborContext: NeighborContext(numericTendency: 0, labelTendency: 0),
            row: 1,
            col: 1
        )

        XCTAssertEqual(merged.type, .label)
        // 无公共前缀，标准长度=6，显示 6 个下划线
        XCTAssertEqual(merged.displayValue, "______")
    }

    func testMergedCellHaoKeywordWithEqualLengthIntegersAreLabels() {
        // 左邻含"号"字（如"身份证号"、"学号"），当前格是统一长度整数 → 标签（编码）
        let cells = [
            (filename: "file1", cell: CellData(value: "331024199001011111")),
            (filename: "file2", cell: CellData(value: "331024198502022222")),
            (filename: "file3", cell: CellData(value: "331024200003033333"))
        ]

        let merged = MergedCell.from(
            cells: cells,
            leftCells: [(filename: "file1", cell: CellData(value: "身份证号"))],
            neighborContext: NeighborContext(numericTendency: 0, labelTendency: 0),
            row: 1,
            col: 1
        )

        XCTAssertEqual(merged.type, .label)
        // 公共前缀 "331024"，标准长度=18，剩余 12 个下划线
        XCTAssertEqual(merged.displayValue, "331024____________")
    }

    func testMergedCellSingleType() {
        // 只有一个文件 → 单值
        let cells = [
            (filename: "file1", cell: CellData(value: "hello"))
        ]

        let merged = MergedCell.from(
            cells: cells,
            leftCells: [],
            neighborContext: NeighborContext(numericTendency: 0, labelTendency: 0),
            row: 1,
            col: 1
        )

        if case .single(let value) = merged.type {
            XCTAssertEqual(value, "hello")
        } else {
            XCTFail("Expected single type")
        }
        XCTAssertEqual(merged.displayValue, "hello")
    }

    func testSingleNumericValueWithNumericFormatBecomesSum() {
        let cells: [(filename: String, cell: CellData?)] = [
            (filename: "file1", cell: CellData(value: "1250.50", numericValue: 1250.5, formatCode: "#,##0.00")),
            (filename: "file2", cell: nil)
        ]

        let merged = MergedCell.from(
            cells: cells,
            leftCells: [(filename: "file1", cell: CellData(value: "金额"))],
            neighborContext: NeighborContext(numericTendency: 0, labelTendency: 0),
            row: 2,
            col: 2
        )

        XCTAssertEqual(merged.type, .sum(1250.5))
    }

    func testSingleZeroWithBlankSourcesAndNumericContextBecomesSum() {
        let cells: [(filename: String, cell: CellData?)] = [
            (filename: "file1", cell: CellData(value: "0.0", numericValue: 0, formatCode: "0.0")),
            (filename: "file2", cell: CellData(value: "")),
            (filename: "file3", cell: nil)
        ]

        let merged = MergedCell.from(
            cells: cells,
            leftCells: [],
            neighborContext: NeighborContext(numericTendency: 0.6, labelTendency: 0.1),
            row: 4,
            col: 3
        )

        XCTAssertEqual(merged.type, .sum(0))
        XCTAssertEqual(merged.displayValue, "0.0")
    }

    func testAllZeroDecimalValuesBecomeSum() {
        let cells: [(filename: String, cell: CellData?)] = [
            (filename: "file1", cell: CellData(value: "0.0", numericValue: 0, formatCode: "0.0")),
            (filename: "file2", cell: CellData(value: "0.0", numericValue: 0, formatCode: "0.0")),
            (filename: "file3", cell: CellData(value: "0.0", numericValue: 0, formatCode: "0.0"))
        ]

        let merged = MergedCell.from(
            cells: cells,
            leftCells: [],
            neighborContext: NeighborContext(numericTendency: 0, labelTendency: 0),
            row: 2,
            col: 2
        )

        XCTAssertEqual(merged.type, .sum(0))
        XCTAssertEqual(merged.displayValue, "0.0")
        XCTAssertTrue(merged.decision.decisionReasons.contains("所有非空来源均为 0，按可累加单元格求和处理"))
    }

    func testAllZeroDecimalValuesRespectCodeSemanticVeto() {
        let cells: [(filename: String, cell: CellData?)] = [
            (filename: "file1", cell: CellData(value: "0.0", numericValue: 0, formatCode: "0.0")),
            (filename: "file2", cell: CellData(value: "0.0", numericValue: 0, formatCode: "0.0"))
        ]

        let merged = MergedCell.from(
            cells: cells,
            leftCells: [(filename: "file1", cell: CellData(value: "人员编号"))],
            neighborContext: NeighborContext(numericTendency: 0, labelTendency: 0),
            row: 2,
            col: 2
        )

        XCTAssertEqual(merged.type, .label)
        XCTAssertEqual(merged.displayValue, "0.0")
    }

    func testIdenticalNonZeroIntegerValuesPreferLabel() {
        let cells: [(filename: String, cell: CellData?)] = [
            (filename: "file1", cell: CellData(value: "2024")),
            (filename: "file2", cell: CellData(value: "2024")),
            (filename: "file3", cell: CellData(value: "2024"))
        ]

        let merged = MergedCell.from(
            cells: cells,
            leftCells: [],
            neighborContext: NeighborContext(numericTendency: 0, labelTendency: 0),
            row: 3,
            col: 2
        )

        XCTAssertEqual(merged.type, .label)
        XCTAssertEqual(merged.displayValue, "2024")
        XCTAssertTrue(merged.decision.decisionReasons.contains("所有来源为相同非零整数，且无明确可累加语义，按标签处理"))
    }

    func testIdenticalNonZeroIntegerValuesInFirstColumnPreferLabel() {
        let cells: [(filename: String, cell: CellData?)] = [
            (filename: "file1", cell: CellData(value: "2024")),
            (filename: "file2", cell: CellData(value: "2024")),
            (filename: "file3", cell: CellData(value: "2024"))
        ]

        let merged = MergedCell.from(
            cells: cells,
            leftCells: [],
            neighborContext: NeighborContext(numericTendency: 0, labelTendency: 0),
            row: 3,
            col: 0
        )

        XCTAssertEqual(merged.type, .label)
        XCTAssertEqual(merged.displayValue, "2024")
        XCTAssertTrue(merged.decision.decisionReasons.contains("首列所有来源为相同非零整数，按标签处理"))
    }

    func testIdenticalNonZeroIntegerValuesWithAmountSemanticCanSum() {
        let cells: [(filename: String, cell: CellData?)] = [
            (filename: "file1", cell: CellData(value: "1")),
            (filename: "file2", cell: CellData(value: "1")),
            (filename: "file3", cell: CellData(value: "1"))
        ]

        let merged = MergedCell.from(
            cells: cells,
            leftCells: [(filename: "file1", cell: CellData(value: "人数"))],
            neighborContext: NeighborContext(numericTendency: 0, labelTendency: 0),
            row: 3,
            col: 2
        )

        XCTAssertEqual(merged.type, .sum(3))
        XCTAssertEqual(merged.displayValue, "3")
    }

    func testRepeatedIntegerCountsBecomeSumWhenMetricEvidenceAgrees() {
        let cells: [(filename: String, cell: CellData?)] = [
            (filename: "file1", cell: CellData(value: "1", numericValue: 1, formatCode: "#,##0")),
            (filename: "file2", cell: CellData(value: "1", numericValue: 1, formatCode: "#,##0")),
            (filename: "file3", cell: CellData(value: "1", numericValue: 1, formatCode: "#,##0"))
        ]

        let merged = MergedCell.from(
            cells: cells,
            leftCells: [(filename: "file1", cell: CellData(value: "二、乡镇财政机构数"))],
            neighborContext: NeighborContext(numericTendency: 0.7, labelTendency: 0, columnMetricTendency: 0.8),
            row: 7,
            col: 2
        )

        XCTAssertEqual(merged.type, .sum(3))
        XCTAssertEqual(merged.displayValue, "3")
    }

    func testWeakMetricWordAloneKeepsRepeatedIntegerConservative() {
        let cells: [(filename: String, cell: CellData?)] = [
            (filename: "file1", cell: CellData(value: "1", numericValue: 1, formatCode: "#,##0")),
            (filename: "file2", cell: CellData(value: "1", numericValue: 1, formatCode: "#,##0"))
        ]

        let merged = MergedCell.from(
            cells: cells,
            leftCells: [(filename: "file1", cell: CellData(value: "样本数"))],
            neighborContext: NeighborContext(numericTendency: 0, labelTendency: 0),
            row: 4,
            col: 2
        )

        XCTAssertEqual(merged.type, .label)
        XCTAssertEqual(merged.displayValue, "1")
    }

    func testCodeSemanticsStillProtectAgainstMetricColumnContext() {
        let cells: [(filename: String, cell: CellData?)] = [
            (filename: "file1", cell: CellData(value: "331024001")),
            (filename: "file2", cell: CellData(value: "331024002"))
        ]

        let merged = MergedCell.from(
            cells: cells,
            leftCells: [(filename: "file1", cell: CellData(value: "行政区划代码"))],
            neighborContext: NeighborContext(numericTendency: 0.8, labelTendency: 0, columnMetricTendency: 0.8),
            row: 2,
            col: 3
        )

        XCTAssertEqual(merged.type, .label)
        XCTAssertEqual(merged.displayValue, "33102400_")
    }

    func testColumnMetricAnchorSumsTownFinanceCountRows() {
        func countCell(_ value: Double) -> CellData {
            CellData(value: String(Int(value)), numericValue: value, formatCode: "#,##0")
        }

        func makeSheet() -> SheetData {
            SheetData(name: "乡镇财政基本情况表", rows: [
                [CellData(value: ""), CellData(value: ""), CellData(value: "")],
                [CellData(value: ""), CellData(value: "01表：乡镇财政基本情况表"), CellData(value: "")],
                [CellData(value: ""), CellData(value: ""), CellData(value: "单位：人、个、万元")],
                [CellData(value: ""), CellData(value: "项  目 (一)"), CellData(value: "决算数(一)")],
                [CellData(value: ""), CellData(value: "一、本年乡镇数"), countCell(1)],
                [CellData(value: ""), CellData(value: "其中:实行“乡财县管”的乡镇数"), countCell(0)],
                [CellData(value: ""), CellData(value: "二、乡镇财政机构数"), countCell(1)],
                [CellData(value: ""), CellData(value: "三、已建立乡镇国库的乡镇数"), countCell(0)],
                [CellData(value: ""), CellData(value: "四、实行“分税制”管理体制的乡镇数"), countCell(1)]
            ])
        }

        let files = [
            ExcelFile(filename: "a.xlsx", filepath: "/a.xlsx", sheets: [makeSheet()]),
            ExcelFile(filename: "b.xlsx", filepath: "/b.xlsx", sheets: [makeSheet()]),
            ExcelFile(filename: "c.xlsx", filepath: "/c.xlsx", sheets: [makeSheet()])
        ]

        let result = SimpleMerger().merge(files: files, sheetName: "乡镇财政基本情况表")
        XCTAssertEqual(result.rows[4][2].type, .sum(3))
        XCTAssertEqual(result.rows[6][2].type, .sum(3))
        XCTAssertEqual(result.rows[8][2].type, .sum(3))
    }

    func testAmountColumnWithMixedIntegerAndDecimalValuesIsOrderIndependent() {
        func sheet(amounts: [Double]) -> SheetData {
            SheetData(name: "费用汇总表", rows: [
                [
                    CellData(value: "费用编码"),
                    CellData(value: "费用名称"),
                    CellData(value: "归口部门"),
                    CellData(value: "本期金额"),
                    CellData(value: "备注")
                ],
                [
                    CellData(value: "FY-001"),
                    CellData(value: "人员经费"),
                    CellData(value: "行政管理部"),
                    amountCell(amounts[0]),
                    CellData(value: "预算内")
                ],
                [
                    CellData(value: "FY-002"),
                    CellData(value: "办公耗材"),
                    CellData(value: "综合保障部"),
                    amountCell(amounts[1]),
                    CellData(value: "预算内")
                ],
                [
                    CellData(value: "FY-003"),
                    CellData(value: "设备维护"),
                    CellData(value: "信息技术部"),
                    amountCell(amounts[2]),
                    CellData(value: "维护批次")
                ],
                [
                    CellData(value: "FY-004"),
                    CellData(value: "培训会议"),
                    CellData(value: "业务发展部"),
                    amountCell(amounts[3]),
                    CellData(value: "预算内")
                ],
                [
                    CellData(value: "合计"),
                    CellData(value: "合计"),
                    CellData(value: ""),
                    CellData(value: ""),
                    CellData(value: "自动汇总")
                ]
            ])
        }

        let files = [
            ExcelFile(filename: "测试单位A.xlsx", filepath: "/tmp/a.xlsx", sheets: [
                sheet(amounts: [12800.5, 5400, 2300.75, 980])
            ]),
            ExcelFile(filename: "测试单位B.xlsx", filepath: "/tmp/b.xlsx", sheets: [
                sheet(amounts: [15600, 6100.25, 1800, 1250])
            ]),
            ExcelFile(filename: "测试单位C.xlsx", filepath: "/tmp/c.xlsx", sheets: [
                sheet(amounts: [9900, 4800, 2600, 870.5])
            ]),
            ExcelFile(filename: "测试单位D.xlsx", filepath: "/tmp/d.xlsx", sheets: [
                sheet(amounts: [14200.75, 5900, 2100, 1100])
            ])
        ]

        for orderedFiles in [files, files.reversed()] {
            let result = SimpleMerger().merge(files: Array(orderedFiles), sheetName: "费用汇总表")

            XCTAssertEqual(result.rows[1][3].type, .sum(52501.25))
            XCTAssertEqual(result.rows[2][3].type, .sum(22200.25))
            XCTAssertEqual(result.rows[3][3].type, .sum(8800.75))
            XCTAssertEqual(result.rows[4][3].type, .sum(4200.5))
            XCTAssertEqual(result.rows[5][3].type, .label)
        }
    }

    private func amountCell(_ value: Double) -> CellData {
        let display: String
        if value == floor(value) {
            display = String(format: "%.0f", value)
        } else {
            display = String(value)
        }
        return CellData(value: display, numericValue: value, formatCode: "General")
    }

    func testNumericValuesWithBlankSourcesTreatBlanksAsZero() {
        let cells: [(filename: String, cell: CellData?)] = [
            (filename: "file1", cell: CellData(value: "123456")),
            (filename: "file2", cell: CellData(value: "")),
            (filename: "file3", cell: CellData(value: "123456")),
            (filename: "file4", cell: nil)
        ]

        let merged = MergedCell.from(
            cells: cells,
            leftCells: [],
            neighborContext: NeighborContext(numericTendency: 0, labelTendency: 0),
            row: 3,
            col: 2
        )

        XCTAssertEqual(merged.type, .sum(246912))
        XCTAssertEqual(merged.displayValue, "246912")
        XCTAssertTrue(
            merged.decision.decisionReasons.contains("部分来源为空或缺失，非空来源均为数值，空值按 0 参与可累加判断")
        )
    }

    func testNumericValuesWithBlankSourcesRespectCodeSemanticVeto() {
        let cells: [(filename: String, cell: CellData?)] = [
            (filename: "file1", cell: CellData(value: "331024001")),
            (filename: "file2", cell: CellData(value: "")),
            (filename: "file3", cell: CellData(value: "331024002"))
        ]

        let merged = MergedCell.from(
            cells: cells,
            leftCells: [(filename: "file1", cell: CellData(value: "行政区划代码"))],
            neighborContext: NeighborContext(numericTendency: 0, labelTendency: 0),
            row: 3,
            col: 2
        )

        XCTAssertEqual(merged.type, .label)
        XCTAssertEqual(merged.displayValue, "33102400_")
    }

    func testOverrideLabelUsesCommonPrefixDisplay() {
        let sources = [
            CellSourceEntry(filename: "a.xlsx", filepath: "/tmp/a.xlsx", value: "331024001", state: .value),
            CellSourceEntry(filename: "b.xlsx", filepath: "/tmp/b.xlsx", value: "331024002", state: .value),
            CellSourceEntry(filename: "c.xlsx", filepath: "/tmp/c.xlsx", value: "331024003", state: .value)
        ]
        let cell = MergedCell(type: .single("331024001"), sources: sources)
        let result = MergedResult(sheetName: "Sheet1", rows: [[cell]], sourceFiles: ["a.xlsx", "b.xlsx", "c.xlsx"])

        let overridden = SmartMerger().applyOverrides(
            to: result,
            overrides: [
                CellTypeOverride(sheetName: "Sheet1", rowIndex: 0, colIndex: 0, cellType: .label)
            ]
        )

        XCTAssertEqual(overridden.rows[0][0].type, .label)
        XCTAssertEqual(overridden.rows[0][0].displayValue, "33102400_")
    }

    func testMergedCellWithEmptyValues() {
        // 包含空值的测试
        let cells = [
            (filename: "file1", cell: CellData(value: "1000")),
            (filename: "file2", cell: CellData(value: "")),
            (filename: "file3", cell: CellData(value: "2000"))
        ]

        let merged = MergedCell.from(
            cells: cells,
            leftCells: [],
            neighborContext: NeighborContext(numericTendency: 0, labelTendency: 0),
            row: 1,
            col: 1
        )

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

    /// 验证空行保留：所有文件同一值的标签，最终位置与原始位置一致
    func testMergePreservesEmptyRowAlignment() {
        // 模拟解析后保留空行的结构：row0 表头，row1 空行，row2 数据
        let sheet1 = SheetData(
            name: "Sheet1",
            rows: [
                [CellData(value: "科目编码"), CellData(value: "科目名称")],
                [], // 空行
                [CellData(value: "201"), CellData(value: "一般公共服务")]
            ]
        )

        let sheet2 = SheetData(
            name: "Sheet1",
            rows: [
                [CellData(value: "科目编码"), CellData(value: "科目名称")],
                [], // 空行
                [CellData(value: "201"), CellData(value: "一般公共服务")]
            ]
        )

        let file1 = ExcelFile(filename: "file1.xlsx", filepath: "/tmp/file1.xlsx", sheets: [sheet1])
        let file2 = ExcelFile(filename: "file2.xlsx", filepath: "/tmp/file2.xlsx", sheets: [sheet2])

        let merger = SimpleMerger()
        let result = merger.merge(files: [file1, file2], sheetName: "Sheet1")

        // 应该有 3 行
        XCTAssertEqual(result.rows.count, 3)

        // 第 0 行：表头标签
        XCTAssertEqual(result.rows[0][0].type, .label)
        XCTAssertEqual(result.rows[0][0].displayValue, "科目编码")

        // 第 1 行：空行（保留位置）
        // 由于所有文件在该行都是空值，合并后应为标签但显示值为空
        XCTAssertEqual(result.rows[1].count, 2)
        XCTAssertEqual(result.rows[1][0].type, .label)
        XCTAssertEqual(result.rows[1][0].displayValue, "")
        XCTAssertEqual(result.rows[1][1].type, .label)
        XCTAssertEqual(result.rows[1][1].displayValue, "")

        // 第 2 行：数据标签
        XCTAssertEqual(result.rows[2][0].type, .label)
        XCTAssertEqual(result.rows[2][0].displayValue, "201")
        XCTAssertEqual(result.rows[2][1].type, .label)
        XCTAssertEqual(result.rows[2][1].displayValue, "一般公共服务")
    }
}
