#pragma once

#include <QColor>
#include <QIcon>
#include <QSize>

namespace xlsone::ui {

enum class SymbolIcon {
    Plus,
    Refresh,
    Xmark,
    Export,
    FolderPlus
};

QIcon makeSymbolIcon(SymbolIcon symbol, const QColor& color, const QSize& size = QSize(18, 18));

} // namespace xlsone::ui
