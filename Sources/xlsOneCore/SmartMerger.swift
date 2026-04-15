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
        overrides: [CellTypeOverride]
    ) async throws -> MergeSchema {
        let schema = MergeSchema(
            name: name,
            fingerprint: fingerprint,
            cellOverrides: overrides
        )

        try await schemaRepository.saveSchema(schema)
        return schema
    }

    /// 更新 Schema 的单元格覆盖
    public func updateSchema(
        id: UUID,
        overrides: [CellTypeOverride]
    ) async throws -> MergeSchema? {
        guard let existing = try await schemaRepository.findSchema(id: id) else {
            return nil
        }

        let updated = MergeSchema(
            id: existing.id,
            name: existing.name,
            fingerprint: existing.fingerprint,
            cellOverrides: overrides,
            createdAt: existing.createdAt,
            updatedAt: Date(),
            matchCount: existing.matchCount
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
            let row = override.rowIndex
            let col = override.colIndex

            guard row < modifiedRows.count,
                  col < modifiedRows[row].count else {
                continue
            }

            let originalCell = modifiedRows[row][col]
            let newType = override.cellType.toMergedCellType()

            // 根据覆盖类型重新计算显示值
            let newDisplayValue: String
            switch newType {
            case .label:
                newDisplayValue = originalCell.sourceValues.values.first ?? ""
            case .sum:
                // 重新计算总和
                let sum = Self.calculateSum(from: originalCell.sourceValues)
                newDisplayValue = MergedCell.formatNumber(sum, formatCode: originalCell.formatCode)
            case .mixed:
                let uniqueCount = Set(originalCell.sourceValues.values).count
                newDisplayValue = "\(uniqueCount)条"
            case .single(let value):
                newDisplayValue = value
            }

            let newCell = MergedCell.create(
                type: newType,
                displayValue: newDisplayValue,
                sourceValues: originalCell.sourceValues,
                isOverridden: true,
                formatCode: originalCell.formatCode
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

            case .similar, .none:
                // 无匹配或只有相似匹配，返回基础结果
                return (baseResult, matchResult, nil)
            }
        } catch {
            // 出错时返回基础结果
            return (baseResult, .none, nil)
        }
    }

    // MARK: - 静态工具方法（nonisolated）

    private nonisolated static func calculateSum(from sourceValues: [String: String]) -> Double {
        var total = 0.0
        for (_, value) in sourceValues {
            // 移除千分位逗号，尝试解析数值
            let cleaned = value.replacingOccurrences(of: ",", with: "")
            if let num = Double(cleaned) {
                total += num
            }
        }
        return total
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
        }
    }
}

// MergedCell.create 已定义在 Models.swift 中
