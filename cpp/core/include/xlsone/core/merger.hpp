#pragma once

#include "xlsone/core/models.hpp"

namespace xlsone {

class SimpleMerger {
public:
    MergedResult merge(const std::vector<ExcelFile>& files, const QString& sheetName) const;
    MergedResult mergeFirstSheets(const std::vector<ExcelFile>& files) const;
    QStringList availableSheetNames(const std::vector<ExcelFile>& files) const;
};

} // namespace xlsone

