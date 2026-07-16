import Foundation

/// 单元格类型覆盖定义
public struct CellTypeOverride: Codable, Sendable, Equatable {
    public let sheetName: String?
    public let rowIndex: Int
    public let colIndex: Int
    public let cellType: CellOverrideType
    public let userNote: String?

    public init(
        sheetName: String? = nil,
        rowIndex: Int,
        colIndex: Int,
        cellType: CellOverrideType,
        userNote: String? = nil
    ) {
        self.sheetName = sheetName
        self.rowIndex = rowIndex
        self.colIndex = colIndex
        self.cellType = cellType
        self.userNote = userNote
    }
}

/// 用户可选择的单元格类型（用于覆盖自动识别）
public enum CellOverrideType: String, Codable, Sendable, CaseIterable {
    case label    // 标签型（直接显示值）
    case sum      // 求和型（数值求和）
    case mixed    // 混合型（显示条数）
    case single   // 单值型（保留当前显示值，仅做标记）

    public var displayName: String {
        switch self {
        case .label: return LocaleManager.loc("标签")
        case .sum: return LocaleManager.loc("求和")
        case .mixed: return LocaleManager.loc("混合")
        case .single: return LocaleManager.loc("单值")
        }
    }

    public var description: String {
        switch self {
        case .label:
            return "所有文件值相同时显示该值（如编码、日期）"
        case .sum:
            return "将所有文件的数值累加求和"
        case .mixed:
            return "值不一致时显示不同值的数量"
        case .single:
            return "保留当前显示值并标记为手动单值"
        }
    }
}

/// 文件指纹 - 用于识别和匹配相似的 Excel 文件
public struct FileFingerprint: Codable, Sendable, Equatable {
    /// Schema 版本号（用于兼容性检查）
    public let schemaVersion: Int

    /// 工作表名称
    public let sheetName: String

    /// 行数
    public let rowCount: Int

    /// 列数
    public let colCount: Int

    /// 表头内容哈希（第一行+第一列的组合哈希）
    public let headerHash: String

    /// 样本数据哈希（四角+中心点单元格值的组合哈希）
    public let sampleDataHash: String

    /// 文件名匹配模式（可选，用于模糊匹配）
    public let fileNamePattern: String?

    public init(
        schemaVersion: Int = 1,
        sheetName: String,
        rowCount: Int,
        colCount: Int,
        headerHash: String,
        sampleDataHash: String,
        fileNamePattern: String? = nil
    ) {
        self.schemaVersion = schemaVersion
        self.sheetName = sheetName
        self.rowCount = rowCount
        self.colCount = colCount
        self.headerHash = headerHash
        self.sampleDataHash = sampleDataHash
        self.fileNamePattern = fileNamePattern
    }
}

/// 单张工作表的结构指纹 - 只描述布局，不使用业务值
public struct SheetRuleFingerprint: Codable, Sendable, Equatable, Hashable {
    public let sheetName: String
    public let rowCount: Int
    public let columnCount: Int
    public let layoutHash: String
    public let formatHash: String

    public init(
        sheetName: String,
        rowCount: Int,
        columnCount: Int,
        layoutHash: String,
        formatHash: String
    ) {
        self.sheetName = sheetName
        self.rowCount = rowCount
        self.columnCount = columnCount
        self.layoutHash = layoutHash
        self.formatHash = formatHash
    }
}

/// 工作区级规则指纹 - 一整组同构 Excel 本次只匹配一套规则
public struct WorkbookRuleFingerprint: Codable, Sendable, Equatable {
    public let schemaVersion: Int
    public let sheetFingerprints: [SheetRuleFingerprint]

    public init(
        schemaVersion: Int = 2,
        sheetFingerprints: [SheetRuleFingerprint]
    ) {
        self.schemaVersion = schemaVersion
        self.sheetFingerprints = sheetFingerprints
    }

    public var sheetNames: [String] {
        sheetFingerprints.map(\.sheetName)
    }
}

/// 合并 Schema - 包含用户对特定文件类型的修正配置
public struct MergeSchema: Codable, Sendable, Identifiable {
    public let id: UUID
    public var name: String
    public let fingerprint: FileFingerprint
    public let workbookFingerprint: WorkbookRuleFingerprint?
    public var cellOverrides: [CellTypeOverride]
    public let createdAt: Date
    public var updatedAt: Date

    public init(
        id: UUID = UUID(),
        name: String,
        fingerprint: FileFingerprint,
        workbookFingerprint: WorkbookRuleFingerprint? = nil,
        cellOverrides: [CellTypeOverride],
        createdAt: Date = Date(),
        updatedAt: Date = Date()
    ) {
        self.id = id
        self.name = name
        self.fingerprint = fingerprint
        self.workbookFingerprint = workbookFingerprint
        self.cellOverrides = cellOverrides
        self.createdAt = createdAt
        self.updatedAt = updatedAt
    }
}

/// 规则匹配候选
public struct SchemaMatchCandidate: Sendable {
    public let schema: MergeSchema
    public let score: Double

    public init(schema: MergeSchema, score: Double) {
        self.schema = schema
        self.score = score
    }
}

/// Schema 匹配结果
public enum SchemaMatchResult: Sendable {
    /// 精确匹配
    case exact(MergeSchema)

    /// 多条规则同时高置信命中，需要用户明确选择
    case ambiguous([SchemaMatchCandidate])

    /// 相似匹配（按相似度排序）
    case similar([SchemaMatchCandidate])

    /// 无匹配
    case none
}

/// 指纹相似度评分
public struct FingerprintSimilarity: Sendable {
    public let fingerprint: FileFingerprint
    public let score: Double  // 0.0 - 1.0
    public let breakdown: SimilarityBreakdown

    public var isHighConfidence: Bool { score >= 0.9 }
    public var isMediumConfidence: Bool { score >= 0.7 }
}

/// 相似度评分细节
public struct SimilarityBreakdown: Sendable {
    public let sheetNameScore: Double
    public let dimensionScore: Double
    public let headerHashScore: Double
    public let sampleDataScore: Double
}
