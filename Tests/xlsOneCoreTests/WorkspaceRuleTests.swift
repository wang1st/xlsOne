import XCTest
@testable import xlsOneCore

final class WorkspaceRuleTests: XCTestCase {
    func testWorkbookFingerprintIgnoresBusinessValues() {
        let first = ExcelFile(
            filename: "a.xlsx",
            filepath: "/tmp/a.xlsx",
            sheets: [
                SheetData(
                    name: "汇总表",
                    rows: [
                        [CellData(value: "单位"), CellData(value: "金额")],
                        [CellData(value: "仙居县"), CellData(value: "100")]
                    ]
                )
            ]
        )
        let second = ExcelFile(
            filename: "b.xlsx",
            filepath: "/tmp/b.xlsx",
            sheets: [
                SheetData(
                    name: "汇总表",
                    rows: [
                        [CellData(value: "项目"), CellData(value: "数量")],
                        [CellData(value: "横溪镇"), CellData(value: "230")]
                    ]
                )
            ]
        )

        let firstFingerprint = FingerprintGenerator.generateWorkbook(
            from: [first],
            sheetNames: ["汇总表"]
        )
        let secondFingerprint = FingerprintGenerator.generateWorkbook(
            from: [second],
            sheetNames: ["汇总表"]
        )

        XCTAssertEqual(firstFingerprint, secondFingerprint)
    }

    func testWorkbookMatcherReturnsAmbiguousWhenMultipleRulesMatch() {
        let file = ExcelFile(
            filename: "a.xlsx",
            filepath: "/tmp/a.xlsx",
            sheets: [
                SheetData(
                    name: "汇总表",
                    rows: [
                        [CellData(value: "项目"), CellData(value: "金额")],
                        [CellData(value: "A"), CellData(value: "100")]
                    ]
                )
            ]
        )
        let workbookFingerprint = FingerprintGenerator.generateWorkbook(
            from: [file],
            sheetNames: ["汇总表"]
        )
        let legacyFingerprint = FingerprintGenerator.generateWorkspaceLegacyFingerprint(
            from: [file],
            sheetNames: ["汇总表"]
        )
        let firstRule = MergeSchema(
            name: "规则 A",
            fingerprint: legacyFingerprint,
            workbookFingerprint: workbookFingerprint,
            cellOverrides: [
                CellTypeOverride(sheetName: "汇总表", rowIndex: 1, colIndex: 1, cellType: .sum)
            ]
        )
        let secondRule = MergeSchema(
            name: "规则 B",
            fingerprint: legacyFingerprint,
            workbookFingerprint: workbookFingerprint,
            cellOverrides: [
                CellTypeOverride(sheetName: "汇总表", rowIndex: 1, colIndex: 1, cellType: .label)
            ]
        )

        let result = SchemaMatcher.match(
            workbookFingerprint: workbookFingerprint,
            against: [firstRule, secondRule]
        )

        switch result {
        case .ambiguous(let candidates):
            XCTAssertEqual(candidates.count, 2)
            XCTAssertEqual(Set(candidates.map { $0.schema.name }), ["规则 A", "规则 B"])
        default:
            XCTFail("Expected ambiguous match, got \(result)")
        }
    }

    func testWorkbookMatcherReturnsSingleRuleWhenOnlyOneRuleMatches() {
        let matchingFile = ExcelFile(
            filename: "a.xlsx",
            filepath: "/tmp/a.xlsx",
            sheets: [
                SheetData(name: "汇总表", rows: [[CellData(value: "项目")]])
            ]
        )
        let differentFile = ExcelFile(
            filename: "b.xlsx",
            filepath: "/tmp/b.xlsx",
            sheets: [
                SheetData(name: "明细表", rows: [[CellData(value: "项目")]])
            ]
        )
        let targetFingerprint = FingerprintGenerator.generateWorkbook(
            from: [matchingFile],
            sheetNames: ["汇总表"]
        )
        let matchingRule = MergeSchema(
            name: "匹配规则",
            fingerprint: FingerprintGenerator.generateWorkspaceLegacyFingerprint(
                from: [matchingFile],
                sheetNames: ["汇总表"]
            ),
            workbookFingerprint: targetFingerprint,
            cellOverrides: []
        )
        let otherRule = MergeSchema(
            name: "其他规则",
            fingerprint: FingerprintGenerator.generateWorkspaceLegacyFingerprint(
                from: [differentFile],
                sheetNames: ["明细表"]
            ),
            workbookFingerprint: FingerprintGenerator.generateWorkbook(
                from: [differentFile],
                sheetNames: ["明细表"]
            ),
            cellOverrides: []
        )

        let result = SchemaMatcher.match(
            workbookFingerprint: targetFingerprint,
            against: [matchingRule, otherRule]
        )

        switch result {
        case .exact(let schema):
            XCTAssertEqual(schema.name, "匹配规则")
        default:
            XCTFail("Expected exact match, got \(result)")
        }
    }

    func testWorkbookMatcherAppliesWhenSheetNamesAndDimensionsMatchDespiteValueLayoutDifferences() {
        let savedBatch = ExcelFile(
            filename: "saved.xlsx",
            filepath: "/tmp/saved.xlsx",
            sheets: [
                SheetData(
                    name: "乡镇表",
                    rows: [
                        [CellData(value: "项目"), CellData(value: "金额")],
                        [CellData(value: "仙居县"), CellData(value: "100")],
                        [CellData(value: "备注"), CellData(value: "")]
                    ]
                )
            ]
        )
        let newBatch = ExcelFile(
            filename: "new.xlsx",
            filepath: "/tmp/new.xlsx",
            sheets: [
                SheetData(
                    name: "乡镇表",
                    rows: [
                        [CellData(value: "项目"), CellData(value: "金额")],
                        [CellData(value: "横溪镇"), CellData(value: "")],
                        [CellData(value: "备注"), CellData(value: "已填报")]
                    ]
                )
            ]
        )
        let savedFingerprint = FingerprintGenerator.generateWorkbook(
            from: [savedBatch],
            sheetNames: ["乡镇表"]
        )
        let newFingerprint = FingerprintGenerator.generateWorkbook(
            from: [newBatch],
            sheetNames: ["乡镇表"]
        )
        XCTAssertNotEqual(savedFingerprint, newFingerprint)

        let rule = MergeSchema(
            name: "乡镇规则",
            fingerprint: FingerprintGenerator.generateWorkspaceLegacyFingerprint(
                from: [savedBatch],
                sheetNames: ["乡镇表"]
            ),
            workbookFingerprint: savedFingerprint,
            cellOverrides: [
                CellTypeOverride(sheetName: "乡镇表", rowIndex: 1, colIndex: 1, cellType: .sum)
            ]
        )

        let result = SchemaMatcher.match(
            workbookFingerprint: newFingerprint,
            against: [rule]
        )

        switch result {
        case .exact(let schema):
            XCTAssertEqual(schema.name, "乡镇规则")
        default:
            XCTFail("Expected exact match by sheet names and dimensions, got \(result)")
        }
    }

    func testLegacySingleSheetRuleCanStillMatchSingleSheetWorkspace() {
        let file = ExcelFile(
            filename: "a.xlsx",
            filepath: "/tmp/a.xlsx",
            sheets: [
                SheetData(name: "汇总表", rows: [[CellData(value: "项目"), CellData(value: "金额")]])
            ]
        )
        let targetFingerprint = FingerprintGenerator.generateWorkbook(
            from: [file],
            sheetNames: ["汇总表"]
        )
        let legacyRule = MergeSchema(
            name: "旧规则",
            fingerprint: FileFingerprint(
                schemaVersion: 1,
                sheetName: "汇总表",
                rowCount: 1,
                colCount: 2,
                headerHash: "旧表头",
                sampleDataHash: "旧样本"
            ),
            cellOverrides: [
                CellTypeOverride(sheetName: "汇总表", rowIndex: 0, colIndex: 1, cellType: .sum)
            ]
        )

        let result = SchemaMatcher.match(
            workbookFingerprint: targetFingerprint,
            against: [legacyRule]
        )

        switch result {
        case .exact(let schema):
            XCTAssertEqual(schema.name, "旧规则")
        default:
            XCTFail("Expected legacy exact match, got \(result)")
        }
    }

    func testLegacyRuleDoesNotAutoApplyToMultiSheetWorkspace() {
        let file = ExcelFile(
            filename: "a.xlsx",
            filepath: "/tmp/a.xlsx",
            sheets: [
                SheetData(name: "汇总表", rows: [[CellData(value: "项目"), CellData(value: "金额")]]),
                SheetData(name: "明细表", rows: [[CellData(value: "项目")]])
            ]
        )
        let targetFingerprint = FingerprintGenerator.generateWorkbook(
            from: [file],
            sheetNames: ["汇总表", "明细表"]
        )
        let legacyRule = MergeSchema(
            name: "旧规则",
            fingerprint: FileFingerprint(
                schemaVersion: 1,
                sheetName: "汇总表",
                rowCount: 1,
                colCount: 2,
                headerHash: "旧表头",
                sampleDataHash: "旧样本"
            ),
            cellOverrides: []
        )

        let result = SchemaMatcher.match(
            workbookFingerprint: targetFingerprint,
            against: [legacyRule]
        )

        switch result {
        case .similar(let candidates):
            XCTAssertEqual(candidates.first?.schema.name, "旧规则")
        default:
            XCTFail("Expected legacy rule to be similar only, got \(result)")
        }
    }

    func testImportAdjustmentMemoryRebindsOnlyMatchingWorkspace() async throws {
        let savedFile = ExcelFile(
            filename: "saved.xlsx",
            filepath: "/tmp/saved.xlsx",
            sheets: [
                SheetData(
                    name: "汇总表",
                    rows: [
                        [CellData(value: "项目"), CellData(value: "金额")],
                        [CellData(value: "A"), CellData(value: "100")]
                    ]
                )
            ]
        )
        let currentFile = ExcelFile(
            filename: "current.xlsx",
            filepath: "/tmp/current.xlsx",
            sheets: [
                SheetData(
                    name: "汇总表",
                    rows: [
                        [CellData(value: "项目"), CellData(value: "金额")],
                        [CellData(value: "B"), CellData(value: "230")]
                    ]
                )
            ]
        )
        let sourceSchema = MergeSchema(
            name: "同构调整记忆",
            fingerprint: FingerprintGenerator.generateWorkspaceLegacyFingerprint(
                from: [savedFile],
                sheetNames: ["汇总表"]
            ),
            workbookFingerprint: FingerprintGenerator.generateWorkbook(
                from: [savedFile],
                sheetNames: ["汇总表"]
            ),
            cellOverrides: [
                CellTypeOverride(sheetName: "汇总表", rowIndex: 1, colIndex: 1, cellType: .sum)
            ]
        )

        let repository = try makeTemporaryRepository()
        let merger = SmartMerger(repository: repository)
        let saved = try await merger.importSchema(
            data: JSONEncoder().encode(sourceSchema),
            forCurrentWorkspaceFiles: [currentFile],
            sheetNames: ["汇总表"]
        )

        XCTAssertEqual(saved.cellOverrides, sourceSchema.cellOverrides)
        XCTAssertEqual(
            saved.workbookFingerprint,
            FingerprintGenerator.generateWorkbook(from: [currentFile], sheetNames: ["汇总表"])
        )
        let schemas = try await repository.loadAllSchemas()
        XCTAssertEqual(schemas.count, 1)
        XCTAssertEqual(schemas.first?.id, saved.id)
    }

    func testImportAdjustmentMemoryRejectsDifferentWorkspaceWithoutSaving() async throws {
        let savedFile = ExcelFile(
            filename: "saved.xlsx",
            filepath: "/tmp/saved.xlsx",
            sheets: [
                SheetData(
                    name: "汇总表",
                    rows: [
                        [CellData(value: "项目"), CellData(value: "金额")],
                        [CellData(value: "A"), CellData(value: "100")]
                    ]
                )
            ]
        )
        let differentFile = ExcelFile(
            filename: "different.xlsx",
            filepath: "/tmp/different.xlsx",
            sheets: [
                SheetData(
                    name: "明细表",
                    rows: [
                        [CellData(value: "项目"), CellData(value: "金额"), CellData(value: "备注")],
                        [CellData(value: "A"), CellData(value: "100"), CellData(value: "x")]
                    ]
                )
            ]
        )
        let sourceSchema = MergeSchema(
            name: "其他结构调整记忆",
            fingerprint: FingerprintGenerator.generateWorkspaceLegacyFingerprint(
                from: [savedFile],
                sheetNames: ["汇总表"]
            ),
            workbookFingerprint: FingerprintGenerator.generateWorkbook(
                from: [savedFile],
                sheetNames: ["汇总表"]
            ),
            cellOverrides: [
                CellTypeOverride(sheetName: "汇总表", rowIndex: 1, colIndex: 1, cellType: .sum)
            ]
        )

        let repository = try makeTemporaryRepository()
        let merger = SmartMerger(repository: repository)

        do {
            _ = try await merger.importSchema(
                data: JSONEncoder().encode(sourceSchema),
                forCurrentWorkspaceFiles: [differentFile],
                sheetNames: ["明细表"]
            )
            XCTFail("不同构调整记忆不应被导入")
        } catch let error as AdjustmentMemoryImportError {
            guard case .incompatibleWorkspace = error else {
                return XCTFail("Expected incompatibleWorkspace, got \(error)")
            }
        }

        let schemas = try await repository.loadAllSchemas()
        XCTAssertTrue(schemas.isEmpty)
    }

    private func makeTemporaryRepository() throws -> SchemaRepository {
        let url = FileManager.default.temporaryDirectory
            .appendingPathComponent("xlsone-schema-tests-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
        addTeardownBlock {
            try? FileManager.default.removeItem(at: url)
        }
        return SchemaRepository(baseDirectory: url)
    }
}
