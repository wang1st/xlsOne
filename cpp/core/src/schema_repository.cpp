#include "xlsone/core/schema_repository.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

namespace xlsone {

namespace {

QDir defaultSchemaDir()
{
    const auto root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(root + QStringLiteral("/schemas"));
}

QString schemaFileName(const QUuid& id)
{
    return id.toString(QUuid::WithoutBraces) + QStringLiteral(".json");
}

QString normalized(QString text)
{
    text = text.toLower().trimmed();
    text.remove(QRegularExpression(QStringLiteral("\\s+")));
    return text;
}

QString shortHash(const QString& text)
{
    const auto hash = QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QString::fromLatin1(hash.left(16));
}

QString structuralToken(const CellData* cell)
{
    if (cell == nullptr || cell->value.trimmed().isEmpty()) {
        return QStringLiteral("E");
    }
    if (cell->isDate) {
        return QStringLiteral("D");
    }
    if (cell->numericValue.has_value()) {
        return QStringLiteral("N");
    }
    if (cell->value.contains(QRegularExpression(QStringLiteral("\\p{Han}")))) {
        return QStringLiteral("C");
    }
    if (cell->value.contains(QRegularExpression(QStringLiteral("[A-Za-z]")))) {
        return QStringLiteral("A");
    }
    return QStringLiteral("T");
}

struct SheetDimensions {
    int rows = 0;
    int columns = 0;

    bool operator==(const SheetDimensions& other) const
    {
        return rows == other.rows && columns == other.columns;
    }
    bool operator<(const SheetDimensions& other) const
    {
        if (rows != other.rows) {
            return rows < other.rows;
        }
        return columns < other.columns;
    }
};

SheetDimensions effectiveDimensions(const SheetData& sheet)
{
    return {sheet.effectiveRowCount(), sheet.effectiveColumnCount()};
}

SheetDimensions chooseDominantDimensions(const std::vector<SheetDimensions>& dimensions)
{
    if (dimensions.empty()) {
        return {};
    }

    struct Bucket {
        SheetDimensions dimensions;
        int count = 0;
        int firstIndex = 0;
    };

    std::vector<Bucket> buckets;
    for (int index = 0; index < static_cast<int>(dimensions.size()); ++index) {
        const auto& current = dimensions[static_cast<size_t>(index)];
        auto iterator = std::find_if(buckets.begin(), buckets.end(), [&](const Bucket& bucket) {
            return bucket.dimensions == current;
        });
        if (iterator == buckets.end()) {
            buckets.push_back({current, 1, index});
        } else {
            ++iterator->count;
        }
    }

    return std::max_element(buckets.begin(), buckets.end(), [](const Bucket& lhs, const Bucket& rhs) {
        if (lhs.count != rhs.count) {
            return lhs.count < rhs.count;
        }
        if (!(lhs.dimensions == rhs.dimensions)) {
            return lhs.dimensions < rhs.dimensions;
        }
        return lhs.firstIndex > rhs.firstIndex;
    })->dimensions;
}

SheetRuleFingerprint sheetFingerprint(const SheetData& sheet, int rows, int columns)
{
    QStringList layout;
    QStringList format;
    layout << sheet.name << QStringLiteral("%1x%2").arg(rows).arg(columns);

    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const auto* cell = sheet.cellAt(row, column);
            const bool empty = cell == nullptr || cell->value.trimmed().isEmpty();
            layout << (empty ? QStringLiteral("0") : QStringLiteral("1"));
            format << structuralToken(cell);
        }
    }

    return {
        sheet.name,
        rows,
        columns,
        shortHash(layout.join(QLatin1Char('|'))),
        shortHash(format.join(QLatin1Char('|')))
    };
}

SheetRuleFingerprint sheetFingerprint(const SheetData& sheet)
{
    const auto dimensions = effectiveDimensions(sheet);
    return sheetFingerprint(sheet, dimensions.rows, dimensions.columns);
}

SheetRuleFingerprint consensusSheetFingerprint(const std::vector<ExcelFile>& files, const QString& sheetName)
{
    std::vector<const SheetData*> sheets;
    std::vector<SheetDimensions> dimensions;
    for (const auto& file : files) {
        if (const auto* sheet = file.sheetNamed(sheetName)) {
            sheets.push_back(sheet);
            dimensions.push_back(effectiveDimensions(*sheet));
        }
    }
    if (!sheets.empty()) {
        const auto dominant = chooseDominantDimensions(dimensions);
        return sheetFingerprint(*sheets.front(), dominant.rows, dominant.columns);
    }
    return {sheetName, 0, 0, {}, {}};
}

double sheetSimilarity(const SheetRuleFingerprint& lhs, const SheetRuleFingerprint& rhs)
{
    double score = 0.0;
    score += lhs.sheetName == rhs.sheetName ? 0.30 : 0.0;
    score += (lhs.rowCount == rhs.rowCount && lhs.columnCount == rhs.columnCount) ? 0.30 : 0.0;
    score += lhs.layoutHash == rhs.layoutHash ? 0.25 : 0.0;
    score += lhs.formatHash == rhs.formatHash ? 0.15 : 0.0;
    return score;
}

std::vector<QString> sourceValues(const MergedCell& cell)
{
    std::vector<QString> values;
    for (const auto& source : cell.sources) {
        if (source.state == CellSourceState::Value) {
            values.push_back(source.value);
        }
    }
    return values;
}

QString commonPrefixDisplay(const MergedCell& cell)
{
    auto values = sourceValues(cell);
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

    QString prefix = values.front();
    for (const auto& value : values) {
        while (!value.startsWith(prefix)) {
            prefix.chop(1);
            if (prefix.isEmpty()) {
                break;
            }
        }
    }

    std::map<int, int> lengths;
    int total = 0;
    for (const auto& value : values) {
        ++lengths[value.size()];
        total += value.size();
    }
    int standardLength = std::round(static_cast<double>(total) / static_cast<double>(values.size()));
    for (auto iterator = lengths.rbegin(); iterator != lengths.rend(); ++iterator) {
        if (static_cast<double>(iterator->second) / static_cast<double>(values.size()) >= 0.75) {
            standardLength = iterator->first;
            break;
        }
    }

    return prefix + QString(std::max<qsizetype>(0, static_cast<qsizetype>(standardLength) - prefix.size()), QLatin1Char('_'));
}

double sumSources(const MergedCell& cell)
{
    double total = 0.0;
    for (const auto& source : cell.sources) {
        if (source.state != CellSourceState::Value) {
            continue;
        }
        auto numeric = CellData::parseNumber(source.value);
        if (numeric.has_value()) {
            total += *numeric;
        }
    }
    return total;
}

int distinctSourceValueCount(const MergedCell& cell)
{
    std::set<QString> values;
    for (const auto& source : cell.sources) {
        if (source.state == CellSourceState::Value) {
            values.insert(source.value);
        }
    }
    return static_cast<int>(values.size());
}

QString overrideTypeToString(SchemaCellOverrideType type)
{
    switch (type) {
    case SchemaCellOverrideType::Sum:
        return QStringLiteral("sum");
    case SchemaCellOverrideType::Mixed:
        return QStringLiteral("mixed");
    case SchemaCellOverrideType::Single:
        return QStringLiteral("single");
    case SchemaCellOverrideType::Label:
        return QStringLiteral("label");
    }
    return QStringLiteral("label");
}

SchemaCellOverrideType overrideTypeFromJsonValue(const QJsonValue& value)
{
    if (value.isString()) {
        const auto text = value.toString().toLower();
        if (text == QStringLiteral("sum")) {
            return SchemaCellOverrideType::Sum;
        }
        if (text == QStringLiteral("mixed")) {
            return SchemaCellOverrideType::Mixed;
        }
        if (text == QStringLiteral("single")) {
            return SchemaCellOverrideType::Single;
        }
        return SchemaCellOverrideType::Label;
    }
    const int legacy = value.toInt(0);
    switch (legacy) {
    case 1:
        return SchemaCellOverrideType::Sum;
    case 2:
        return SchemaCellOverrideType::Mixed;
    case 3:
        return SchemaCellOverrideType::Single;
    default:
        return SchemaCellOverrideType::Label;
    }
}

QJsonObject sheetFingerprintToJson(const SheetRuleFingerprint& fingerprint)
{
    QJsonObject object;
    object.insert(QStringLiteral("sheetName"), fingerprint.sheetName);
    object.insert(QStringLiteral("rowCount"), fingerprint.rowCount);
    object.insert(QStringLiteral("columnCount"), fingerprint.columnCount);
    object.insert(QStringLiteral("layoutHash"), fingerprint.layoutHash);
    object.insert(QStringLiteral("formatHash"), fingerprint.formatHash);
    return object;
}

SheetRuleFingerprint sheetFingerprintFromJson(const QJsonObject& object)
{
    return {
        object.value(QStringLiteral("sheetName")).toString(),
        object.value(QStringLiteral("rowCount")).toInt(),
        object.value(QStringLiteral("columnCount")).toInt(),
        object.value(QStringLiteral("layoutHash")).toString(),
        object.value(QStringLiteral("formatHash")).toString()
    };
}

} // namespace

SchemaRepository::SchemaRepository() :
    baseDirectory_(defaultSchemaDir())
{
}

SchemaRepository::SchemaRepository(QDir baseDirectory) :
    baseDirectory_(std::move(baseDirectory))
{
}

std::vector<MergeSchema> SchemaRepository::loadAll() const
{
    std::vector<MergeSchema> schemas;
    if (!baseDirectory_.exists()) {
        return schemas;
    }
    for (const auto& fileName : baseDirectory_.entryList({QStringLiteral("*.json")}, QDir::Files)) {
        QFile file(baseDirectory_.filePath(fileName));
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        schemas.push_back(schemaFromJson(QJsonDocument::fromJson(file.readAll()).object()));
    }
    return schemas;
}

void SchemaRepository::save(const MergeSchema& schema) const
{
    if (!baseDirectory_.exists() && !baseDirectory_.mkpath(QStringLiteral("."))) {
        throw std::runtime_error("无法创建 schema 存储目录");
    }
    QFile file(baseDirectory_.filePath(schemaFileName(schema.id)));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        throw std::runtime_error(QStringLiteral("无法保存 schema: %1").arg(file.errorString()).toStdString());
    }
    file.write(QJsonDocument(toJson(schema)).toJson(QJsonDocument::Indented));
}

std::optional<MergeSchema> SchemaRepository::find(const QUuid& id) const
{
    QFile file(baseDirectory_.filePath(schemaFileName(id)));
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    return schemaFromJson(QJsonDocument::fromJson(file.readAll()).object());
}

void SchemaRepository::remove(const QUuid& id) const
{
    QFile::remove(baseDirectory_.filePath(schemaFileName(id)));
}

QDir SchemaRepository::baseDirectory() const
{
    return baseDirectory_;
}

WorkbookFingerprint fingerprintFor(const std::vector<ExcelFile>& files)
{
    QStringList sheetNames;
    if (!files.empty()) {
        for (const auto& sheet : files.front().sheets) {
            sheetNames.append(sheet.name);
        }
    }
    return fingerprintFor(files, sheetNames);
}

WorkbookFingerprint fingerprintFor(const std::vector<ExcelFile>& files, const QStringList& sheetNames)
{
    WorkbookFingerprint fingerprint;
    fingerprint.fileCount = static_cast<int>(files.size());
    fingerprint.sheetNames = sheetNames;
    for (const auto& sheetName : sheetNames) {
        fingerprint.sheetFingerprints.push_back(consensusSheetFingerprint(files, sheetName));
    }
    QStringList parts;
    parts << QString::number(fingerprint.fileCount);
    for (const auto& sheet : fingerprint.sheetFingerprints) {
        parts << sheet.sheetName
              << QString::number(sheet.rowCount)
              << QString::number(sheet.columnCount)
              << sheet.layoutHash
              << sheet.formatHash;
    }
    fingerprint.signature = shortHash(parts.join(QLatin1Char('|')));
    return fingerprint;
}

std::optional<MergeSchema> SchemaMatchResult::exactSchema() const
{
    if (kind == SchemaMatchKind::Exact && candidates.size() == 1) {
        return candidates.front().schema;
    }
    return std::nullopt;
}

SchemaMatchResult SchemaMatcher::match(
    const WorkbookFingerprint& fingerprint,
    const std::vector<MergeSchema>& schemas
)
{
    SchemaMatchResult result;
    if (schemas.empty()) {
        return result;
    }

    for (const auto& schema : schemas) {
        result.candidates.push_back({schema, calculateSimilarity(fingerprint, schema.fingerprint)});
    }

    std::sort(result.candidates.begin(), result.candidates.end(), [](const auto& lhs, const auto& rhs) {
        if (std::fabs(lhs.score - rhs.score) > 0.0000001) {
            return lhs.score > rhs.score;
        }
        return lhs.schema.updatedAt > rhs.schema.updatedAt;
    });

    std::vector<SchemaMatchCandidate> exact;
    std::copy_if(result.candidates.begin(), result.candidates.end(), std::back_inserter(exact), [](const auto& candidate) {
        return candidate.score >= HighConfidenceThreshold;
    });
    if (exact.size() == 1) {
        result.kind = SchemaMatchKind::Exact;
        result.candidates = exact;
        return result;
    }
    if (exact.size() > 1) {
        result.kind = SchemaMatchKind::Ambiguous;
        result.candidates = exact;
        return result;
    }

    std::vector<SchemaMatchCandidate> similar;
    std::copy_if(result.candidates.begin(), result.candidates.end(), std::back_inserter(similar), [](const auto& candidate) {
        return candidate.score >= MediumConfidenceThreshold;
    });
    if (!similar.empty()) {
        result.kind = SchemaMatchKind::Similar;
        result.candidates = similar;
        return result;
    }

    result.kind = SchemaMatchKind::None;
    result.candidates.clear();
    return result;
}

double SchemaMatcher::calculateSimilarity(
    const WorkbookFingerprint& lhs,
    const WorkbookFingerprint& rhs
)
{
    if (!lhs.sheetFingerprints.empty() && lhs.sheetFingerprints.size() == rhs.sheetFingerprints.size()) {
        double total = 0.0;
        bool namesAndDimensionsMatch = true;
        for (size_t index = 0; index < lhs.sheetFingerprints.size(); ++index) {
            const auto& left = lhs.sheetFingerprints[index];
            const auto& right = rhs.sheetFingerprints[index];
            if (left.sheetName != right.sheetName || left.rowCount != right.rowCount || left.columnCount != right.columnCount) {
                namesAndDimensionsMatch = false;
            }
            total += sheetSimilarity(left, right);
        }
        const double score = total / static_cast<double>(lhs.sheetFingerprints.size());
        return namesAndDimensionsMatch ? std::max(score, HighConfidenceThreshold) : score;
    }

    if (lhs.signature == rhs.signature && !lhs.signature.isEmpty()) {
        return 1.0;
    }
    double score = 0.0;
    score += lhs.sheetNames == rhs.sheetNames ? 0.50 : 0.0;
    score += lhs.fileCount == rhs.fileCount ? 0.20 : 0.0;
    return score;
}

MergedResult applySchema(const MergeSchema& schema, const MergedResult& result)
{
    MergedResult modified = result;
    for (const auto& override : schema.overrides) {
        if (!override.sheetName.isEmpty() && override.sheetName != result.sheetName) {
            continue;
        }
        const int row = override.position.row;
        const int column = override.position.column;
        if (row < 0 || column < 0 || row >= static_cast<int>(modified.rows.size()) || column >= static_cast<int>(modified.rows[static_cast<size_t>(row)].size())) {
            continue;
        }

        auto& cell = modified.rows[static_cast<size_t>(row)][static_cast<size_t>(column)];
        switch (override.type) {
        case SchemaCellOverrideType::Label:
            cell.type = {CellKind::Label, 0.0, 0, {}};
            cell.displayValue = commonPrefixDisplay(cell);
            break;
        case SchemaCellOverrideType::Sum: {
            const double total = sumSources(cell);
            cell.type = {CellKind::Sum, total, 0, {}};
            cell.displayValue = MergedCell::formatNumber(total, cell.formatCode);
            break;
        }
        case SchemaCellOverrideType::Mixed: {
            const int count = distinctSourceValueCount(cell);
            cell.type = {CellKind::Mixed, 0.0, count, {}};
            cell.displayValue = QStringLiteral("%1条").arg(count);
            break;
        }
        case SchemaCellOverrideType::Single:
            cell.type = {CellKind::Single, 0.0, 0, cell.displayValue};
            break;
        }
        cell.isOverridden = true;
        cell.decision.decisionReasons.append(QStringLiteral("已应用规则：%1").arg(schema.name));
    }
    return modified;
}

QJsonObject toJson(const MergeSchema& schema)
{
    QJsonArray overrides;
    for (const auto& override : schema.overrides) {
        QJsonObject object;
        object.insert(QStringLiteral("row"), override.position.row);
        object.insert(QStringLiteral("column"), override.position.column);
        object.insert(QStringLiteral("type"), overrideTypeToString(override.type));
        object.insert(QStringLiteral("sheetName"), override.sheetName);
        overrides.append(object);
    }

    QJsonArray sheetFingerprints;
    for (const auto& sheet : schema.fingerprint.sheetFingerprints) {
        sheetFingerprints.append(sheetFingerprintToJson(sheet));
    }

    QJsonObject fingerprint;
    fingerprint.insert(QStringLiteral("fileCount"), schema.fingerprint.fileCount);
    fingerprint.insert(QStringLiteral("sheetNames"), QJsonArray::fromStringList(schema.fingerprint.sheetNames));
    fingerprint.insert(QStringLiteral("signature"), schema.fingerprint.signature);
    fingerprint.insert(QStringLiteral("sheetFingerprints"), sheetFingerprints);

    QJsonObject object;
    object.insert(QStringLiteral("id"), schema.id.toString(QUuid::WithoutBraces));
    object.insert(QStringLiteral("name"), schema.name);
    object.insert(QStringLiteral("version"), schema.version);
    object.insert(QStringLiteral("fingerprint"), fingerprint);
    object.insert(QStringLiteral("overrides"), overrides);
    object.insert(QStringLiteral("createdAt"), schema.createdAt.toString(Qt::ISODate));
    object.insert(QStringLiteral("updatedAt"), schema.updatedAt.toString(Qt::ISODate));
    return object;
}

MergeSchema schemaFromJson(const QJsonObject& object)
{
    MergeSchema schema;
    schema.id = QUuid(object.value(QStringLiteral("id")).toString());
    schema.name = object.value(QStringLiteral("name")).toString();
    schema.version = object.value(QStringLiteral("version")).toInt(1);
    const auto fingerprint = object.value(QStringLiteral("fingerprint")).toObject();
    schema.fingerprint.fileCount = fingerprint.value(QStringLiteral("fileCount")).toInt();
    for (const auto& value : fingerprint.value(QStringLiteral("sheetNames")).toArray()) {
        schema.fingerprint.sheetNames.append(value.toString());
    }
    schema.fingerprint.signature = fingerprint.value(QStringLiteral("signature")).toString();
    for (const auto& value : fingerprint.value(QStringLiteral("sheetFingerprints")).toArray()) {
        schema.fingerprint.sheetFingerprints.push_back(sheetFingerprintFromJson(value.toObject()));
    }
    for (const auto& value : object.value(QStringLiteral("overrides")).toArray()) {
        const auto item = value.toObject();
        schema.overrides.push_back({
            {item.value(QStringLiteral("row")).toInt(), item.value(QStringLiteral("column")).toInt()},
            overrideTypeFromJsonValue(item.value(QStringLiteral("type"))),
            item.value(QStringLiteral("sheetName")).toString()
        });
    }
    schema.createdAt = QDateTime::fromString(object.value(QStringLiteral("createdAt")).toString(), Qt::ISODate);
    schema.updatedAt = QDateTime::fromString(object.value(QStringLiteral("updatedAt")).toString(), Qt::ISODate);
    return schema;
}

} // namespace xlsone
