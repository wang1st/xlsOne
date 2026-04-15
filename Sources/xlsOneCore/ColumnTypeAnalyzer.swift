import Foundation

/// 列类型推断器 - 基于多因子评分系统的智能推断
/// 核心公式: f(表头信号，数据统计，位置信息) → 列类型 + 置信度
public struct ColumnTypeAnalyzer {

    /// 列的语义类型
    public enum SemanticType: Equatable, Sendable {
        case code           // 编码列（区划代码、科目代码等）
        case amount         // 金额/数值列（需要求和）
        case label          // 标签列（名称、描述等）
        case date           // 日期列
        case unknown        // 未知类型

        public var displayName: String {
            switch self {
            case .code: return "编码"
            case .amount: return "金额"
            case .label: return "标签"
            case .date: return "日期"
            case .unknown: return "未知"
            }
        }
    }

    /// 信号强度
    private enum SignalStrength: Double {
        case strong = 1.0      // 强信号，高置信度
        case medium = 0.5     // 中信号，需要其他证据
        case weak = 0.3       // 弱信号，仅供参考
        case negative = -1.0  // 反信号，排除某类型
    }

    /// 列类型推断结果
    public struct ColumnProfile: Sendable {
        public let colIndex: Int
        public let header: String
        public let semanticType: SemanticType
        public let confidence: Double  // 0-1
        public let evidence: [String]  // 推断依据
        public let dataStats: DataStats // 数据统计
    }

    /// 数据统计特征
    public struct DataStats: Sendable {
        public let totalCount: Int           // 总数据行数
        public let numericCount: Int         // 可解析为数值的数量
        public let uniqueCount: Int          // 唯一值数量
        public let emptyCount: Int           // 空值数量
        public let maxValue: Double?         // 最大值
        public let minValue: Double?         // 最小值
        public let avgLength: Double         // 平均文本长度

        public var numericRatio: Double {
            guard totalCount > 0 else { return 0 }
            return Double(numericCount) / Double(totalCount)
        }

        public var uniqueRatio: Double {
            guard totalCount > 0 else { return 0 }
            return Double(uniqueCount) / Double(totalCount)
        }

        /// 是否是统一长度（编码特征）
        public var isUniformLength: Bool {
            return avgLength > 0 && avgLength < 20  // 简化的判断
        }
    }

    // MARK: - 关键词权重表

    /// 金额类关键词 - 强信号（权重1.0）
    private static let strongAmountKeywords = [
        "金额", "总额", "合计", "总计", "小计",
        "预算", "执行", "决算", "收入", "支出",
        "费用", "成本", "资金", "付款", "收款"
    ]

    /// 金额类关键词 - 中信号（权重0.5，需要数据验证）
    private static let mediumAmountKeywords = [
        "数", "额", "值", "量", "价"
    ]

    /// 标签类关键词 - 强信号（权重1.0）
    private static let strongLabelKeywords = [
        "名称", "名字", "描述", "说明", "备注",
        "标题", "内容", "详情", "注释"
    ]

    /// 编码类关键词 - 强信号（权重1.0，反信号：排除金额）
    private static let strongCodeKeywords = [
        "代码", "编码", "编号", "序号", "id", "code", "no",
        "区划", "行政区划", "科目代码", "项目代码",
        "邮编", "电话", "传真", "社会信用代码", "统一代码"
    ]

    /// 日期类关键词 - 强信号（权重1.0）
    private static let strongDateKeywords = [
        "日期", "时间", "年度", "年份", "月份", "年月",
        "填报日期", "报送日期", "截止日期", "创建时间"
    ]

    // MARK: - 公共接口

    /// 分析工作表的所有列类型
    public static func analyzeSheet(_ sheet: SheetData) -> [ColumnProfile] {
        guard !sheet.rows.isEmpty else { return [] }

        let maxCols = sheet.rows.map { $0.count }.max() ?? 0
        var profiles: [ColumnProfile] = []

        for colIdx in 0..<maxCols {
            let profile = analyzeColumn(sheet: sheet, colIndex: colIdx)
            profiles.append(profile)
        }

        return profiles
    }

    // MARK: - 核心分析逻辑

    /// 分析单列类型 - 多因子评分系统
    private static func analyzeColumn(sheet: SheetData, colIndex: Int) -> ColumnProfile {
        // 1. 获取列头
        let header = sheet.rows.first?.element(at: colIndex)?.value ?? ""
        let headerLower = header.lowercased()

        // 2. 收集该列的所有值（跳过第一行）
        var values: [String] = []
        for row in sheet.rows.dropFirst() {
            if colIndex < row.count {
                let value = row[colIndex].value.trimmingCharacters(in: .whitespaces)
                if !value.isEmpty {
                    values.append(value)
                }
            }
        }

        // 3. 计算数据统计特征
        let dataStats = calculateDataStats(values: values)

        // 4. 多因子评分
        var scores: [SemanticType: Double] = [
            .amount: 0,
            .label: 0,
            .code: 0,
            .date: 0
        ]
        var evidence: [String] = []

        // 4.1 表头信号评分
        let headerScores = scoreFromHeader(headerLower)
        for (type, score) in headerScores {
            scores[type, default: 0] += score
        }

        // 4.2 数据统计信号评分
        let dataScores = scoreFromData(stats: dataStats, header: headerLower)
        for (type, score) in dataScores {
            scores[type, default: 0] += score
        }

        // 4.3 位置信号（第一列通常是标签）
        if colIndex == 0 {
            scores[.label, default: 0] += 0.3
            evidence.append("第一列默认为标签")
        }

        // 5. 反信号处理（某些组合可以排除特定类型）
        applyNegativeSignals(scores: &scores, header: headerLower, stats: dataStats)

        // 6. 选择最高分的类型
        let (bestType, maxScore) = scores.max { $0.value < $1.value } ?? (.unknown, 0)

        // 7. 计算置信度（0-1）
        let confidence = min(abs(maxScore), 1.0)

        // 8. 构建证据说明
        if maxScore >= 0.8 {
            evidence.insert("强信号: 表头「\(header)」匹配\(bestType.displayName)类型", at: 0)
        } else if maxScore >= 0.5 {
            evidence.insert("中信号: 表头「\(header)」与数据特征共同指向\(bestType.displayName)", at: 0)
        } else if maxScore > 0 {
            evidence.insert("弱信号: 推测为\(bestType.displayName)", at: 0)
        } else {
            evidence.insert("无法确定类型", at: 0)
        }

        return ColumnProfile(
            colIndex: colIndex,
            header: header,
            semanticType: maxScore > 0.2 ? bestType : .unknown,
            confidence: confidence,
            evidence: evidence,
            dataStats: dataStats
        )
    }

    /// 从表头计算评分
    private static func scoreFromHeader(_ header: String) -> [SemanticType: Double] {
        var scores: [SemanticType: Double] = [:]

        // 强金额信号 (+1.0)
        for keyword in strongAmountKeywords {
            if header.contains(keyword) {
                scores[.amount, default: 0] += SignalStrength.strong.rawValue
                return scores  // 强信号直接返回
            }
        }

        // 强编码信号 (+1.0)
        for keyword in strongCodeKeywords {
            if header.contains(keyword) {
                scores[.code, default: 0] += SignalStrength.strong.rawValue
                return scores
            }
        }

        // 强日期信号 (+1.0)
        for keyword in strongDateKeywords {
            if header.contains(keyword) {
                scores[.date, default: 0] += SignalStrength.strong.rawValue
                return scores
            }
        }

        // 强标签信号 (+1.0)
        for keyword in strongLabelKeywords {
            if header.contains(keyword) {
                scores[.label, default: 0] += SignalStrength.strong.rawValue
                return scores
            }
        }

        // 中金额信号 (+0.5) - 需要后续数据验证
        for char in mediumAmountKeywords {
            if header.contains(char) {
                scores[.amount, default: 0] += SignalStrength.medium.rawValue
                break
            }
        }

        return scores
    }

    /// 从数据特征计算评分
    private static func scoreFromData(stats: DataStats, header: String) -> [SemanticType: Double] {
        var scores: [SemanticType: Double] = [:]

        // 数值化比例高 → 金额候选
        if stats.numericRatio >= 0.8 {
            scores[.amount, default: 0] += 0.5
        }

        // 唯一值比例100%且统一长度 → 编码候选
        if stats.uniqueRatio >= 0.9 && stats.isUniformLength && stats.numericRatio >= 0.5 {
            scores[.code, default: 0] += 0.7
        }

        // 数值化比例低 → 标签候选
        if stats.numericRatio < 0.3 {
            scores[.label, default: 0] += 0.4
        }

        return scores
    }

    /// 应用反信号（排除逻辑）
    private static func applyNegativeSignals(
        scores: inout [SemanticType: Double],
        header: String,
        stats: DataStats
    ) {
        // 如果包含编码关键词，降低金额评分
        let hasCodeKeyword = strongCodeKeywords.contains { header.contains($0) }
        if hasCodeKeyword {
            scores[.amount, default: 0] -= 0.8  // 编码列不应该求和
        }

        // 如果数值都是统一长度的纯数字（如3位科目代码、9位区划代码），降低金额评分
        if stats.numericRatio >= 0.8 && stats.isUniformLength && stats.uniqueRatio >= 0.9 {
            // 可能是编码而非金额
            if scores[.code, default: 0] < scores[.amount, default: 0] {
                scores[.amount, default: 0] -= 0.5
                scores[.code, default: 0] += 0.5
            }
        }

        // 如果金额评分很低但中金额信号存在，需要惩罚
        if scores[.amount, default: 0] < 0.3 {
            for char in mediumAmountKeywords {
                if header.contains(char) && hasCodeKeyword {
                    // "科目代码"不应该因为是"码"而被识别为金额
                    scores[.amount] = 0
                    break
                }
            }
        }
    }

    /// 计算数据统计特征
    private static func calculateDataStats(values: [String]) -> DataStats {
        let totalCount = values.count
        guard totalCount > 0 else {
            return DataStats(
                totalCount: 0,
                numericCount: 0,
                uniqueCount: 0,
                emptyCount: 0,
                maxValue: nil,
                minValue: nil,
                avgLength: 0
            )
        }

        // 数值解析
        var numericValues: [Double] = []
        var totalLength = 0

        for value in values {
            totalLength += value.count
            if let num = parseNumber(value) {
                numericValues.append(num)
            }
        }

        let uniqueCount = Set(values).count
        let numericCount = numericValues.count

        return DataStats(
            totalCount: totalCount,
            numericCount: numericCount,
            uniqueCount: uniqueCount,
            emptyCount: 0,
            maxValue: numericValues.max(),
            minValue: numericValues.min(),
            avgLength: Double(totalLength) / Double(totalCount)
        )
    }

    /// 解析数值（简化版）
    private static func parseNumber(_ text: String) -> Double? {
        let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)

        if trimmed.isEmpty { return nil }

        // 处理负数
        var workingText = trimmed
        var isNegative = false

        if trimmed.hasPrefix("(") && trimmed.hasSuffix(")") {
            workingText = String(trimmed.dropFirst().dropLast())
            isNegative = true
        } else if trimmed.hasPrefix("-") {
            workingText = String(trimmed.dropFirst())
            isNegative = true
        }

        // 标准化（移除千分位分隔符）
        let normalized = workingText
            .replacingOccurrences(of: ",", with: "")
            .replacingOccurrences(of: "，", with: "")

        if let number = Double(normalized) {
            return isNegative ? -number : number
        }

        return nil
    }
}

// MARK: - 数组扩展

private extension Array {
    func element(at index: Int) -> Element? {
        guard index >= 0 && index < count else { return nil }
        return self[index]
    }
}
