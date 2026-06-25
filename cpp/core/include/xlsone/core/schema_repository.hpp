#pragma once

#include "xlsone/core/models.hpp"

#include <QDateTime>
#include <QDir>
#include <QUuid>
#include <optional>

namespace xlsone {

enum class SchemaCellOverrideType {
    Label,
    Sum,
    Mixed,
    Single,
};

struct SchemaCellOverride {
    CellPosition position;
    SchemaCellOverrideType type = SchemaCellOverrideType::Label;
    QString sheetName;
};

struct SheetRuleFingerprint {
    QString sheetName;
    int rowCount = 0;
    int columnCount = 0;
    QString layoutHash;
    QString formatHash;
};

struct WorkbookFingerprint {
    QStringList sheetNames;
    int fileCount = 0;
    QString signature;
    std::vector<SheetRuleFingerprint> sheetFingerprints;
};

struct MergeSchema {
    QUuid id;
    QString name;
    int version = 1;
    WorkbookFingerprint fingerprint;
    std::vector<SchemaCellOverride> overrides;
    QDateTime createdAt;
    QDateTime updatedAt;
};

struct SchemaMatchCandidate {
    MergeSchema schema;
    double score = 0.0;
};

enum class SchemaMatchKind {
    None,
    Exact,
    Ambiguous,
    Similar,
};

struct SchemaMatchResult {
    SchemaMatchKind kind = SchemaMatchKind::None;
    std::vector<SchemaMatchCandidate> candidates;

    std::optional<MergeSchema> exactSchema() const;
};

class SchemaMatcher {
public:
    static constexpr double HighConfidenceThreshold = 0.90;
    static constexpr double MediumConfidenceThreshold = 0.70;

    static SchemaMatchResult match(
        const WorkbookFingerprint& fingerprint,
        const std::vector<MergeSchema>& schemas
    );

    static double calculateSimilarity(
        const WorkbookFingerprint& lhs,
        const WorkbookFingerprint& rhs
    );
};

class SchemaRepository {
public:
    explicit SchemaRepository(QDir baseDirectory = {});

    std::vector<MergeSchema> loadAll() const;
    void save(const MergeSchema& schema) const;
    std::optional<MergeSchema> find(const QUuid& id) const;
    void remove(const QUuid& id) const;
    QDir baseDirectory() const;

private:
    QDir baseDirectory_;
};

WorkbookFingerprint fingerprintFor(const std::vector<ExcelFile>& files);
WorkbookFingerprint fingerprintFor(const std::vector<ExcelFile>& files, const QStringList& sheetNames);
MergedResult applySchema(const MergeSchema& schema, const MergedResult& result);
QJsonObject toJson(const MergeSchema& schema);
MergeSchema schemaFromJson(const QJsonObject& object);

} // namespace xlsone
