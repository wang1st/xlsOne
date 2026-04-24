import XCTest
import xlsOneCore
@testable import xlsOne

final class WorkspaceDiagnosticsTests: XCTestCase {
    func testAnomalyQueueStaysEmptyWhenReviewHintsAreHidden() {
        let mixedCell = MergedCell(
            type: .mixed(2),
            displayValue: "2条",
            sources: [],
            decision: MergedCellDecision(
                autoDetectedType: .mixed(2),
                confidence: 0.9,
                decisionReasons: ["mixed"],
                isSuspicious: true
            )
        )
        let lowConfidenceCell = MergedCell(
            type: .label,
            displayValue: "33102400_",
            sources: [],
            decision: MergedCellDecision(
                autoDetectedType: .label,
                confidence: 0.55,
                decisionReasons: ["low confidence"],
                isSuspicious: true
            )
        )
        let overriddenCell = MergedCell(
            type: .sum(3000),
            displayValue: "3000",
            sources: [],
            isOverridden: true,
            decision: MergedCellDecision(
                autoDetectedType: .label,
                confidence: 0.92,
                decisionReasons: ["overridden"],
                isSuspicious: false
            )
        )

        let result = MergedResult(
            sheetName: "Sheet1",
            rows: [
                [mixedCell, lowConfidenceCell],
                [overriddenCell]
            ],
            sourceFiles: ["a.xlsx", "b.xlsx"]
        )

        let queue = WorkspaceDiagnostics.buildAnomalyQueue(for: result)

        XCTAssertTrue(queue.isEmpty)
    }

    func testColumnLettersSupportMultipleCharacters() {
        XCTAssertEqual(WorkspaceDiagnostics.columnLetters(0), "A")
        XCTAssertEqual(WorkspaceDiagnostics.columnLetters(25), "Z")
        XCTAssertEqual(WorkspaceDiagnostics.columnLetters(26), "AA")
        XCTAssertEqual(WorkspaceDiagnostics.cellReference(row: 4, col: 27), "AB5")
    }

    func testSheetOverviewBuildsMergedAndSkippedRows() {
        let report = WorkbookValidationReport(
            readiness: .ready,
            templateFile: nil,
            files: [
                FileValidationReport(
                    filename: "a.xlsx",
                    filepath: "/tmp/a.xlsx",
                    status: .included,
                    isTemplate: true,
                    issues: [],
                    sheetReports: [
                        SheetValidationReport(
                            sheetName: "Sheet1",
                            readiness: .ready,
                            issues: [],
                            templateRowCount: 12,
                            templateColumnCount: 5,
                            candidateRowCount: 12,
                            candidateColumnCount: 5
                        ),
                        SheetValidationReport(
                            sheetName: "Sheet2",
                            readiness: .blocked,
                            issues: [],
                            templateRowCount: 8,
                            templateColumnCount: 4,
                            candidateRowCount: 8,
                            candidateColumnCount: 4
                        )
                    ]
                ),
                FileValidationReport(
                    filename: "b.xlsx",
                    filepath: "/tmp/b.xlsx",
                    status: .included,
                    isTemplate: false,
                    issues: [],
                    sheetReports: [
                        SheetValidationReport(
                            sheetName: "Sheet1",
                            readiness: .ready,
                            issues: [],
                            templateRowCount: 12,
                            templateColumnCount: 5,
                            candidateRowCount: 12,
                            candidateColumnCount: 5
                        ),
                        SheetValidationReport(
                            sheetName: "Sheet2",
                            readiness: .blocked,
                            issues: [],
                            templateRowCount: 8,
                            templateColumnCount: 4,
                            candidateRowCount: 9,
                            candidateColumnCount: 4
                        )
                    ]
                )
            ],
            commonSheetNames: ["Sheet1"],
            skippedSheetNames: ["Sheet2"],
            skippedSheetIssues: [
                ValidationIssue(
                    severity: .warning,
                    code: .rowCountMismatch,
                    fileName: "b.xlsx",
                    filePath: "/tmp/b.xlsx",
                    sheetName: "Sheet2",
                    message: "工作表“Sheet2”有效行数不一致（忽略尾部空白后：多数文件为 8 行，当前文件为 9 行），已从本次汇总中排除"
                )
            ]
        )

        let items = WorkspaceDiagnostics.buildSheetOverview(
            report: report,
            anomalyItems: [
                CellAnomalyItem(
                    sheetName: "Sheet1",
                    position: CellPosition(row: 0, col: 0),
                    cellReference: "A1",
                    displayValue: "100",
                    summary: "存在多种来源值"
                )
            ]
        )

        XCTAssertEqual(items.map(\.sheetName), ["Sheet1", "Sheet2"])
        XCTAssertEqual(items[0].status, .mergeable)
        XCTAssertEqual(items[0].participatingFileCount, 2)
        XCTAssertEqual(items[0].totalFileCount, 2)
        XCTAssertEqual(items[0].effectiveRowCount, 12)
        XCTAssertEqual(items[0].anomalyCount, 1)

        XCTAssertEqual(items[1].status, .skipped)
        XCTAssertEqual(items[1].participatingFileCount, 1)
        XCTAssertEqual(items[1].reasonSummary, "有效行数不一致")
        XCTAssertEqual(items[1].detailMessages.count, 1)
    }

    func testSheetOverviewMissingSheetUsesConciseReasonSummary() {
        let report = WorkbookValidationReport(
            readiness: .blocked,
            templateFile: nil,
            files: [
                FileValidationReport(
                    filename: "a.xlsx",
                    filepath: "/tmp/a.xlsx",
                    status: .included,
                    isTemplate: true,
                    issues: [],
                    sheetReports: [
                        SheetValidationReport(
                            sheetName: "SheetX",
                            readiness: .blocked,
                            issues: [],
                            templateRowCount: 6,
                            templateColumnCount: 3,
                            candidateRowCount: 6,
                            candidateColumnCount: 3
                        )
                    ]
                ),
                FileValidationReport(
                    filename: "b.xlsx",
                    filepath: "/tmp/b.xlsx",
                    status: .included,
                    isTemplate: false,
                    issues: [],
                    sheetReports: [
                        SheetValidationReport(
                            sheetName: "SheetX",
                            readiness: .blocked,
                            issues: [],
                            templateRowCount: 6,
                            templateColumnCount: 3,
                            candidateRowCount: 0,
                            candidateColumnCount: 0
                        )
                    ]
                )
            ],
            commonSheetNames: [],
            skippedSheetNames: ["SheetX"],
            skippedSheetIssues: [
                ValidationIssue(
                    severity: .warning,
                    code: .missingSheet,
                    fileName: "b.xlsx",
                    filePath: "/tmp/b.xlsx",
                    sheetName: "SheetX",
                    message: "工作表“SheetX”未在所有文件中同时出现，已从本次汇总中排除"
                )
            ]
        )

        let items = WorkspaceDiagnostics.buildSheetOverview(report: report, anomalyItems: [])

        XCTAssertEqual(items.first?.reasonSummary, "部分文件缺少该工作表")
    }

    func testSkippedSheetConsensusBuildsStructureGroupsFromOverallComparison() {
        let report = WorkbookValidationReport(
            readiness: .ready,
            templateFile: nil,
            files: [
                FileValidationReport(
                    filename: "a.xlsx",
                    filepath: "/tmp/a.xlsx",
                    status: .included,
                    isTemplate: false,
                    issues: [],
                    sheetReports: [
                        SheetValidationReport(
                            sheetName: "Sheet2",
                            readiness: .blocked,
                            issues: [],
                            templateRowCount: 8,
                            templateColumnCount: 4,
                            candidateRowCount: 8,
                            candidateColumnCount: 4
                        )
                    ]
                ),
                FileValidationReport(
                    filename: "b.xlsx",
                    filepath: "/tmp/b.xlsx",
                    status: .included,
                    isTemplate: false,
                    issues: [],
                    sheetReports: [
                        SheetValidationReport(
                            sheetName: "Sheet2",
                            readiness: .blocked,
                            issues: [],
                            templateRowCount: 8,
                            templateColumnCount: 4,
                            candidateRowCount: 8,
                            candidateColumnCount: 4
                        )
                    ]
                ),
                FileValidationReport(
                    filename: "c.xlsx",
                    filepath: "/tmp/c.xlsx",
                    status: .included,
                    isTemplate: false,
                    issues: [
                        ValidationIssue(
                            severity: .warning,
                            code: .missingSheet,
                            fileName: "c.xlsx",
                            filePath: "/tmp/c.xlsx",
                            sheetName: "Sheet2",
                            message: "工作表“Sheet2”未在所有文件中同时出现，已从本次汇总中排除"
                        )
                    ],
                    sheetReports: [
                        SheetValidationReport(
                            sheetName: "Sheet2",
                            readiness: .blocked,
                            issues: [
                                ValidationIssue(
                                    severity: .warning,
                                    code: .missingSheet,
                                    fileName: "c.xlsx",
                                    filePath: "/tmp/c.xlsx",
                                    sheetName: "Sheet2",
                                    message: "工作表“Sheet2”未在所有文件中同时出现，已从本次汇总中排除"
                                )
                            ],
                            templateRowCount: 0,
                            templateColumnCount: 0,
                            candidateRowCount: 0,
                            candidateColumnCount: 0
                        )
                    ]
                )
            ],
            commonSheetNames: [],
            skippedSheetNames: ["Sheet2"],
            skippedSheetIssues: []
        )

        let consensus = WorkspaceDiagnostics.buildSkippedSheetConsensus(report: report, sheetName: "Sheet2")

        XCTAssertEqual(consensus?.comparedFileCount, 3)
        XCTAssertEqual(consensus?.groupCount, 2)
        XCTAssertEqual(consensus?.groups.first?.detail, "有效尺寸 8 行 × 4 列")
        XCTAssertEqual(consensus?.groups.first?.fileCount, 2)
        XCTAssertEqual(consensus?.groups.last?.detail, "未包含该工作表")
    }

    func testWorkspaceSummaryUsesCompactReadyStateCopy() {
        let report = WorkbookValidationReport(
            readiness: .ready,
            templateFile: nil,
            files: [
                FileValidationReport(
                    filename: "a.xlsx",
                    filepath: "/tmp/a.xlsx",
                    status: .included,
                    isTemplate: false,
                    issues: [],
                    sheetReports: []
                ),
                FileValidationReport(
                    filename: "b.xlsx",
                    filepath: "/tmp/b.xlsx",
                    status: .included,
                    isTemplate: false,
                    issues: [],
                    sheetReports: []
                )
            ],
            commonSheetNames: ["Sheet1", "Sheet2", "Sheet3"],
            skippedSheetNames: ["Sheet4"],
            skippedSheetIssues: []
        )

        let summary = WorkspaceDiagnostics.workspaceSummary(report: report)

        XCTAssertEqual(summary, "2 个文件参与 · 3 张可合并 · 1 张跳过")
    }

    func testDecisionSummaryUsesMostDecisiveReason() {
        let cell = MergedCell(
            type: .label,
            displayValue: "331024001",
            sources: [],
            decision: MergedCellDecision(
                autoDetectedType: .label,
                confidence: 0.67,
                decisionReasons: [
                    "自身格式指纹: 整数编码",
                    "综合得分偏向标签"
                ],
                isSuspicious: true
            )
        )

        let summary = WorkspaceDiagnostics.decisionSummary(for: cell)

        XCTAssertEqual(summary, "综合得分偏向标签")
    }

    func testSourceInspectionOverviewBuildsCompactSummary() {
        let sources = [
            CellSourceEntry(filename: "a.xlsx", filepath: "/tmp/a.xlsx", value: "100", state: .value),
            CellSourceEntry(filename: "b.xlsx", filepath: "/tmp/b.xlsx", value: "100", state: .value),
            CellSourceEntry(filename: "c.xlsx", filepath: "/tmp/c.xlsx", value: "", state: .empty),
            CellSourceEntry(filename: "d.xlsx", filepath: "/tmp/d.xlsx", value: "", state: .missing)
        ]

        let overview = WorkspaceDiagnostics.buildSourceInspectionOverview(for: sources)

        XCTAssertEqual(overview.sourceCount, 4)
        XCTAssertEqual(overview.valueCount, 2)
        XCTAssertEqual(overview.emptyCount, 1)
        XCTAssertEqual(overview.missingCount, 1)
        XCTAssertEqual(overview.distinctValueCount, 1)
        XCTAssertEqual(overview.summaryText, "4 个来源，1 个空值，1 个缺失")
    }

    func testSourceInspectionOverviewOnlyHighlightsMismatchWhenValuesDiffer() {
        let sources = [
            CellSourceEntry(filename: "a.xlsx", filepath: "/tmp/a.xlsx", value: "100", state: .value),
            CellSourceEntry(filename: "b.xlsx", filepath: "/tmp/b.xlsx", value: "120", state: .value),
            CellSourceEntry(filename: "c.xlsx", filepath: "/tmp/c.xlsx", value: "", state: .empty)
        ]

        let overview = WorkspaceDiagnostics.buildSourceInspectionOverview(for: sources)

        XCTAssertEqual(overview.distinctValueCount, 2)
        XCTAssertEqual(overview.summaryText, "3 个来源，1 个空值，内容不完全一致")
    }

    func testCompactSourceNamesTrimSharedWorkbookSuffix() {
        let sources = [
            CellSourceEntry(
                filename: "仙居县安洲街道办事处2025乡镇报表主体信息表.xlsx",
                filepath: "/tmp/a.xlsx",
                value: "1",
                state: .value
            ),
            CellSourceEntry(
                filename: "仙居县白塔镇人民政府2025乡镇报表主体信息表.xlsx",
                filepath: "/tmp/b.xlsx",
                value: "2",
                state: .value
            )
        ]

        let names = WorkspaceDiagnostics.compactSourceNames(for: sources)

        XCTAssertEqual(names, ["仙居县安洲街道办事处", "仙居县白塔镇人民政府"])
    }

    func testExportNamingPrefersLongestSharedPhraseAcrossFilenames() {
        let exportName = ExportNaming.suggestedWorkbookName(
            from: [
                "仙居县安洲街道办事处2025乡镇报表主体信息表.xlsx",
                "仙居县白塔镇人民政府2025乡镇报表主体信息表.xlsx",
                "仙居县横溪镇人民政府2025乡镇报表主体信息表.xlsx"
            ]
        )

        XCTAssertEqual(exportName, "2025乡镇报表主体信息表_汇总")
    }

    func testExportNamingTrimsTrailingDelimitersFromSharedPrefix() {
        let exportName = ExportNaming.suggestedWorkbookName(
            from: [
                "2025-乡镇-A-主体信息表.xlsx",
                "2025-乡镇-B-主体信息表.xlsx"
            ]
        )

        XCTAssertEqual(exportName, "2025-乡镇_汇总")
    }

    func testExportNamingFallsBackToMostRepeatedTokensWhenPrefixIsTooShort() {
        let exportName = ExportNaming.suggestedWorkbookName(
            from: [
                "甲村2025主体信息表.xlsx",
                "乙村2025主体信息表.xlsx",
                "丙村2025主体信息表.xlsx"
            ]
        )

        XCTAssertEqual(exportName, "2025主体信息表_汇总")
    }

    func testExportNamingFallsBackToSingleStemForSingleFile() {
        let exportName = ExportNaming.suggestedWorkbookName(
            from: ["安洲街道主体信息表.xlsx"]
        )

        XCTAssertEqual(exportName, "安洲街道主体信息表_汇总")
    }

    func testToolbarPresentationUsesImportAsPrimaryActionWhenWorkspaceIsEmpty() {
        let presentation = WorkspaceToolbar.buildPresentation(
            selectedFileCount: 0,
            canExport: false
        )

        XCTAssertEqual(presentation.importTitle, "导入文件")
        XCTAssertFalse(presentation.appendEnabled)
        XCTAssertTrue(presentation.importIsProminent)
        XCTAssertFalse(presentation.exportIsProminent)
    }

    func testToolbarPresentationPromotesExportWhenWorkspaceCanExport() {
        let presentation = WorkspaceToolbar.buildPresentation(
            selectedFileCount: 3,
            canExport: true
        )

        XCTAssertEqual(presentation.importTitle, "导入文件")
        XCTAssertTrue(presentation.appendEnabled)
        XCTAssertFalse(presentation.importIsProminent)
        XCTAssertTrue(presentation.exportIsProminent)
    }
}
