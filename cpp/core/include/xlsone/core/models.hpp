#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <optional>
#include <variant>
#include <vector>

namespace xlsone {

struct CellPosition {
    int row = 0;
    int column = 0;

    bool operator==(const CellPosition&) const = default;
};

struct CellData {
    QString value;
    std::optional<QString> rawValue;
    std::optional<double> numericValue;
    std::optional<QString> formatCode;
    bool isDate = false;

    explicit CellData(
        QString value = {},
        std::optional<QString> rawValue = std::nullopt,
        std::optional<double> numericValue = std::nullopt,
        std::optional<QString> formatCode = std::nullopt,
        bool isDate = false
    );

    bool isNumeric() const;

    static std::optional<double> parseNumber(const QString& text);
};

enum class CellSourceState {
    Value,
    Empty,
    Missing,
};

struct CellSourceEntry {
    QString filename;
    QString filepath;
    QString value;
    std::optional<QString> rawValue;
    CellSourceState state = CellSourceState::Missing;
};

struct CellMergeInput {
    QString filename;
    QString filepath;
    std::optional<CellData> cell;
};

struct NeighborContext {
    double numericTendency = 0.0;
    double labelTendency = 0.0;
};

enum class CellKind {
    Label,
    Sum,
    Mixed,
    Single,
};

struct CellType {
    CellKind kind = CellKind::Label;
    double sum = 0.0;
    int mixedCount = 0;
    QString singleValue;
};

struct MergedCellDecision {
    CellKind autoDetectedType = CellKind::Label;
    double confidence = 0.0;
    QStringList decisionReasons;
    bool isSuspicious = false;
};

struct MergedCell {
    CellType type;
    QString displayValue;
    std::vector<CellSourceEntry> sources;
    bool isOverridden = false;
    std::optional<QString> formatCode;
    MergedCellDecision decision;

    static MergedCell from(
        const std::vector<CellMergeInput>& cells,
        const std::vector<CellMergeInput>& leftCells = {},
        int row = 1,
        int column = 0
    );

    static MergedCell from(
        const std::vector<CellMergeInput>& cells,
        const std::vector<CellMergeInput>& leftCells,
        NeighborContext neighborContext,
        int row,
        int column
    );

    static QString formatNumber(double value, const std::optional<QString>& formatCode = std::nullopt);
};

struct SheetData {
    QString name;
    std::vector<std::vector<CellData>> rows;

    const CellData* cellAt(int row, int column) const;
    int effectiveRowCount() const;
    int effectiveColumnCount() const;
};

struct ExcelFile {
    QString filename;
    QString filepath;
    std::vector<SheetData> sheets;

    const SheetData* sheetNamed(const QString& sheetName) const;
};

struct MergedResult {
    QString sheetName;
    std::vector<std::vector<MergedCell>> rows;
    QStringList sourceFiles;
};

QString cellReference(int row, int column);
QString columnLetters(int column);
QString cellKindName(CellKind kind);
QJsonObject toJson(const MergedCell& cell);
QJsonObject toJson(const MergedResult& result);

} // namespace xlsone
