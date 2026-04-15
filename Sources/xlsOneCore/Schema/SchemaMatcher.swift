import Foundation

/// Schema 匹配器 - 根据文件指纹匹配最合适的 Schema
public struct SchemaMatcher {

    /// 高置信度阈值
    public static let highConfidenceThreshold = 0.9

    /// 中置信度阈值
    public static let mediumConfidenceThreshold = 0.7

    /// 匹配 Schema
    /// - Parameters:
    ///   - fingerprint: 目标文件指纹
    ///   - schemas: 可用的 Schema 列表
    /// - Returns: 匹配结果
    public static func match(
        fingerprint: FileFingerprint,
        against schemas: [MergeSchema]
    ) -> SchemaMatchResult {
        guard !schemas.isEmpty else { return .none }

        // 计算每个 Schema 的相似度
        var similarities: [(schema: MergeSchema, score: Double, breakdown: SimilarityBreakdown)] = []

        for schema in schemas {
            let (score, breakdown) = calculateSimilarity(
                fingerprint,
                schema.fingerprint
            )
            similarities.append((schema, score, breakdown))
        }

        // 按相似度排序
        similarities.sort { $0.score > $1.score }

        // 检查是否有精确匹配
        if let exact = similarities.first(where: { $0.score >= highConfidenceThreshold }) {
            return .exact(exact.schema)
        }

        // 返回相似匹配（中置信度及以上）
        let similarMatches = similarities.filter { $0.score >= mediumConfidenceThreshold }
        if !similarMatches.isEmpty {
            let results = similarMatches.map { (schema: $0.schema, score: $0.score) }
            return .similar(results)
        }

        return .none
    }

    /// 计算两个指纹的相似度
    /// - Returns: (相似度分数 0-1, 评分细节)
    public static func calculateSimilarity(
        _ fp1: FileFingerprint,
        _ fp2: FileFingerprint
    ) -> (score: Double, breakdown: SimilarityBreakdown) {
        var score = 0.0
        var totalWeight = 0.0

        // 1. 工作表名称匹配 (权重: 25%)
        totalWeight += 0.25
        let sheetNameScore = fp1.sheetName == fp2.sheetName ? 1.0 : 0.0
        score += 0.25 * sheetNameScore

        // 2. 行列维度匹配 (权重: 20%)
        // 允许 10% 的误差范围
        totalWeight += 0.20
        let rowRatio = ratioScore(fp1.rowCount, fp2.rowCount, tolerance: 0.1)
        let colRatio = ratioScore(fp1.colCount, fp2.colCount, tolerance: 0.1)
        let dimensionScore = (rowRatio + colRatio) / 2
        score += 0.20 * dimensionScore

        // 3. 表头哈希匹配 (权重: 35%)
        totalWeight += 0.35
        let headerScore = fp1.headerHash == fp2.headerHash ? 1.0 : 0.0
        score += 0.35 * headerScore

        // 4. 样本数据哈希匹配 (权重: 20%)
        totalWeight += 0.20
        let sampleScore = fp1.sampleDataHash == fp2.sampleDataHash ? 1.0 : 0.0
        score += 0.20 * sampleScore

        let breakdown = SimilarityBreakdown(
            sheetNameScore: sheetNameScore,
            dimensionScore: dimensionScore,
            headerHashScore: headerScore,
            sampleDataScore: sampleScore
        )

        // 归一化分数
        return (score / totalWeight, breakdown)
    }

    /// 计算比例相似度
    /// - Parameters:
    ///   - v1: 值1
    ///   - v2: 值2
    ///   - tolerance: 允许误差比例
    /// - Returns: 相似度 0-1
    private static func ratioScore(_ v1: Int, _ v2: Int, tolerance: Double) -> Double {
        if v1 == v2 { return 1.0 }
        if v1 == 0 || v2 == 0 { return 0.0 }

        let ratio = Double(min(v1, v2)) / Double(max(v1, v2))
        if ratio >= (1.0 - tolerance) {
            return ratio
        }
        return 0.0
    }

    /// 查找最佳匹配的 Schema（简化接口）
    public static func findBestMatch(
        fingerprint: FileFingerprint,
        schemas: [MergeSchema]
    ) -> MergeSchema? {
        let result = match(fingerprint: fingerprint, against: schemas)

        switch result {
        case .exact(let schema):
            return schema
        case .similar(let matches):
            return matches.first?.schema
        case .none:
            return nil
        }
    }
}
