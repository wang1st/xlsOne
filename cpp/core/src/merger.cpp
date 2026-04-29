#include "xlsone/core/merger.hpp"

#include <QRegularExpression>
#include <QSet>
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

NeighborContext buildNeighborContext(
    const std::vector<const SheetData*>& sheets,
    int row,
    int column
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
            if (!found || count > dominantCount) {
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
        return {};
    }
    return {numericScore / totalWeight, labelScore / totalWeight};
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

            mergedRow.push_back(MergedCell::from(inputs, leftInputs, buildNeighborContext(sheets, row, column), row, column));
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
