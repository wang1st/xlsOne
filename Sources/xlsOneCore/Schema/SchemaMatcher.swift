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

        // 检查是否有高置信匹配；多个高置信命中时不静默任选
        let exactMatches = similarities.filter { $0.score >= highConfidenceThreshold }
        if exactMatches.count == 1, let exact = exactMatches.first {
            return .exact(exact.schema)
        }
        if exactMatches.count > 1 {
            return .ambiguous(exactMatches.map { SchemaMatchCandidate(schema: $0.schema, score: $0.score) })
        }

        // 返回相似匹配（中置信度及以上）
        let similarMatches = similarities.filter { $0.score >= mediumConfidenceThreshold }
        if !similarMatches.isEmpty {
            let results = similarMatches.map { SchemaMatchCandidate(schema: $0.schema, score: $0.score) }
            return .similar(results)
        }

        return .none
    }

    /// 匹配工作区级规则。一个工作区只能自动应用一套明确规则。
    public static func match(
        workbookFingerprint: WorkbookRuleFingerprint,
        against schemas: [MergeSchema]
    ) -> SchemaMatchResult {
        guard !schemas.isEmpty else { return .none }

        var similarities: [(schema: MergeSchema, score: Double)] = []

        for schema in schemas {
            let score: Double
            if let savedWorkbookFingerprint = schema.workbookFingerprint {
                score = calculateWorkbookSimilarity(
                    workbookFingerprint,
                    savedWorkbookFingerprint
                )
            } else {
                score = calculateLegacySimilarity(
                    workbookFingerprint: workbookFingerprint,
                    schemaFingerprint: schema.fingerprint
                )
            }
            similarities.append((schema, score))
        }

        similarities.sort { lhs, rhs in
            if lhs.score != rhs.score {
                return lhs.score > rhs.score
            }
            if lhs.schema.matchCount != rhs.schema.matchCount {
                return lhs.schema.matchCount > rhs.schema.matchCount
            }
            return lhs.schema.updatedAt > rhs.schema.updatedAt
        }

        let exactMatches = similarities.filter { $0.score >= highConfidenceThreshold }
        if exactMatches.count == 1, let exact = exactMatches.first {
            return .exact(exact.schema)
        }
        if exactMatches.count > 1 {
            return .ambiguous(exactMatches.map { SchemaMatchCandidate(schema: $0.schema, score: $0.score) })
        }

        let similarMatches = similarities.filter { $0.score >= mediumConfidenceThreshold }
        if !similarMatches.isEmpty {
            return .similar(similarMatches.map { SchemaMatchCandidate(schema: $0.schema, score: $0.score) })
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

    public static func calculateWorkbookSimilarity(
        _ fp1: WorkbookRuleFingerprint,
        _ fp2: WorkbookRuleFingerprint
    ) -> Double {
        guard !fp1.sheetFingerprints.isEmpty,
              fp1.sheetFingerprints.count == fp2.sheetFingerprints.count else {
            return 0
        }

        var totalScore = 0.0
        var sheetNamesAndDimensionsMatch = true

        for (lhs, rhs) in zip(fp1.sheetFingerprints, fp2.sheetFingerprints) {
            if lhs.sheetName != rhs.sheetName ||
                lhs.rowCount != rhs.rowCount ||
                lhs.columnCount != rhs.columnCount {
                sheetNamesAndDimensionsMatch = false
            }

            var sheetScore = 0.0
            sheetScore += lhs.sheetName == rhs.sheetName ? 0.30 : 0
            sheetScore += (lhs.rowCount == rhs.rowCount && lhs.columnCount == rhs.columnCount) ? 0.30 : 0
            sheetScore += lhs.layoutHash == rhs.layoutHash ? 0.25 : 0
            sheetScore += lhs.formatHash == rhs.formatHash ? 0.15 : 0
            totalScore += sheetScore
        }

        let score = totalScore / Double(fp1.sheetFingerprints.count)
        if sheetNamesAndDimensionsMatch {
            return max(score, highConfidenceThreshold)
        }
        return score
    }

    private static func calculateLegacySimilarity(
        workbookFingerprint: WorkbookRuleFingerprint,
        schemaFingerprint: FileFingerprint
    ) -> Double {
        guard let matchingSheet = workbookFingerprint.sheetFingerprints.first(where: {
            $0.sheetName == schemaFingerprint.sheetName
        }) else {
            return 0
        }

        let dimensionsMatch = matchingSheet.rowCount == schemaFingerprint.rowCount &&
            matchingSheet.columnCount == schemaFingerprint.colCount
        var score = 0.0
        score += matchingSheet.sheetName == schemaFingerprint.sheetName ? 0.30 : 0
        score += dimensionsMatch ? 0.30 : 0
        score += matchingSheet.layoutHash == schemaFingerprint.headerHash ? 0.25 : 0
        score += matchingSheet.formatHash == schemaFingerprint.sampleDataHash ? 0.15 : 0

        if schemaFingerprint.schemaVersion < 2,
           dimensionsMatch {
            let legacyFloor = workbookFingerprint.sheetFingerprints.count == 1
                ? highConfidenceThreshold
                : mediumConfidenceThreshold
            score = max(score, legacyFloor)
        }

        return score
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
        case .ambiguous:
            return nil
        case .similar(let matches):
            return matches.first?.schema
        case .none:
            return nil
        }
    }
}
