#pragma once

#include "xlsone/core/models.hpp"

namespace xlsone {

class TemplateWorkbookExporter {
public:
    void exportWorkbook(
        const QString& templatePath,
        const std::vector<MergedResult>& results,
        const QString& outputPath,
        const QString& watermarkText = QString()
    ) const;
};

} // namespace xlsone

