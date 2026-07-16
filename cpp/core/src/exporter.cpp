#include "xlsone/core/exporter.hpp"

#include "zip_archive.hpp"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QTextStream>
#include <QXmlStreamReader>
#include <QtEndian>
#include <cmath>
#include <stdexcept>
#if __has_include(<QtZlib/zlib.h>)
#include <QtZlib/zlib.h>
#else
#include <zlib.h>
#endif

namespace xlsone {

namespace {

struct ZipCentralRecord {
    QString name;
    quint32 crc = 0;
    quint32 compressedSize = 0;
    quint32 uncompressedSize = 0;
    quint32 localHeaderOffset = 0;
    quint16 method = 8;
};

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

QByteArray deflateRaw(const QByteArray& input)
{
    if (input.isEmpty()) {
        return {};
    }

    z_stream stream {};
    if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        throw std::runtime_error("无法初始化 ZIP deflate 压缩器");
    }

    QByteArray output;
    output.resize(static_cast<qsizetype>(deflateBound(&stream, static_cast<uLong>(input.size()))));

    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.constData()));
    stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = reinterpret_cast<Bytef*>(output.data());
    stream.avail_out = static_cast<uInt>(output.size());

    const int result = deflate(&stream, Z_FINISH);
    deflateEnd(&stream);
    if (result != Z_STREAM_END) {
        throw std::runtime_error("ZIP deflate 压缩失败");
    }

    output.resize(static_cast<qsizetype>(stream.total_out));
    return output;
}

void writeZipFile(const QString& outputPath, const QHash<QString, QByteArray>& entries)
{
    QByteArray fileData;
    std::vector<ZipCentralRecord> centralRecords;
    QStringList names = entries.keys();
    names.sort();

    for (const auto& name : names) {
        if (name.endsWith(QLatin1Char('/'))) {
            continue;
        }

        const QByteArray uncompressed = entries.value(name);
        const QByteArray compressed = deflateRaw(uncompressed);
        const QByteArray encodedName = name.toUtf8();
        const quint32 crc = crc32(0L, reinterpret_cast<const Bytef*>(uncompressed.constData()), static_cast<uInt>(uncompressed.size()));

        ZipCentralRecord record;
        record.name = name;
        record.crc = crc;
        record.compressedSize = static_cast<quint32>(compressed.size());
        record.uncompressedSize = static_cast<quint32>(uncompressed.size());
        record.localHeaderOffset = static_cast<quint32>(fileData.size());
        record.method = 8;

        appendU32(fileData, 0x04034b50);
        appendU16(fileData, 20);
        appendU16(fileData, 1 << 11);
        appendU16(fileData, record.method);
        appendU16(fileData, 0);
        appendU16(fileData, 0);
        appendU32(fileData, record.crc);
        appendU32(fileData, record.compressedSize);
        appendU32(fileData, record.uncompressedSize);
        appendU16(fileData, static_cast<quint16>(encodedName.size()));
        appendU16(fileData, 0);
        fileData.append(encodedName);
        fileData.append(compressed);

        centralRecords.push_back(record);
    }

    const quint32 centralDirectoryOffset = static_cast<quint32>(fileData.size());
    for (const auto& record : centralRecords) {
        const QByteArray encodedName = record.name.toUtf8();
        appendU32(fileData, 0x02014b50);
        appendU16(fileData, 20);
        appendU16(fileData, 20);
        appendU16(fileData, 1 << 11);
        appendU16(fileData, record.method);
        appendU16(fileData, 0);
        appendU16(fileData, 0);
        appendU32(fileData, record.crc);
        appendU32(fileData, record.compressedSize);
        appendU32(fileData, record.uncompressedSize);
        appendU16(fileData, static_cast<quint16>(encodedName.size()));
        appendU16(fileData, 0);
        appendU16(fileData, 0);
        appendU16(fileData, 0);
        appendU16(fileData, 0);
        appendU32(fileData, 0);
        appendU32(fileData, record.localHeaderOffset);
        fileData.append(encodedName);
    }

    const quint32 centralDirectorySize = static_cast<quint32>(fileData.size()) - centralDirectoryOffset;
    appendU32(fileData, 0x06054b50);
    appendU16(fileData, 0);
    appendU16(fileData, 0);
    appendU16(fileData, static_cast<quint16>(centralRecords.size()));
    appendU16(fileData, static_cast<quint16>(centralRecords.size()));
    appendU32(fileData, centralDirectorySize);
    appendU32(fileData, centralDirectoryOffset);
    appendU16(fileData, 0);

    QFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        throw std::runtime_error(QStringLiteral("无法写入导出文件: %1").arg(file.errorString()).toStdString());
    }
    file.write(fileData);
}

QString attributeValue(const QXmlStreamAttributes& attributes, const QString& name)
{
    for (const auto& attribute : attributes) {
        if (attribute.name() == name || attribute.qualifiedName() == name) {
            return attribute.value().toString();
        }
    }
    return {};
}

QString normalizeRelationshipTarget(QString target)
{
    if (target.startsWith(QLatin1Char('/'))) {
        target.remove(0, 1);
    }
    if (!target.startsWith(QStringLiteral("xl/"))) {
        target.prepend(QStringLiteral("xl/"));
    }
    return target;
}

QHash<QString, QString> worksheetPathMap(const ZipArchive& archive)
{
    struct SheetRef {
        QString name;
        QString relationshipId;
    };

    std::vector<SheetRef> sheets;
    QXmlStreamReader workbookReader(archive.readText(QStringLiteral("xl/workbook.xml")));
    while (!workbookReader.atEnd()) {
        workbookReader.readNext();
        if (workbookReader.isStartElement() && workbookReader.name() == QStringLiteral("sheet")) {
            const auto attributes = workbookReader.attributes();
            sheets.push_back({
                attributeValue(attributes, QStringLiteral("name")),
                attributeValue(attributes, QStringLiteral("r:id"))
            });
        }
    }
    if (workbookReader.hasError()) {
        throw std::runtime_error(QStringLiteral("workbook.xml 解析失败: %1").arg(workbookReader.errorString()).toStdString());
    }

    QHash<QString, QString> targetByRelationship;
    QXmlStreamReader relsReader(archive.readText(QStringLiteral("xl/_rels/workbook.xml.rels")));
    while (!relsReader.atEnd()) {
        relsReader.readNext();
        if (relsReader.isStartElement() && relsReader.name() == QStringLiteral("Relationship")) {
            const auto attributes = relsReader.attributes();
            const auto id = attributeValue(attributes, QStringLiteral("Id"));
            const auto target = attributeValue(attributes, QStringLiteral("Target"));
            if (!id.isEmpty() && !target.isEmpty()) {
                targetByRelationship.insert(id, normalizeRelationshipTarget(target));
            }
        }
    }
    if (relsReader.hasError()) {
        throw std::runtime_error(QStringLiteral("workbook.xml.rels 解析失败: %1").arg(relsReader.errorString()).toStdString());
    }

    QHash<QString, QString> paths;
    for (const auto& sheet : sheets) {
        const auto target = targetByRelationship.value(sheet.relationshipId);
        if (!sheet.name.isEmpty() && !target.isEmpty()) {
            paths.insert(sheet.name, target);
        }
    }
    return paths;
}

QString escapeXml(QString text)
{
    text.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    text.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    text.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    text.replace(QLatin1Char('"'), QStringLiteral("&quot;"));
    return text;
}

QString numericCellValue(double value)
{
    if (std::fabs(value - std::round(value)) < 0.0000001) {
        return QString::number(static_cast<qint64>(std::llround(value)));
    }
    return QString::number(value, 'g', 15);
}

QString sanitizedAttributes(QString raw)
{
    raw.remove(QLatin1Char('/'));
    raw.remove(QRegularExpression(QStringLiteral("\\s+t=\"[^\"]*\"")));
    raw.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return raw.trimmed();
}

QString cellXml(const QString& reference, const QString& attributes, const MergedCell& cell)
{
    const QString attrText = attributes.isEmpty() ? QString() : QStringLiteral(" ") + attributes;
    if (cell.type.kind == CellKind::Sum) {
        return QStringLiteral("<c r=\"%1\"%2><v>%3</v></c>")
            .arg(reference, attrText, numericCellValue(cell.type.sum));
    }
    if (cell.displayValue.isEmpty()) {
        return QStringLiteral("<c r=\"%1\"%2/>").arg(reference, attrText);
    }
    return QStringLiteral("<c r=\"%1\" t=\"inlineStr\"%2><is><t>%3</t></is></c>")
        .arg(reference, attrText, escapeXml(cell.displayValue));
}

QString rewriteCell(QString xml, const QString& reference, const MergedCell& cell)
{
    const QString escapedReference = QRegularExpression::escape(reference);
    const QStringList patterns = {
        QStringLiteral("<c\\b([^>]*)\\br=\"%1\"([^>]*)/>").arg(escapedReference),
        QStringLiteral("<c\\b([^>/]*)\\br=\"%1\"([^>/]*)>(.*?)</c>").arg(escapedReference),
    };

    for (const auto& pattern : patterns) {
        QRegularExpression regex(pattern, QRegularExpression::DotMatchesEverythingOption);
        const auto match = regex.match(xml);
        if (!match.hasMatch()) {
            continue;
        }
        const auto attributes = sanitizedAttributes(match.captured(1) + match.captured(2));
        xml.replace(match.capturedStart(), match.capturedLength(), cellXml(reference, attributes, cell));
        return xml;
    }

    return xml;
}

QByteArray rewriteWorksheet(QByteArray data, const MergedResult& result, const QString& watermarkText)
{
    QString xml = QString::fromUtf8(data);
    for (int row = 0; row < static_cast<int>(result.rows.size()); ++row) {
        const auto& rowData = result.rows[static_cast<size_t>(row)];
        for (int column = 0; column < static_cast<int>(rowData.size()); ++column) {
            xml = rewriteCell(xml, cellReference(row, column), rowData[static_cast<size_t>(column)]);
        }
    }
    // 受限水印：在数据末尾追加一行醒目的水印声明
    if (!watermarkText.isEmpty() && !result.rows.empty()) {
        static const QRegularExpression lastRowRegex(QStringLiteral("<row[^>]*\\br=\"(\\d+)\""));
        int maxRowNumber = 0;
        auto it = lastRowRegex.globalMatch(xml);
        while (it.hasNext()) {
            const auto match = it.next();
            const int rowNumber = match.captured(1).toInt();
            if (rowNumber > maxRowNumber) {
                maxRowNumber = rowNumber;
            }
        }

        const int watermarkRowNumber = maxRowNumber + 1;
        const QString watermarkRef = QStringLiteral("A%1").arg(watermarkRowNumber);
        const QString watermarkRow = QStringLiteral(
            "<row r=\"%1\" spans=\"1:1\">"
            "<c r=\"%2\" t=\"inlineStr\"><is><t>%3</t></is></c>"
            "</row>"
        ).arg(watermarkRowNumber).arg(watermarkRef).arg(escapeXml(watermarkText));

        // Insert before the closing </sheetData>
        const int sheetDataEnd = xml.lastIndexOf(QStringLiteral("</sheetData>"));
        if (sheetDataEnd >= 0) {
            xml.insert(sheetDataEnd, watermarkRow);
        }
    }
    return xml.toUtf8();
}

void exportCsvBaseline(const std::vector<MergedResult>& results, const QString& outputPath, const QString& watermarkText)
{
    if (results.empty()) {
        throw std::runtime_error("没有可导出的汇总结果");
    }

    QFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        throw std::runtime_error(QStringLiteral("无法写入导出文件: %1").arg(file.errorString()).toStdString());
    }

    QTextStream stream(&file);
    if (!watermarkText.isEmpty()) {
        stream << QStringLiteral("# %1\n").arg(watermarkText);
    }
    const auto& result = results.front();
    for (const auto& row : result.rows) {
        QStringList values;
        for (const auto& cell : row) {
            QString value = cell.displayValue;
            value.replace(QStringLiteral("\""), QStringLiteral("\"\""));
            values.append(QStringLiteral("\"%1\"").arg(value));
        }
        stream << values.join(QLatin1Char(',')) << '\n';
    }
}

void exportXlsxWorkbook(const QString& templatePath, const std::vector<MergedResult>& results, const QString& outputPath, const QString& watermarkText)
{
    if (templatePath.isEmpty()) {
        throw std::runtime_error("导出 .xlsx 需要模板工作簿路径");
    }
    if (results.empty()) {
        throw std::runtime_error("没有可导出的汇总结果");
    }

    const ZipArchive archive(templatePath);
    const auto worksheetPaths = worksheetPathMap(archive);
    QHash<QString, QByteArray> entries;
    for (const auto& name : archive.entryNames()) {
        if (!name.endsWith(QLatin1Char('/'))) {
            entries.insert(name, archive.read(name));
        }
    }

    for (const auto& result : results) {
        const auto worksheetPath = worksheetPaths.value(result.sheetName);
        if (worksheetPath.isEmpty() || !entries.contains(worksheetPath)) {
            continue;
        }
        entries.insert(worksheetPath, rewriteWorksheet(entries.value(worksheetPath), result, watermarkText));
    }

    writeZipFile(outputPath, entries);
}

} // namespace

void TemplateWorkbookExporter::exportWorkbook(
    const QString& templatePath,
    const std::vector<MergedResult>& results,
    const QString& outputPath,
    const QString& watermarkText
) const
{
    const auto suffix = QFileInfo(outputPath).suffix().toLower();
    if (suffix == QStringLiteral("xlsx")) {
        exportXlsxWorkbook(templatePath, results, outputPath, watermarkText);
        return;
    }
    exportCsvBaseline(results, outputPath, watermarkText);
}

} // namespace xlsone

