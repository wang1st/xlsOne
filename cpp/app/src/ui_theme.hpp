#pragma once

#include <QColor>
#include <QWidget>

namespace xlsone::ui {

struct Theme {
    bool isDark = false;

    QColor bg0;
    QColor bg1;
    QColor bg2;
    QColor bg3;
    QColor border;
    QColor borderSoft;
    QColor text;
    QColor textMuted;
    QColor accent;
    QColor accentSoft;
    QColor labelBg;
    QColor labelFg;
    QColor labelBorder;
    QColor sumBg;
    QColor sumFg;
    QColor sumBorder;
    QColor warningSoft;
    QColor adjusted;

    static Theme light();
    static Theme dark();
};

const Theme& theme();
bool isSystemDark();
void applyAppStyle(QWidget* root);

} // namespace xlsone::ui
