#include "xlsone/core/merger.hpp"

#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <map>

namespace xlsone {

namespace {

enum class LocalFingerprint {
    StrongNumeric,
    IntegerWide,
    IntegerCode,
    Label,
    Empty,
    Mixed,
};

bool allDigits(const QString& value)
{
    if (value.isEmpty()) {
        return false;
    }
    for (const auto ch : value) {
        if (!ch.isDigit()) {
            return false;
        }
    }
    return true;
}

LocalFingerprint fingerprint(const CellData* cell)
{
    if (cell == nullptr || cell->value.isEmpty()) {
        return LocalFingerprint::Empty;
    }
    if (cell->isDate) {
        return LocalFingerprint::Label;
    }
    if (cell->numericValue.has_value()) {
        const double number = *cell->numericValue;
        if (std::fabs(number - std::floor(number)) > 0.0000001) {
            return LocalFingerprint::StrongNumeric;
        }

        const bool integerFormat = cell->formatCode.has_value()
            && !cell->formatCode->contains(QLatin1Char('.'))
            && !cell->formatCode->contains(QStringLiteral("¥"))
            && !cell->formatCode->contains(QStringLiteral("\\¥"))
            && !cell->formatCode->contains(QStringLiteral("[$¥]"))
            && !cell->formatCode->contains(QLatin1Char('$'))
            && !cell->formatCode->contains(QLatin1Char('%'));

        QString text = cell->value;
        if (integerFormat && text.endsWith(QStringLiteral(".0"))) {
            text.chop(2);
        }
        const bool scientific = text.contains(QRegularExpression(QStringLiteral("[eE][+-]?\\d+")));
        if (!scientific
            && !integerFormat
            && (text.contains(QLatin1Char('.')) || text.contains(QLatin1Char(',')) || text.contains(QStringLiteral("，")))) {
            return LocalFingerprint::StrongNumeric;
        }

        QString digits;
        for (const auto ch : text) {
            if (ch.isDigit()) {
                digits.append(ch);
            }
        }
        static const QSet<int> codeLengths = {6, 9, 11, 12, 15, 18};
        if (digits.size() >= 2 && digits.size() == text.size() && codeLengths.contains(digits.size())) {
            return LocalFingerprint::IntegerCode;
        }
        if (cell->formatCode.has_value() && *cell->formatCode == QStringLiteral("@") && digits.size() >= 2 && digits.size() == text.size()) {
            return LocalFingerprint::IntegerCode;
        }
        return LocalFingerprint::IntegerWide;
    }
    if (cell->value.contains(QRegularExpression(QStringLiteral("\\p{Han}")))) {
        return LocalFingerprint::Label;
    }
    bool alpha = true;
    for (const auto ch : cell->value) {
        if (!ch.isLetter() && !ch.isSpace()) {
            alpha = false;
            break;
        }
    }
    return alpha ? LocalFingerprint::Label : LocalFingerprint::Mixed;
}

bool isNumericFingerprint(LocalFingerprint value)
{
    return value == LocalFingerprint::StrongNumeric
        || value == LocalFingerprint::IntegerWide
        || value == LocalFingerprint::IntegerCode;
}

int numericPreferenceRank(LocalFingerprint value)
{
    switch (value) {
    case LocalFingerprint::StrongNumeric:
        return 3;
    case LocalFingerprint::IntegerWide:
        return 2;
    case LocalFingerprint::IntegerCode:
        return 1;
    case LocalFingerprint::Label:
    case LocalFingerprint::Empty:
    case LocalFingerprint::Mixed:
        return 0;
    }
    return 0;
}

bool shouldPreferTiedFingerprint(LocalFingerprint candidate, LocalFingerprint current)
{
    if (isNumericFingerprint(candidate) && isNumericFingerprint(current)) {
        return numericPreferenceRank(candidate) > numericPreferenceRank(current);
    }
    return false;
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

QStringList metricAnchorPatterns()
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
        QStringLiteral("数"), QStringLiteral("额"), QStringLiteral("值"),
        QStringLiteral("量"), QStringLiteral("价")
    };
}

QStringList codeAnchorPatterns()
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
        QStringLiteral("条码"), QStringLiteral("档案号"), QStringLiteral("许可证号")
    };
}

bool isMetricAnchor(const CellData* cell)
{
    if (cell == nullptr || cell->value.isEmpty()) {
        return false;
    }
    if (matchesAnySemantic(cell->value, codeAnchorPatterns())) {
        return false;
    }
    return matchesAnySemantic(cell->value, metricAnchorPatterns());
}

const CellData* nearestLabelForFirstNumeric(const SheetData& sheet, int row, int column)
{
    const int maxDistance = std::max(static_cast<int>(sheet.rows.size()), column + 1);
    for (int distance = 1; distance <= maxDistance; ++distance) {
        const int aboveRow = row - distance;
        if (aboveRow >= 0) {
            const auto* cell = sheet.cellAt(aboveRow, column);
            if (cell != nullptr && !cell->value.isEmpty() && !cell->numericValue.has_value()) {
                return cell;
            }
        }
    }

    for (int leftColumn = column - 1; leftColumn >= 0; --leftColumn) {
        const auto* cell = sheet.cellAt(row, leftColumn);
        if (cell != nullptr && !cell->value.isEmpty() && !cell->numericValue.has_value()) {
            return cell;
        }
    }

    for (int distance = 1; distance <= maxDistance; ++distance) {
        const int aboveLeftRow = row - distance;
        if (aboveLeftRow >= 0 && column > 0) {
            const auto* cell = sheet.cellAt(aboveLeftRow, column - 1);
            if (cell != nullptr && !cell->value.isEmpty() && !cell->numericValue.has_value()) {
                return cell;
            }
        }
    }
    return nullptr;
}

double buildColumnMetricTendency(const std::vector<const SheetData*>& sheets, int column)
{
    int labeledColumns = 0;
    int metricAnchors = 0;
    for (const auto* sheet : sheets) {
        if (sheet == nullptr) {
            continue;
        }
        for (int row = 0; row < static_cast<int>(sheet->rows.size()); ++row) {
            const auto* cell = sheet->cellAt(row, column);
            if (cell == nullptr || cell->value.isEmpty() || !cell->numericValue.has_value()) {
                continue;
            }
            const auto* label = nearestLabelForFirstNumeric(*sheet, row, column);
            if (label != nullptr) {
                ++labeledColumns;
                if (isMetricAnchor(label)) {
                    ++metricAnchors;
                }
            }
            break;
        }
    }

    if (labeledColumns == 0) {
        return 0.0;
    }
    return static_cast<double>(metricAnchors) / static_cast<double>(labeledColumns);
}

NeighborContext buildNeighborContext(
    const std::vector<const SheetData*>& sheets,
    int row,
    int column,
    double columnMetricTendency
)
{
    double numericScore = 0.0;
    double labelScore = 0.0;
    double totalWeight = 0.0;

    int maxRows = 0;
    for (const auto* sheet : sheets) {
        maxRows = std::max(maxRows, static_cast<int>(sheet->rows.size()));
    }

    auto applyRow = [&](int targetRow, double weight) {
        std::map<LocalFingerprint, int> counts;
        std::vector<LocalFingerprint> order;
        for (const auto* sheet : sheets) {
            const auto cellFingerprint = fingerprint(sheet->cellAt(targetRow, column));
            if (cellFingerprint == LocalFingerprint::Empty) {
                continue;
            }
            if (counts[cellFingerprint] == 0) {
                order.push_back(cellFingerprint);
            }
            ++counts[cellFingerprint];
        }

        bool found = false;
        LocalFingerprint dominant = LocalFingerprint::Empty;
        int dominantCount = 0;
        for (const auto cellFingerprint : order) {
            const int count = counts[cellFingerprint];
            if (!found
                || count > dominantCount
                || (count == dominantCount && shouldPreferTiedFingerprint(cellFingerprint, dominant))) {
                dominant = cellFingerprint;
                dominantCount = count;
                found = true;
            }
        }

        if (found) {
            if (dominant == LocalFingerprint::StrongNumeric || dominant == LocalFingerprint::IntegerWide) {
                numericScore += weight;
            } else if (dominant == LocalFingerprint::Label) {
                labelScore += weight;
            }
        }
        totalWeight += weight;
    };

    for (int offset = 1; offset <= 3; ++offset) {
        const int above = row - offset;
        if (above >= 0) {
            applyRow(above, 1.0 / static_cast<double>(offset));
        }
        const int below = row + offset;
        if (below < maxRows) {
            applyRow(below, 1.0 / static_cast<double>(offset));
        }
    }

    if (totalWeight <= 0) {
        return {0.0, 0.0, columnMetricTendency};
    }
    return {numericScore / totalWeight, labelScore / totalWeight, columnMetricTendency};
}

} // namespace

MergedResult SimpleMerger::merge(const std::vector<ExcelFile>& files, const QString& sheetName) const
{
    struct Entry {
        QString filename;
        QString filepath;
        const SheetData* sheet = nullptr;
    };

    std::vector<Entry> entries;
    for (const auto& file : files) {
        if (const auto* sheet = file.sheetNamed(sheetName)) {
            entries.push_back({file.filename, file.filepath, sheet});
        }
    }

    MergedResult result;
    result.sheetName = sheetName;
    for (const auto& entry : entries) {
        result.sourceFiles.append(entry.filename);
    }
    if (entries.empty()) {
        return result;
    }

    std::vector<const SheetData*> sheets;
    sheets.reserve(entries.size());
    for (const auto& entry : entries) {
        sheets.push_back(entry.sheet);
    }

    int maxRows = 0;
    int maxColumns = 0;
    for (const auto& entry : entries) {
        maxRows = std::max(maxRows, static_cast<int>(entry.sheet->rows.size()));
        for (const auto& row : entry.sheet->rows) {
            maxColumns = std::max(maxColumns, static_cast<int>(row.size()));
        }
    }

    std::vector<double> columnMetricTendencies(static_cast<size_t>(maxColumns), 0.0);
    for (int column = 0; column < maxColumns; ++column) {
        columnMetricTendencies[static_cast<size_t>(column)] = buildColumnMetricTendency(sheets, column);
    }

    result.rows.reserve(static_cast<size_t>(maxRows));
    for (int row = 0; row < maxRows; ++row) {
        std::vector<MergedCell> mergedRow;
        mergedRow.reserve(static_cast<size_t>(maxColumns));
        for (int column = 0; column < maxColumns; ++column) {
            std::vector<CellMergeInput> inputs;
            std::vector<CellMergeInput> leftInputs;
            inputs.reserve(entries.size());
            leftInputs.reserve(entries.size());

            for (const auto& entry : entries) {
                const auto* cell = entry.sheet->cellAt(row, column);
                inputs.push_back({
                    entry.filename,
                    entry.filepath,
                    cell == nullptr ? std::optional<CellData>{} : std::optional<CellData>{*cell}
                });
                const auto* leftCell = column > 0 ? entry.sheet->cellAt(row, column - 1) : nullptr;
                leftInputs.push_back({
                    entry.filename,
                    entry.filepath,
                    leftCell == nullptr ? std::optional<CellData>{} : std::optional<CellData>{*leftCell}
                });
            }

            mergedRow.push_back(MergedCell::from(
                inputs,
                leftInputs,
                buildNeighborContext(sheets, row, column, columnMetricTendencies[static_cast<size_t>(column)]),
                row,
                column
            ));
        }
        result.rows.push_back(std::move(mergedRow));
    }

    return result;
}

MergedResult SimpleMerger::mergeFirstSheets(const std::vector<ExcelFile>& files) const
{
    for (const auto& file : files) {
        if (!file.sheets.empty()) {
            return merge(files, file.sheets.front().name);
        }
    }
    return {};
}

QStringList SimpleMerger::availableSheetNames(const std::vector<ExcelFile>& files) const
{
    QStringList names;
    QSet<QString> seen;
    for (const auto& file : files) {
        for (const auto& sheet : file.sheets) {
            if (!seen.contains(sheet.name)) {
                seen.insert(sheet.name);
                names.append(sheet.name);
            }
        }
    }
    return names;
}

} // namespace xlsone
