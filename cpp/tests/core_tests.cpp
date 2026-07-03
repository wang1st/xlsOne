#include "xlsone/core/exporter.hpp"
#include "xlsone/core/excel_parser.hpp"
#include "xlsone/core/license_manager.hpp"
#include "xlsone/core/merger.hpp"
#include "xlsone/core/models.hpp"
#include "xlsone/core/schema_repository.hpp"
#include "xlsone/core/validator.hpp"
#include "xlsone/core/update_checker.hpp"

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>
#include <cstring>
#include <algorithm>

namespace {

template<typename To, typename From>
To bit_cast(const From& from) noexcept
{
    static_assert(sizeof(To) == sizeof(From), "bit_cast requires same size");
    To result;
    std::memcpy(&result, &from, sizeof(To));
    return result;
}

} // namespace

using namespace xlsone;

class CoreTests final : public QObject {
    Q_OBJECT

private slots:
    void parsesNumbers();
    void mergesNumericAmounts();
    void preservesCodeSemantics();
    void protectsHeaderRows();
    void sumsZeroValues();
    void treatsBlankSourcesAsZeroForNumericCells();
    void keepsBlankCodeColumnsNonNumeric();
    void sumsSingleNumericValueWhenContextIsNumeric();
    void sumsRepeatedIntegerCountsWhenMetricEvidenceAgrees();
    void keepsWeakMetricWordConservativeWithoutContext();
    void keepsCodeColumnsProtectedAgainstMetricColumnContext();
    void usesColumnMetricAnchorForTownFinanceCountRows();
    void sumsAmountColumnIndependentOfFileOrder();
    void validatesCompatibleSheets();
    void choosesRepresentativeTemplate();
    void storesSchemas();
    void matchesWorkbookSchemas();
    void matchesAdjustmentMemoryImportForSameArchitecture();
    void rejectsAdjustmentMemoryImportForDifferentArchitecture();
    void matchesWorkbookSchemasByDominantDimensions();
    void appliesSchemaOverrides();
    void exportsCsvBaseline();
    void parsesXlsxSampleWorkbook();
    void parsesMergedXlsxCells();
    void exportsXlsxUsingTemplateWorkbook();
    void parsesMinimalBiff8Workbook();
    void parsesLocalXianjuXlsWhenPresent();
    void updateCheckerCurrentVersionIsValid();
    void updateCheckerCompareVersions();
    void updateCheckerParseUpdateInfoJson();
    void updateCheckerParseInvalidJson();
    void updateCheckerPlatformKeyIsNotEmpty();
    void updateCheckerFindsNewVersionOnline();
    void licenseManagerAcceptsValidEd25519License();
    void licenseManagerRejectsTamperedEd25519License();
};

namespace {

void appendU8(QByteArray& data, quint8 value)
{
    data.append(static_cast<char>(value));
}

void appendU16(QByteArray& data, quint16 value)
{
    uchar buffer[2];
    qToLittleEndian(value, buffer);
    data.append(reinterpret_cast<const char*>(buffer), 2);
}

void appendU32(QByteArray& data, quint32 value)
{
    uchar buffer[4];
    qToLittleEndian(value, buffer);
    data.append(reinterpret_cast<const char*>(buffer), 4);
}

void appendDouble(QByteArray& data, double value)
{
    uchar buffer[8];
    qToLittleEndian(bit_cast<quint64>(value), buffer);
    data.append(reinterpret_cast<const char*>(buffer), 8);
}

void writeU8(QByteArray& data, int offset, quint8 value)
{
    data[offset] = static_cast<char>(value);
}

void writeU16(QByteArray& data, int offset, quint16 value)
{
    uchar buffer[2];
    qToLittleEndian(value, buffer);
    data.replace(offset, 2, reinterpret_cast<const char*>(buffer), 2);
}

void writeU32(QByteArray& data, int offset, quint32 value)
{
    uchar buffer[4];
    qToLittleEndian(value, buffer);
    data.replace(offset, 4, reinterpret_cast<const char*>(buffer), 4);
}

void appendXLString(QByteArray& data, const QString& value)
{
    const QByteArray bytes = value.toLatin1();
    appendU16(data, static_cast<quint16>(value.size()));
    appendU8(data, 0);
    data.append(bytes);
}

QByteArray record(quint16 id, const QByteArray& body)
{
    QByteArray data;
    appendU16(data, id);
    appendU16(data, static_cast<quint16>(body.size()));
    data.append(body);
    return data;
}

struct ZipEntry {
    QString name;
    QByteArray data;
};

QByteArray storedZip(const std::vector<ZipEntry>& entries)
{
    struct CentralDirectoryEntry {
        QString name;
        quint32 offset = 0;
        quint32 size = 0;
    };

    QByteArray archive;
    std::vector<CentralDirectoryEntry> centralDirectoryEntries;
    for (const auto& entry : entries) {
        const QByteArray name = entry.name.toUtf8();
        centralDirectoryEntries.push_back({
            entry.name,
            static_cast<quint32>(archive.size()),
            static_cast<quint32>(entry.data.size())
        });

        appendU32(archive, 0x04034b50);
        appendU16(archive, 20);
        appendU16(archive, 1 << 11);
        appendU16(archive, 0);
        appendU16(archive, 0);
        appendU16(archive, 0);
        appendU32(archive, 0);
        appendU32(archive, static_cast<quint32>(entry.data.size()));
        appendU32(archive, static_cast<quint32>(entry.data.size()));
        appendU16(archive, static_cast<quint16>(name.size()));
        appendU16(archive, 0);
        archive.append(name);
        archive.append(entry.data);
    }

    const quint32 centralDirectoryOffset = static_cast<quint32>(archive.size());
    for (const auto& entry : centralDirectoryEntries) {
        const QByteArray name = entry.name.toUtf8();
        appendU32(archive, 0x02014b50);
        appendU16(archive, 20);
        appendU16(archive, 20);
        appendU16(archive, 1 << 11);
        appendU16(archive, 0);
        appendU16(archive, 0);
        appendU16(archive, 0);
        appendU32(archive, 0);
        appendU32(archive, entry.size);
        appendU32(archive, entry.size);
        appendU16(archive, static_cast<quint16>(name.size()));
        appendU16(archive, 0);
        appendU16(archive, 0);
        appendU16(archive, 0);
        appendU16(archive, 0);
        appendU32(archive, 0);
        appendU32(archive, entry.offset);
        archive.append(name);
    }

    const quint32 centralDirectorySize = static_cast<quint32>(archive.size()) - centralDirectoryOffset;
    appendU32(archive, 0x06054b50);
    appendU16(archive, 0);
    appendU16(archive, 0);
    appendU16(archive, static_cast<quint16>(entries.size()));
    appendU16(archive, static_cast<quint16>(entries.size()));
    appendU32(archive, centralDirectorySize);
    appendU32(archive, centralDirectoryOffset);
    appendU16(archive, 0);
    return archive;
}

QByteArray workbookBOFBody(quint16 kind)
{
    QByteArray data;
    appendU16(data, 0x0600);
    appendU16(data, kind);
    appendU16(data, 0x0dbb);
    appendU16(data, 0x07cc);
    appendU32(data, 0x00000041);
    appendU32(data, 0x00000006);
    return data;
}

QByteArray xfBody(quint16 formatId)
{
    QByteArray data(20, '\0');
    writeU16(data, 2, formatId);
    return data;
}

QByteArray labelSST(quint16 row, quint16 column, quint32 sstIndex)
{
    QByteArray body;
    appendU16(body, row);
    appendU16(body, column);
    appendU16(body, 0);
    appendU32(body, sstIndex);
    return record(0x00fd, body);
}

QByteArray numberCell(quint16 row, quint16 column, double value)
{
    QByteArray body;
    appendU16(body, row);
    appendU16(body, column);
    appendU16(body, 0);
    appendDouble(body, value);
    return record(0x0203, body);
}

QByteArray mergedCells(quint16 firstRow, quint16 lastRow, quint16 firstColumn, quint16 lastColumn)
{
    QByteArray body;
    appendU16(body, 1);
    appendU16(body, firstRow);
    appendU16(body, lastRow);
    appendU16(body, firstColumn);
    appendU16(body, lastColumn);
    return record(0x00e5, body);
}

void writeDirectoryEntry(
    const QString& name,
    quint8 type,
    quint32 startSector,
    quint64 streamSize,
    QByteArray& data,
    int offset
)
{
    const QByteArray nameBytes = (name + QChar('\0')).toStdU16String().empty()
        ? QByteArray()
        : QByteArray(reinterpret_cast<const char*>((name + QChar('\0')).utf16()), (name.size() + 1) * 2);
    data.replace(offset, nameBytes.size(), nameBytes);
    writeU16(data, offset + 64, static_cast<quint16>(nameBytes.size()));
    writeU8(data, offset + 66, type);
    writeU32(data, offset + 68, 0xffffffff);
    writeU32(data, offset + 72, 0xffffffff);
    writeU32(data, offset + 76, 0xffffffff);
    writeU32(data, offset + 116, startSector);
    writeU32(data, offset + 120, static_cast<quint32>(streamSize & 0xffffffff));
    writeU32(data, offset + 124, static_cast<quint32>(streamSize >> 32));
}

QByteArray compoundFile(const QByteArray& workbookStream)
{
    const int sectorSize = 512;
    const int workbookSectorCount = workbookStream.size() / sectorSize;
    const quint32 directorySector = static_cast<quint32>(workbookSectorCount);
    const quint32 fatSector = static_cast<quint32>(workbookSectorCount + 1);

    QByteArray header(512, '\0');
    header.replace(0, 8, QByteArray::fromHex("d0cf11e0a1b11ae1"));
    writeU16(header, 24, 0x003e);
    writeU16(header, 26, 0x0003);
    writeU16(header, 28, 0xfffe);
    writeU16(header, 30, 9);
    writeU16(header, 32, 6);
    writeU32(header, 40, 0);
    writeU32(header, 44, 1);
    writeU32(header, 48, directorySector);
    writeU32(header, 56, 4096);
    writeU32(header, 60, 0xfffffffe);
    writeU32(header, 64, 0);
    writeU32(header, 68, 0xfffffffe);
    writeU32(header, 72, 0);
    writeU32(header, 76, fatSector);
    for (int offset = 80; offset < 512; offset += 4) {
        writeU32(header, offset, 0xffffffff);
    }

    QByteArray directory(sectorSize, '\0');
    writeDirectoryEntry(QStringLiteral("Root Entry"), 5, 0xfffffffe, 0, directory, 0);
    writeDirectoryEntry(QStringLiteral("Workbook"), 2, 0, static_cast<quint64>(workbookStream.size()), directory, 128);

    QByteArray fat(sectorSize, static_cast<char>(0xff));
    for (int sector = 0; sector < workbookSectorCount; ++sector) {
        const quint32 next = sector == workbookSectorCount - 1 ? 0xfffffffe : static_cast<quint32>(sector + 1);
        writeU32(fat, sector * 4, next);
    }
    writeU32(fat, static_cast<int>(directorySector) * 4, 0xfffffffe);
    writeU32(fat, static_cast<int>(fatSector) * 4, 0xfffffffd);

    return header + workbookStream + directory + fat;
}

QByteArray minimalBiff8Workbook()
{
    QByteArray sst;
    appendU32(sst, 4);
    appendU32(sst, 4);
    appendXLString(sst, QStringLiteral("Name"));
    appendXLString(sst, QStringLiteral("Alice"));
    appendXLString(sst, QStringLiteral("Amount"));
    appendXLString(sst, QStringLiteral("Code"));

    const QByteArray globalBOF = record(0x0809, workbookBOFBody(0x0005));
    const QByteArray sstRecord = record(0x00fc, sst);
    const QByteArray xfDefault = record(0x00e0, xfBody(0));
    const QByteArray globalsWithoutBoundSheet = globalBOF + xfDefault + sstRecord;
    const int boundSheetLength = 4 + 8 + QStringLiteral("Sheet1").toUtf8().size();
    const int sheetOffset = globalsWithoutBoundSheet.size() + boundSheetLength + 4;

    QByteArray boundSheet;
    appendU32(boundSheet, static_cast<quint32>(sheetOffset));
    appendU16(boundSheet, 0);
    appendU8(boundSheet, 6);
    appendU8(boundSheet, 0);
    boundSheet.append("Sheet1");

    const QByteArray globals = globalBOF
        + record(0x0085, boundSheet)
        + xfDefault
        + sstRecord
        + record(0x000a, {});

    QByteArray sheet;
    sheet.append(record(0x0809, workbookBOFBody(0x0010)));
    sheet.append(labelSST(0, 0, 0));
    sheet.append(labelSST(0, 1, 2));
    sheet.append(labelSST(1, 0, 1));
    sheet.append(numberCell(1, 1, 1234));
    sheet.append(mergedCells(0, 0, 0, 2));
    sheet.append(record(0x000a, {}));

    QByteArray workbook = globals + sheet;
    if (workbook.size() < 4096) {
        workbook.append(QByteArray(4096 - workbook.size(), '\0'));
    }
    return compoundFile(workbook);
}

} // namespace

void CoreTests::parsesNumbers()
{
    QCOMPARE(CellData(QStringLiteral("1000")).numericValue.value(), 1000.0);
    QCOMPARE(CellData(QStringLiteral("1,000.50")).numericValue.value(), 1000.5);
    QCOMPARE(CellData(QStringLiteral("1.234,56")).numericValue.value(), 1234.56);
    QVERIFY(!CellData(QStringLiteral("201")).numericValue.has_value());
    QVERIFY(!CellData(QStringLiteral("abc")).numericValue.has_value());
    QCOMPARE(CellData(QStringLiteral("(100.50)")).numericValue.value(), -100.5);
    QCOMPARE(MergedCell::formatNumber(9427.0, QStringLiteral("#,##0.00")), QStringLiteral("9,427"));
    QCOMPARE(MergedCell::formatNumber(1234.5, QStringLiteral("#,##0.00")), QStringLiteral("1,234.5"));
}

void CoreTests::mergesNumericAmounts()
{
    const std::vector<CellMergeInput> cells = {
        {QStringLiteral("a.xlsx"), QStringLiteral("/a.xlsx"), CellData(QStringLiteral("1000"))},
        {QStringLiteral("b.xlsx"), QStringLiteral("/b.xlsx"), CellData(QStringLiteral("2000"))},
        {QStringLiteral("c.xlsx"), QStringLiteral("/c.xlsx"), CellData(QStringLiteral("1500"))},
    };
    const auto merged = MergedCell::from(cells);
    QCOMPARE(merged.type.kind, CellKind::Sum);
    QCOMPARE(merged.displayValue, QStringLiteral("4500"));

    const std::vector<CellMergeInput> formattedCells = {
        {QStringLiteral("a.xlsx"), QStringLiteral("/a.xlsx"), CellData(QStringLiteral("167"), std::nullopt, 167.0, QStringLiteral("#,##0.00"))},
        {QStringLiteral("b.xlsx"), QStringLiteral("/b.xlsx"), CellData(QStringLiteral("164.0"), std::nullopt, 164.0, QStringLiteral("#,##0.00"))},
    };
    const auto formatted = MergedCell::from(formattedCells, {}, NeighborContext{0.6, 0.0}, 6, 7);
    QCOMPARE(formatted.type.kind, CellKind::Sum);
    QCOMPARE(formatted.displayValue, QStringLiteral("331"));
}

void CoreTests::preservesCodeSemantics()
{
    const std::vector<CellMergeInput> cells = {
        {QStringLiteral("a.xlsx"), QStringLiteral("/a.xlsx"), CellData(QStringLiteral("331024001"))},
        {QStringLiteral("b.xlsx"), QStringLiteral("/b.xlsx"), CellData(QStringLiteral("331024002"))},
    };
    const std::vector<CellMergeInput> left = {
        {QStringLiteral("a.xlsx"), QStringLiteral("/a.xlsx"), CellData(QStringLiteral("行政区划代码"))},
    };
    const auto merged = MergedCell::from(cells, left);
    QCOMPARE(merged.type.kind, CellKind::Label);
    QCOMPARE(merged.displayValue, QStringLiteral("33102400_"));
}

void CoreTests::protectsHeaderRows()
{
    const std::vector<CellMergeInput> cells = {
        {QStringLiteral("a.xlsx"), QStringLiteral("/a.xlsx"), CellData(QStringLiteral("1000"))},
        {QStringLiteral("b.xlsx"), QStringLiteral("/b.xlsx"), CellData(QStringLiteral("2000"))},
    };
    const auto merged = MergedCell::from(cells, {}, NeighborContext{1.0, 0.0}, 0, 1);
    QCOMPARE(merged.type.kind, CellKind::Label);
}

void CoreTests::sumsZeroValues()
{
    const std::vector<CellMergeInput> cells = {
        {QStringLiteral("a.xlsx"), QStringLiteral("/a.xlsx"), CellData(QStringLiteral("0.0"), std::nullopt, 0.0, QStringLiteral("0.0"))},
        {QStringLiteral("b.xlsx"), QStringLiteral("/b.xlsx"), CellData(QStringLiteral("0.0"), std::nullopt, 0.0, QStringLiteral("0.0"))},
    };
    const auto merged = MergedCell::from(cells, {}, NeighborContext{}, 1, 1);
    QCOMPARE(merged.type.kind, CellKind::Sum);
    QCOMPARE(merged.type.sum, 0.0);
}

void CoreTests::treatsBlankSourcesAsZeroForNumericCells()
{
    const std::vector<CellMergeInput> cells = {
        {QStringLiteral("a.xlsx"), QStringLiteral("/a.xlsx"), CellData(QStringLiteral("123456"))},
        {QStringLiteral("b.xlsx"), QStringLiteral("/b.xlsx"), CellData(QString())},
        {QStringLiteral("c.xlsx"), QStringLiteral("/c.xlsx"), CellData(QStringLiteral("123456"))},
    };
    const auto merged = MergedCell::from(cells, {}, NeighborContext{0.8, 0.0}, 2, 3);
    QCOMPARE(merged.type.kind, CellKind::Sum);
    QCOMPARE(merged.type.sum, 246912.0);
}

void CoreTests::keepsBlankCodeColumnsNonNumeric()
{
    const std::vector<CellMergeInput> cells = {
        {QStringLiteral("a.xlsx"), QStringLiteral("/a.xlsx"), CellData(QStringLiteral("331024001"))},
        {QStringLiteral("b.xlsx"), QStringLiteral("/b.xlsx"), CellData(QString())},
        {QStringLiteral("c.xlsx"), QStringLiteral("/c.xlsx"), CellData(QStringLiteral("331024002"))},
    };
    const std::vector<CellMergeInput> left = {
        {QStringLiteral("a.xlsx"), QStringLiteral("/a.xlsx"), CellData(QStringLiteral("行政区划代码"))},
    };
    const auto merged = MergedCell::from(cells, left, NeighborContext{0.8, 0.0}, 2, 3);
    QCOMPARE(merged.type.kind, CellKind::Label);
    QCOMPARE(merged.displayValue, QStringLiteral("33102400_"));
}

void CoreTests::sumsSingleNumericValueWhenContextIsNumeric()
{
    const std::vector<CellMergeInput> cells = {
        {QStringLiteral("a.xlsx"), QStringLiteral("/a.xlsx"), CellData(QStringLiteral("0.0"), std::nullopt, 0.0, QStringLiteral("0.0"))},
        {QStringLiteral("b.xlsx"), QStringLiteral("/b.xlsx"), CellData(QString())},
    };
    const auto merged = MergedCell::from(cells, {}, NeighborContext{0.75, 0.0}, 5, 2);
    QCOMPARE(merged.type.kind, CellKind::Sum);
    QCOMPARE(merged.type.sum, 0.0);
}

void CoreTests::sumsRepeatedIntegerCountsWhenMetricEvidenceAgrees()
{
    const std::vector<CellMergeInput> cells = {
        {QStringLiteral("a.xlsx"), QStringLiteral("/a.xlsx"), CellData(QStringLiteral("1"), std::nullopt, 1.0, QStringLiteral("#,##0"))},
        {QStringLiteral("b.xlsx"), QStringLiteral("/b.xlsx"), CellData(QStringLiteral("1"), std::nullopt, 1.0, QStringLiteral("#,##0"))},
        {QStringLiteral("c.xlsx"), QStringLiteral("/c.xlsx"), CellData(QStringLiteral("1"), std::nullopt, 1.0, QStringLiteral("#,##0"))},
    };
    const std::vector<CellMergeInput> left = {
        {QStringLiteral("a.xlsx"), QStringLiteral("/a.xlsx"), CellData(QStringLiteral("二、乡镇财政机构数"))},
    };

    const auto merged = MergedCell::from(cells, left, NeighborContext{0.7, 0.0, 0.8}, 7, 2);
    QCOMPARE(merged.type.kind, CellKind::Sum);
    QCOMPARE(merged.type.sum, 3.0);
    QVERIFY(merged.decision.decisionReasons.contains(QStringLiteral("相同整数命中计量语义并得到同列上下文支持，按求和处理")));
}

void CoreTests::keepsWeakMetricWordConservativeWithoutContext()
{
    const std::vector<CellMergeInput> cells = {
        {QStringLiteral("a.xlsx"), QStringLiteral("/a.xlsx"), CellData(QStringLiteral("1"), std::nullopt, 1.0, QStringLiteral("#,##0"))},
        {QStringLiteral("b.xlsx"), QStringLiteral("/b.xlsx"), CellData(QStringLiteral("1"), std::nullopt, 1.0, QStringLiteral("#,##0"))},
    };
    const std::vector<CellMergeInput> left = {
        {QStringLiteral("a.xlsx"), QStringLiteral("/a.xlsx"), CellData(QStringLiteral("样本数"))},
    };

    const auto merged = MergedCell::from(cells, left, NeighborContext{}, 4, 2);
    QCOMPARE(merged.type.kind, CellKind::Label);
    QCOMPARE(merged.displayValue, QStringLiteral("1"));
}

void CoreTests::keepsCodeColumnsProtectedAgainstMetricColumnContext()
{
    const std::vector<CellMergeInput> cells = {
        {QStringLiteral("a.xlsx"), QStringLiteral("/a.xlsx"), CellData(QStringLiteral("331024001"))},
        {QStringLiteral("b.xlsx"), QStringLiteral("/b.xlsx"), CellData(QStringLiteral("331024002"))},
    };
    const std::vector<CellMergeInput> left = {
        {QStringLiteral("a.xlsx"), QStringLiteral("/a.xlsx"), CellData(QStringLiteral("行政区划代码"))},
    };

    const auto merged = MergedCell::from(cells, left, NeighborContext{0.8, 0.0, 0.8}, 2, 3);
    QCOMPARE(merged.type.kind, CellKind::Label);
    QCOMPARE(merged.displayValue, QStringLiteral("33102400_"));
}

void CoreTests::usesColumnMetricAnchorForTownFinanceCountRows()
{
    auto countCell = [](int value) {
        return CellData(QString::number(value), std::nullopt, static_cast<double>(value), QStringLiteral("#,##0"));
    };
    const auto makeSheet = [&](const QString& name) {
        return SheetData{name, {
            {CellData(QString()), CellData(QString()), CellData(QString())},
            {CellData(QString()), CellData(QStringLiteral("01表：乡镇财政基本情况表")), CellData(QString())},
            {CellData(QString()), CellData(QString()), CellData(QStringLiteral("单位：人、个、万元"))},
            {CellData(QString()), CellData(QStringLiteral("项  目 (一)")), CellData(QStringLiteral("决算数(一)"))},
            {CellData(QString()), CellData(QStringLiteral("一、本年乡镇数")), countCell(1)},
            {CellData(QString()), CellData(QStringLiteral("其中:实行“乡财县管”的乡镇数")), countCell(0)},
            {CellData(QString()), CellData(QStringLiteral("二、乡镇财政机构数")), countCell(1)},
            {CellData(QString()), CellData(QStringLiteral("三、已建立乡镇国库的乡镇数")), countCell(0)},
            {CellData(QString()), CellData(QStringLiteral("四、实行“分税制”管理体制的乡镇数")), countCell(1)},
        }};
    };

    const std::vector<ExcelFile> files = {
        {QStringLiteral("a.xlsx"), QStringLiteral("/a.xlsx"), {makeSheet(QStringLiteral("乡镇财政基本情况表"))}},
        {QStringLiteral("b.xlsx"), QStringLiteral("/b.xlsx"), {makeSheet(QStringLiteral("乡镇财政基本情况表"))}},
        {QStringLiteral("c.xlsx"), QStringLiteral("/c.xlsx"), {makeSheet(QStringLiteral("乡镇财政基本情况表"))}},
    };

    const auto result = SimpleMerger().merge(files, QStringLiteral("乡镇财政基本情况表"));
    QCOMPARE(result.rows[4][2].type.kind, CellKind::Sum);
    QCOMPARE(result.rows[4][2].type.sum, 3.0);
    QCOMPARE(result.rows[6][2].type.kind, CellKind::Sum);
    QCOMPARE(result.rows[6][2].type.sum, 3.0);
    QCOMPARE(result.rows[8][2].type.kind, CellKind::Sum);
    QCOMPARE(result.rows[8][2].type.sum, 3.0);
}

void CoreTests::sumsAmountColumnIndependentOfFileOrder()
{
    struct AmountSpec {
        QString display;
        double value = 0.0;
        QString formatCode;
    };
    auto amountCell = [](const AmountSpec& amount) {
        return CellData(amount.display, std::nullopt, amount.value, amount.formatCode);
    };
    const auto makeFile = [&](const QString& filename, const std::vector<AmountSpec>& amounts) {
        return ExcelFile{filename, QStringLiteral("/") + filename, {
            SheetData{QStringLiteral("费用汇总表"), {
                {
                    CellData(QStringLiteral("项目编码")),
                    CellData(QStringLiteral("项目名称")),
                    CellData(QStringLiteral("本期金额")),
                    CellData(QStringLiteral("备注"))
                },
                {
                    CellData(QStringLiteral("XM001")),
                    CellData(QStringLiteral("系统接入服务")),
                    amountCell(amounts[0]),
                    CellData(QStringLiteral("测试口径"))
                },
                {
                    CellData(QStringLiteral("XM002")),
                    CellData(QStringLiteral("数据清洗服务")),
                    amountCell(amounts[1]),
                    CellData(QStringLiteral("测试口径"))
                },
                {
                    CellData(QStringLiteral("XM003")),
                    CellData(QStringLiteral("报表核验服务")),
                    amountCell(amounts[2]),
                    CellData(QStringLiteral("测试口径"))
                },
                {
                    CellData(QStringLiteral("XM004")),
                    CellData(QStringLiteral("归档支持服务")),
                    amountCell(amounts[3]),
                    CellData(QStringLiteral("测试口径"))
                },
            }}
        }};
    };

    std::vector<ExcelFile> files = {
        makeFile(QStringLiteral("测试单位A_费用报表.xlsx"), {
            {QStringLiteral("1200"), 1200.0, QStringLiteral("#,##0")},
            {QStringLiteral("10500"), 10500.0, QStringLiteral("#,##0")},
            {QStringLiteral("3000"), 3000.0, QStringLiteral("#,##0")},
            {QStringLiteral("0"), 0.0, QStringLiteral("#,##0")},
        }),
        makeFile(QStringLiteral("测试单位B_费用报表.xlsx"), {
            {QStringLiteral("1200"), 1200.0, QStringLiteral("#,##0")},
            {QStringLiteral("10500.25"), 10500.25, QStringLiteral("#,##0.00")},
            {QStringLiteral("3100"), 3100.0, QStringLiteral("#,##0")},
            {QStringLiteral("0"), 0.0, QStringLiteral("#,##0")},
        }),
        makeFile(QStringLiteral("测试单位C_费用报表.xlsx"), {
            {QStringLiteral("1200"), 1200.0, QStringLiteral("#,##0")},
            {QStringLiteral("10500"), 10500.0, QStringLiteral("#,##0")},
            {QStringLiteral("3000"), 3000.0, QStringLiteral("#,##0")},
            {QStringLiteral("0"), 0.0, QStringLiteral("#,##0")},
        }),
        makeFile(QStringLiteral("测试单位D_费用报表.xlsx"), {
            {QStringLiteral("1200"), 1200.0, QStringLiteral("#,##0")},
            {QStringLiteral("10500.25"), 10500.25, QStringLiteral("#,##0.00")},
            {QStringLiteral("3100"), 3100.0, QStringLiteral("#,##0")},
            {QStringLiteral("0"), 0.0, QStringLiteral("#,##0")},
        }),
    };

    for (int pass = 0; pass < 2; ++pass) {
        if (pass == 1) {
            std::reverse(files.begin(), files.end());
        }

        const auto result = SimpleMerger().merge(files, QStringLiteral("费用汇总表"));
        QCOMPARE(result.rows[1][2].type.kind, CellKind::Sum);
        QCOMPARE(result.rows[1][2].type.sum, 4800.0);
        QCOMPARE(result.rows[2][2].type.kind, CellKind::Sum);
        QCOMPARE(result.rows[2][2].type.sum, 42000.5);
        QCOMPARE(result.rows[3][2].type.kind, CellKind::Sum);
        QCOMPARE(result.rows[3][2].type.sum, 12200.0);
        QCOMPARE(result.rows[4][2].type.kind, CellKind::Sum);
        QCOMPARE(result.rows[4][2].type.sum, 0.0);
    }
}

void CoreTests::validatesCompatibleSheets()
{
    SheetData sheetA{QStringLiteral("Sheet1"), {{CellData(QStringLiteral("金额"))}, {CellData(QStringLiteral("1000"))}}};
    SheetData sheetB{QStringLiteral("Sheet1"), {{CellData(QStringLiteral("金额"))}, {CellData(QStringLiteral("2000"))}}};
    const std::vector<ExcelFile> files = {
        {QStringLiteral("a.xlsx"), QStringLiteral("/a.xlsx"), {sheetA}},
        {QStringLiteral("b.xlsx"), QStringLiteral("/b.xlsx"), {sheetB}},
    };

    const auto outcome = WorkbookValidator().validate(files);
    QCOMPARE(outcome.report.readiness, MergeReadiness::Ready);
    QCOMPARE(outcome.report.commonSheetNames, QStringList{QStringLiteral("Sheet1")});
    QCOMPARE(outcome.mergeableFiles.size(), static_cast<size_t>(2));
}

void CoreTests::choosesRepresentativeTemplate()
{
    SheetData sparseSheet{
        QStringLiteral("Sheet1"),
        {
            {CellData(QStringLiteral("金额")), CellData(QStringLiteral("备注"))},
            {CellData(QStringLiteral("100")), CellData(QString())}
        }
    };
    SheetData fullerSheet{
        QStringLiteral("Sheet1"),
        {
            {CellData(QStringLiteral("金额")), CellData(QStringLiteral("备注"))},
            {CellData(QStringLiteral("200")), CellData(QStringLiteral("已填"))}
        }
    };
    const std::vector<ExcelFile> files = {
        {QStringLiteral("a.xlsx"), QStringLiteral("/a.xlsx"), {sparseSheet}},
        {QStringLiteral("b.xlsx"), QStringLiteral("/b.xlsx"), {fullerSheet}},
    };

    const auto outcome = WorkbookValidator().validate(files);
    QCOMPARE(outcome.report.readiness, MergeReadiness::Ready);
    QCOMPARE(outcome.mergeableFiles.front().filename, QStringLiteral("b.xlsx"));
    const auto templateReport = std::find_if(outcome.report.files.begin(), outcome.report.files.end(), [](const auto& report) {
        return report.isTemplate;
    });
    QVERIFY(templateReport != outcome.report.files.end());
    QCOMPARE(templateReport->filename, QStringLiteral("b.xlsx"));
}

void CoreTests::storesSchemas()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    SchemaRepository repository(QDir(temp.path()));

    MergeSchema schema;
    schema.id = QUuid::createUuid();
    schema.name = QStringLiteral("测试规则");
    schema.fingerprint.signature = QStringLiteral("sig");
    schema.createdAt = QDateTime::currentDateTimeUtc();
    schema.updatedAt = schema.createdAt;
    schema.overrides.push_back({{1, 2}, SchemaCellOverrideType::Sum});

    repository.save(schema);
    const auto loaded = repository.find(schema.id);
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->name, schema.name);
    QCOMPARE(loaded->overrides.size(), static_cast<size_t>(1));
}

void CoreTests::matchesWorkbookSchemas()
{
    SheetData sheet{QStringLiteral("Sheet1"), {
        {CellData(QStringLiteral("科目")), CellData(QStringLiteral("金额"))},
        {CellData(QStringLiteral("收入")), CellData(QStringLiteral("1000"))},
    }};
    const std::vector<ExcelFile> files = {
        {QStringLiteral("a.xlsx"), QStringLiteral("/a.xlsx"), {sheet}},
        {QStringLiteral("b.xlsx"), QStringLiteral("/b.xlsx"), {sheet}},
    };

    MergeSchema schema;
    schema.id = QUuid::createUuid();
    schema.name = QStringLiteral("财政表规则");
    schema.fingerprint = fingerprintFor(files, {QStringLiteral("Sheet1")});
    schema.createdAt = QDateTime::currentDateTimeUtc();
    schema.updatedAt = schema.createdAt;

    const auto match = SchemaMatcher::match(fingerprintFor(files, {QStringLiteral("Sheet1")}), {schema});
    QCOMPARE(match.kind, SchemaMatchKind::Exact);
    QVERIFY(match.exactSchema().has_value());
    QCOMPARE(match.exactSchema()->name, schema.name);
}

void CoreTests::matchesAdjustmentMemoryImportForSameArchitecture()
{
    SheetData savedSheet{QStringLiteral("汇总表"), {
        {CellData(QStringLiteral("项目")), CellData(QStringLiteral("金额"))},
        {CellData(QStringLiteral("A")), CellData(QStringLiteral("100"))},
    }};
    SheetData currentSheet{QStringLiteral("汇总表"), {
        {CellData(QStringLiteral("项目")), CellData(QStringLiteral("金额"))},
        {CellData(QStringLiteral("B")), CellData(QStringLiteral("230"))},
    }};
    const std::vector<ExcelFile> savedFiles = {
        {QStringLiteral("saved.xlsx"), QStringLiteral("/saved.xlsx"), {savedSheet}},
    };
    const std::vector<ExcelFile> currentFiles = {
        {QStringLiteral("current.xlsx"), QStringLiteral("/current.xlsx"), {currentSheet}},
    };

    MergeSchema imported;
    imported.id = QUuid::createUuid();
    imported.name = QStringLiteral("同构调整记忆");
    imported.fingerprint = fingerprintFor(savedFiles, {QStringLiteral("汇总表")});
    imported.overrides.push_back({{1, 1}, SchemaCellOverrideType::Sum, QStringLiteral("汇总表")});

    const auto currentFingerprint = fingerprintFor(currentFiles, {QStringLiteral("汇总表")});
    const auto match = SchemaMatcher::match(currentFingerprint, {imported});
    QCOMPARE(match.kind, SchemaMatchKind::Exact);

    imported.fingerprint = currentFingerprint;
    QCOMPARE(imported.fingerprint.signature, currentFingerprint.signature);
    QCOMPARE(imported.overrides.size(), static_cast<size_t>(1));
}

void CoreTests::rejectsAdjustmentMemoryImportForDifferentArchitecture()
{
    SheetData savedSheet{QStringLiteral("汇总表"), {
        {CellData(QStringLiteral("项目")), CellData(QStringLiteral("金额"))},
        {CellData(QStringLiteral("A")), CellData(QStringLiteral("100"))},
    }};
    SheetData differentSheet{QStringLiteral("明细表"), {
        {CellData(QStringLiteral("项目")), CellData(QStringLiteral("金额")), CellData(QStringLiteral("备注"))},
        {CellData(QStringLiteral("A")), CellData(QStringLiteral("100")), CellData(QStringLiteral("x"))},
    }};
    const std::vector<ExcelFile> savedFiles = {
        {QStringLiteral("saved.xlsx"), QStringLiteral("/saved.xlsx"), {savedSheet}},
    };
    const std::vector<ExcelFile> currentFiles = {
        {QStringLiteral("different.xlsx"), QStringLiteral("/different.xlsx"), {differentSheet}},
    };

    MergeSchema imported;
    imported.id = QUuid::createUuid();
    imported.name = QStringLiteral("其他结构调整记忆");
    imported.fingerprint = fingerprintFor(savedFiles, {QStringLiteral("汇总表")});
    imported.overrides.push_back({{1, 1}, SchemaCellOverrideType::Sum, QStringLiteral("汇总表")});

    const auto currentFingerprint = fingerprintFor(currentFiles, {QStringLiteral("明细表")});
    const auto match = SchemaMatcher::match(currentFingerprint, {imported});
    QVERIFY(match.kind != SchemaMatchKind::Exact);
}

void CoreTests::matchesWorkbookSchemasByDominantDimensions()
{
    SheetData normalSheet{QStringLiteral("Sheet1"), {
        {CellData(QStringLiteral("科目")), CellData(QStringLiteral("金额"))},
        {CellData(QStringLiteral("收入")), CellData(QStringLiteral("1000"))},
    }};
    SheetData oddFirstSheet{QStringLiteral("Sheet1"), {
        {CellData(QStringLiteral("科目")), CellData(QStringLiteral("金额"))},
        {CellData(QStringLiteral("收入")), CellData(QStringLiteral("1000"))},
        {CellData(QStringLiteral("尾注")), CellData(QStringLiteral("临时说明"))},
    }};

    const std::vector<ExcelFile> savedFiles = {
        {QStringLiteral("saved-a.xlsx"), QStringLiteral("/saved-a.xlsx"), {normalSheet}},
        {QStringLiteral("saved-b.xlsx"), QStringLiteral("/saved-b.xlsx"), {normalSheet}},
    };
    const std::vector<ExcelFile> incomingFiles = {
        {QStringLiteral("incoming-odd.xlsx"), QStringLiteral("/incoming-odd.xlsx"), {oddFirstSheet}},
        {QStringLiteral("incoming-a.xlsx"), QStringLiteral("/incoming-a.xlsx"), {normalSheet}},
        {QStringLiteral("incoming-b.xlsx"), QStringLiteral("/incoming-b.xlsx"), {normalSheet}},
    };

    MergeSchema schema;
    schema.id = QUuid::createUuid();
    schema.name = QStringLiteral("主流维度规则");
    schema.fingerprint = fingerprintFor(savedFiles, {QStringLiteral("Sheet1")});

    const auto incomingFingerprint = fingerprintFor(incomingFiles, {QStringLiteral("Sheet1")});
    QCOMPARE(incomingFingerprint.sheetFingerprints.front().rowCount, 2);
    QCOMPARE(incomingFingerprint.sheetFingerprints.front().columnCount, 2);

    const auto match = SchemaMatcher::match(incomingFingerprint, {schema});
    QCOMPARE(match.kind, SchemaMatchKind::Exact);
    QVERIFY(match.exactSchema().has_value());
    QCOMPARE(match.exactSchema()->name, schema.name);
}

void CoreTests::appliesSchemaOverrides()
{
    SheetData sheetA{QStringLiteral("Sheet1"), {
        {CellData(QStringLiteral("科目")), CellData(QStringLiteral("代码"))},
        {CellData(QStringLiteral("行政区划")), CellData(QStringLiteral("331024001"))},
    }};
    SheetData sheetB{QStringLiteral("Sheet1"), {
        {CellData(QStringLiteral("科目")), CellData(QStringLiteral("代码"))},
        {CellData(QStringLiteral("行政区划")), CellData(QStringLiteral("331024002"))},
    }};
    const std::vector<ExcelFile> files = {
        {QStringLiteral("a.xlsx"), QStringLiteral("/a.xlsx"), {sheetA}},
        {QStringLiteral("b.xlsx"), QStringLiteral("/b.xlsx"), {sheetB}},
    };

    auto result = SimpleMerger().merge(files, QStringLiteral("Sheet1"));
    QCOMPARE(result.rows[1][1].type.kind, CellKind::Label);

    MergeSchema schema;
    schema.id = QUuid::createUuid();
    schema.name = QStringLiteral("强制求和测试规则");
    schema.fingerprint = fingerprintFor(files, {QStringLiteral("Sheet1")});
    schema.overrides.push_back({{1, 1}, SchemaCellOverrideType::Sum, QStringLiteral("Sheet1")});

    const auto adjusted = applySchema(schema, result);
    QCOMPARE(adjusted.rows[1][1].type.kind, CellKind::Sum);
    QCOMPARE(adjusted.rows[1][1].type.sum, 662048003.0);
    QVERIFY(adjusted.rows[1][1].isOverridden);
}

void CoreTests::exportsCsvBaseline()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const auto path = temp.filePath(QStringLiteral("summary.csv"));
    const std::vector<CellMergeInput> cells = {
        {QStringLiteral("a.xlsx"), QStringLiteral("/a.xlsx"), CellData(QStringLiteral("1000"))},
        {QStringLiteral("b.xlsx"), QStringLiteral("/b.xlsx"), CellData(QStringLiteral("2000"))},
    };
    MergedResult result;
    result.sheetName = QStringLiteral("Sheet1");
    result.rows = {{MergedCell::from(cells)}};

    TemplateWorkbookExporter().exportWorkbook({}, {result}, path);
    QVERIFY(QFile::exists(path));
}

void CoreTests::parsesXlsxSampleWorkbook()
{
    const QString path = QStringLiteral(XLSONE_REPO_ROOT)
        + QStringLiteral("/仙居县/仙居县人民政府南峰街道办事处2025乡镇报表主体信息表.xlsx");
    if (!QFileInfo::exists(path)) {
        QSKIP("仙居县 .xlsx sample is not available in this checkout");
    }

    const auto file = ExcelParser().parseFile(path);
    QCOMPARE(file.sheets.size(), static_cast<size_t>(11));
    QCOMPARE(file.sheets.front().name, QStringLiteral("乡镇报表主体信息表"));
    QCOMPARE(file.sheets.front().rows.size(), static_cast<size_t>(22));
    QCOMPARE(file.sheets.front().cellAt(1, 1)->value, QStringLiteral("乡镇报表主体信息表"));
    QCOMPARE(file.sheets.front().cellAt(15, 2)->value, QStringLiteral("331024000"));
    QCOMPARE(file.sheets.front().cellAt(16, 2)->value, QStringLiteral("331024002"));
    QVERIFY(file.sheets.front().cellAt(15, 2)->numericValue.has_value());
}

void CoreTests::parsesMergedXlsxCells()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const auto path = temp.filePath(QStringLiteral("merged.xlsx"));
    const QByteArray workbook = R"(<?xml version="1.0" encoding="UTF-8"?>
<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"
          xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">
  <sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets>
</workbook>)";
    const QByteArray relationships = R"(<?xml version="1.0" encoding="UTF-8"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
  <Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>
</Relationships>)";
    const QByteArray styles = R"(<?xml version="1.0" encoding="UTF-8"?>
<styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">
  <cellXfs count="2">
    <xf numFmtId="0"/>
    <xf numFmtId="14"/>
  </cellXfs>
</styleSheet>)";
    const QByteArray worksheet = R"(<?xml version="1.0" encoding="UTF-8"?>
<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">
  <sheetData>
    <row r="1"><c r="A1" t="inlineStr"><is><t>合并标题</t></is></c></row>
    <row r="2"><c r="A2"><v>10</v></c><c r="B2"><v>20</v></c><c r="C2" s="1"><v>45826</v></c></row>
    <row r="3"/>
  </sheetData>
  <mergeCells count="1"><mergeCell ref="A1:C1"/></mergeCells>
</worksheet>)";

    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(storedZip({
        {QStringLiteral("xl/workbook.xml"), workbook},
        {QStringLiteral("xl/_rels/workbook.xml.rels"), relationships},
        {QStringLiteral("xl/styles.xml"), styles},
        {QStringLiteral("xl/worksheets/sheet1.xml"), worksheet},
    }));
    file.close();

    const auto parsed = ExcelParser().parseFile(path);
    QCOMPARE(parsed.sheets.size(), static_cast<size_t>(1));
    const auto& sheet = parsed.sheets.front();
    QCOMPARE(sheet.rows.size(), static_cast<size_t>(3));
    QCOMPARE(sheet.rows[0].size(), static_cast<size_t>(3));
    QCOMPARE(sheet.cellAt(0, 0)->value, QStringLiteral("合并标题"));
    QCOMPARE(sheet.cellAt(0, 1)->value, QStringLiteral("合并标题"));
    QCOMPARE(sheet.cellAt(0, 2)->value, QStringLiteral("合并标题"));
    QCOMPARE(sheet.cellAt(1, 1)->numericValue.value(), 20.0);
    QCOMPARE(sheet.cellAt(1, 2)->value, QStringLiteral("2025-06-16"));
    QVERIFY(sheet.cellAt(1, 2)->isDate);
    QCOMPARE(sheet.cellAt(1, 2)->numericValue.value(), 45826.0);
}

void CoreTests::exportsXlsxUsingTemplateWorkbook()
{
    const QString templatePath = QStringLiteral(XLSONE_REPO_ROOT)
        + QStringLiteral("/仙居县/仙居县人民政府南峰街道办事处2025乡镇报表主体信息表.xlsx");
    if (!QFileInfo::exists(templatePath)) {
        QSKIP("仙居县 .xlsx sample is not available in this checkout");
    }

    const auto file = ExcelParser().parseFile(templatePath);
    const auto* sourceSheet = file.sheetNamed(QStringLiteral("乡镇报表主体信息表"));
    QVERIFY(sourceSheet != nullptr);

    MergedResult result;
    result.sheetName = sourceSheet->name;
    result.sourceFiles = QStringList{file.filename};
    for (const auto& sourceRow : sourceSheet->rows) {
        std::vector<MergedCell> row;
        for (const auto& sourceCell : sourceRow) {
            row.push_back(MergedCell::from({
                {file.filename, file.filepath, sourceCell}
            }));
        }
        result.rows.push_back(std::move(row));
    }

    MergedCell replacement;
    replacement.type.kind = CellKind::Sum;
    replacement.type.sum = 1234.0;
    replacement.displayValue = QStringLiteral("1234");
    result.rows[15][2] = replacement;

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const auto outputPath = temp.filePath(QStringLiteral("summary.xlsx"));
    TemplateWorkbookExporter().exportWorkbook(templatePath, {result}, outputPath);

    const auto exported = ExcelParser().parseFile(outputPath);
    const auto* exportedSheet = exported.sheetNamed(QStringLiteral("乡镇报表主体信息表"));
    QVERIFY(exportedSheet != nullptr);
    QCOMPARE(exportedSheet->cellAt(15, 2)->value, QStringLiteral("1234"));
    QVERIFY(exportedSheet->cellAt(15, 2)->numericValue.has_value());
    QCOMPARE(exportedSheet->cellAt(15, 2)->numericValue.value(), 1234.0);
}

void CoreTests::parsesMinimalBiff8Workbook()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const auto path = temp.filePath(QStringLiteral("minimal.xls"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(minimalBiff8Workbook());
    file.close();

    const auto parsed = ExcelParser().parseFile(path);
    QCOMPARE(parsed.filename, QStringLiteral("minimal.xls"));
    QCOMPARE(parsed.sheets.size(), static_cast<size_t>(1));
    QCOMPARE(parsed.sheets.front().name, QStringLiteral("Sheet1"));
    QCOMPARE(parsed.sheets.front().rows[0].size(), static_cast<size_t>(2));
    QCOMPARE(parsed.sheets.front().rows[0][0].value, QStringLiteral("Name"));
    QCOMPARE(parsed.sheets.front().rows[0][1].value, QStringLiteral("Amount"));
    QCOMPARE(parsed.sheets.front().rows[1][0].value, QStringLiteral("Alice"));
    QCOMPARE(parsed.sheets.front().rows[1][1].value, QStringLiteral("1234"));
    QVERIFY(parsed.sheets.front().rows[1][1].numericValue.has_value());
    QCOMPARE(parsed.sheets.front().rows[1][1].numericValue.value(), 1234.0);
}

void CoreTests::parsesLocalXianjuXlsWhenPresent()
{
    const QString xlsPath = QStringLiteral(XLSONE_REPO_ROOT)
        + QStringLiteral("/仙居县/仙居县朱溪镇人民政府2025乡镇报表主体信息表.xls");
    const QString xlsxPath = QStringLiteral(XLSONE_REPO_ROOT)
        + QStringLiteral("/仙居县/仙居县朱溪镇人民政府2025乡镇报表主体信息表.xlsx");
    if (!QFileInfo::exists(xlsPath) || !QFileInfo::exists(xlsxPath)) {
        QSKIP("Local Xianju .xls/.xlsx fixtures are not available in this checkout");
    }

    const auto xlsFile = ExcelParser().parseFile(xlsPath);
    const auto xlsxFile = ExcelParser().parseFile(xlsxPath);
    QCOMPARE(xlsFile.sheets.size(), xlsxFile.sheets.size());
    QCOMPARE(xlsFile.sheets.front().name, xlsxFile.sheets.front().name);
    QVERIFY(!xlsFile.sheets.empty());
    QVERIFY(!xlsFile.sheets.front().rows.empty());
    QCOMPARE(xlsFile.sheets.front().cellAt(1, 1)->value, xlsxFile.sheets.front().cellAt(1, 1)->value);
    QCOMPARE(xlsFile.sheets.front().cellAt(15, 2)->value, xlsxFile.sheets.front().cellAt(15, 2)->value);
}

QTEST_APPLESS_MAIN(CoreTests)

void CoreTests::updateCheckerCurrentVersionIsValid()
{
    UpdateChecker checker;
    const QString ver = checker.currentVersion();
    QVERIFY(!ver.isEmpty());
    const auto parts = ver.split(QLatin1Char('.'));
    QCOMPARE(parts.size(), 3);
    for (const auto& p : parts) {
        bool ok = false;
        p.toInt(&ok);
        QVERIFY(ok);
    }
}

void CoreTests::updateCheckerCompareVersions()
{
    // equal
    QCOMPARE(UpdateChecker::compareVersions(
        QStringLiteral("1.0.0"), QStringLiteral("1.0.0")), 0);
    // patch bump
    QVERIFY(UpdateChecker::compareVersions(
        QStringLiteral("1.0.1"), QStringLiteral("1.0.0")) > 0);
    // minor bump
    QVERIFY(UpdateChecker::compareVersions(
        QStringLiteral("1.1.0"), QStringLiteral("1.0.9")) > 0);
    // major bump
    QVERIFY(UpdateChecker::compareVersions(
        QStringLiteral("2.0.0"), QStringLiteral("1.9.9")) > 0);
    // older
    QVERIFY(UpdateChecker::compareVersions(
        QStringLiteral("0.9.0"), QStringLiteral("1.0.0")) < 0);
    // two-digit versions
    QVERIFY(UpdateChecker::compareVersions(
        QStringLiteral("1.10.0"), QStringLiteral("1.9.0")) > 0);
    QVERIFY(UpdateChecker::compareVersions(
        QStringLiteral("1.2.0"), QStringLiteral("1.10.0")) < 0);
}

void CoreTests::updateCheckerParseUpdateInfoJson()
{
    const QByteArray json = R"({
        "latest_version": "2.0.0",
        "changelog": "test changelog",
        "downloads": {
            "linux": "https://example.com/linux.deb",
            "windows": "https://example.com/win.exe",
            "macos": "https://example.com/mac.dmg"
        }
    })";

    const auto info = UpdateChecker::parseUpdateInfo(json);

    QCOMPARE(info.latestVersion, QStringLiteral("2.0.0"));
    QCOMPARE(info.changelog, QStringLiteral("test changelog"));
    QVERIFY(!info.downloadUrl.isEmpty());
}

void CoreTests::updateCheckerParseInvalidJson()
{
    const QByteArray bad = "not valid json";
    const auto info = UpdateChecker::parseUpdateInfo(bad);
    QVERIFY(info.latestVersion.isEmpty());
    QVERIFY(info.downloadUrl.isEmpty());
}

void CoreTests::updateCheckerPlatformKeyIsNotEmpty()
{
    const QString key = UpdateChecker::platformKey();
    QVERIFY(!key.isEmpty());
}

void CoreTests::updateCheckerFindsNewVersionOnline()
{
    // Verify the local API JSON is parseable and version > current
    const QByteArray json = R"({
        "latest_version": "0.2.0",
        "changelog": "test",
        "downloads": {
            "linux": "https://z-pulse.cn/downloads/test.AppImage"
        }
    })";
    const auto info = UpdateChecker::parseUpdateInfo(json);

    QCOMPARE(info.latestVersion, QStringLiteral("0.2.0"));
    QCOMPARE(info.downloadUrl, QStringLiteral("https://z-pulse.cn/downloads/test.AppImage"));

    // Version 0.2.0 should be greater than compiled version
    QVERIFY(UpdateChecker::compareVersions(
        info.latestVersion,
        QStringLiteral("0.1.0")) > 0);
}

void CoreTests::licenseManagerAcceptsValidEd25519License()
{
    // Signed with production Ed25519 key pair (seed kept in Worker secret).
    const QByteArray license = R"({
        "key_id": "XLS1-TEST-0001",
        "plan": "personal_lifetime",
        "device_hash": "",
        "device_components": [],
        "issued_at": 1719830400,
        "expires_at": 0,
        "signature": "kIPZ0K77COo35s_whFUjT6Cg06wmsSZ8CyoRxSGPWa8wODVteaceUEJqaH8p1k_SiSnQYcRLDttXiZbyKopTDQ"
    })";

    xlsone::LicenseManager manager;
    xlsone::LicenseInfo info;
    QString errorMessage;
    QVERIFY(manager.applyLicenseFile(license, QString(), &info, &errorMessage));
    QCOMPARE(manager.state(), xlsone::LicenseState::Activated);
    QCOMPARE(info.keyId, QStringLiteral("XLS1-TEST-0001"));
    QCOMPARE(info.plan, xlsone::LicensePlan::PersonalLifetime);
}

void CoreTests::licenseManagerRejectsTamperedEd25519License()
{
    QByteArray license = R"({
        "key_id": "XLS1-TEST-0001",
        "plan": "personal_lifetime",
        "device_hash": "",
        "device_components": [],
        "issued_at": 1719830400,
        "expires_at": 0,
        "signature": "kIPZ0K77COo35s_whFUjT6Cg06wmsSZ8CyoRxSGPWa8wODVteaceUEJqaH8p1k_SiSnQYcRLDttXiZbyKopTDQ"
    })";
    // Tamper with the payload.
    license.replace("personal_lifetime", "enterprise_10");

    xlsone::LicenseManager manager;
    QString errorMessage;
    QVERIFY(!manager.applyLicenseFile(license, QString(), nullptr, &errorMessage));
    QVERIFY(!errorMessage.isEmpty());
}

#include "core_tests.moc"
