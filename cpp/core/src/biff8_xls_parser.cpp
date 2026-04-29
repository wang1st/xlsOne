#include "biff8_xls_parser.hpp"

#include "compound_file_reader.hpp"

#include <QDate>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QtEndian>
#include <algorithm>
#include <bit>
#include <cmath>
#include <map>
#include <optional>
#include <stdexcept>

namespace xlsone {

namespace {

struct Record {
    quint16 id = 0;
    int offset = 0;
    QByteArray body;
};

struct BoundSheet {
    int offset = 0;
    QString name;
};

struct CellRange {
    CellPosition first;
    CellPosition last;
};

struct WorkbookGlobals {
    std::vector<BoundSheet> sheets;
    std::vector<QString> sharedStrings;
    QHash<int, QString> formats;
    QHash<int, int> xfFormatIds;
};

quint8 u8(const QByteArray& data, qsizetype offset)
{
    if (offset < 0 || offset >= data.size()) {
        return 0;
    }
    return static_cast<quint8>(data.at(offset));
}

quint16 u16(const QByteArray& data, qsizetype offset)
{
    if (offset < 0 || offset + 2 > data.size()) {
        return 0;
    }
    return qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(data.constData() + offset));
}

quint32 u32(const QByteArray& data, qsizetype offset)
{
    if (offset < 0 || offset + 4 > data.size()) {
        return 0;
    }
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(data.constData() + offset));
}

double f64(const QByteArray& data, qsizetype offset)
{
    if (offset < 0 || offset + 8 > data.size()) {
        return 0.0;
    }
    const quint64 bits = qFromLittleEndian<quint64>(reinterpret_cast<const uchar*>(data.constData() + offset));
    return std::bit_cast<double>(bits);
}

QString decodeByteString(const QByteArray& data)
{
    return QString::fromLatin1(data.constData(), data.size());
}

QString decodeWideString(const QByteArray& data)
{
    QString result;
    result.reserve(data.size() / 2);
    for (qsizetype offset = 0; offset + 2 <= data.size(); offset += 2) {
        result.append(QChar(u16(data, offset)));
    }
    return result;
}

std::vector<Record> parseRecords(const QByteArray& data, int startOffset = 0)
{
    std::vector<Record> records;
    int offset = startOffset;
    while (offset + 4 <= data.size()) {
        const quint16 id = u16(data, offset);
        const int length = u16(data, offset + 2);
        const int bodyOffset = offset + 4;
        if (bodyOffset + length > data.size()) {
            break;
        }
        records.push_back({id, offset, data.mid(bodyOffset, length)});
        offset = bodyOffset + length;
        if (id == 0x000a && startOffset != 0) {
            break;
        }
    }
    return records;
}

std::vector<QByteArray> continuationChunks(const std::vector<Record>& records, size_t index)
{
    std::vector<QByteArray> chunks;
    size_t next = index + 1;
    while (next < records.size() && records[next].id == 0x003c) {
        chunks.push_back(records[next].body);
        ++next;
    }
    return chunks;
}

std::optional<std::pair<QString, int>> parseXLUnicodeString(const QByteArray& data, int offset)
{
    if (offset + 3 > data.size()) {
        return std::nullopt;
    }

    const int characterCount = u16(data, offset);
    const quint8 flags = u8(data, offset + 2);
    int cursor = offset + 3;
    const bool hasAsianPhonetics = (flags & 0x04) != 0;
    const bool hasRichText = (flags & 0x08) != 0;
    const bool isWide = (flags & 0x01) != 0;

    int richTextRunCount = 0;
    int extensionByteCount = 0;
    if (hasRichText) {
        if (cursor + 2 > data.size()) {
            return std::nullopt;
        }
        richTextRunCount = u16(data, cursor);
        cursor += 2;
    }
    if (hasAsianPhonetics) {
        if (cursor + 4 > data.size()) {
            return std::nullopt;
        }
        extensionByteCount = static_cast<int>(u32(data, cursor));
        cursor += 4;
    }

    const int byteCount = characterCount * (isWide ? 2 : 1);
    if (cursor + byteCount > data.size()) {
        return std::nullopt;
    }
    const QByteArray stringData = data.mid(cursor, byteCount);
    const QString value = isWide ? decodeWideString(stringData) : decodeByteString(stringData);
    cursor += byteCount + richTextRunCount * 4 + extensionByteCount;
    if (cursor > data.size()) {
        return std::nullopt;
    }
    return std::make_pair(value, cursor);
}

class SharedStringCursor {
public:
    explicit SharedStringCursor(std::vector<QByteArray> segments) : segments_(std::move(segments)) {}

    bool isAtEnd() const
    {
        size_t index = segmentIndex_;
        int cursor = offset_;
        while (index < segments_.size()) {
            if (cursor < segments_[index].size()) {
                return false;
            }
            ++index;
            cursor = 0;
        }
        return true;
    }

    std::optional<QString> readXLUnicodeString()
    {
        const auto characterCount = readUInt16();
        const auto flagsValue = readUInt8();
        if (!characterCount.has_value() || !flagsValue.has_value()) {
            return std::nullopt;
        }

        bool isWide = (*flagsValue & 0x01) != 0;
        const bool hasAsianPhonetics = (*flagsValue & 0x04) != 0;
        const bool hasRichText = (*flagsValue & 0x08) != 0;

        int richTextRunCount = 0;
        int extensionByteCount = 0;
        if (hasRichText) {
            const auto value = readUInt16();
            if (!value.has_value()) {
                return std::nullopt;
            }
            richTextRunCount = *value;
        }
        if (hasAsianPhonetics) {
            const auto value = readUInt32();
            if (!value.has_value()) {
                return std::nullopt;
            }
            extensionByteCount = static_cast<int>(*value);
        }

        const auto value = readCharacters(*characterCount, isWide);
        if (!value.has_value() || !skip(richTextRunCount * 4 + extensionByteCount)) {
            return std::nullopt;
        }
        return value;
    }

private:
    std::vector<QByteArray> segments_;
    size_t segmentIndex_ = 0;
    int offset_ = 0;

    std::optional<QString> readCharacters(int count, bool& isWide)
    {
        QString value;
        value.reserve(count);
        for (int index = 0; index < count; ++index) {
            const int byteCount = isWide ? 2 : 1;
            if (!ensureTextBytes(byteCount, isWide)) {
                return std::nullopt;
            }
            const auto bytes = readBytes(byteCount);
            if (!bytes.has_value()) {
                return std::nullopt;
            }
            value.append(isWide ? decodeWideString(*bytes) : decodeByteString(*bytes));
        }
        return value;
    }

    bool ensureTextBytes(int byteCount, bool& isWide)
    {
        while (segmentIndex_ < segments_.size()) {
            const int available = segments_[segmentIndex_].size() - offset_;
            if (available >= byteCount) {
                return true;
            }
            if (available != 0) {
                return false;
            }
            ++segmentIndex_;
            offset_ = 0;
            if (segmentIndex_ >= segments_.size()) {
                return false;
            }
            const auto flags = readUInt8();
            if (!flags.has_value()) {
                return false;
            }
            isWide = (*flags & 0x01) != 0;
        }
        return false;
    }

    bool skip(int count)
    {
        return count <= 0 || readBytes(count).has_value();
    }

    std::optional<quint8> readUInt8()
    {
        const auto bytes = readBytes(1);
        return bytes.has_value() ? std::optional<quint8>{u8(*bytes, 0)} : std::nullopt;
    }

    std::optional<quint16> readUInt16()
    {
        const auto bytes = readBytes(2);
        return bytes.has_value() ? std::optional<quint16>{u16(*bytes, 0)} : std::nullopt;
    }

    std::optional<quint32> readUInt32()
    {
        const auto bytes = readBytes(4);
        return bytes.has_value() ? std::optional<quint32>{u32(*bytes, 0)} : std::nullopt;
    }

    std::optional<QByteArray> readBytes(int count)
    {
        if (count < 0) {
            return std::nullopt;
        }
        QByteArray result;
        int remaining = count;
        while (remaining > 0) {
            if (segmentIndex_ >= segments_.size()) {
                return std::nullopt;
            }
            const auto& segment = segments_[segmentIndex_];
            const int available = segment.size() - offset_;
            if (available == 0) {
                ++segmentIndex_;
                offset_ = 0;
                continue;
            }
            const int chunkSize = std::min(available, remaining);
            result.append(segment.mid(offset_, chunkSize));
            offset_ += chunkSize;
            remaining -= chunkSize;
        }
        return result;
    }
};

BoundSheet parseBoundSheet(const QByteArray& data)
{
    if (data.size() < 8) {
        return {};
    }
    const int offset = static_cast<int>(u32(data, 0));
    const int nameLength = u8(data, 6);
    const quint8 flags = u8(data, 7);
    const int nameOffset = 8;
    QString name;
    if ((flags & 0x01) == 0) {
        if (nameOffset + nameLength <= data.size()) {
            name = decodeByteString(data.mid(nameOffset, nameLength));
        }
    } else {
        const int byteLength = nameLength * 2;
        if (nameOffset + byteLength <= data.size()) {
            name = decodeWideString(data.mid(nameOffset, byteLength));
        }
    }
    if (name.isEmpty()) {
        name = QStringLiteral("Sheet");
    }
    return {offset, name};
}

std::vector<QString> parseSharedStrings(const QByteArray& recordBody, const std::vector<QByteArray>& continuationChunks)
{
    if (recordBody.size() < 8) {
        return {};
    }
    const int uniqueCount = static_cast<int>(u32(recordBody, 4));
    std::vector<QByteArray> segments;
    segments.push_back(recordBody.mid(8));
    segments.insert(segments.end(), continuationChunks.begin(), continuationChunks.end());
    SharedStringCursor cursor(std::move(segments));

    std::vector<QString> strings;
    while (!cursor.isAtEnd() && static_cast<int>(strings.size()) < uniqueCount) {
        const auto parsed = cursor.readXLUnicodeString();
        if (!parsed.has_value()) {
            break;
        }
        strings.push_back(*parsed);
    }
    return strings;
}

std::optional<std::pair<int, QString>> parseFormat(const QByteArray& data)
{
    if (data.size() < 5) {
        return std::nullopt;
    }
    const int id = u16(data, 0);
    const auto parsed = parseXLUnicodeString(data, 2);
    if (!parsed.has_value()) {
        return std::nullopt;
    }
    return std::make_pair(id, parsed->first);
}

QString defaultFormat(int formatId)
{
    switch (formatId) {
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
    case 14: return QStringLiteral("yyyy-MM-dd");
    case 15: return QStringLiteral("d-mmm-yy");
    case 16: return QStringLiteral("d-mmm");
    case 17: return QStringLiteral("mmm-yy");
    case 18: return QStringLiteral("h:mm AM/PM");
    case 19: return QStringLiteral("h:mm:ss AM/PM");
    case 20: return QStringLiteral("h:mm");
    case 21: return QStringLiteral("h:mm:ss");
    case 22: return QStringLiteral("m/d/yy h:mm");
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

QHash<int, QString> defaultFormats()
{
    QHash<int, QString> formats;
    for (const int id : {0, 1, 2, 3, 4, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 37, 38, 39, 40, 45, 46, 47, 48, 49}) {
        formats.insert(id, defaultFormat(id));
    }
    return formats;
}

WorkbookGlobals parseWorkbookGlobals(const QByteArray& data)
{
    WorkbookGlobals globals;
    globals.formats = defaultFormats();

    const auto records = parseRecords(data);
    size_t index = 0;
    while (index < records.size()) {
        const auto& record = records[index];
        switch (record.id) {
        case 0x0085:
            globals.sheets.push_back(parseBoundSheet(record.body));
            break;
        case 0x00fc: {
            const auto chunks = continuationChunks(records, index);
            globals.sharedStrings = parseSharedStrings(record.body, chunks);
            index += chunks.size();
            break;
        }
        case 0x041e: {
            const auto format = parseFormat(record.body);
            if (format.has_value()) {
                globals.formats.insert(format->first, format->second);
            }
            break;
        }
        case 0x00e0:
            if (record.body.size() >= 4) {
                const int xfIndex = globals.xfFormatIds.size();
                globals.xfFormatIds.insert(xfIndex, u16(record.body, 2));
            }
            break;
        case 0x000a:
            std::sort(globals.sheets.begin(), globals.sheets.end(), [](const BoundSheet& lhs, const BoundSheet& rhs) {
                return lhs.offset < rhs.offset;
            });
            return globals;
        default:
            break;
        }
        ++index;
    }

    std::sort(globals.sheets.begin(), globals.sheets.end(), [](const BoundSheet& lhs, const BoundSheet& rhs) {
        return lhs.offset < rhs.offset;
    });
    return globals;
}

QString formatCode(int xfIndex, const WorkbookGlobals& globals)
{
    if (!globals.xfFormatIds.contains(xfIndex)) {
        return {};
    }
    return globals.formats.value(globals.xfFormatIds.value(xfIndex));
}

bool isDateFormat(QString formatCode)
{
    formatCode = formatCode.toLower();
    return !formatCode.isEmpty()
        && !formatCode.contains(QStringLiteral("general"))
        && !formatCode.contains(QLatin1Char('@'))
        && formatCode.contains(QRegularExpression(QStringLiteral("[ymdhs]")));
}

QString formatExcelDate(double number)
{
    const QDate epoch(1899, 12, 28);
    return epoch.addDays(static_cast<qint64>(std::floor(number))).toString(QStringLiteral("yyyy-MM-dd"));
}

QString numberDisplayValue(double number)
{
    if (std::isfinite(number) && std::round(number) == number) {
        return QString::number(static_cast<qint64>(number));
    }
    return QString::number(number, 'g', 15);
}

CellData numericCell(double number, int xfIndex, const WorkbookGlobals& globals)
{
    const auto code = formatCode(xfIndex, globals);
    if (isDateFormat(code)) {
        const auto display = formatExcelDate(number);
        return CellData(display, numberDisplayValue(number), number, code.isEmpty() ? std::nullopt : std::optional<QString>{code}, true);
    }
    const auto display = numberDisplayValue(number);
    return CellData(display, display, number, code.isEmpty() ? std::nullopt : std::optional<QString>{code}, false);
}

double decodeRK(quint32 rk)
{
    const bool divideBy100 = (rk & 0x01) != 0;
    const bool isInteger = (rk & 0x02) != 0;
    double value = 0;
    if (isInteger) {
        value = static_cast<double>(static_cast<qint32>(rk & 0xfffffffc) >> 2);
    } else {
        const quint64 bits = static_cast<quint64>(rk & 0xfffffffc) << 32;
        value = std::bit_cast<double>(bits);
    }
    return divideBy100 ? value / 100.0 : value;
}

QString errorValue(quint8 code)
{
    switch (code) {
    case 0x00: return QStringLiteral("#NULL!");
    case 0x07: return QStringLiteral("#DIV/0!");
    case 0x0f: return QStringLiteral("#VALUE!");
    case 0x17: return QStringLiteral("#REF!");
    case 0x1d: return QStringLiteral("#NAME?");
    case 0x24: return QStringLiteral("#NUM!");
    case 0x2a: return QStringLiteral("#N/A");
    default: return QStringLiteral("#ERROR");
    }
}

std::pair<int, int> parseDimensions(const QByteArray& data)
{
    if (data.size() >= 14) {
        return {std::max(-1, static_cast<int>(u32(data, 4)) - 1), std::max(-1, static_cast<int>(u16(data, 10)) - 1)};
    }
    if (data.size() >= 10) {
        return {std::max(-1, static_cast<int>(u16(data, 2)) - 1), std::max(-1, static_cast<int>(u16(data, 6)) - 1)};
    }
    return {-1, -1};
}

std::vector<std::tuple<int, int, CellData>> parseMulRK(const QByteArray& data, const WorkbookGlobals& globals)
{
    if (data.size() < 6) {
        return {};
    }
    const int row = u16(data, 0);
    const int firstColumn = u16(data, 2);
    const int lastColumn = u16(data, data.size() - 2);
    const int count = std::max(0, lastColumn - firstColumn + 1);
    std::vector<std::tuple<int, int, CellData>> entries;
    for (int index = 0; index < count; ++index) {
        const int offset = 4 + index * 6;
        if (offset + 6 > data.size() - 2) {
            break;
        }
        entries.emplace_back(row, firstColumn + index, numericCell(decodeRK(u32(data, offset + 2)), u16(data, offset), globals));
    }
    return entries;
}

std::vector<CellRange> parseMergedCells(const QByteArray& data)
{
    if (data.size() < 2) {
        return {};
    }
    const int count = u16(data, 0);
    std::vector<CellRange> ranges;
    ranges.reserve(static_cast<size_t>(count));
    for (int index = 0; index < count; ++index) {
        const int offset = 2 + index * 8;
        if (offset + 8 > data.size()) {
            break;
        }
        const int firstRow = u16(data, offset);
        const int lastRow = u16(data, offset + 2);
        const int firstColumn = u16(data, offset + 4);
        const int lastColumn = u16(data, offset + 6);
        ranges.push_back({
            {std::min(firstRow, lastRow), std::min(firstColumn, lastColumn)},
            {std::max(firstRow, lastRow), std::max(firstColumn, lastColumn)}
        });
    }
    return ranges;
}

std::optional<std::tuple<int, int, CellData>> parseFormula(const QByteArray& data, const WorkbookGlobals& globals)
{
    if (data.size() < 14) {
        return std::nullopt;
    }
    const int row = u16(data, 0);
    const int column = u16(data, 2);
    const int xfIndex = u16(data, 4);
    const quint16 resultMarker = u16(data, 12);
    const auto code = formatCode(xfIndex, globals);
    const std::optional<QString> codeOption = code.isEmpty() ? std::nullopt : std::optional<QString>{code};

    if (resultMarker == 0xffff) {
        const quint8 type = u8(data, 6);
        QString value;
        switch (type) {
        case 0:
            value.clear();
            break;
        case 1:
            value = u8(data, 8) == 0 ? QStringLiteral("FALSE") : QStringLiteral("TRUE");
            break;
        case 2:
            value = errorValue(u8(data, 8));
            break;
        default:
            return std::nullopt;
        }
        return std::make_tuple(row, column, CellData(value, std::nullopt, std::nullopt, codeOption));
    }

    return std::make_tuple(row, column, numericCell(f64(data, 6), xfIndex, globals));
}

void setCell(
    const CellData& cell,
    int row,
    int column,
    std::map<std::pair<int, int>, CellData>& cells,
    int& maxRow,
    int& maxColumn
)
{
    cells[{row, column}] = cell;
    maxRow = std::max(maxRow, row);
    maxColumn = std::max(maxColumn, column);
}

bool isBlankCell(const CellData& cell)
{
    return cell.value.trimmed().isEmpty()
        && !cell.rawValue.has_value()
        && !cell.numericValue.has_value();
}

void applyMergedRanges(
    std::map<std::pair<int, int>, CellData>& cells,
    const std::vector<CellRange>& ranges,
    int& maxRow,
    int& maxColumn
)
{
    for (const auto& range : ranges) {
        const auto sourceIterator = cells.find({range.first.row, range.first.column});
        const CellData source = sourceIterator == cells.end() ? CellData(QString()) : sourceIterator->second;
        maxRow = std::max(maxRow, range.last.row);
        maxColumn = std::max(maxColumn, range.last.column);
        for (int row = range.first.row; row <= range.last.row; ++row) {
            for (int column = range.first.column; column <= range.last.column; ++column) {
                const auto key = std::make_pair(row, column);
                const auto target = cells.find(key);
                if (target == cells.end() || isBlankCell(target->second)) {
                    cells[key] = source;
                }
            }
        }
    }
}

std::vector<std::vector<CellData>> buildRows(const std::map<std::pair<int, int>, CellData>& cells, int maxRow, int maxColumn)
{
    std::vector<std::vector<CellData>> rows;
    if (maxRow < 0 || maxColumn < 0) {
        return rows;
    }
    for (int row = 0; row <= maxRow; ++row) {
        std::vector<CellData> rowData;
        rowData.reserve(static_cast<size_t>(maxColumn + 1));
        for (int column = 0; column <= maxColumn; ++column) {
            const auto iterator = cells.find({row, column});
            rowData.push_back(iterator == cells.end() ? CellData(QString()) : iterator->second);
        }
        rows.push_back(std::move(rowData));
    }
    return rows;
}

SheetData parseWorksheet(const QByteArray& workbookData, const BoundSheet& sheet, const WorkbookGlobals& globals)
{
    std::map<std::pair<int, int>, CellData> cells;
    int maxRow = -1;
    int maxColumn = -1;

    auto finalize = [&]() {
        return SheetData{sheet.name, buildRows(cells, maxRow, maxColumn)};
    };

    for (const auto& record : parseRecords(workbookData, sheet.offset)) {
        switch (record.id) {
        case 0x000a:
            return finalize();
        case 0x0200: {
            const auto [row, column] = parseDimensions(record.body);
            maxRow = std::max(maxRow, row);
            maxColumn = std::max(maxColumn, column);
            break;
        }
        case 0x00fd:
            if (record.body.size() >= 10) {
                const int row = u16(record.body, 0);
                const int column = u16(record.body, 2);
                const int xfIndex = u16(record.body, 4);
                const int stringIndex = static_cast<int>(u32(record.body, 6));
                const auto value = stringIndex >= 0 && stringIndex < static_cast<int>(globals.sharedStrings.size())
                    ? globals.sharedStrings[static_cast<size_t>(stringIndex)]
                    : QString();
                const auto code = formatCode(xfIndex, globals);
                setCell(CellData(value, std::nullopt, std::nullopt, code.isEmpty() ? std::nullopt : std::optional<QString>{code}), row, column, cells, maxRow, maxColumn);
            }
            break;
        case 0x0204:
        case 0x00d6:
            if (record.body.size() >= 8) {
                const int row = u16(record.body, 0);
                const int column = u16(record.body, 2);
                const int xfIndex = u16(record.body, 4);
                const auto parsed = parseXLUnicodeString(record.body, 6);
                const auto code = formatCode(xfIndex, globals);
                setCell(CellData(parsed.has_value() ? parsed->first : QString(), std::nullopt, std::nullopt, code.isEmpty() ? std::nullopt : std::optional<QString>{code}), row, column, cells, maxRow, maxColumn);
            }
            break;
        case 0x0203:
            if (record.body.size() >= 14) {
                setCell(numericCell(f64(record.body, 6), u16(record.body, 4), globals), u16(record.body, 0), u16(record.body, 2), cells, maxRow, maxColumn);
            }
            break;
        case 0x027e:
            if (record.body.size() >= 10) {
                setCell(numericCell(decodeRK(u32(record.body, 6)), u16(record.body, 4), globals), u16(record.body, 0), u16(record.body, 2), cells, maxRow, maxColumn);
            }
            break;
        case 0x00bd:
            for (const auto& [row, column, cell] : parseMulRK(record.body, globals)) {
                setCell(cell, row, column, cells, maxRow, maxColumn);
            }
            break;
        case 0x0205:
            if (record.body.size() >= 8) {
                const int xfIndex = u16(record.body, 4);
                const bool isError = u8(record.body, 7) != 0;
                const auto value = isError ? errorValue(u8(record.body, 6)) : (u8(record.body, 6) == 0 ? QStringLiteral("FALSE") : QStringLiteral("TRUE"));
                const auto code = formatCode(xfIndex, globals);
                setCell(CellData(value, std::nullopt, std::nullopt, code.isEmpty() ? std::nullopt : std::optional<QString>{code}), u16(record.body, 0), u16(record.body, 2), cells, maxRow, maxColumn);
            }
            break;
        case 0x0006: {
            const auto parsed = parseFormula(record.body, globals);
            if (parsed.has_value()) {
                const auto& [row, column, cell] = *parsed;
                setCell(cell, row, column, cells, maxRow, maxColumn);
            }
            break;
        }
        case 0x00e5: {
            break;
        }
        default:
            break;
        }
    }

    return finalize();
}

} // namespace

ExcelFile parseBIFF8XLSFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        throw std::runtime_error(QStringLiteral("无法打开 .xls 文件: %1").arg(file.errorString()).toStdString());
    }

    const CompoundFileReader compoundFile(file.readAll());
    const QByteArray workbookData = compoundFile.stream({QStringLiteral("Workbook"), QStringLiteral("Book")});
    const auto globals = parseWorkbookGlobals(workbookData);

    std::vector<SheetData> sheets;
    for (const auto& sheet : globals.sheets) {
        if (sheet.offset >= 0 && sheet.offset < workbookData.size()) {
            sheets.push_back(parseWorksheet(workbookData, sheet, globals));
        }
    }
    if (sheets.empty()) {
        throw std::runtime_error("无法从 .xls 文件中解析工作表");
    }

    QFileInfo info(path);
    return {info.fileName(), path, sheets};
}

} // namespace xlsone
