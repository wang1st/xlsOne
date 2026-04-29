#include "xlsone/core/excel_parser.hpp"

#include "biff8_xls_parser.hpp"
#include "zip_archive.hpp"

#include <QDate>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>
#include <QXmlStreamReader>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>

namespace xlsone {

namespace {

struct WorkbookSheet {
    QString name;
    QString sheetId;
    QString relationshipId;
};

struct CellStyleTable {
    QHash<int, QString> numberFormats;
    QHash<int, int> cellFormats;

    QString formatCode(int styleIndex) const
    {
        if (!cellFormats.contains(styleIndex)) {
            return {};
        }
        const int numberFormatId = cellFormats.value(styleIndex);
        if (numberFormats.contains(numberFormatId)) {
            return numberFormats.value(numberFormatId);
        }
        return builtInFormatCode(numberFormatId);
    }

    bool isDateFormat(int styleIndex) const
    {
        if (!cellFormats.contains(styleIndex)) {
            return false;
        }
        const int numberFormatId = cellFormats.value(styleIndex);
        if ((numberFormatId >= 14 && numberFormatId <= 22) || (numberFormatId >= 45 && numberFormatId <= 47)) {
            return true;
        }
        const auto code = formatCode(styleIndex);
        if (code.isEmpty()) {
            return false;
        }
        return isDateFormatCode(code);
    }

    static QString builtInFormatCode(int numberFormatId)
    {
        switch (numberFormatId) {
        case 0: return QStringLiteral("General");
        case 1: return QStringLiteral("0");
        case 2: return QStringLiteral("0.00");
        case 3: return QStringLiteral("#,##0");
        case 4: return QStringLiteral("#,##0.00");
        case 9: return QStringLiteral("0%");
        case 10: return QStringLiteral("0.00%");
        case 11: return QStringLiteral("0.00E+00");
        case 12: return QStringLiteral("# ?/?");
        case 13: return QStringLiteral("# ??") + QStringLiteral("/??");
        case 14:
        case 15:
        case 16:
        case 17:
        case 18:
        case 19:
        case 20:
        case 21:
        case 22:
            return QStringLiteral("yyyy-MM-dd");
        case 37: return QStringLiteral("#,##0 ;(#,##0)");
        case 38: return QStringLiteral("#,##0 ;[Red](#,##0)");
        case 39: return QStringLiteral("#,##0.00;(#,##0.00)");
        case 40: return QStringLiteral("#,##0.00;[Red](#,##0.00)");
        case 45: return QStringLiteral("mm:ss");
        case 46: return QStringLiteral("[h]:mm:ss");
        case 47: return QStringLiteral("mmss.0");
        case 48: return QStringLiteral("##0.0E+0");
        case 49: return QStringLiteral("@");
        default: return {};
        }
    }

    static bool isDateFormatCode(QString code)
    {
        code.remove(QRegularExpression(QStringLiteral("\\[[^\\]]*\\]")));
        code.remove(QRegularExpression(QStringLiteral("\"[^\"]*\"")));
        return code.contains(QRegularExpression(QStringLiteral("[ymdhsYMDHS]")));
    }
};

struct ParsedWorksheetCell {
    int row = 0;
    int column = 0;
    QString type;
    int styleIndex = -1;
    QString rawValue;
    QString inlineText;
};

struct CellRange {
    CellPosition first;
    CellPosition last;
};

QString attributeValue(const QXmlStreamAttributes& attributes, const QString& name)
{
    for (const auto& attribute : attributes) {
        if (attribute.name() == name || attribute.qualifiedName() == name) {
            return attribute.value().toString();
        }
    }
    return {};
}

QString normalizeRelationshipTarget(const QString& target)
{
    QString normalized = target;
    if (normalized.startsWith(QLatin1Char('/'))) {
        normalized.remove(0, 1);
    }
    if (!normalized.startsWith(QStringLiteral("xl/"))) {
        normalized.prepend(QStringLiteral("xl/"));
    }
    return normalized;
}

int columnIndexFromLetters(const QString& letters)
{
    int result = 0;
    for (const QChar ch : letters.toUpper()) {
        if (!ch.isLetter()) {
            continue;
        }
        result = result * 26 + (ch.unicode() - QLatin1Char('A').unicode() + 1);
    }
    return result - 1;
}

std::optional<CellPosition> parseCellCoordinate(const QString& reference)
{
    QString columnLetters;
    QString rowDigits;
    for (const QChar ch : reference) {
        if (ch.isLetter()) {
            columnLetters.append(ch);
        } else if (ch.isDigit()) {
            rowDigits.append(ch);
        }
    }
    if (columnLetters.isEmpty() || rowDigits.isEmpty()) {
        return std::nullopt;
    }
    bool ok = false;
    const int row = rowDigits.toInt(&ok);
    if (!ok || row <= 0) {
        return std::nullopt;
    }
    return CellPosition{row - 1, columnIndexFromLetters(columnLetters)};
}

std::optional<CellRange> parseCellRange(const QString& reference)
{
    const auto parts = reference.split(QLatin1Char(':'));
    if (parts.size() != 2) {
        return std::nullopt;
    }
    const auto first = parseCellCoordinate(parts[0]);
    const auto last = parseCellCoordinate(parts[1]);
    if (!first.has_value() || !last.has_value()) {
        return std::nullopt;
    }
    return CellRange{
        {std::min(first->row, last->row), std::min(first->column, last->column)},
        {std::max(first->row, last->row), std::max(first->column, last->column)}
    };
}

CellData& ensureCell(SheetData& sheet, int rowIndex, int columnIndex)
{
    while (sheet.rows.size() <= static_cast<size_t>(rowIndex)) {
        sheet.rows.emplace_back();
    }
    auto& row = sheet.rows[static_cast<size_t>(rowIndex)];
    while (row.size() <= static_cast<size_t>(columnIndex)) {
        row.emplace_back(QString());
    }
    return row[static_cast<size_t>(columnIndex)];
}

bool isBlankCell(const CellData& cell)
{
    return cell.value.trimmed().isEmpty()
        && !cell.rawValue.has_value()
        && !cell.numericValue.has_value();
}

void applyMergedRanges(SheetData& sheet, const std::vector<CellRange>& ranges)
{
    for (const auto& range : ranges) {
        const auto source = ensureCell(sheet, range.first.row, range.first.column);
        for (int row = range.first.row; row <= range.last.row; ++row) {
            for (int column = range.first.column; column <= range.last.column; ++column) {
                auto& target = ensureCell(sheet, row, column);
                if (isBlankCell(target)) {
                    target = source;
                }
            }
        }
    }
}

QString formatExcelDate(double numericValue)
{
    const QDate epoch(1899, 12, 28);
    return epoch.addDays(static_cast<qint64>(std::floor(numericValue))).toString(QStringLiteral("yyyy-MM-dd"));
}

QString displayNumber(double number, const QString& rawValue, const QString& formatCode)
{
    QString display = rawValue;
    if (display.isEmpty()) {
        display = QString::number(number, 'g', 15);
    } else if (display.contains(QRegularExpression(QStringLiteral("[eE][+-]?\\d+")))) {
        if (std::fabs(number - std::round(number)) < 0.0000001) {
            display = QString::number(static_cast<qint64>(std::llround(number)));
        } else {
            display = QString::number(number, 'g', 15);
        }
    }

    const bool integerFormat = !formatCode.contains(QLatin1Char('.'))
        && !formatCode.contains(QStringLiteral("¥"))
        && !formatCode.contains(QStringLiteral("\\¥"))
        && !formatCode.contains(QStringLiteral("[$¥]"))
        && !formatCode.contains(QLatin1Char('$'))
        && !formatCode.contains(QLatin1Char('%'));
    if (integerFormat && display.endsWith(QStringLiteral(".0"))) {
        display.chop(2);
    }
    return display;
}

std::vector<QString> parseDelimitedLine(const QString& line)
{
    std::vector<QString> values;
    QString current;
    bool inQuotes = false;
    for (qsizetype index = 0; index < line.size(); ++index) {
        const auto ch = line[index];
        if (ch == QLatin1Char('"')) {
            inQuotes = !inQuotes;
            continue;
        }
        if (!inQuotes && (ch == QLatin1Char(',') || ch == QLatin1Char('\t'))) {
            values.push_back(current);
            current.clear();
            continue;
        }
        current.append(ch);
    }
    values.push_back(current);
    return values;
}

ExcelFile parseTextWorkbook(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        throw std::runtime_error(QStringLiteral("无法打开文件: %1").arg(file.errorString()).toStdString());
    }

    SheetData sheet;
    sheet.name = QStringLiteral("Sheet1");
    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const auto values = parseDelimitedLine(stream.readLine());
        std::vector<CellData> row;
        row.reserve(values.size());
        for (const auto& value : values) {
            row.emplace_back(value);
        }
        sheet.rows.push_back(std::move(row));
    }

    QFileInfo info(path);
    return {info.fileName(), path, {sheet}};
}

std::vector<WorkbookSheet> parseWorkbookSheets(const QString& xml)
{
    std::vector<WorkbookSheet> sheets;
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement() || reader.name() != QStringLiteral("sheet")) {
            continue;
        }
        const auto attributes = reader.attributes();
        sheets.push_back({
            attributeValue(attributes, QStringLiteral("name")),
            attributeValue(attributes, QStringLiteral("sheetId")),
            attributeValue(attributes, QStringLiteral("r:id"))
        });
    }
    if (reader.hasError()) {
        throw std::runtime_error(QStringLiteral("workbook.xml 解析失败: %1").arg(reader.errorString()).toStdString());
    }
    return sheets;
}

QHash<QString, QString> parseWorkbookRelationships(const QString& xml)
{
    QHash<QString, QString> targetsByRelationshipId;
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement() || reader.name() != QStringLiteral("Relationship")) {
            continue;
        }
        const auto attributes = reader.attributes();
        const auto id = attributeValue(attributes, QStringLiteral("Id"));
        const auto target = attributeValue(attributes, QStringLiteral("Target"));
        if (!id.isEmpty() && !target.isEmpty()) {
            targetsByRelationshipId.insert(id, normalizeRelationshipTarget(target));
        }
    }
    if (reader.hasError()) {
        throw std::runtime_error(QStringLiteral("workbook.xml.rels 解析失败: %1").arg(reader.errorString()).toStdString());
    }
    return targetsByRelationshipId;
}

std::vector<QString> parseSharedStrings(const ZipArchive& archive)
{
    std::vector<QString> strings;
    if (!archive.contains(QStringLiteral("xl/sharedStrings.xml"))) {
        return strings;
    }

    QXmlStreamReader reader(archive.readText(QStringLiteral("xl/sharedStrings.xml")));
    bool inStringItem = false;
    bool inText = false;
    QString current;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            if (reader.name() == QStringLiteral("si")) {
                inStringItem = true;
                current.clear();
            } else if (inStringItem && reader.name() == QStringLiteral("t")) {
                inText = true;
            }
        } else if (reader.isCharacters() && inStringItem && inText) {
            current.append(reader.text());
        } else if (reader.isEndElement()) {
            if (reader.name() == QStringLiteral("t")) {
                inText = false;
            } else if (reader.name() == QStringLiteral("si")) {
                strings.push_back(current);
                inStringItem = false;
            }
        }
    }
    if (reader.hasError()) {
        throw std::runtime_error(QStringLiteral("sharedStrings.xml 解析失败: %1").arg(reader.errorString()).toStdString());
    }
    return strings;
}

CellStyleTable parseStyles(const ZipArchive& archive)
{
    CellStyleTable table;
    if (!archive.contains(QStringLiteral("xl/styles.xml"))) {
        return table;
    }

    QXmlStreamReader reader(archive.readText(QStringLiteral("xl/styles.xml")));
    bool inCellFormats = false;
    int cellFormatIndex = 0;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            if (reader.name() == QStringLiteral("numFmt")) {
                const auto attributes = reader.attributes();
                bool ok = false;
                const int id = attributeValue(attributes, QStringLiteral("numFmtId")).toInt(&ok);
                const auto code = attributeValue(attributes, QStringLiteral("formatCode"));
                if (ok && !code.isEmpty()) {
                    table.numberFormats.insert(id, code);
                }
            } else if (reader.name() == QStringLiteral("cellXfs")) {
                inCellFormats = true;
                cellFormatIndex = 0;
            } else if (inCellFormats && reader.name() == QStringLiteral("xf")) {
                bool ok = false;
                const int numberFormatId = attributeValue(reader.attributes(), QStringLiteral("numFmtId")).toInt(&ok);
                if (ok) {
                    table.cellFormats.insert(cellFormatIndex, numberFormatId);
                }
                ++cellFormatIndex;
            }
        } else if (reader.isEndElement() && reader.name() == QStringLiteral("cellXfs")) {
            inCellFormats = false;
        }
    }
    if (reader.hasError()) {
        throw std::runtime_error(QStringLiteral("styles.xml 解析失败: %1").arg(reader.errorString()).toStdString());
    }
    return table;
}

CellData cellDataFromParsedCell(
    const ParsedWorksheetCell& parsed,
    const std::vector<QString>& sharedStrings,
    const CellStyleTable& styles
)
{
    std::optional<QString> formatCode;
    if (parsed.styleIndex >= 0) {
        const auto code = styles.formatCode(parsed.styleIndex);
        if (!code.isEmpty()) {
            formatCode = code;
        }
    }
    const QString code = formatCode.value_or(QString());

    if (parsed.type == QStringLiteral("s")) {
        bool ok = false;
        const int index = parsed.rawValue.toInt(&ok);
        const QString value = ok && index >= 0 && index < static_cast<int>(sharedStrings.size())
            ? sharedStrings[static_cast<size_t>(index)]
            : QString();
        return CellData(value, parsed.rawValue, std::nullopt, formatCode, false);
    }

    if (parsed.type == QStringLiteral("inlineStr")) {
        return CellData(parsed.inlineText, parsed.rawValue, std::nullopt, formatCode, false);
    }

    if (parsed.type == QStringLiteral("b")) {
        const QString value = parsed.rawValue == QStringLiteral("1") ? QStringLiteral("TRUE") : QStringLiteral("FALSE");
        return CellData(value, parsed.rawValue, std::nullopt, formatCode, false);
    }

    if (parsed.type == QStringLiteral("str")) {
        return CellData(parsed.rawValue, parsed.rawValue, std::nullopt, formatCode, false);
    }

    bool numericOk = false;
    const double number = parsed.rawValue.toDouble(&numericOk);
    if (numericOk) {
        if (parsed.styleIndex >= 0 && styles.isDateFormat(parsed.styleIndex)) {
            return CellData(formatExcelDate(number), parsed.rawValue, number, formatCode, true);
        }
        return CellData(displayNumber(number, parsed.rawValue, code), parsed.rawValue, number, formatCode, false);
    }

    return CellData(parsed.rawValue, parsed.rawValue, std::nullopt, formatCode, false);
}

SheetData parseWorksheet(
    const QString& sheetName,
    const QString& xml,
    const std::vector<QString>& sharedStrings,
    const CellStyleTable& styles
)
{
    SheetData sheet;
    sheet.name = sheetName;

    QXmlStreamReader reader(xml);
    std::optional<ParsedWorksheetCell> currentCell;
    std::vector<CellRange> mergedRanges;
    bool inValue = false;
    bool inInlineText = false;

    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            if (reader.name() == QStringLiteral("row")) {
                bool ok = false;
                const int rowIndex = attributeValue(reader.attributes(), QStringLiteral("r")).toInt(&ok) - 1;
                if (ok && rowIndex >= 0) {
                    while (sheet.rows.size() <= static_cast<size_t>(rowIndex)) {
                        sheet.rows.emplace_back();
                    }
                }
            } else if (reader.name() == QStringLiteral("c")) {
                const auto attributes = reader.attributes();
                const auto reference = attributeValue(attributes, QStringLiteral("r"));
                const auto coordinate = parseCellCoordinate(reference);
                if (!coordinate.has_value()) {
                    currentCell.reset();
                    continue;
                }
                currentCell = ParsedWorksheetCell{};
                currentCell->row = coordinate->row;
                currentCell->column = coordinate->column;
                currentCell->type = attributeValue(attributes, QStringLiteral("t"));
                bool ok = false;
                const int styleIndex = attributeValue(attributes, QStringLiteral("s")).toInt(&ok);
                currentCell->styleIndex = ok ? styleIndex : -1;
            } else if (reader.name() == QStringLiteral("mergeCell")) {
                const auto range = parseCellRange(attributeValue(reader.attributes(), QStringLiteral("ref")));
                if (range.has_value()) {
                    mergedRanges.push_back(*range);
                }
            } else if (currentCell.has_value() && reader.name() == QStringLiteral("v")) {
                inValue = true;
                currentCell->rawValue.clear();
            } else if (currentCell.has_value() && reader.name() == QStringLiteral("t")) {
                inInlineText = true;
            }
        } else if (reader.isCharacters() && currentCell.has_value()) {
            if (inValue) {
                currentCell->rawValue.append(reader.text());
            } else if (inInlineText) {
                currentCell->inlineText.append(reader.text());
            }
        } else if (reader.isEndElement()) {
            if (reader.name() == QStringLiteral("v")) {
                inValue = false;
            } else if (reader.name() == QStringLiteral("t")) {
                inInlineText = false;
            } else if (reader.name() == QStringLiteral("c") && currentCell.has_value()) {
                while (sheet.rows.size() <= static_cast<size_t>(currentCell->row)) {
                    sheet.rows.emplace_back();
                }
                auto& row = sheet.rows[static_cast<size_t>(currentCell->row)];
                while (row.size() < static_cast<size_t>(currentCell->column)) {
                    row.emplace_back(QString());
                }
                const auto cell = cellDataFromParsedCell(*currentCell, sharedStrings, styles);
                if (row.size() == static_cast<size_t>(currentCell->column)) {
                    row.push_back(cell);
                } else {
                    row[static_cast<size_t>(currentCell->column)] = cell;
                }
                currentCell.reset();
            }
        }
    }

    if (reader.hasError()) {
        throw std::runtime_error(QStringLiteral("%1 解析失败: %2").arg(sheetName, reader.errorString()).toStdString());
    }

    applyMergedRanges(sheet, mergedRanges);

    size_t maxColumns = 0;
    for (const auto& row : sheet.rows) {
        maxColumns = std::max(maxColumns, row.size());
    }
    for (auto& row : sheet.rows) {
        while (row.size() < maxColumns) {
            row.emplace_back(QString());
        }
    }

    return sheet;
}

ExcelFile parseXlsxWorkbook(const QString& path)
{
    const ZipArchive archive(path);
    const auto workbookSheets = parseWorkbookSheets(archive.readText(QStringLiteral("xl/workbook.xml")));
    const auto targetsByRelationshipId = parseWorkbookRelationships(archive.readText(QStringLiteral("xl/_rels/workbook.xml.rels")));
    const auto sharedStrings = parseSharedStrings(archive);
    const auto styles = parseStyles(archive);

    std::vector<SheetData> sheets;
    sheets.reserve(workbookSheets.size());
    for (const auto& workbookSheet : workbookSheets) {
        const auto targetIterator = targetsByRelationshipId.constFind(workbookSheet.relationshipId);
        if (targetIterator == targetsByRelationshipId.constEnd() || !archive.contains(targetIterator.value())) {
            continue;
        }
        sheets.push_back(parseWorksheet(
            workbookSheet.name,
            archive.readText(targetIterator.value()),
            sharedStrings,
            styles
        ));
    }

    QFileInfo info(path);
    return {info.fileName(), path, sheets};
}

} // namespace

ExcelFile ExcelParser::parseFile(const QString& path) const
{
    QFileInfo info(path);
    if (!info.exists()) {
        throw std::runtime_error(QStringLiteral("文件不存在").toStdString());
    }

    const auto suffix = info.suffix().toLower();
    if (suffix == QStringLiteral("xlsx")) {
        return parseXlsxWorkbook(path);
    }
    if (suffix == QStringLiteral("xls")) {
        return parseBIFF8XLSFile(path);
    }
    if (suffix == QStringLiteral("csv") || suffix == QStringLiteral("tsv")) {
        return parseTextWorkbook(path);
    }

    throw std::runtime_error(QStringLiteral("C++/Qt 迁移基座尚未接入 .%1 解析器").arg(suffix).toStdString());
}

ExcelParseBatchResult ExcelParser::parseFiles(const QStringList& paths) const
{
    ExcelParseBatchResult result;
    for (const auto& path : paths) {
        try {
            result.files.push_back(parseFile(path));
        } catch (const std::exception& error) {
            result.failures.push_back({path, QString::fromUtf8(error.what())});
        }
    }
    return result;
}

} // namespace xlsone
