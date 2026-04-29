#pragma once

#include "xlsone/core/models.hpp"
#include "xlsone/core/validator.hpp"

#include <QStringList>

namespace xlsone {

struct ExcelParseBatchResult {
    std::vector<ExcelFile> files;
    std::vector<ExcelParseFailure> failures;
};

class ExcelParser {
public:
    ExcelFile parseFile(const QString& path) const;
    ExcelParseBatchResult parseFiles(const QStringList& paths) const;
};

} // namespace xlsone

