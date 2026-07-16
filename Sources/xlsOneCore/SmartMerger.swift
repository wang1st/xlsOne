import Foundation

/// 智能合并器 - 集成 Schema 系统的合并器
public actor SmartMerger {

    /// 基础合并器
    private let baseMerger = SimpleMerger()

    /// Schema 仓库
    private let schemaRepository: SchemaRepositoryProtocol

    /// 当前应用的 Schema（如果有）
    public private(set) var appliedSchema: MergeSchema?

    /// 当前 Schema 匹配结果
    public private(set) var lastMatchResult: SchemaMatchResult?

    public init(repository: SchemaRepositoryProtocol = SchemaRepository.shared) {
        self.schemaRepository = repository
    }

    // MARK: - 公共接口

    /// 合并文件（自动应用匹配的 Schema）
    /// - Parameters:
    ///   - files: Excel 文件列表
    ///   - sheetName: 工作表名称
    /// - Returns: 合并结果（已应用 Schema 覆盖）
    public func merge(files: [ExcelFile], sheetName: String) async -> MergedResult {
        // 1. 使用基础合并器生成结果
        let baseResult = baseMerger.merge(files: files, sheetName: sheetName)

        guard !files.isEmpty else { return baseResult }

        // 2. 生成文件指纹
        let fingerprint = FingerprintGenerator.generate(from: files[0])

        // 3. 查找匹配的 Schema
        let matchResult = await findAndApplySchema(
            fingerprint: fingerprint,
            baseResult: baseResult
        )

        self.lastMatchResult = matchResult.matchResult
        self.appliedSchema = matchResult.appliedSchema

        return matchResult.result
    }

    /// 合并指定工作表，并只应用调用方明确选定的一套规则
    public func merge(
        files: [ExcelFile],
        sheetName: String,
        applying schema: MergeSchema?
    ) async -> MergedResult {
        let baseResult = baseMerger.merge(files: files, sheetName: sheetName)
        guard let schema else { return baseResult }
        return applySchema(schema, to: baseResult)
    }

    /// 合并第一个工作表（自动应用匹配的 Schema）
    public func mergeFirstSheets(from files: [ExcelFile]) async -> MergedResult {
        let baseResult = baseMerger.mergeFirstSheets(from: files)

        guard !files.isEmpty else { return baseResult }

        let fingerprint = FingerprintGenerator.generate(from: files[0])
        let matchResult = await findAndApplySchema(
            fingerprint: fingerprint,
            baseResult: baseResult
        )

        self.lastMatchResult = matchResult.matchResult
        self.appliedSchema = matchResult.appliedSchema

        return matchResult.result
    }

    /// 获取可用的工作表名称
    public func availableSheetNames(from files: [ExcelFile]) -> [String] {
        return baseMerger.availableSheetNames(from: files)
    }

    // MARK: - Schema 操作

    /// 创建并保存 Schema
    /// - Parameters:
    ///   - name: Schema 名称
    ///   - fingerprint: 文件指纹
    ///   - overrides: 单元格类型覆盖列表
    public func createSchema(
        name: String,
        fingerprint: FileFingerprint,
        workbookFingerprint: WorkbookRuleFingerprint? = nil,
        overrides: [CellTypeOverride]
    ) async throws -> MergeSchema {
        let schema = MergeSchema(
            name: name,
            fingerprint: fingerprint,
            workbookFingerprint: workbookFingerprint,
            cellOverrides: overrides
        )

        try await schemaRepository.saveSchema(schema)
        return schema
    }

    /// 更新 Schema 的单元格覆盖
    public func updateSchema(
        id: UUID,
        overrides: [CellTypeOverride],
        fingerprint: FileFingerprint? = nil,
        workbookFingerprint: WorkbookRuleFingerprint? = nil
    ) async throws -> MergeSchema? {
        guard let existing = try await schemaRepository.findSchema(id: id) else {
            return nil
        }

        let updated = MergeSchema(
            id: existing.id,
            name: existing.name,
            fingerprint: fingerprint ?? existing.fingerprint,
            workbookFingerprint: workbookFingerprint ?? existing.workbookFingerprint,
            cellOverrides: overrides,
            createdAt: existing.createdAt,
            updatedAt: Date()
        )

        try await schemaRepository.saveSchema(updated)
        return updated
    }

    /// 应用覆盖到合并结果（非 actor 隔离，纯计算）
    nonisolated public func applyOverrides(
        to result: MergedResult,
        overrides: [CellTypeOverride]
    ) -> MergedResult {
        var modifiedRows = result.rows

        for override in overrides {
            if let overrideSheetName = override.sheetName,
               overrideSheetName != result.sheetName {
                continue
            }

            let row = override.rowIndex
            let col = override.colIndex

            guard row < modifiedRows.count,
                  col < modifiedRows[row].count else {
                continue
            }

            let originalCell = modifiedRows[row][col]
            let newType: MergedCell.CellType
            switch override.cellType {
            case .label:
                newType = .label
            case .sum:
                newType = .sum(Self.calculateSum(from: originalCell.sources))
            case .mixed:
                let uniqueCount = Set(
                    originalCell.sources
                        .filter { $0.state == .value }
                        .map(\.value)
                ).count
                newType = .mixed(uniqueCount)
            case .single:
                newType = .single(originalCell.displayValue)
            }

            let adjustedDecision = MergedCellDecision(
                autoDetectedType: originalCell.decision.autoDetectedType,
                confidence: originalCell.decision.confidence,
                decisionReasons: originalCell.decision.decisionReasons + [LocaleManager.loc("已按类型调整显示为\(override.cellType.displayName)")],
                isSuspicious: originalCell.decision.isSuspicious
            )

            let newCell = MergedCell(
                type: newType,
                sources: originalCell.sources,
                isOverridden: true,
                formatCode: originalCell.formatCode,
                decision: adjustedDecision
            )

            modifiedRows[row][col] = newCell
        }

        return MergedResult(
            sheetName: result.sheetName,
            rows: modifiedRows,
            sourceFiles: result.sourceFiles
        )
    }

    /// 手动应用指定 Schema 到结果
    public func applySchema(_ schema: MergeSchema, to result: MergedResult) -> MergedResult {
        return applyOverrides(to: result, overrides: schema.cellOverrides)
    }

    /// 加载所有可用的 Schema
    public func loadAllSchemas() async throws -> [MergeSchema] {
        return try await schemaRepository.loadAllSchemas()
    }

    /// 为整个工作区选择一套可自动应用的规则
    public func prepareWorkspaceSchema(
        files: [ExcelFile],
        sheetNames: [String]
    ) async -> SchemaMatchResult {
        guard !files.isEmpty, !sheetNames.isEmpty else {
            appliedSchema = nil
            lastMatchResult = SchemaMatchResult.none
            return SchemaMatchResult.none
        }

        let fingerprint = FingerprintGenerator.generateWorkbook(
            from: files,
            sheetNames: sheetNames
        )

        do {
            let matchResult = try await schemaRepository.findMatchingSchema(workbookFingerprint: fingerprint)
            lastMatchResult = matchResult

            switch matchResult {
            case .exact(let schema):
                appliedSchema = schema
            case .ambiguous, .similar, .none:
                appliedSchema = nil
            }

            return matchResult
        } catch {
            appliedSchema = nil
            lastMatchResult = SchemaMatchResult.none
            return SchemaMatchResult.none
        }
    }

    /// 删除 Schema
    public func deleteSchema(id: UUID) async throws {
        try await schemaRepository.deleteSchema(id: id)
        if appliedSchema?.id == id {
            appliedSchema = nil
        }
    }

    /// 导出 Schema
    public func exportSchema(id: UUID) async throws -> Data {
        return try await schemaRepository.exportSchema(id: id)
    }

    /// 导入 Schema
    public func importSchema(data: Data) async throws -> MergeSchema {
        return try await schemaRepository.importSchema(data: data)
    }

    /// 导入修正规则，并限定只能绑定到当前同构工作区。
    public func importSchema(
        data: Data,
        forCurrentWorkspaceFiles files: [ExcelFile],
        sheetNames: [String]
    ) async throws -> MergeSchema {
        guard !files.isEmpty, !sheetNames.isEmpty else {
            throw AdjustmentMemoryImportError.emptyWorkspace
        }

        let imported = try SchemaRepository.decodeImportableSchema(data: data)
        let workbookFingerprint = FingerprintGenerator.generateWorkbook(
            from: files,
            sheetNames: sheetNames
        )
        let matchResult = SchemaMatcher.match(
            workbookFingerprint: workbookFingerprint,
            against: [imported]
        )
        guard case .exact = matchResult else {
            throw AdjustmentMemoryImportError.incompatibleWorkspace
        }

        let legacyFingerprint = FingerprintGenerator.generateWorkspaceLegacyFingerprint(
            from: files,
            sheetNames: sheetNames
        )
        let now = Date()
        let rebound = MergeSchema(
            id: imported.id,
            name: imported.name,
            fingerprint: legacyFingerprint,
            workbookFingerprint: workbookFingerprint,
            cellOverrides: imported.cellOverrides,
            createdAt: now,
            updatedAt: now
        )
        try await schemaRepository.saveSchema(rebound)
        return rebound
    }

    // MARK: - 私有方法

    private func findAndApplySchema(
        fingerprint: FileFingerprint,
        baseResult: MergedResult
    ) async -> (result: MergedResult, matchResult: SchemaMatchResult, appliedSchema: MergeSchema?) {
        do {
            let matchResult = try await schemaRepository.findMatchingSchema(fingerprint: fingerprint)

            switch matchResult {
            case .exact(let schema):
                // 精确匹配，应用 Schema
                try? await schemaRepository.incrementMatchCount(id: schema.id)
                let modifiedResult = applySchema(schema, to: baseResult)
                return (modifiedResult, matchResult, schema)

            case .ambiguous, .similar, .none:
                // 无匹配或只有相似匹配，返回基础结果
                return (baseResult, matchResult, nil)
            }
        } catch {
            // 出错时返回基础结果
            return (baseResult, .none, nil)
        }
    }

    // MARK: - 静态工具方法（nonisolated）

    private nonisolated static func calculateSum(from sources: [CellSourceEntry]) -> Double {
        var total = 0.0
        for source in sources where source.state == .value {
            let value = source.value
            // 移除千分位逗号，尝试解析数值
            let cleaned = value.replacingOccurrences(of: ",", with: "")
            if let num = Double(cleaned) {
                total += num
            }
        }
        return total
    }

}

public enum AdjustmentMemoryImportError: LocalizedError, Sendable {
    case emptyWorkspace
    case incompatibleWorkspace

    public var errorDescription: String? {
        switch self {
        case .emptyWorkspace:
            return LocaleManager.loc("当前没有可绑定修正规则的同构工作区。")
        case .incompatibleWorkspace:
            return LocaleManager.loc("导入的修正规则不属于当前同构结构。")
        }
    }
}

// MARK: - CellOverrideType 扩展

extension CellOverrideType {
    func toMergedCellType() -> MergedCell.CellType {
        switch self {
        case .label:
            return .label
        case .sum:
            return .sum(0)  // 数值会在应用时重新计算
        case .mixed:
            return .mixed(0)  // 计数会在应用时重新计算
        case .single:
            return .single("")  // 显示值会在应用时重新计算
        }
    }
}

// MergedCell.create 已定义在 Models.swift 中
