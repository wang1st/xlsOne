#include "xlsone/core/validator.hpp"

#include <QFileInfo>
#include <QSet>
#include <algorithm>
#include <map>
#include <optional>

namespace xlsone {

namespace {

struct SheetDimensions {
    int rows = 0;
    int columns = 0;

    bool operator==(const SheetDimensions& other) const
    {
        return rows == other.rows && columns == other.columns;
    }
    bool operator<(const SheetDimensions& other) const
    {
        return rows == other.rows ? columns < other.columns : rows < other.rows;
    }
};

struct TemplateScore {
    int sheetCount = 0;
    int totalRows = 0;
    int totalColumns = 0;
    int nonEmptyCellCount = 0;

    bool operator<(const TemplateScore& other) const
    {
        if (sheetCount != other.sheetCount) {
            return sheetCount < other.sheetCount;
        }
        if (totalRows != other.totalRows) {
            return totalRows < other.totalRows;
        }
        if (totalColumns != other.totalColumns) {
            return totalColumns < other.totalColumns;
        }
        return nonEmptyCellCount < other.nonEmptyCellCount;
    }
};

ValidationIssue issue(
    ValidationSeverity severity,
    ValidationIssueCode code,
    const ExcelFile& file,
    const QString& sheetName,
    const QString& message
)
{
    return {severity, code, file.filename, file.filepath, sheetName, message};
}

SheetDimensions effectiveDimensions(const SheetData& sheet)
{
    return {sheet.effectiveRowCount(), sheet.effectiveColumnCount()};
}

int nonEmptyCellCount(const SheetData& sheet)
{
    int count = 0;
    for (const auto& row : sheet.rows) {
        for (const auto& cell : row) {
            if (!cell.value.isEmpty()) {
                ++count;
            }
        }
    }
    return count;
}

TemplateScore templateScore(const ExcelFile& file, const QSet<QString>& relevantSheetNames)
{
    TemplateScore score;
    for (const auto& sheet : file.sheets) {
        if (!relevantSheetNames.contains(sheet.name)) {
            continue;
        }
        const auto dimensions = effectiveDimensions(sheet);
        ++score.sheetCount;
        score.totalRows += dimensions.rows;
        score.totalColumns += dimensions.columns;
        score.nonEmptyCellCount += nonEmptyCellCount(sheet);
    }
    return score;
}

QSet<QString> allSheetNames(const std::vector<ExcelFile>& files)
{
    QSet<QString> names;
    for (const auto& file : files) {
        for (const auto& sheet : file.sheets) {
            names.insert(sheet.name);
        }
    }
    return names;
}

int chooseRepresentativeTemplateIndex(const std::vector<ExcelFile>& files, const std::optional<QStringList>& mergeableSheetNames)
{
    if (files.empty()) {
        return -1;
    }

    QSet<QString> relevantSheetNames;
    if (mergeableSheetNames.has_value()) {
        for (const auto& name : *mergeableSheetNames) {
            relevantSheetNames.insert(name);
        }
    } else {
        relevantSheetNames = allSheetNames(files);
    }

    int bestIndex = 0;
    TemplateScore bestScore = templateScore(files.front(), relevantSheetNames);
    for (int index = 1; index < static_cast<int>(files.size()); ++index) {
        const auto score = templateScore(files[static_cast<size_t>(index)], relevantSheetNames);
        if (bestScore < score) {
            bestIndex = index;
            bestScore = score;
        }
    }
    return bestIndex;
}

QStringList orderedSheetNames(const std::vector<ExcelFile>& files, int preferredIndex)
{
    QStringList names;
    QSet<QString> seen;
    std::vector<int> order;
    if (preferredIndex >= 0 && preferredIndex < static_cast<int>(files.size())) {
        order.push_back(preferredIndex);
    }
    for (int index = 0; index < static_cast<int>(files.size()); ++index) {
        if (index != preferredIndex) {
            order.push_back(index);
        }
    }

    for (const int index : order) {
        for (const auto& sheet : files[static_cast<size_t>(index)].sheets) {
            if (!seen.contains(sheet.name)) {
                seen.insert(sheet.name);
                names.append(sheet.name);
            }
        }
    }
    return names;
}

SheetDimensions chooseDominantDimensions(const std::vector<std::pair<int, SheetDimensions>>& dimensionsByFile)
{
    std::map<SheetDimensions, std::vector<int>> grouped;
    for (const auto& [index, dimensions] : dimensionsByFile) {
        grouped[dimensions].push_back(index);
    }

    SheetDimensions best;
    std::vector<int> bestIndexes;
    bool hasBest = false;
    for (const auto& [dimensions, indexes] : grouped) {
        if (!hasBest
            || indexes.size() > bestIndexes.size()
            || (indexes.size() == bestIndexes.size() && best < dimensions)
            || (indexes.size() == bestIndexes.size() && !(dimensions < best) && !indexes.empty() && !bestIndexes.empty() && indexes.front() < bestIndexes.front())) {
            best = dimensions;
            bestIndexes = indexes;
            hasBest = true;
        }
    }
    return best;
}

QStringList reorderSheetNames(const QStringList& sheetNames, const std::vector<ExcelFile>& files, int preferredIndex, const QStringList& fallbackOrder)
{
    QSet<QString> target;
    for (const auto& sheetName : sheetNames) {
        target.insert(sheetName);
    }
    QSet<QString> seen;
    QStringList ordered;

    if (preferredIndex >= 0 && preferredIndex < static_cast<int>(files.size())) {
        for (const auto& sheet : files[static_cast<size_t>(preferredIndex)].sheets) {
            if (target.contains(sheet.name) && !seen.contains(sheet.name)) {
                seen.insert(sheet.name);
                ordered.append(sheet.name);
            }
        }
    }

    for (const auto& sheetName : fallbackOrder) {
        if (target.contains(sheetName) && !seen.contains(sheetName)) {
            seen.insert(sheetName);
            ordered.append(sheetName);
        }
    }
    return ordered;
}

std::vector<ExcelFile> orderedFiles(const std::vector<ExcelFile>& files, int preferredIndex)
{
    std::vector<ExcelFile> ordered;
    if (preferredIndex >= 0 && preferredIndex < static_cast<int>(files.size())) {
        ordered.push_back(files[static_cast<size_t>(preferredIndex)]);
    }
    for (int index = 0; index < static_cast<int>(files.size()); ++index) {
        if (index != preferredIndex) {
            ordered.push_back(files[static_cast<size_t>(index)]);
        }
    }
    return ordered;
}

} // namespace

WorkbookValidationOutcome WorkbookValidator::validate(
    const std::vector<ExcelFile>& files,
    const std::vector<ExcelParseFailure>& parseFailures
) const
{
    WorkbookValidationOutcome outcome;

    for (const auto& failure : parseFailures) {
        FileValidationReport report;
        report.filename = QFileInfo(failure.path).fileName();
        report.filepath = failure.path;
        report.status = FileValidationStatus::Warning;
        report.issues.push_back({
            ValidationSeverity::Warning,
            ValidationIssueCode::ParseFailure,
            report.filename,
            report.filepath,
            {},
            QStringLiteral("解析失败: %1").arg(failure.message)
        });
        outcome.report.files.push_back(std::move(report));
    }

    std::vector<ExcelFile> candidates;
    for (const auto& file : files) {
        FileValidationReport report;
        report.filename = file.filename;
        report.filepath = file.filepath;
        if (file.sheets.empty()) {
            report.status = FileValidationStatus::Warning;
            report.issues.push_back({
                ValidationSeverity::Warning,
                ValidationIssueCode::EmptyWorkbook,
                file.filename,
                file.filepath,
                {},
                QStringLiteral("工作簿中没有可用工作表，已跳过")
            });
        } else {
            candidates.push_back(file);
            continue;
        }
        outcome.report.files.push_back(std::move(report));
    }

    if (candidates.empty()) {
        outcome.report.readiness = MergeReadiness::Blocked;
        return outcome;
    }

    const int preliminaryTemplateIndex = chooseRepresentativeTemplateIndex(candidates, std::nullopt);
    const QStringList orderedNames = orderedSheetNames(candidates, preliminaryTemplateIndex);
    std::map<QString, SheetDimensions> referenceDimensionsBySheet;

    for (const auto& sheetName : orderedNames) {
        bool mergeable = true;
        std::vector<ValidationIssue> sheetIssues;
        std::vector<std::pair<int, SheetDimensions>> dimensionsByFile;

        for (int fileIndex = 0; fileIndex < static_cast<int>(candidates.size()); ++fileIndex) {
            const auto& file = candidates[static_cast<size_t>(fileIndex)];
            const auto* sheet = file.sheetNamed(sheetName);
            if (sheet == nullptr) {
                mergeable = false;
                sheetIssues.push_back(issue(
                    ValidationSeverity::Warning,
                    ValidationIssueCode::MissingSheet,
                    file,
                    sheetName,
                    QStringLiteral("缺少工作表 %1，已跳过该工作表").arg(sheetName)
                ));
                continue;
            }
            dimensionsByFile.push_back({fileIndex, effectiveDimensions(*sheet)});
        }

        if (!mergeable) {
            outcome.report.skippedSheetNames.append(sheetName);
            outcome.report.skippedSheetIssues.insert(outcome.report.skippedSheetIssues.end(), sheetIssues.begin(), sheetIssues.end());
            continue;
        }

        const auto dominantDimensions = chooseDominantDimensions(dimensionsByFile);
        const bool allDimensionsMatch = std::all_of(dimensionsByFile.begin(), dimensionsByFile.end(), [&](const auto& item) {
            return item.second == dominantDimensions;
        });

        if (allDimensionsMatch) {
            outcome.report.commonSheetNames.append(sheetName);
            referenceDimensionsBySheet.insert({sheetName, dominantDimensions});
        } else {
            outcome.report.skippedSheetNames.append(sheetName);
            for (const auto& [fileIndex, dimensions] : dimensionsByFile) {
                if (dimensions == dominantDimensions) {
                    continue;
                }
                const auto& file = candidates[static_cast<size_t>(fileIndex)];
                if (dimensions.rows != dominantDimensions.rows) {
                    outcome.report.skippedSheetIssues.push_back(issue(
                        ValidationSeverity::Warning,
                        ValidationIssueCode::RowCountMismatch,
                        file,
                        sheetName,
                        QStringLiteral("工作表“%1”有效行数不一致（忽略尾部空白后：多数文件为 %2 行，当前文件为 %3 行），已从本次汇总中排除")
                            .arg(sheetName)
                            .arg(dominantDimensions.rows)
                            .arg(dimensions.rows)
                    ));
                }
                if (dimensions.columns != dominantDimensions.columns) {
                    outcome.report.skippedSheetIssues.push_back(issue(
                        ValidationSeverity::Warning,
                        ValidationIssueCode::ColumnCountMismatch,
                        file,
                        sheetName,
                        QStringLiteral("工作表“%1”有效列数不一致（忽略尾部空白后：多数文件为 %2 列，当前文件为 %3 列），已从本次汇总中排除")
                            .arg(sheetName)
                            .arg(dominantDimensions.columns)
                            .arg(dimensions.columns)
                    ));
                }
            }
        }
    }

    const int templateIndex = chooseRepresentativeTemplateIndex(candidates, std::optional<QStringList>{outcome.report.commonSheetNames});
    outcome.report.commonSheetNames = reorderSheetNames(outcome.report.commonSheetNames, candidates, templateIndex, orderedNames);
    outcome.report.skippedSheetNames = reorderSheetNames(outcome.report.skippedSheetNames, candidates, templateIndex, orderedNames);

    const auto mergeableFiles = orderedFiles(candidates, templateIndex);
    for (const auto& file : mergeableFiles) {
        FileValidationReport report;
        report.filename = file.filename;
        report.filepath = file.filepath;
        report.status = FileValidationStatus::Included;
        report.isTemplate = templateIndex >= 0 && file.filepath == candidates[static_cast<size_t>(templateIndex)].filepath;

        for (const auto& sheetName : orderedNames) {
            const auto referenceIterator = referenceDimensionsBySheet.find(sheetName);
            const SheetDimensions referenceDimensions = referenceIterator == referenceDimensionsBySheet.end()
                ? SheetDimensions{}
                : referenceIterator->second;
            const auto* sheet = file.sheetNamed(sheetName);
            const SheetDimensions candidateDimensions = sheet == nullptr ? SheetDimensions{} : effectiveDimensions(*sheet);
            std::vector<ValidationIssue> issues;
            std::copy_if(
                outcome.report.skippedSheetIssues.begin(),
                outcome.report.skippedSheetIssues.end(),
                std::back_inserter(issues),
                [&](const auto& item) {
                    return item.filePath == file.filepath && item.sheetName == sheetName;
                }
            );
            report.sheetReports.push_back({
                sheetName,
                outcome.report.commonSheetNames.contains(sheetName) ? MergeReadiness::Ready : MergeReadiness::Blocked,
                issues,
                referenceDimensions.rows,
                referenceDimensions.columns,
                candidateDimensions.rows,
                candidateDimensions.columns
            });
        }
        outcome.report.files.push_back(std::move(report));
    }

    std::sort(outcome.report.files.begin(), outcome.report.files.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.isTemplate != rhs.isTemplate) {
            return lhs.isTemplate && !rhs.isTemplate;
        }
        if (lhs.status != rhs.status) {
            return static_cast<int>(lhs.status) < static_cast<int>(rhs.status);
        }
        return lhs.filename.localeAwareCompare(rhs.filename) < 0;
    });

    outcome.report.readiness = outcome.report.commonSheetNames.isEmpty()
        ? MergeReadiness::Blocked
        : MergeReadiness::Ready;
    if (outcome.report.readiness == MergeReadiness::Blocked) {
        outcome.report.commonSheetNames.clear();
    }
    outcome.mergeableFiles = outcome.report.readiness == MergeReadiness::Ready ? mergeableFiles : std::vector<ExcelFile>{};
    return outcome;
}

} // namespace xlsone
