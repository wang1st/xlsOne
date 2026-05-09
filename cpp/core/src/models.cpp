#include "xlsone/core/models.hpp"

#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace xlsone {

namespace {

bool isAllDigits(const QString& value)
{
    if (value.isEmpty()) {
        return false;
    }
    for (const QChar ch : value) {
        if (!ch.isDigit()) {
            return false;
        }
    }
    return true;
}

QString normalizedText(const QString& text)
{
    return text.trimmed();
}

enum class FormatFingerprint {
    StrongNumeric,
    IntegerWide,
    IntegerCode,
    ChineseText,
    AlphaText,
    Date,
    DashMarker,
    Empty,
    Mixed,
};

bool isNumericFingerprint(FormatFingerprint fingerprint)
{
    return fingerprint == FormatFingerprint::StrongNumeric
        || fingerprint == FormatFingerprint::IntegerWide
        || fingerprint == FormatFingerprint::IntegerCode;
}

bool formatCodeLooksNumeric(const std::optional<QString>& formatCode)
{
    if (!formatCode.has_value()) {
        return false;
    }
    const QString code = formatCode->trimmed().toLower();
    if (code.isEmpty() || code.contains(QLatin1Char('@'))) {
        return false;
    }
    if (code.contains(QStringLiteral("yy"))
        || code.contains(QStringLiteral("dd"))
        || code.contains(QStringLiteral("hh"))
        || code.contains(QStringLiteral("ss"))
        || code.contains(QStringLiteral("年"))
        || code.contains(QStringLiteral("月"))
        || code.contains(QStringLiteral("日"))) {
        return false;
    }
    return code.contains(QLatin1Char('0'))
        || code.contains(QLatin1Char('#'))
        || code.contains(QStringLiteral("¥"))
        || code.contains(QLatin1Char('$'))
        || code.contains(QLatin1Char('%'));
}

FormatFingerprint fingerprintFor(const CellData* cell)
{
    if (cell == nullptr || cell->value.isEmpty()) {
        return FormatFingerprint::Empty;
    }

    const QString text = cell->value.trimmed();
    static const QStringList markers = {
        QStringLiteral("—"),
        QStringLiteral("-"),
        QStringLiteral("/"),
        QStringLiteral("NA"),
        QStringLiteral("N/A"),
        QStringLiteral("无"),
        QStringLiteral("null"),
        QStringLiteral("NULL"),
        QStringLiteral("~")
    };
    if (markers.contains(text) || text == QStringLiteral(" ")) {
        return FormatFingerprint::DashMarker;
    }

    if (cell->isDate) {
        return FormatFingerprint::Date;
    }

    if (cell->numericValue.has_value()) {
        const double number = *cell->numericValue;
        if (std::fabs(number - std::floor(number)) > 0.0000001) {
            return FormatFingerprint::StrongNumeric;
        }

        const bool integerFormat = cell->formatCode.has_value()
            && !cell->formatCode->contains(QLatin1Char('.'))
            && !cell->formatCode->contains(QStringLiteral("¥"))
            && !cell->formatCode->contains(QStringLiteral("\\¥"))
            && !cell->formatCode->contains(QStringLiteral("[$¥]"))
            && !cell->formatCode->contains(QLatin1Char('$'))
            && !cell->formatCode->contains(QLatin1Char('%'));

        if (!integerFormat && (text.contains(QLatin1Char('.')) || text.contains(QLatin1Char(',')) || text.contains(QStringLiteral("，")))) {
            return FormatFingerprint::StrongNumeric;
        }

        QString fingerprintText = text;
        if (integerFormat && fingerprintText.endsWith(QStringLiteral(".0"))) {
            fingerprintText.chop(2);
        }

        QString digits;
        for (const auto ch : fingerprintText) {
            if (ch.isDigit()) {
                digits.append(ch);
            }
        }
        static const std::set<int> codeLengths = {6, 9, 11, 12, 15, 18};
        if (digits.size() >= 2 && digits.size() == fingerprintText.size() && codeLengths.count(digits.size())) {
            return FormatFingerprint::IntegerCode;
        }
        if (cell->formatCode.has_value() && *cell->formatCode == QStringLiteral("@") && digits.size() >= 2 && digits.size() == fingerprintText.size()) {
            return FormatFingerprint::IntegerCode;
        }
        return FormatFingerprint::IntegerWide;
    }

    if (text.contains(QRegularExpression(QStringLiteral("\\p{Han}")))) {
        return FormatFingerprint::ChineseText;
    }

    bool allAlpha = !text.isEmpty();
    for (const auto ch : text) {
        if (!ch.isLetter() && !ch.isSpace()) {
            allAlpha = false;
            break;
        }
    }
    if (allAlpha) {
        return FormatFingerprint::AlphaText;
    }

    return FormatFingerprint::Mixed;
}

QString cleanSemanticText(QString text)
{
    text = text.trimmed();
    static const QString trailing = QStringLiteral("：:、。．;；/／-—");
    while (!text.isEmpty() && trailing.contains(text.back())) {
        text.chop(1);
    }
    return text.toLower();
}

bool matchesAnySemantic(const QString& text, const QStringList& patterns)
{
    const auto cleaned = cleanSemanticText(text);
    for (const auto& pattern : patterns) {
        if (cleaned.contains(pattern.toLower())) {
            return true;
        }
    }
    return false;
}

QStringList amountSemanticPatterns()
{
    return {
        QStringLiteral("合计"), QStringLiteral("总计"), QStringLiteral("小计"),
        QStringLiteral("金额"), QStringLiteral("数额"), QStringLiteral("额度"),
        QStringLiteral("数量"), QStringLiteral("单价"), QStringLiteral("总价"),
        QStringLiteral("价格"), QStringLiteral("数值"), QStringLiteral("预算"),
        QStringLiteral("收入"), QStringLiteral("支出"), QStringLiteral("成本"),
        QStringLiteral("费用"), QStringLiteral("利润"), QStringLiteral("执行"),
        QStringLiteral("决算"), QStringLiteral("款"), QStringLiteral("税金"),
        QStringLiteral("人数"), QStringLiteral("人口"), QStringLiteral("户数"),
        QStringLiteral("家数"), QStringLiteral("个数"), QStringLiteral("人员"),
        QStringLiteral("编制"), QStringLiteral("职工"),
        QStringLiteral("sum"), QStringLiteral("total"), QStringLiteral("subtotal"),
        QStringLiteral("amount"), QStringLiteral("quantity"), QStringLiteral("qty"),
        QStringLiteral("price"), QStringLiteral("value"), QStringLiteral("budget"),
        QStringLiteral("revenue"), QStringLiteral("income"), QStringLiteral("expense"),
        QStringLiteral("cost"), QStringLiteral("fee"), QStringLiteral("profit"),
        QStringLiteral("tax"), QStringLiteral("fund"), QStringLiteral("population"),
        QStringLiteral("headcount"), QStringLiteral("staff")
    };
}

QStringList weakAmountSemanticPatterns()
{
    return {
        QStringLiteral("数"), QStringLiteral("额"), QStringLiteral("值"),
        QStringLiteral("量"), QStringLiteral("价")
    };
}

QStringList codeSemanticPatterns()
{
    return {
        QStringLiteral("代码"), QStringLiteral("编码"), QStringLiteral("编号"),
        QStringLiteral("序号"), QStringLiteral("号码"), QStringLiteral("证号"),
        QStringLiteral("区划"), QStringLiteral("邮编"), QStringLiteral("邮政编码"),
        QStringLiteral("身份证"), QStringLiteral("电话"), QStringLiteral("传真"),
        QStringLiteral("期间"), QStringLiteral("年月"), QStringLiteral("年份"),
        QStringLiteral("日期"), QStringLiteral("时间"), QStringLiteral("学号"),
        QStringLiteral("工号"), QStringLiteral("账号"), QStringLiteral("户号"),
        QStringLiteral("卡号"), QStringLiteral("单号"), QStringLiteral("订单号"),
        QStringLiteral("票号"), QStringLiteral("发票号"), QStringLiteral("批号"),
        QStringLiteral("条码"), QStringLiteral("档案号"), QStringLiteral("许可证号"),
        QStringLiteral("code"), QStringLiteral("number"), QStringLiteral("no."),
        QStringLiteral(" no "), QStringLiteral(" id"), QStringLiteral("index"),
        QStringLiteral("serial"), QStringLiteral("zip"), QStringLiteral("postal code"),
        QStringLiteral("phone"), QStringLiteral("tel"), QStringLiteral("fax"),
        QStringLiteral("period"), QStringLiteral("date"), QStringLiteral("time"),
        QStringLiteral("year"), QStringLiteral("month")
    };
}

QStringList labelSemanticPatterns()
{
    return {
        QStringLiteral("名称"), QStringLiteral("名字"), QStringLiteral("描述"),
        QStringLiteral("说明"), QStringLiteral("备注"), QStringLiteral("标题"),
        QStringLiteral("内容"), QStringLiteral("详情"), QStringLiteral("类型"),
        QStringLiteral("性质"), QStringLiteral("状态"),
        QStringLiteral("name"), QStringLiteral("desc"), QStringLiteral("description"),
        QStringLiteral("title"), QStringLiteral("remark"), QStringLiteral("note"),
        QStringLiteral("type"), QStringLiteral("kind"), QStringLiteral("status")
    };
}

std::vector<CellSourceEntry> buildSources(const std::vector<CellMergeInput>& inputs)
{
    std::vector<CellSourceEntry> sources;
    sources.reserve(inputs.size());
    for (const auto& input : inputs) {
        if (!input.cell.has_value()) {
            sources.push_back({input.filename, input.filepath, {}, std::nullopt, CellSourceState::Missing});
            continue;
        }
        const auto value = input.cell->value;
        sources.push_back({
            input.filename,
            input.filepath,
            value,
            input.cell->rawValue,
            value.isEmpty() ? CellSourceState::Empty : CellSourceState::Value
        });
    }
    return sources;
}

bool leftCellHasCodeSemantic(const std::vector<CellMergeInput>& leftCells)
{
    for (const auto& input : leftCells) {
        if (!input.cell.has_value()) {
            continue;
        }
        if (matchesAnySemantic(input.cell->value, codeSemanticPatterns())) {
            return true;
        }
    }
    return false;
}

bool leftCellHasAmountSemantic(const std::vector<CellMergeInput>& leftCells)
{
    for (const auto& input : leftCells) {
        if (!input.cell.has_value()) {
            continue;
        }
        if (matchesAnySemantic(input.cell->value, amountSemanticPatterns())) {
            return true;
        }
    }
    return false;
}

bool leftCellHasWeakAmountSemantic(const std::vector<CellMergeInput>& leftCells)
{
    for (const auto& input : leftCells) {
        if (!input.cell.has_value()) {
            continue;
        }
        const auto& value = input.cell->value;
        if (matchesAnySemantic(value, codeSemanticPatterns()) || matchesAnySemantic(value, labelSemanticPatterns())) {
            continue;
        }
        if (matchesAnySemantic(value, weakAmountSemanticPatterns())) {
            return true;
        }
    }
    return false;
}

bool leftCellHasLabelSemantic(const std::vector<CellMergeInput>& leftCells)
{
    for (const auto& input : leftCells) {
        if (input.cell.has_value() && matchesAnySemantic(input.cell->value, labelSemanticPatterns())) {
            return true;
        }
    }
    return false;
}

const CellData* firstNonEmptyLeftCell(const std::vector<CellMergeInput>& leftCells)
{
    for (const auto& input : leftCells) {
        if (input.cell.has_value() && !input.cell->value.isEmpty()) {
            return &*input.cell;
        }
    }
    return nullptr;
}

struct SemanticScore {
    double numeric = 0.0;
    double label = 0.0;
};

SemanticScore analyzeLeftNeighbor(const CellData& leftCell)
{
    if (matchesAnySemantic(leftCell.value, amountSemanticPatterns())) {
        return {0.8, 0.0};
    }
    if (matchesAnySemantic(leftCell.value, codeSemanticPatterns())) {
        return {0.0, 0.6};
    }
    if (matchesAnySemantic(leftCell.value, labelSemanticPatterns())) {
        return {0.0, 0.5};
    }
    if (matchesAnySemantic(leftCell.value, weakAmountSemanticPatterns())) {
        return {0.45, 0.0};
    }

    const auto fingerprint = fingerprintFor(&leftCell);
    if (fingerprint == FormatFingerprint::IntegerCode) {
        return {0.0, 0.1};
    }
    if (fingerprint == FormatFingerprint::StrongNumeric || fingerprint == FormatFingerprint::IntegerWide) {
        return {0.2, 0.0};
    }
    return {};
}

struct DominantProfile {
    std::optional<FormatFingerprint> fingerprint;
    double ratio = 0.0;
};

DominantProfile dominantProfile(const std::vector<CellData>& cells)
{
    std::map<FormatFingerprint, int> counts;
    std::vector<FormatFingerprint> order;
    for (const auto& cell : cells) {
        const auto fingerprint = fingerprintFor(&cell);
        if (counts[fingerprint] == 0) {
            order.push_back(fingerprint);
        }
        ++counts[fingerprint];
    }

    FormatFingerprint best = FormatFingerprint::Empty;
    int bestCount = 0;
    bool found = false;
    for (const auto fingerprint : order) {
        const int count = counts[fingerprint];
        if (!found || count > bestCount) {
            best = fingerprint;
            bestCount = count;
            found = true;
        }
    }
    if (!found || cells.empty()) {
        return {};
    }
    return {best, static_cast<double>(bestCount) / static_cast<double>(cells.size())};
}

std::vector<CellData> sampledValidCellsForProfile(const std::vector<CellMergeInput>& cells)
{
    std::vector<size_t> indices;
    const size_t count = cells.size();
    if (count <= 10) {
        for (size_t index = 0; index < count; ++index) {
            indices.push_back(index);
        }
    } else if (count <= 50) {
        for (size_t index = 0; index < std::min<size_t>(3, count); ++index) {
            indices.push_back(index);
        }
        const size_t midStart = std::max<size_t>(3, (count - 4) / 2);
        for (size_t index = midStart; index < std::min(midStart + 4, count - 3); ++index) {
            indices.push_back(index);
        }
        for (size_t index = count > 3 ? count - 3 : 0; index < count; ++index) {
            indices.push_back(index);
        }
        if (indices.size() > 10) {
            indices.resize(10);
        }
    } else {
        for (size_t index = 0; index < std::min<size_t>(5, count); ++index) {
            indices.push_back(index);
        }
        for (const size_t start : {count / 4, count / 2, count * 3 / 4}) {
            for (size_t index = start; index < std::min(start + 3, count); ++index) {
                indices.push_back(index);
            }
        }
        for (size_t index = count > 4 ? count - 4 : 0; index < count; ++index) {
            indices.push_back(index);
        }
        if (indices.size() > 15) {
            indices.resize(15);
        }
    }

    std::vector<CellData> sampled;
    for (const auto index : indices) {
        if (index < cells.size() && cells[index].cell.has_value() && !cells[index].cell->value.isEmpty()) {
            sampled.push_back(*cells[index].cell);
        }
    }
    return sampled;
}

SemanticScore classificationScores(
    const std::vector<CellData>& validCells,
    const std::vector<CellMergeInput>& cells,
    const std::vector<CellMergeInput>& leftCells,
    NeighborContext neighborContext
)
{
    SemanticScore scores;
    if (validCells.empty()) {
        return scores;
    }

    switch (fingerprintFor(&validCells.front())) {
    case FormatFingerprint::StrongNumeric:
        scores.numeric += 0.4;
        break;
    case FormatFingerprint::IntegerWide:
        scores.numeric += 0.32;
        break;
    case FormatFingerprint::IntegerCode:
        scores.numeric += 0.08;
        scores.label += 0.12;
        break;
    case FormatFingerprint::ChineseText:
    case FormatFingerprint::Date:
        scores.label += 0.4;
        break;
    case FormatFingerprint::AlphaText:
        scores.label += 0.32;
        break;
    case FormatFingerprint::Mixed:
        scores.label += 0.08;
        break;
    case FormatFingerprint::DashMarker:
    case FormatFingerprint::Empty:
        break;
    }

    const auto profile = dominantProfile(sampledValidCellsForProfile(cells));
    if (profile.fingerprint.has_value()) {
        switch (*profile.fingerprint) {
        case FormatFingerprint::StrongNumeric:
            scores.numeric += profile.ratio >= 0.8 ? 0.3 : profile.ratio * 0.3;
            break;
        case FormatFingerprint::IntegerWide:
            scores.numeric += 0.7 * 0.3 * profile.ratio;
            break;
        case FormatFingerprint::IntegerCode:
            scores.label += 0.6 * 0.3 * profile.ratio;
            break;
        case FormatFingerprint::ChineseText:
        case FormatFingerprint::AlphaText:
        case FormatFingerprint::Date:
            scores.label += 0.3 * profile.ratio;
            break;
        case FormatFingerprint::DashMarker:
        case FormatFingerprint::Empty:
        case FormatFingerprint::Mixed:
            break;
        }
    }

    if (const auto* leftCell = firstNonEmptyLeftCell(leftCells)) {
        const auto score = analyzeLeftNeighbor(*leftCell);
        scores.numeric += score.numeric * 0.2;
        scores.label += score.label * 0.2;
    }

    scores.numeric += neighborContext.numericTendency * 0.1;
    scores.label += neighborContext.labelTendency * 0.1;
    scores.numeric += neighborContext.columnMetricTendency * 0.08;
    return scores;
}

double confidenceFromScores(SemanticScore scores)
{
    const double scoreGap = std::fabs(scores.numeric - scores.label);
    return std::max(0.35, std::min(0.99, 0.55 + scoreGap * 0.8));
}

bool numericSumShouldBeSuspicious(
    const std::vector<CellData>& validCells,
    const std::vector<CellMergeInput>& cells,
    const std::vector<CellMergeInput>& leftCells,
    NeighborContext neighborContext,
    int blankSourceCount
)
{
    const auto scores = classificationScores(validCells, cells, leftCells, neighborContext);

    if (scores.numeric <= 0.5) {
        return true;
    }

    return confidenceFromScores(scores) < 0.72;
}

bool labelShouldBeSuspicious(
    const std::vector<CellData>& validCells,
    const std::vector<CellMergeInput>& cells,
    const std::vector<CellMergeInput>& leftCells,
    NeighborContext neighborContext
)
{
    std::set<QString> distinctValues;
    for (const auto& cell : validCells) {
        distinctValues.insert(cell.value);
    }
    if (distinctValues.size() > 1) {
        return true;
    }

    const auto scores = classificationScores(validCells, cells, leftCells, neighborContext);
    if (scores.label > 0.5) {
        return confidenceFromScores(scores) < 0.72;
    }
    return true;
}

QString longestCommonPrefix(const std::vector<QString>& values)
{
    if (values.empty() || values.front().isEmpty()) {
        return {};
    }
    QString prefix = values.front();
    for (const auto& value : values) {
        while (!value.startsWith(prefix)) {
            prefix.chop(1);
            if (prefix.isEmpty()) {
                return {};
            }
        }
    }
    return prefix;
}

int resolveStandardLength(const std::vector<QString>& values)
{
    if (values.empty()) {
        return 0;
    }
    std::map<int, int> counts;
    int total = 0;
    for (const auto& value : values) {
        ++counts[value.size()];
        total += value.size();
    }
    for (auto iterator = counts.rbegin(); iterator != counts.rend(); ++iterator) {
        if (static_cast<double>(iterator->second) / static_cast<double>(values.size()) >= 0.75) {
            return iterator->first;
        }
    }
    return static_cast<int>(std::round(static_cast<double>(total) / static_cast<double>(values.size())));
}

QString labelDisplayValue(const std::vector<CellSourceEntry>& sources)
{
    std::vector<QString> values;
    for (const auto& source : sources) {
        if (source.state == CellSourceState::Value) {
            values.push_back(source.value);
        }
    }
    if (values.empty()) {
        return {};
    }
    if (values.size() == 1) {
        return values.front();
    }
    std::set<QString> unique(values.begin(), values.end());
    if (unique.size() == 1) {
        return values.front();
    }

    const auto prefix = longestCommonPrefix(values);
    const int standardLength = resolveStandardLength(values);
    return prefix + QString(std::max<qsizetype>(0, static_cast<qsizetype>(standardLength) - prefix.size()), QLatin1Char('_'));
}

int distinctValueCount(const std::vector<CellData>& cells)
{
    std::set<QString> values;
    for (const auto& cell : cells) {
        values.insert(cell.value);
    }
    return static_cast<int>(values.size());
}

QString groupedDecimal(double value, int maximumFractionDigits)
{
    QString fixed = QString::number(std::fabs(value), 'f', maximumFractionDigits);
    while (fixed.contains(QLatin1Char('.')) && fixed.endsWith(QLatin1Char('0'))) {
        fixed.chop(1);
    }
    if (fixed.endsWith(QLatin1Char('.'))) {
        fixed.chop(1);
    }

    const int dotIndex = fixed.indexOf(QLatin1Char('.'));
    QString integerPart = dotIndex >= 0 ? fixed.left(dotIndex) : fixed;
    const QString fractionalPart = dotIndex >= 0 ? fixed.mid(dotIndex) : QString();

    for (int index = integerPart.size() - 3; index > 0; index -= 3) {
        integerPart.insert(index, QLatin1Char(','));
    }
    return (value < 0 ? QStringLiteral("-") : QString()) + integerPart + fractionalPart;
}

std::optional<QString> firstFormatCode(const std::vector<CellMergeInput>& cells)
{
    for (const auto& input : cells) {
        if (input.cell.has_value() && input.cell->formatCode.has_value()) {
            return input.cell->formatCode;
        }
    }
    return std::nullopt;
}

bool codeLikeNumericSequence(const std::vector<CellData>& validCells)
{
    if (validCells.size() <= 1) {
        return false;
    }

    std::set<int> lengths;
    bool allIntegers = true;
    std::vector<QString> values;
    for (const auto& cell : validCells) {
        if (!cell.numericValue.has_value() || std::fabs(*cell.numericValue - std::floor(*cell.numericValue)) > 0.0000001) {
            allIntegers = false;
            break;
        }
        QString text = cell.value;
        if (!isAllDigits(text)) {
            return false;
        }
        lengths.insert(text.size());
        values.push_back(text);
    }
    if (!allIntegers || lengths.size() != 1) {
        return false;
    }

    static const std::set<int> codeLengths = {3, 6, 9, 11, 12, 15, 18};
    const int length = *lengths.begin();
    if (!codeLengths.count(length)) {
        return false;
    }

    const auto prefix = longestCommonPrefix(values);
    return prefix.size() * 2 >= length;
}

MergedCell makeCell(
    CellKind kind,
    QString display,
    std::vector<CellSourceEntry> sources,
    double sum = 0.0,
    int mixedCount = 0,
    QString singleValue = {},
    std::optional<QString> formatCode = std::nullopt,
    QStringList reasons = {},
    bool isSuspicious = false
)
{
    MergedCell cell;
    cell.type.kind = kind;
    cell.type.sum = sum;
    cell.type.mixedCount = mixedCount;
    cell.type.singleValue = singleValue;
    if (kind == CellKind::Label && display.isEmpty()) {
        cell.displayValue = labelDisplayValue(sources);
    } else {
        cell.displayValue = std::move(display);
    }
    cell.sources = std::move(sources);
    cell.formatCode = std::move(formatCode);
    cell.decision.autoDetectedType = kind;
    cell.decision.confidence = 0.8;
    cell.decision.decisionReasons = std::move(reasons);
    cell.decision.isSuspicious = isSuspicious;
    return cell;
}

} // namespace

CellData::CellData(
    QString value,
    std::optional<QString> rawValue,
    std::optional<double> numericValue,
    std::optional<QString> formatCode,
    bool isDate
) :
    value(normalizedText(value)),
    rawValue(std::move(rawValue)),
    numericValue(numericValue.has_value() ? numericValue : parseNumber(this->value)),
    formatCode(std::move(formatCode)),
    isDate(isDate)
{
}

bool CellData::isNumeric() const
{
    return numericValue.has_value();
}

std::optional<double> CellData::parseNumber(const QString& text)
{
    QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return std::nullopt;
    }

    if (trimmed.size() == 3 && isAllDigits(trimmed)) {
        return std::nullopt;
    }

    bool isNegative = false;
    if (trimmed.startsWith(QStringLiteral("(")) && trimmed.endsWith(QStringLiteral(")"))) {
        trimmed = trimmed.mid(1, trimmed.size() - 2).trimmed();
        isNegative = true;
    } else if (trimmed.startsWith(QStringLiteral("-"))) {
        trimmed = trimmed.mid(1).trimmed();
        isNegative = true;
    }

    QString normalized;
    const int lastComma = trimmed.lastIndexOf(QLatin1Char(','));
    const int lastPeriod = trimmed.lastIndexOf(QLatin1Char('.'));

    if (lastComma >= 0 && lastPeriod >= 0) {
        if (trimmed.size() - lastComma <= 3) {
            normalized = trimmed;
            normalized.remove(QLatin1Char('.'));
            normalized.replace(QLatin1Char(','), QLatin1Char('.'));
        } else {
            normalized = trimmed;
            normalized.remove(QLatin1Char(','));
        }
    } else if (lastComma >= 0) {
        if (trimmed.size() - lastComma == 3) {
            normalized = trimmed;
            normalized.replace(QLatin1Char(','), QLatin1Char('.'));
        } else {
            normalized = trimmed;
            normalized.remove(QLatin1Char(','));
        }
    } else {
        normalized = trimmed;
    }

    bool ok = false;
    const double value = normalized.toDouble(&ok);
    if (!ok) {
        return std::nullopt;
    }
    return isNegative ? -value : value;
}

MergedCell MergedCell::from(
    const std::vector<CellMergeInput>& cells,
    const std::vector<CellMergeInput>& leftCells,
    int row,
    int column
)
{
    return MergedCell::from(cells, leftCells, NeighborContext{}, row, column);
}

MergedCell MergedCell::from(
    const std::vector<CellMergeInput>& cells,
    const std::vector<CellMergeInput>& leftCells,
    NeighborContext neighborContext,
    int row,
    int column
)
{
    const auto sources = buildSources(cells);
    std::vector<CellData> validCells;
    validCells.reserve(cells.size());
    for (const auto& input : cells) {
        if (input.cell.has_value() && !input.cell->value.isEmpty()) {
            validCells.push_back(*input.cell);
        }
    }
    const auto formatCode = firstFormatCode(cells);

    if (validCells.empty()) {
        return makeCell(CellKind::Label, {}, sources, 0.0, 0, {}, formatCode, {QStringLiteral("所有来源为空或缺失")});
    }

    if (row == 0) {
        return makeCell(
            CellKind::Label,
            {},
            sources,
            0.0,
            0,
            {},
            formatCode,
            {QStringLiteral("首行按表头处理，强制视为标签列")},
            distinctValueCount(validCells) > 1
        );
    }

    const int blankSourceCount = static_cast<int>(std::count_if(sources.begin(), sources.end(), [](const CellSourceEntry& source) {
        return source.state == CellSourceState::Empty || source.state == CellSourceState::Missing;
    }));
    const bool codeSemantic = leftCellHasCodeSemantic(leftCells);
    const bool amountSemantic = leftCellHasAmountSemantic(leftCells);
    const bool weakAmountSemantic = leftCellHasWeakAmountSemantic(leftCells);
    const bool labelSemantic = leftCellHasLabelSemantic(leftCells);

    if (validCells.size() == 1) {
        const auto& cell = validCells.front();
        const auto fingerprint = fingerprintFor(&cell);
        const bool contextNumeric = neighborContext.numericTendency >= 0.55 && neighborContext.numericTendency > neighborContext.labelTendency;
        const bool columnMetricSemantic = neighborContext.columnMetricTendency >= 0.55;
        const int weakMetricEvidenceCount = (weakAmountSemantic ? 1 : 0) + (contextNumeric ? 1 : 0) + (columnMetricSemantic ? 1 : 0);
        const bool metricSemantic = amountSemantic || (!codeSemantic && !labelSemantic && weakMetricEvidenceCount >= 2);
        const bool isCodeLike = fingerprint == FormatFingerprint::IntegerCode || (cell.formatCode.has_value() && *cell.formatCode == QStringLiteral("@"));
        const bool zeroWithBlankBias = cell.numericValue.has_value()
            && std::fabs(*cell.numericValue) < 0.0000001
            && blankSourceCount > 0
            && !codeSemantic
            && !labelSemantic
            && neighborContext.labelTendency < 0.65;

        if (cell.numericValue.has_value()
            && !isCodeLike
            && !codeSemantic
            && !labelSemantic
            && (formatCodeLooksNumeric(cell.formatCode)
                || formatCodeLooksNumeric(formatCode)
                || amountSemantic
                || metricSemantic
                || contextNumeric
                || zeroWithBlankBias)) {
            return makeCell(
                CellKind::Sum,
                formatNumber(*cell.numericValue, cell.formatCode),
                sources,
                *cell.numericValue,
                0,
                {},
                cell.formatCode.has_value() ? cell.formatCode : formatCode,
                {QStringLiteral("仅有一个非空数值，格式或上下文支持按求和处理")}
            );
        }
        if (cells.size() == 1) {
            return makeCell(CellKind::Single, cell.value, sources, 0.0, 0, cell.value, cell.formatCode, {QStringLiteral("只有一个有效来源")});
        }
        return makeCell(CellKind::Single, cell.value, sources, 0.0, 0, cell.value, cell.formatCode, {QStringLiteral("仅有一个非空来源值，按单值显示")});
    }

    std::set<QString> distinctValues;
    bool allNumeric = true;
    bool allIntegers = true;
    double total = 0.0;
    std::optional<QString> firstFormatCode;
    std::vector<double> numericValues;
    for (const auto& cell : validCells) {
        distinctValues.insert(cell.value);
        if (!cell.numericValue.has_value()) {
            allNumeric = false;
        } else {
            const double value = *cell.numericValue;
            total += value;
            numericValues.push_back(value);
            allIntegers = allIntegers && std::fabs(value - std::floor(value)) < 0.0000001;
        }
        if (!firstFormatCode.has_value() && cell.formatCode.has_value()) {
            firstFormatCode = cell.formatCode;
        }
    }

    const bool sameValues = distinctValues.size() == 1;
    const bool allZero = allNumeric && std::all_of(numericValues.begin(), numericValues.end(), [](double value) {
        return std::fabs(value) < 0.0000001;
    });
    const bool allDates = std::all_of(validCells.begin(), validCells.end(), [](const CellData& cell) {
        return cell.isDate;
    });
    const bool identicalNonZeroIntegers = allNumeric
        && allIntegers
        && !numericValues.empty()
        && std::all_of(numericValues.begin(), numericValues.end(), [&](double value) {
            return std::fabs(value) >= 0.0000001 && std::fabs(value - numericValues.front()) < 0.0000001;
        });
    const bool codeLikeSequence = codeLikeNumericSequence(validCells);
    const bool contextNumeric = neighborContext.numericTendency >= 0.55 && neighborContext.numericTendency > neighborContext.labelTendency;
    const bool columnMetricSemantic = neighborContext.columnMetricTendency >= 0.55;
    const int weakMetricEvidenceCount = (weakAmountSemantic ? 1 : 0) + (contextNumeric ? 1 : 0) + (columnMetricSemantic ? 1 : 0);
    const bool metricSemantic = amountSemantic || (!codeSemantic && !labelSemantic && weakMetricEvidenceCount >= 2);

    if (allZero && !codeSemantic && !labelSemantic) {
        return makeCell(
            CellKind::Sum,
            formatNumber(0, firstFormatCode),
            sources,
            0,
            0,
            {},
            firstFormatCode,
            {QStringLiteral("所有非空来源均为 0，按可累加单元格求和处理")}
        );
    }

    if (allDates) {
        return makeCell(
            CellKind::Label,
            {},
            sources,
            0.0,
            0,
            {},
            firstFormatCode,
            {QStringLiteral("日期单元格按标签处理")},
            distinctValueCount(validCells) > 1
        );
    }

    if (identicalNonZeroIntegers && blankSourceCount == 0 && !metricSemantic) {
        return makeCell(
            CellKind::Label,
            {},
            sources,
            0.0,
            0,
            {},
            firstFormatCode,
            {QStringLiteral("所有来源为相同非零整数，且无明确可累加语义，按标签处理")}
        );
    }

    const bool strongNumeric = std::any_of(validCells.begin(), validCells.end(), [](const CellData& cell) {
        return fingerprintFor(&cell) == FormatFingerprint::StrongNumeric;
    });
    const bool textFormat = firstFormatCode.has_value()
        && (*firstFormatCode == QStringLiteral("@") || *firstFormatCode == QStringLiteral(";;;"));

    if (codeSemantic && codeLikeSequence && !amountSemantic) {
        return makeCell(
            CellKind::Label,
            {},
            sources,
            0.0,
            0,
            {},
            firstFormatCode,
            {QStringLiteral("左邻列命中编码语义，按标签处理")},
            distinctValueCount(validCells) > 1
        );
    }

    if (allNumeric
        && !codeSemantic
        && !labelSemantic
        && (!codeLikeSequence || blankSourceCount > 0 || metricSemantic)
        && (!sameValues || metricSemantic || blankSourceCount > 0 || firstFormatCode.has_value() || strongNumeric || contextNumeric)) {
        const bool isSuspicious = numericSumShouldBeSuspicious(validCells, cells, leftCells, neighborContext, blankSourceCount);
        return makeCell(
            CellKind::Sum,
            formatNumber(total, firstFormatCode),
            sources,
            total,
            0,
            {},
            firstFormatCode,
            {blankSourceCount > 0
                ? QStringLiteral("部分来源为空或缺失，非空来源均为数值，空值按 0 参与求和")
                : sameValues && metricSemantic
                    ? QStringLiteral("相同整数命中计量语义并得到同列上下文支持，按求和处理")
                    : QStringLiteral("所有有效来源均为数值，按求和处理")},
            isSuspicious
        );
    }

    if (column == 0 && allNumeric && !codeLikeSequence && !sameValues && (strongNumeric || contextNumeric)) {
        return makeCell(
            CellKind::Sum,
            formatNumber(total, firstFormatCode),
            sources,
            total,
            0,
            {},
            firstFormatCode,
            {QStringLiteral("首列以数值格式为主且上下文支持，按求和处理")}
        );
    }

    if (sameValues) {
        return makeCell(
            CellKind::Label,
            {},
            sources,
            0.0,
            0,
            {},
            validCells.front().formatCode,
            {QStringLiteral("所有有效来源值一致，按标签处理")},
            labelShouldBeSuspicious(validCells, cells, leftCells, neighborContext)
        );
    }

    if (column == 0) {
        return makeCell(
            CellKind::Label,
            {},
            sources,
            0.0,
            0,
            {},
            firstFormatCode,
            {QStringLiteral("首列采用保守策略，按标签处理")},
            distinctValueCount(validCells) > 1
        );
    }

    if (!allNumeric && (textFormat || labelSemantic || neighborContext.labelTendency >= 0.5)) {
        return makeCell(
            CellKind::Label,
            {},
            sources,
            0.0,
            0,
            {},
            firstFormatCode,
            {QStringLiteral("文本格式或上下文偏向标签，按标签处理")},
            distinctValueCount(validCells) > 1
        );
    }

    return makeCell(
        CellKind::Mixed,
        QStringLiteral("%1条").arg(distinctValues.size()),
        sources,
        0.0,
        static_cast<int>(distinctValues.size()),
        {},
        std::nullopt,
        {QStringLiteral("存在多个不同的非聚合值")}
    );
}

QString MergedCell::formatNumber(double value, const std::optional<QString>& formatCode)
{
    if (formatCode.has_value()) {
        const QString code = *formatCode;
        if (code.contains(QStringLiteral("¥"))
            || code.contains(QStringLiteral("\\¥"))
            || code.contains(QStringLiteral("[$¥]"))) {
            return QStringLiteral("¥") + QString::number(value, 'f', 2);
        }
        if (code.contains(QLatin1Char('$')) && !code.contains(QStringLiteral("[$-"))) {
            return QStringLiteral("$") + QString::number(value, 'f', 2);
        }
        if (code.contains(QStringLiteral("#,##0"))) {
            return groupedDecimal(value, 2);
        }
        if (code.contains(QLatin1Char('%'))) {
            return QString::number(value * 100.0, 'f', 2) + QStringLiteral("%");
        }
        if (code.contains(QStringLiteral(".00"))) {
            return QString::number(value, 'f', 2);
        }
        if (code.contains(QStringLiteral(".0")) && !code.contains(QStringLiteral(".00"))) {
            return QString::number(value, 'f', 1);
        }
    }

    if (std::fabs(value - std::round(value)) < 0.0000001) {
        return QString::number(static_cast<qint64>(std::llround(value)));
    }
    QString display = QString::number(value, 'f', 2);
    display.remove(QRegularExpression(QStringLiteral("\\.00$")));
    display.replace(QRegularExpression(QStringLiteral("(\\d)0+$")), QStringLiteral("\\1"));
    return display;
}

const CellData* SheetData::cellAt(int row, int column) const
{
    if (row < 0 || column < 0 || row >= static_cast<int>(rows.size())) {
        return nullptr;
    }
    const auto& rowData = rows[static_cast<size_t>(row)];
    if (column >= static_cast<int>(rowData.size())) {
        return nullptr;
    }
    return &rowData[static_cast<size_t>(column)];
}

int SheetData::effectiveRowCount() const
{
    int lastNonEmpty = -1;
    for (int row = 0; row < static_cast<int>(rows.size()); ++row) {
        for (const auto& cell : rows[static_cast<size_t>(row)]) {
            if (!cell.value.isEmpty()) {
                lastNonEmpty = row;
                break;
            }
        }
    }
    return lastNonEmpty + 1;
}

int SheetData::effectiveColumnCount() const
{
    int maxColumn = 0;
    for (const auto& row : rows) {
        int lastNonEmpty = -1;
        for (int column = 0; column < static_cast<int>(row.size()); ++column) {
            if (!row[static_cast<size_t>(column)].value.isEmpty()) {
                lastNonEmpty = column;
            }
        }
        maxColumn = std::max(maxColumn, lastNonEmpty + 1);
    }
    return maxColumn;
}

const SheetData* ExcelFile::sheetNamed(const QString& sheetName) const
{
    for (const auto& sheet : sheets) {
        if (sheet.name == sheetName) {
            return &sheet;
        }
    }
    return nullptr;
}

QString columnLetters(int column)
{
    QString result;
    int number = column;
    do {
        result.prepend(QChar(QLatin1Char('A').unicode() + (number % 26)));
        number = number / 26 - 1;
    } while (number >= 0);
    return result;
}

QString cellReference(int row, int column)
{
    return QStringLiteral("%1%2").arg(columnLetters(column)).arg(row + 1);
}

QString cellKindName(CellKind kind)
{
    switch (kind) {
    case CellKind::Label: return QStringLiteral("label");
    case CellKind::Sum: return QStringLiteral("sum");
    case CellKind::Mixed: return QStringLiteral("mixed");
    case CellKind::Single: return QStringLiteral("single");
    }
    return QStringLiteral("unknown");
}

QJsonObject toJson(const MergedCell& cell)
{
    QJsonArray sources;
    for (const auto& source : cell.sources) {
        QJsonObject object;
        object.insert(QStringLiteral("filename"), source.filename);
        object.insert(QStringLiteral("value"), source.value);
        object.insert(QStringLiteral("state"), source.state == CellSourceState::Value ? QStringLiteral("value") : source.state == CellSourceState::Empty ? QStringLiteral("empty") : QStringLiteral("missing"));
        sources.append(object);
    }

    QJsonObject object;
    object.insert(QStringLiteral("type"), cellKindName(cell.type.kind));
    object.insert(QStringLiteral("displayValue"), cell.displayValue);
    object.insert(QStringLiteral("sources"), sources);
    return object;
}

QJsonObject toJson(const MergedResult& result)
{
    QJsonArray rows;
    for (const auto& row : result.rows) {
        QJsonArray rowArray;
        for (const auto& cell : row) {
            rowArray.append(toJson(cell));
        }
        rows.append(rowArray);
    }

    QJsonObject object;
    object.insert(QStringLiteral("sheetName"), result.sheetName);
    object.insert(QStringLiteral("rows"), rows);
    object.insert(QStringLiteral("sourceFiles"), QJsonArray::fromStringList(result.sourceFiles));
    return object;
}

} // namespace xlsone
