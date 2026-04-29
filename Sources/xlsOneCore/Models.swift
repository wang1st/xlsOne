import Foundation

/// 单元格位置（用于多选）
public struct CellPosition: Hashable, Equatable, Sendable {
    public let row: Int
    public let col: Int

    public init(row: Int, col: Int) {
        self.row = row
        self.col = col
    }
}

/// 表示一个单元格的数据
public struct CellData: Equatable, Sendable {
    public let value: String
    public let rawValue: String?
    /// 原始数值（仅当单元格是数字类型时有值）
    public let numericValue: Double?
    /// Excel 数字格式码（如 "#,##0.00", "yyyy-MM-dd"）
    public let formatCode: String?
    /// 是否是日期格式
    public let isDate: Bool

    public init(
        value: String,
        rawValue: String? = nil,
        numericValue: Double? = nil,
        formatCode: String? = nil,
        isDate: Bool = false
    ) {
        self.value = value.trimmingCharacters(in: .whitespacesAndNewlines)
        self.rawValue = rawValue
        self.formatCode = formatCode
        self.isDate = isDate
        // 如果没有提供 numericValue，自动解析
        self.numericValue = numericValue ?? Self.parseNumber(self.value)
    }

    /// 判断是否可能是数值（金额）
    /// 科目代码（如201、301）不应被视为金额
    public var isNumeric: Bool {
        return numericValue != nil
    }

    /// 解析数值（支持千分位和欧式格式）
    /// 返回 nil 如果值看起来像科目代码（3位纯数字）
    private static func parseNumber(_ text: String) -> Double? {
        let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)

        // 排除纯3位数字（如201、301等科目代码）
        // 但保留有千分位、小数点或超过3位的数字
        if trimmed.count == 3,
           trimmed.allSatisfy({ $0.isNumber }) {
            return nil
        }

        return Self.parseNumberInternal(trimmed)
    }

    private static func parseNumberInternal(_ text: String) -> Double? {
        // 如果为空，返回 nil
        if text.isEmpty {
            return nil
        }

        // 处理负数（括号格式）
        var workingText = text
        var isNegative = false

        if text.hasPrefix("(") && text.hasSuffix(")") {
            workingText = String(text.dropFirst().dropLast())
            isNegative = true
        } else if text.hasPrefix("-") {
            workingText = String(text.dropFirst())
            isNegative = true
        }

        // 检测格式：判断最后一个小数点或逗号的位置
        let lastComma = workingText.lastIndex(of: ",")
        let lastPeriod = workingText.lastIndex(of: ".")

        var normalizedText: String

        if let lastComma = lastComma, lastPeriod != nil {
            // 同时包含逗号和点，根据位置判断哪个是小数分隔符
            if workingText.distance(from: lastComma, to: workingText.endIndex) <= 3 {
                // 逗号后面只有1-2位数字，欧式格式 (1.234,56)
                normalizedText = workingText
                    .replacingOccurrences(of: ".", with: "")
                    .replacingOccurrences(of: ",", with: ".")
            } else {
                // 美式格式 (1,234.56)
                normalizedText = workingText.replacingOccurrences(of: ",", with: "")
            }
        } else if let lastComma = lastComma {
            // 只有逗号，检查是否是千分位
            let charsAfterComma = workingText.distance(from: lastComma, to: workingText.endIndex)
            if charsAfterComma == 3 {
                // 可能是欧式小数 (123,45)
                normalizedText = workingText.replacingOccurrences(of: ",", with: ".")
            } else {
                // 千分位分隔符，直接移除
                normalizedText = workingText.replacingOccurrences(of: ",", with: "")
            }
        } else {
            // 只有数字和可能的点（美式小数）
            normalizedText = workingText
        }

        // 尝试解析
        if let number = Double(normalizedText) {
            return isNegative ? -number : number
        }

        return nil
    }
}

public enum CellSourceState: String, Equatable, Sendable {
    case value
    case empty
    case missing
}

public struct CellSourceEntry: Equatable, Sendable {
    public let filename: String
    public let filepath: String
    public let value: String
    public let rawValue: String?
    public let state: CellSourceState

    public init(
        filename: String,
        filepath: String,
        value: String,
        rawValue: String? = nil,
        state: CellSourceState
    ) {
        self.filename = filename
        self.filepath = filepath
        self.value = value
        self.rawValue = rawValue
        self.state = state
    }
}

public struct CellMergeInput: Equatable, Sendable {
    public let filename: String
    public let filepath: String
    public let cell: CellData?

    public init(filename: String, filepath: String, cell: CellData?) {
        self.filename = filename
        self.filepath = filepath
        self.cell = cell
    }
}

public struct MergedCellDecision: Equatable, Sendable {
    public let autoDetectedType: MergedCell.CellType
    public let confidence: Double
    public let decisionReasons: [String]
    public let isSuspicious: Bool

    public init(
        autoDetectedType: MergedCell.CellType,
        confidence: Double,
        decisionReasons: [String],
        isSuspicious: Bool
    ) {
        self.autoDetectedType = autoDetectedType
        self.confidence = confidence
        self.decisionReasons = decisionReasons
        self.isSuspicious = isSuspicious
    }
}

/// 聚合后的单元格数据
public struct MergedCell: Equatable, Sendable {
    public enum CellType: Equatable, Sendable {
        case label           // 标签型（所有值相同）
        case sum(Double)     // 可聚合，显示总和
        case mixed(Int)      // 混合类型，显示 "X条"
        case single(String)  // 只有一个文件
    }

    public let type: CellType
    public let displayValue: String
    public let sources: [CellSourceEntry]

    /// 是否是用户覆盖的类型
    public let isOverridden: Bool

    /// Excel 原始格式码（用于输出时复刻格式）
    public let formatCode: String?

    /// 自动判定解释信息
    public let decision: MergedCellDecision

    public var sourceValues: [String: String] {
        Dictionary(uniqueKeysWithValues: sources.compactMap { source in
            guard source.state == .value else { return nil }
            return (source.filename, source.value)
        })
    }

    public init(
        type: CellType,
        sources: [CellSourceEntry] = [],
        isOverridden: Bool = false,
        formatCode: String? = nil,
        decision: MergedCellDecision? = nil
    ) {
        self.type = type
        self.sources = sources
        self.isOverridden = isOverridden
        self.formatCode = formatCode

        switch type {
        case .label:
            self.displayValue = Self.computeLabelDisplayValue(sources: sources)
        case .sum(let total):
            self.displayValue = MergedCell.formatNumber(total, formatCode: formatCode)
        case .mixed(let count):
            self.displayValue = "\(count)条"
        case .single(let value):
            self.displayValue = value
        }
        self.decision = decision ?? Self.defaultDecision(for: type)
    }

    /// 完整初始化器（用于自定义显示值）
    public init(
        type: CellType,
        displayValue: String,
        sources: [CellSourceEntry],
        isOverridden: Bool = false,
        formatCode: String? = nil,
        decision: MergedCellDecision? = nil
    ) {
        self.type = type
        self.displayValue = displayValue
        self.sources = sources
        self.isOverridden = isOverridden
        self.formatCode = formatCode
        self.decision = decision ?? Self.defaultDecision(for: type)
    }

    /// 从多个单元格创建聚合单元格 - 多因子动态判定系统
    /// 核心公式: f(自身格式40%, 垂直穿透一致性30%, 左邻列语义20%, 上下文邻居10%)
    public static func from(
        cells: [(filename: String, cell: CellData?)],
        leftCells: [(filename: String, cell: CellData?)] = [],
        neighborContext: NeighborContext,
        row: Int,
        col: Int
    ) -> MergedCell {
        let expandedCells = cells.map { CellMergeInput(filename: $0.filename, filepath: "", cell: $0.cell) }
        let expandedLeftCells = leftCells.map { CellMergeInput(filename: $0.filename, filepath: "", cell: $0.cell) }
        return from(
            cells: expandedCells,
            leftCells: expandedLeftCells,
            neighborContext: neighborContext,
            row: row,
            col: col
        )
    }

    public static func from(
        cells: [CellMergeInput],
        leftCells: [CellMergeInput] = [],
        neighborContext: NeighborContext,
        row: Int,
        col: Int
    ) -> MergedCell {
        let sources = buildSources(from: cells)

        // 过滤空值
        let validCells = cells.compactMap { tuple -> (CellMergeInput, CellData)? in
            guard let cell = tuple.cell, !cell.value.isEmpty else { return nil }
            return (tuple, cell)
        }

        // 提取 formatCode（取第一个非空单元格的格式）
        let formatCode = cells.compactMap(\.cell?.formatCode).first

        if validCells.isEmpty {
            let type = CellType.label
            return MergedCell(
                type: type,
                displayValue: "",
                sources: sources,
                formatCode: formatCode,
                decision: defaultDecision(
                    for: type,
                    reasons: ["所有来源单元格均为空或缺失，按空标签保留原始位置"],
                    confidence: 1.0,
                    isSuspicious: false
                )
            )
        }

        // 第一行强制表头
        if row == 0 {
            let type = CellType.label
            return MergedCell(
                type: type,
                sources: sources,
                formatCode: formatCode,
                decision: defaultDecision(
                    for: type,
                    reasons: ["首行按表头处理，强制视为标签列"],
                    confidence: 1.0,
                    isSuspicious: Set(validCells.map(\.1.value)).count > 1
                )
            )
        }

        // 单个非空来源也要参考格式和同列上下文，避免金额列中的 0/空值组合被误当作普通单值。
        if validCells.count == 1 {
            return determineSingleValue(
                validCells: validCells,
                sources: sources,
                leftCells: leftCells,
                neighborContext: neighborContext,
                formatCode: formatCode
            )
        }

        // 第一列特殊处理（无左邻列）
        if col == 0 {
            return determineFirstColumn(
                validCells: validCells,
                sources: sources,
                formatCode: formatCode,
                row: row
            )
        }

        // 自身格式
        let selfFP = FormatProfile.fingerprint(for: validCells.first?.1)
        let blankSourceCount = sources.filter { $0.state == .empty || $0.state == .missing }.count
        let blanksWithNumericValues = blankSourceCount > 0 &&
            validCells.allSatisfy { $0.1.numericValue != nil }

        // 垂直穿透分析
        let sampledCells = sampleFiles(from: cells).map(\.cell)
        let verticalProfile = FormatProfile(cells: sampledCells)

        // 左邻列分析
        let leftValidCells = leftCells.compactMap { tuple -> CellData? in
            guard let cell = tuple.cell, !cell.value.isEmpty else { return nil }
            return cell
        }
        let leftCell = leftValidCells.first  // 取第一个非空左邻单元格
        let codeSemantic = leftCell.map(leftCellHasCodeSemantic(_:)) ?? false
        let amountSemantic = leftCell.map(leftCellHasAmountSemantic(_:)) ?? false
        let weakAmountSemantic = leftCell.map(leftCellHasWeakAmountSemantic(_:)) ?? false
        let labelSemantic = leftCell.map(leftCellHasLabelSemantic(_:)) ?? false
        let contextNumeric = neighborContext.numericTendency >= 0.55 &&
            neighborContext.numericTendency > neighborContext.labelTendency
        let columnMetricSemantic = neighborContext.columnMetricTendency >= 0.55
        let weakMetricEvidenceCount = (weakAmountSemantic ? 1 : 0) +
            (contextNumeric ? 1 : 0) +
            (columnMetricSemantic ? 1 : 0)
        let metricSemantic = amountSemantic ||
            (!codeSemantic && !labelSemantic && weakMetricEvidenceCount >= 2)

        let numericValues = validCells.compactMap { $0.1.numericValue }
        let allNonEmptyValuesAreNumeric = numericValues.count == validCells.count
        let allNumericValuesAreZero = allNonEmptyValuesAreNumeric &&
            numericValues.allSatisfy { abs($0) < 0.0000001 }
        let allNumericValuesAreIdenticalNonZeroIntegers = allNonEmptyValuesAreNumeric &&
            numericValues.allSatisfy { $0 == floor($0) && abs($0) >= 0.0000001 } &&
            Set(numericValues).count == 1
        if allNumericValuesAreZero,
           !codeSemantic,
           !labelSemantic {
            let type = CellType.sum(0)
            return MergedCell(
                type: type,
                sources: sources,
                formatCode: formatCode,
                decision: defaultDecision(
                    for: type,
                    reasons: ["所有非空来源均为 0，按可累加单元格求和处理"],
                    confidence: 0.9,
                    isSuspicious: false
                )
            )
        }
        if allNumericValuesAreIdenticalNonZeroIntegers,
           blankSourceCount == 0,
           !metricSemantic {
            let type = CellType.label
            return MergedCell(
                type: type,
                sources: sources,
                formatCode: formatCode,
                decision: defaultDecision(
                    for: type,
                    reasons: ["所有来源为相同非零整数，且无明确可累加语义，按标签处理"],
                    confidence: 0.9,
                    isSuspicious: false
                )
            )
        }

        // 多因子评分
        var amountScore = 0.0
        var labelScore = 0.0
        var decisionReasons: [String] = [
            "自身格式指纹: \(selfFP.descriptionText)"
        ]
        if blanksWithNumericValues {
            decisionReasons.append("部分来源为空或缺失，非空来源均为数值，空值按 0 参与可累加判断")
        }

        // 1. 自身格式 (40%)
        let selfWeight = 0.4
        switch selfFP {
        case .strongNumeric:
            amountScore += 1.0 * selfWeight
        case .integerWide:
            amountScore += 0.8 * selfWeight
        case .integerCode:
            amountScore += 0.2 * selfWeight
            labelScore += 0.3 * selfWeight
        case .chineseText:
            labelScore += 1.0 * selfWeight
        case .alphaText:
            labelScore += 0.8 * selfWeight
        case .date:
            labelScore += 1.0 * selfWeight
        case .dashMarker, .empty:
            break
        case .mixed:
            labelScore += 0.2 * selfWeight
        }
        decisionReasons.append("自身格式得分 数值 \(Self.prettyScore(amountScore)) / 标签 \(Self.prettyScore(labelScore))")

        // 2. 垂直穿透一致性 (30%)
        let vertWeight = 0.3
        if let dominant = verticalProfile.dominantFingerprint {
            decisionReasons.append(
                "垂直穿透主导格式: \(dominant.descriptionText) (\(Int((verticalProfile.dominantRatio * 100).rounded()))%)"
            )
            switch dominant {
            case .strongNumeric:
                if verticalProfile.dominantRatio >= 0.8 {
                    amountScore += 1.0 * vertWeight
                } else {
                    amountScore += verticalProfile.dominantRatio * vertWeight
                }
            case .integerWide:
                amountScore += 0.7 * vertWeight * verticalProfile.dominantRatio
            case .integerCode:
                labelScore += 0.6 * vertWeight * verticalProfile.dominantRatio
            case .chineseText, .alphaText, .date:
                labelScore += 1.0 * vertWeight * verticalProfile.dominantRatio
            case .dashMarker, .empty, .mixed:
                break
            }
        }

        // 3. 左邻列语义 (20%)
        if let left = leftCell {
            let leftScore = analyzeLeftNeighbor(left)
            decisionReasons.append(
                "左邻列“\(left.value)”语义倾向 数值 \(Self.prettyScore(leftScore.numeric)) / 标签 \(Self.prettyScore(leftScore.label))"
            )
            amountScore += leftScore.numeric * 0.2
            labelScore += leftScore.label * 0.2
        }

        // 4. 上下文邻居 (10%)
        amountScore += neighborContext.numericTendency * 0.1
        labelScore += neighborContext.labelTendency * 0.1
        amountScore += neighborContext.columnMetricTendency * 0.08
        decisionReasons.append(
            "邻居上下文倾向 数值 \(Self.prettyScore(neighborContext.numericTendency)) / 标签 \(Self.prettyScore(neighborContext.labelTendency))"
        )

        // 特殊覆盖：左邻含"合计/总计" + 自身可解析为数字 → 强制求和
        if let left = leftCell {
            let leftText = left.value.lowercased()
            let isSummary = ["合计", "总计", "小计", "sum", "total"].contains { leftText.contains($0) }
            if isSummary && selfFP.isNumeric {
                let total = validCells.compactMap { $0.1.numericValue }.reduce(0, +)
                let type = CellType.sum(total)
                return MergedCell(
                    type: type,
                    sources: sources,
                    formatCode: formatCode,
                    decision: defaultDecision(
                        for: type,
                        reasons: decisionReasons + ["左邻列命中合计语义，强制按求和处理"],
                        confidence: 0.98,
                        isSuspicious: false
                    )
                )
            }
        }

        // 判定
        let scoreGap = abs(amountScore - labelScore)
        let baseConfidence = max(0.35, min(0.99, 0.55 + scoreGap * 0.8))

        if amountScore > 0.5 {
            let accumulable = checkAccumulable(
                selfFP: selfFP,
                leftCell: leftCell,
                verticalProfile: verticalProfile,
                validCells: validCells,
                blankSourceCount: blankSourceCount
            )
            if accumulable {
                let total = validCells.compactMap { $0.1.numericValue }.reduce(0, +)
                let type = CellType.sum(total)
                let isSuspicious = baseConfidence < 0.72
                return MergedCell(
                    type: type,
                    sources: sources,
                    formatCode: formatCode,
                    decision: defaultDecision(
                        for: type,
                        reasons: decisionReasons + ["综合得分偏向数值，且通过可累加性检查"],
                        confidence: baseConfidence,
                        isSuspicious: isSuspicious
                    )
                )
            } else {
                let type = CellType.label
                return MergedCell(
                    type: type,
                    sources: sources,
                    formatCode: formatCode,
                    decision: defaultDecision(
                        for: type,
                        reasons: decisionReasons + ["数值得分较高，但可累加性检查失败，回退为标签"],
                        confidence: max(0.5, baseConfidence - 0.1),
                        isSuspicious: true
                    )
                )
            }
        } else if labelScore > 0.5 {
            let type = CellType.label
            let uniqueCount = Set(validCells.map(\.1.value)).count
            return MergedCell(
                type: type,
                sources: sources,
                formatCode: formatCode,
                decision: defaultDecision(
                    for: type,
                    reasons: decisionReasons + ["综合得分偏向标签"],
                    confidence: baseConfidence,
                    isSuspicious: uniqueCount > 1 || baseConfidence < 0.72
                )
            )
        } else {
            // 边界情况：都是数值但分数不够高
            if selfFP.isNumeric {
                let accumulable = checkAccumulable(
                    selfFP: selfFP,
                    leftCell: leftCell,
                    verticalProfile: verticalProfile,
                    validCells: validCells,
                    blankSourceCount: blankSourceCount
                )
                if accumulable {
                    let total = validCells.compactMap { $0.1.numericValue }.reduce(0, +)
                    let type = CellType.sum(total)
                    return MergedCell(
                        type: type,
                        sources: sources,
                        formatCode: formatCode,
                        decision: defaultDecision(
                            for: type,
                            reasons: decisionReasons + ["边界场景下仍满足可累加性，按求和处理"],
                            confidence: max(0.5, baseConfidence - 0.08),
                            isSuspicious: true
                        )
                    )
                }
            }
            let type = CellType.label
            return MergedCell(
                type: type,
                sources: sources,
                formatCode: formatCode,
                decision: defaultDecision(
                    for: type,
                    reasons: decisionReasons + ["分数接近，按保守策略作为标签处理"],
                    confidence: max(0.45, baseConfidence - 0.12),
                    isSuspicious: true
                )
            )
        }
    }

    /// 第一列判定（无左邻列）
    private static func determineFirstColumn(
        validCells: [(CellMergeInput, CellData)],
        sources: [CellSourceEntry],
        formatCode: String?,
        row: Int
    ) -> MergedCell {
        let blankSourceCount = sources.filter { $0.state == .empty || $0.state == .missing }.count
        let numericValues = validCells.compactMap { $0.1.numericValue }
        let allNonEmptyValuesAreNumeric = numericValues.count == validCells.count
        let allNumericValuesAreZero = allNonEmptyValuesAreNumeric &&
            numericValues.allSatisfy { abs($0) < 0.0000001 }
        let allNumericValuesAreIdenticalNonZeroIntegers = allNonEmptyValuesAreNumeric &&
            numericValues.allSatisfy { $0 == floor($0) && abs($0) >= 0.0000001 } &&
            Set(numericValues).count == 1

        if allNumericValuesAreZero {
            let type = CellType.sum(0)
            return MergedCell(
                type: type,
                sources: sources,
                formatCode: formatCode,
                decision: defaultDecision(
                    for: type,
                    reasons: ["首列所有非空来源均为 0，按可累加单元格求和处理"],
                    confidence: 0.9,
                    isSuspicious: false
                )
            )
        }

        if allNumericValuesAreIdenticalNonZeroIntegers, blankSourceCount == 0 {
            let type = CellType.label
            return MergedCell(
                type: type,
                sources: sources,
                formatCode: formatCode,
                decision: defaultDecision(
                    for: type,
                    reasons: ["首列所有来源为相同非零整数，按标签处理"],
                    confidence: 0.9,
                    isSuspicious: false
                )
            )
        }

        let sampled = sampleFiles(from: validCells).map(\.1)
        let profile = FormatProfile(cells: sampled)

        if let dominant = profile.dominantFingerprint {
            switch dominant {
            case .chineseText, .alphaText, .date:
                let type = CellType.label
                return MergedCell(
                    type: type,
                    sources: sources,
                    formatCode: formatCode,
                    decision: defaultDecision(
                        for: type,
                        reasons: ["第 \(row + 1) 行首列主导格式为 \(dominant.descriptionText)，按标签处理"],
                        confidence: 0.92,
                        isSuspicious: Set(validCells.map(\.1.value)).count > 1
                    )
                )
            case .integerCode:
                let type = CellType.label
                return MergedCell(
                    type: type,
                    sources: sources,
                    formatCode: formatCode,
                    decision: defaultDecision(
                        for: type,
                        reasons: ["首列主导格式为整数编码，按标签处理"],
                        confidence: 0.95,
                        isSuspicious: Set(validCells.map(\.1.value)).count > 1
                    )
                )
            case .strongNumeric, .integerWide:
                if profile.dominantRatio >= 0.8 {
                    let total = validCells.compactMap { $0.1.numericValue }.reduce(0, +)
                    let type = CellType.sum(total)
                    return MergedCell(
                        type: type,
                        sources: sources,
                        formatCode: formatCode,
                        decision: defaultDecision(
                            for: type,
                            reasons: ["首列以数值格式为主且一致性高，按求和处理"],
                            confidence: 0.84,
                            isSuspicious: false
                        )
                    )
                }
            default:
                break
            }
        }
        let type = CellType.label
        return MergedCell(
            type: type,
            sources: sources,
            formatCode: formatCode,
            decision: defaultDecision(
                for: type,
                reasons: ["首列采用保守策略，按标签处理"],
                confidence: 0.7,
                isSuspicious: Set(validCells.map(\.1.value)).count > 1
            )
        )
    }

    private static func determineSingleValue(
        validCells: [(CellMergeInput, CellData)],
        sources: [CellSourceEntry],
        leftCells: [CellMergeInput],
        neighborContext: NeighborContext,
        formatCode: String?
    ) -> MergedCell {
        guard let (_, cell) = validCells.first else {
            let type = CellType.label
            return MergedCell(type: type, displayValue: "", sources: sources, formatCode: formatCode)
        }

        let selfFP = FormatProfile.fingerprint(for: cell)
        let leftCell = leftCells.compactMap { input -> CellData? in
            guard let cell = input.cell, !cell.value.isEmpty else { return nil }
            return cell
        }.first
        let leftScore = leftCell.map(analyzeLeftNeighbor(_:)) ?? (numeric: 0, label: 0)
        let formatSuggestsNumeric = formatCodeLooksNumeric(cell.formatCode ?? formatCode) || selfFP == .strongNumeric
        let sameColumnSuggestsNumeric = neighborContext.numericTendency >= 0.55 &&
            neighborContext.numericTendency > neighborContext.labelTendency
        let weakSameColumnNumeric = neighborContext.numericTendency > 0 &&
            neighborContext.numericTendency >= neighborContext.labelTendency
        let leftSuggestsNumeric = leftScore.numeric >= 0.5
        let leftSuggestsLabel = leftScore.label >= 0.5
        let codeSemantic = leftCell.map(leftCellHasCodeSemantic(_:)) ?? false
        let amountSemantic = leftCell.map(leftCellHasAmountSemantic(_:)) ?? false
        let weakAmountSemantic = leftCell.map(leftCellHasWeakAmountSemantic(_:)) ?? false
        let labelSemantic = leftCell.map(leftCellHasLabelSemantic(_:)) ?? false
        let columnMetricSemantic = neighborContext.columnMetricTendency >= 0.55
        let weakMetricEvidenceCount = (weakAmountSemantic ? 1 : 0) +
            (sameColumnSuggestsNumeric ? 1 : 0) +
            (columnMetricSemantic ? 1 : 0)
        let metricSemantic = amountSemantic ||
            (!codeSemantic && !labelSemantic && weakMetricEvidenceCount >= 2)
        let hasBlankSources = sources.contains { $0.state == .empty || $0.state == .missing }
        let numericValue = cell.numericValue
        let isZero = numericValue.map { abs($0) < 0.0000001 } ?? false
        let isCodeLike = selfFP == .integerCode || cell.formatCode == "@"
        let zeroWithBlankBias = isZero &&
            hasBlankSources &&
            selfFP.isNumeric &&
            !leftSuggestsLabel &&
            neighborContext.labelTendency < 0.65 &&
            (weakSameColumnNumeric || formatSuggestsNumeric || neighborContext.labelTendency == 0)

        if let numericValue,
           !isCodeLike,
           !leftSuggestsLabel,
           (formatSuggestsNumeric || sameColumnSuggestsNumeric || leftSuggestsNumeric || metricSemantic || zeroWithBlankBias) {
            let type = CellType.sum(numericValue)
            var reasons = ["仅有一个非空数值，未直接按单值处理"]
            if formatSuggestsNumeric {
                reasons.append("单元格格式倾向数值")
            }
            if sameColumnSuggestsNumeric || weakSameColumnNumeric {
                reasons.append("同列上下文倾向数值")
            }
            if leftSuggestsNumeric {
                reasons.append("左邻语义倾向可累加")
            }
            if metricSemantic {
                reasons.append("计量语义与同列上下文共同支持求和")
            }
            if zeroWithBlankBias {
                reasons.append("零值与空值/缺失并存，按可求和单元格处理")
            }

            let confidence: Double
            if formatSuggestsNumeric || leftSuggestsNumeric || metricSemantic {
                confidence = 0.86
            } else if sameColumnSuggestsNumeric {
                confidence = 0.80
            } else {
                confidence = 0.74
            }

            return MergedCell(
                type: type,
                sources: sources,
                formatCode: formatCode,
                decision: defaultDecision(
                    for: type,
                    reasons: reasons,
                    confidence: confidence,
                    isSuspicious: confidence < 0.76
                )
            )
        }

        let type = CellType.single(cell.value)
        var reasons = ["仅有一个非空来源值，按单值显示"]
        if isCodeLike || leftSuggestsLabel || neighborContext.labelTendency > neighborContext.numericTendency {
            reasons.append("格式或上下文更偏向标签/编码")
        } else if cell.numericValue != nil {
            reasons.append("暂无足够格式或同列证据支持求和")
        }

        return MergedCell(
            type: type,
            sources: sources,
            formatCode: formatCode,
            decision: defaultDecision(
                for: type,
                reasons: reasons,
                confidence: 0.82,
                isSuspicious: cell.numericValue != nil && !isCodeLike && !formatSuggestsNumeric && neighborContext.numericTendency == 0
            )
        )
    }

    // MARK: - 国际化语义模式

    /// 左邻列语义模式（支持中/英/多语言）
    private enum NeighborSemanticPatterns {
        /// 金额/数值型模式（当前格应求和）
        static let amountPatterns: [String] = [
            // 中文
            "合计", "总计", "小计", "金额", "数额", "额度",
            "数量", "单价", "总价", "价格", "数值", "预算",
            "收入", "支出", "成本", "费用", "利润", "执行",
            "决算", "款", "税金",
            "人数", "人口", "户数", "家数", "个数", "人员",
            "编制", "职工",
            // English
            "sum", "total", "subtotal", "amount", "quantity", "qty",
            "price", "unit price", "total price", "value", "budget",
            "revenue", "income", "expense", "cost", "fee", "profit",
            "tax", "fund",
            "population", "headcount", "staff"
        ]

        /// 弱计量信号（必须叠加同列/邻域证据，不能单独触发求和）
        static let weakAmountPatterns: [String] = [
            "数", "额", "值", "量", "价"
        ]

        /// 编码/标识型模式（当前格应为标签，不可累加）
        static let codePatterns: [String] = [
            // 中文 - 核心编码词
            "代码", "编码", "编号", "序号", "号码", "证号",
            "区划", "邮编", "邮政编码", "身份证", "电话", "传真",
            "期间", "年月", "年份", "日期", "时间",
            // 中文 - 常见"X号"扩展（学号、工号、账号、卡号、单号、票号等）
            "学号", "工号", "账号", "户号", "卡号", "单号", "订单号",
            "票号", "发票号", "批号", "书号", "卷号", "册号", "期号",
            "版号", "件号", "条码", "档案号", "准考证号", "资格证号",
            "许可证号", "机号", "箱号", "包号", "袋号",
            // English
            "code", "number", "no.", "no ", " id", "index", "serial",
            "zip", "zipcode", "postal code", "phone", "tel", "fax",
            "period", "date", "time", "year", "month"
        ]

        /// 名称/描述型模式（当前格应为标签）
        static let labelPatterns: [String] = [
            // 中文
            "名称", "名字", "描述", "说明", "备注", "标题",
            "内容", "详情", "类型", "性质", "状态",
            // English
            "name", "desc", "description", "title", "remark", "note",
            "type", "kind", "status", "content", "detail"
        ]

        static func matchesAny(_ text: String, patterns: [String]) -> Bool {
            // 清洗常见末尾标点
            var cleaned = text.trimmingCharacters(in: .whitespacesAndNewlines)
            let trailingPunctuations = CharacterSet(charactersIn: "：:：、。．;；/／-—")
            while let last = cleaned.last,
                  String(last).rangeOfCharacter(from: trailingPunctuations) != nil {
                cleaned.removeLast()
            }
            let lowercased = cleaned.lowercased()
            for pattern in patterns {
                if lowercased.contains(pattern.lowercased()) {
                    return true
                }
            }
            return false
        }
    }

    /// 分析左邻列语义
    private static func analyzeLeftNeighbor(_ leftCell: CellData) -> (numeric: Double, label: Double) {
        let text = leftCell.value.trimmingCharacters(in: .whitespacesAndNewlines)
        let fp = FormatProfile.fingerprint(for: leftCell)

        // 金额/数值标签
        if NeighborSemanticPatterns.matchesAny(text, patterns: NeighborSemanticPatterns.amountPatterns) {
            return (0.8, 0)
        }

        // 编码标签
        if NeighborSemanticPatterns.matchesAny(text, patterns: NeighborSemanticPatterns.codePatterns) {
            return (0, 0.6)
        }

        // 名称/描述标签
        if NeighborSemanticPatterns.matchesAny(text, patterns: NeighborSemanticPatterns.labelPatterns) {
            return (0, 0.5)
        }

        if NeighborSemanticPatterns.matchesAny(text, patterns: NeighborSemanticPatterns.weakAmountPatterns) {
            return (0.45, 0)
        }

        // 左邻是统一长度整数编码 → 当前格可能是名称（但权重要低，避免误判金额列）
        if fp == .integerCode {
            return (0, 0.1)
        }

        // 左邻是强数值 → 当前格也可能数值（同语义组延续）
        if fp == .strongNumeric || fp == .integerWide {
            return (0.2, 0)
        }

        return (0, 0)
    }

    private static func leftCellHasCodeSemantic(_ leftCell: CellData) -> Bool {
        let text = leftCell.value.trimmingCharacters(in: .whitespacesAndNewlines)
        return NeighborSemanticPatterns.matchesAny(text, patterns: NeighborSemanticPatterns.codePatterns)
    }

    private static func leftCellHasAmountSemantic(_ leftCell: CellData) -> Bool {
        let text = leftCell.value.trimmingCharacters(in: .whitespacesAndNewlines)
        return NeighborSemanticPatterns.matchesAny(text, patterns: NeighborSemanticPatterns.amountPatterns)
    }

    private static func leftCellHasWeakAmountSemantic(_ leftCell: CellData) -> Bool {
        let text = leftCell.value.trimmingCharacters(in: .whitespacesAndNewlines)
        if NeighborSemanticPatterns.matchesAny(text, patterns: NeighborSemanticPatterns.codePatterns) ||
            NeighborSemanticPatterns.matchesAny(text, patterns: NeighborSemanticPatterns.labelPatterns) {
            return false
        }
        return NeighborSemanticPatterns.matchesAny(text, patterns: NeighborSemanticPatterns.weakAmountPatterns)
    }

    private static func leftCellHasLabelSemantic(_ leftCell: CellData) -> Bool {
        let text = leftCell.value.trimmingCharacters(in: .whitespacesAndNewlines)
        return NeighborSemanticPatterns.matchesAny(text, patterns: NeighborSemanticPatterns.labelPatterns)
    }

    /// 检查数值可累加性
    private static func checkAccumulable(
        selfFP: FormatFingerprint,
        leftCell: CellData?,
        verticalProfile: FormatProfile,
        validCells: [(CellMergeInput, CellData)],
        blankSourceCount: Int = 0
    ) -> Bool {
        let numericValues = validCells.compactMap { $0.1.numericValue }
        guard !numericValues.isEmpty else { return false }

        let allNonEmptyValuesAreNumeric = numericValues.count == validCells.count
        let allIntegers = numericValues.allSatisfy { $0 == floor($0) }
        let uniqueValues = Set(numericValues)
        let allUnique = uniqueValues.count == numericValues.count
        let values = validCells.map { $0.1.value }
        let valueCounts = values.reduce(into: [:]) { $0[$1, default: 0] += 1 }
        let dominantRatio = Double(valueCounts.values.max() ?? 0) / Double(validCells.count)

        // 左邻含编码/标识关键词 → 明确不可累加（否决权，优先级最高）
        if let left = leftCell {
            let leftText = left.value.trimmingCharacters(in: .whitespacesAndNewlines)
            if NeighborSemanticPatterns.matchesAny(leftText, patterns: NeighborSemanticPatterns.codePatterns) {
                return false
            }
        }

        // 增强：左邻标签含"码"/"号"字 + 当前是统一长度整数 → 编码，不可累加
        if let left = leftCell {
            let leftText = left.value.trimmingCharacters(in: .whitespacesAndNewlines)
            if leftText.contains("码") || leftText.contains("号") {
                let lengths = Set(validCells.map { $0.1.value.count })
                if allIntegers, lengths.count == 1, validCells.count > 1 {
                    return false
                }
            }
        }

        // 非空来源都是数字，其他来源为空/缺失时，空值应视为 0 参与求和。
        // 左邻的编码/编号语义已在上方否决，避免把明确标识字段误累加。
        if blankSourceCount > 0, allNonEmptyValuesAreNumeric {
            return true
        }

        // 1. 编码特征检查：纯整数 + 长度一致 + 有公共前缀 + 在已知编码长度列表中
        let codeLengths = [3, 6, 9, 11, 12, 15, 18]
        if allIntegers && validCells.count > 1 {
            let lengths = Set(values.map { $0.count })
            if lengths.count == 1, let length = lengths.first,
               codeLengths.contains(length) {
                let prefix = longestCommonPrefix(values)
                // 公共前缀长度占标准长度比例 >= 50% 视为编码序列
                if prefix.count * 2 >= length {
                    return false
                }
            }
        }

        // 2. 强数值格式（带小数）→ 可累加
        if selfFP == .strongNumeric { return true }

        // 3. 左邻含金额/数量关键词 → 可累加
        if let left = leftCell {
            let leftText = left.value.trimmingCharacters(in: .whitespacesAndNewlines)
            if NeighborSemanticPatterns.matchesAny(leftText, patterns: NeighborSemanticPatterns.amountPatterns) {
                return true
            }
        }

        // 4. 垂直穿透强数值 → 可累加
        if let dominant = verticalProfile.dominantFingerprint,
           dominant == .strongNumeric && verticalProfile.dominantRatio >= 0.7 {
            return true
        }

        // 5. 纯整数且各不相同 → 统计量（人数/人口/金额），可累加
        if allIntegers && allUnique {
            return true
        }

        // 6. 值域差异大 → 可累加
        if let min = numericValues.min(), let max = numericValues.max(), min > 0 {
            if max / min > 100 { return true }
        }

        // 7. 数值重复的处理
        if !allUnique {
            // 统一格式的编码（95%一致）→ 标签
            if selfFP == .integerCode && dominantRatio >= 0.95 {
                return false
            }
            // 普通整数重复 → 可累加（如各乡镇都填1的计数项）
            if selfFP == .strongNumeric || selfFP == .integerWide {
                return true
            }
            if selfFP == .integerCode {
                return false
            }
        }

        // 默认：保守策略
        return false
    }

    /// 计算标签类型的智能显示值
    /// 显示公共前缀 + 后续差异部分用下划线填充
    /// 后续长度由75%阈值主导长度决定，无主导则取平均长度
    private static func computeLabelDisplayValue(
        sources: [CellSourceEntry]
    ) -> String {
        let values = sources
            .filter { $0.state == .value }
            .map(\.value)
        guard values.count > 1 else {
            return values.first ?? ""
        }

        // 所有值完全相同 → 直接显示
        let uniqueValues = Set(values)
        if uniqueValues.count == 1 {
            return values.first ?? ""
        }

        let prefix = longestCommonPrefix(values)
        let prefixLength = prefix.count

        // 计算标准总长度
        let lengths = values.map { $0.count }
        let standardLength = resolveStandardLength(lengths, totalCount: values.count)

        let underscoreCount = max(0, standardLength - prefixLength)
        let underscores = String(repeating: "_", count: underscoreCount)

        return prefix + underscores
    }

    /// 根据长度分布解析标准长度
    /// 优先找占比 >= 75% 的主导长度，否则取平均值（四舍五入）
    private static func resolveStandardLength(
        _ lengths: [Int],
        totalCount: Int
    ) -> Int {
        let threshold = 0.75
        var frequency: [Int: Int] = [:]
        for length in lengths {
            frequency[length, default: 0] += 1
        }

        // 找满足阈值的最长长度（优先较长的更保守）
        let qualified = frequency.filter { Double($0.value) / Double(totalCount) >= threshold }
        if let dominant = qualified.max(by: { $0.key < $1.key })?.key {
            return dominant
        }

        // 无主导长度 → 平均值四舍五入
        let avg = Double(lengths.reduce(0, +)) / Double(lengths.count)
        return Int(avg.rounded())
    }

    /// 最长公共前缀
    private static func longestCommonPrefix(_ strings: [String]) -> String {
        guard let first = strings.first, !first.isEmpty else { return "" }
        var prefix = first
        for str in strings.dropFirst() {
            while !str.hasPrefix(prefix) {
                prefix.removeLast()
                if prefix.isEmpty { return "" }
            }
        }
        return prefix
    }

    /// 最长公共后缀
    private static func longestCommonSuffix(_ strings: [String]) -> String {
        guard let first = strings.first, !first.isEmpty else { return "" }
        var suffix = first
        for str in strings.dropFirst() {
            while !str.hasSuffix(suffix) {
                suffix.removeFirst()
                if suffix.isEmpty { return "" }
            }
        }
        return suffix
    }

    /// 格式化数值（复刻Excel格式）
    static func formatNumber(_ value: Double, formatCode: String?) -> String {
        // 如果有原始格式码，尽量复刻
        if let formatCode = formatCode {
            // 货币格式
            if formatCode.contains("¥") || formatCode.contains("\\¥") || formatCode.contains("[$¥]") {
                return String(format: "¥%.2f", value)
            }
            if formatCode.contains("$") && !formatCode.contains("[$-") {
                return String(format: "$%.2f", value)
            }
            // 千分位
            if formatCode.contains("#,##0") {
                let formatter = NumberFormatter()
                formatter.numberStyle = .decimal
                formatter.minimumFractionDigits = 0
                formatter.maximumFractionDigits = 2
                return formatter.string(from: NSNumber(value: value)) ?? String(value)
            }
            // 百分比
            if formatCode.contains("%") {
                return String(format: "%.2f%%", value * 100)
            }
            // 保留2位小数
            if formatCode.contains(".00") {
                return String(format: "%.2f", value)
            }
            // 保留1位小数
            if formatCode.contains(".0") && !formatCode.contains(".00") {
                return String(format: "%.1f", value)
            }
        }

        // 默认：整数显示整数，否则保留2位并去尾0
        if value == floor(value) {
            return String(format: "%.0f", value)
        } else {
            return String(format: "%.2f", value)
                .replacingOccurrences(of: "\\.00$", with: "", options: .regularExpression)
                .replacingOccurrences(of: "(\\d)0+$", with: "$1", options: .regularExpression)
        }
    }

    /// 便捷创建器（用于 SmartMerger）
    static func create(
        type: CellType,
        displayValue: String,
        sources: [CellSourceEntry],
        isOverridden: Bool,
        formatCode: String? = nil,
        decision: MergedCellDecision? = nil
    ) -> MergedCell {
        return MergedCell(
            type: type,
            displayValue: displayValue,
            sources: sources,
            isOverridden: isOverridden,
            formatCode: formatCode,
            decision: decision
        )
    }

    private static func buildSources(from cells: [CellMergeInput]) -> [CellSourceEntry] {
        cells.map { source in
            if let cell = source.cell {
                if cell.value.isEmpty {
                    return CellSourceEntry(
                        filename: source.filename,
                        filepath: source.filepath,
                        value: "",
                        rawValue: cell.rawValue,
                        state: .empty
                    )
                }
                return CellSourceEntry(
                    filename: source.filename,
                    filepath: source.filepath,
                    value: cell.value,
                    rawValue: cell.rawValue,
                    state: .value
                )
            }
            return CellSourceEntry(
                filename: source.filename,
                filepath: source.filepath,
                value: "",
                rawValue: nil,
                state: .missing
            )
        }
    }

    private static func defaultDecision(
        for type: CellType,
        reasons: [String]? = nil,
        confidence: Double = 1.0,
        isSuspicious: Bool = false
    ) -> MergedCellDecision {
        MergedCellDecision(
            autoDetectedType: type,
            confidence: confidence,
            decisionReasons: reasons ?? ["未提供额外判定解释"],
            isSuspicious: isSuspicious
        )
    }

    private static func formatCodeLooksNumeric(_ formatCode: String?) -> Bool {
        guard let formatCode else { return false }
        let code = formatCode.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !code.isEmpty else { return false }

        let lowercased = code.lowercased()
        if lowercased == "@" || lowercased.contains("@") {
            return false
        }

        let dateMarkers = ["yy", "dd", "hh", "ss", "年", "月", "日"]
        if dateMarkers.contains(where: { lowercased.contains($0) }) {
            return false
        }

        return lowercased.contains("0") ||
            lowercased.contains("#") ||
            lowercased.contains("¥") ||
            lowercased.contains("$") ||
            lowercased.contains("%")
    }

    private static func prettyScore(_ score: Double) -> String {
        String(format: "%.2f", score)
    }
}

extension FormatFingerprint {
    var descriptionText: String {
        switch self {
        case .strongNumeric:
            return "强数值"
        case .integerWide:
            return "宽整数"
        case .integerCode:
            return "整数编码"
        case .chineseText:
            return "中文文本"
        case .alphaText:
            return "英文文本"
        case .date:
            return "日期"
        case .dashMarker:
            return "占位符"
        case .empty:
            return "空值"
        case .mixed:
            return "混合格式"
        }
    }
}

/// 表示一个工作表的数据
public struct SheetData: Sendable {
    public let name: String
    public let rows: [[CellData]]

    public init(name: String, rows: [[CellData]]) {
        self.name = name
        self.rows = rows
    }

    /// 获取单元格（行、列从0开始）
    public func cellAt(row: Int, col: Int) -> CellData? {
        guard row >= 0, row < rows.count else { return nil }
        let rowData = rows[row]
        guard col >= 0, col < rowData.count else { return nil }
        return rowData[col]
    }
}

/// 表示一个Excel文件的数据
public struct ExcelFile: Sendable {
    public let filename: String
    public let filepath: String
    public let sheets: [SheetData]

    public init(filename: String, filepath: String, sheets: [SheetData]) {
        self.filename = filename
        self.filepath = filepath
        self.sheets = sheets
    }
}

/// 汇总结果
public struct MergedResult: Sendable {
    public let sheetName: String
    public let rows: [[MergedCell]]
    public let sourceFiles: [String]

    public init(sheetName: String, rows: [[MergedCell]], sourceFiles: [String]) {
        self.sheetName = sheetName
        self.rows = rows
        self.sourceFiles = sourceFiles
    }

    /// 获取行数和列数
    public var dimensions: (rows: Int, cols: Int) {
        let maxRow = rows.count
        let maxCol = rows.map { $0.count }.max() ?? 0
        return (maxRow, maxCol)
    }
}
