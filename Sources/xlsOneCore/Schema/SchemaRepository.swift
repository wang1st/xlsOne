import Foundation

/// Schema 存储仓库协议
public protocol SchemaRepositoryProtocol: Sendable {
    /// 加载所有 Schema
    func loadAllSchemas() async throws -> [MergeSchema]

    /// 保存 Schema
    func saveSchema(_ schema: MergeSchema) async throws

    /// 删除 Schema
    func deleteSchema(id: UUID) async throws

    /// 根据 ID 查找 Schema
    func findSchema(id: UUID) async throws -> MergeSchema?

    /// 根据指纹查找匹配的 Schema
    func findMatchingSchema(fingerprint: FileFingerprint) async throws -> SchemaMatchResult

    /// 根据工作区指纹查找匹配的 Schema
    func findMatchingSchema(workbookFingerprint: WorkbookRuleFingerprint) async throws -> SchemaMatchResult

    /// 更新 Schema 匹配计数
    func incrementMatchCount(id: UUID) async throws

    /// 导出 Schema
    func exportSchema(id: UUID) async throws -> Data

    /// 导入 Schema
    func importSchema(data: Data) async throws -> MergeSchema
}

/// 本地文件系统实现的 Schema 仓库
public actor SchemaRepository: SchemaRepositoryProtocol {

    /// 单例实例
    public static let shared = SchemaRepository()

    /// Schema 存储目录
    private let baseDirectory: URL

    /// Schema 索引文件名
    private let indexFileName = "index.json"

    /// 内存中的缓存
    private var cachedSchemas: [MergeSchema]?

    /// 初始化
    public init(baseDirectory: URL? = nil) {
        if let baseDir = baseDirectory {
            self.baseDirectory = baseDir
        } else {
            // 使用应用支持目录
            let appSupport = FileManager.default.urls(
                for: .applicationSupportDirectory,
                in: .userDomainMask
            ).first!
            self.baseDirectory = appSupport.appendingPathComponent("xlsOne/schemas", isDirectory: true)
        }
    }

    // MARK: - SchemaRepositoryProtocol

    public func loadAllSchemas() async throws -> [MergeSchema] {
        // 返回缓存（如果可用）
        if let cached = cachedSchemas {
            return cached
        }

        // 确保目录存在
        try ensureDirectoryExists()

        // 读取索引文件
        let indexURL = baseDirectory.appendingPathComponent(indexFileName)
        guard FileManager.default.fileExists(atPath: indexURL.path) else {
            cachedSchemas = []
            return []
        }

        let data = try Data(contentsOf: indexURL)
        let ids = try JSONDecoder().decode([UUID].self, from: data)

        // 加载每个 Schema
        var schemas: [MergeSchema] = []
        for id in ids {
            if let schema = try await loadSchema(id: id) {
                schemas.append(schema)
            }
        }

        // 按匹配次数排序（常用优先）
        schemas.sort { $0.matchCount > $1.matchCount }

        cachedSchemas = schemas
        return schemas
    }

    public func saveSchema(_ schema: MergeSchema) async throws {
        try ensureDirectoryExists()

        // 保存 Schema 文件
        let schemaURL = schemaFileURL(id: schema.id)
        let data = try JSONEncoder().encode(schema)
        try data.write(to: schemaURL)

        // 更新索引
        var ids = try loadIndex()
        if !ids.contains(schema.id) {
            ids.append(schema.id)
            try saveIndex(ids)
        }

        // 更新缓存
        if cachedSchemas != nil {
            // 移除旧的（如果存在）
            cachedSchemas?.removeAll { $0.id == schema.id }
            // 添加新的
            cachedSchemas?.append(schema)
        }
    }

    public func deleteSchema(id: UUID) async throws {
        // 删除文件
        let schemaURL = schemaFileURL(id: id)
        if FileManager.default.fileExists(atPath: schemaURL.path) {
            try FileManager.default.removeItem(at: schemaURL)
        }

        // 更新索引
        var ids = try loadIndex()
        ids.removeAll { $0 == id }
        try saveIndex(ids)

        // 更新缓存
        cachedSchemas?.removeAll { $0.id == id }
    }

    public func findSchema(id: UUID) async throws -> MergeSchema? {
        // 先查缓存
        if let cached = cachedSchemas?.first(where: { $0.id == id }) {
            return cached
        }

        // 从文件加载
        return try await loadSchema(id: id)
    }

    public func findMatchingSchema(fingerprint: FileFingerprint) async throws -> SchemaMatchResult {
        let schemas = try await loadAllSchemas()
        return SchemaMatcher.match(fingerprint: fingerprint, against: schemas)
    }

    public func findMatchingSchema(workbookFingerprint: WorkbookRuleFingerprint) async throws -> SchemaMatchResult {
        let schemas = try await loadAllSchemas()
        return SchemaMatcher.match(workbookFingerprint: workbookFingerprint, against: schemas)
    }

    public func incrementMatchCount(id: UUID) async throws {
        guard let schema = try await findSchema(id: id) else { return }

        // 创建更新后的 Schema
        let updatedSchema = MergeSchema(
            id: schema.id,
            name: schema.name,
            fingerprint: schema.fingerprint,
            workbookFingerprint: schema.workbookFingerprint,
            cellOverrides: schema.cellOverrides,
            createdAt: schema.createdAt,
            updatedAt: Date(),
            matchCount: schema.matchCount + 1
        )

        try await saveSchema(updatedSchema)
    }

    // MARK: - 导入/导出

    /// 导出 Schema 为 JSON 数据
    public func exportSchema(id: UUID) async throws -> Data {
        guard let schema = try await findSchema(id: id) else {
            throw SchemaRepositoryError.schemaNotFound
        }

        let wrapper = ExportedSchema(
            version: "1.0",
            exportedAt: Date(),
            schema: schema
        )

        return try JSONEncoder().encode(wrapper)
    }

    /// 从 JSON 数据导入 Schema
    public func importSchema(data: Data) async throws -> MergeSchema {
        let decoder = JSONDecoder()

        // 尝试解析导出的格式
        if let wrapper = try? decoder.decode(ExportedSchema.self, from: data) {
            let schema = wrapper.schema
            // 生成新 ID 避免冲突
            let newSchema = MergeSchema(
                id: UUID(),
                name: schema.name + " (导入)",
                fingerprint: schema.fingerprint,
                workbookFingerprint: schema.workbookFingerprint,
                cellOverrides: schema.cellOverrides,
                matchCount: 0
            )
            try await saveSchema(newSchema)
            return newSchema
        }

        // 尝试直接解析 Schema
        let schema = try decoder.decode(MergeSchema.self, from: data)
        let newSchema = MergeSchema(
            id: UUID(),
            name: schema.name + " (导入)",
            fingerprint: schema.fingerprint,
            workbookFingerprint: schema.workbookFingerprint,
            cellOverrides: schema.cellOverrides,
            matchCount: 0
        )
        try await saveSchema(newSchema)
        return newSchema
    }

    // MARK: - 私有方法

    private func ensureDirectoryExists() throws {
        if !FileManager.default.fileExists(atPath: baseDirectory.path) {
            try FileManager.default.createDirectory(
                at: baseDirectory,
                withIntermediateDirectories: true
            )
        }
    }

    private func schemaFileURL(id: UUID) -> URL {
        baseDirectory.appendingPathComponent("schema-\(id.uuidString).json")
    }

    private func loadSchema(id: UUID) async throws -> MergeSchema? {
        let url = schemaFileURL(id: id)
        guard FileManager.default.fileExists(atPath: url.path) else { return nil }

        let data = try Data(contentsOf: url)
        return try JSONDecoder().decode(MergeSchema.self, from: data)
    }

    private func loadIndex() throws -> [UUID] {
        let url = baseDirectory.appendingPathComponent(indexFileName)
        guard FileManager.default.fileExists(atPath: url.path) else { return [] }

        let data = try Data(contentsOf: url)
        return try JSONDecoder().decode([UUID].self, from: data)
    }

    private func saveIndex(_ ids: [UUID]) throws {
        let url = baseDirectory.appendingPathComponent(indexFileName)
        let data = try JSONEncoder().encode(ids)
        try data.write(to: url)
    }
}

/// 导出的 Schema 包装器
private struct ExportedSchema: Codable {
    let version: String
    let exportedAt: Date
    let schema: MergeSchema
}

/// 仓库错误
public enum SchemaRepositoryError: Error {
    case schemaNotFound
    case invalidData
    case saveFailed
}
