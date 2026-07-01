#pragma once

#include <QColor>
#include <QPushButton>
#include <QString>
#include <QWidget>

namespace xlsone::ui {

struct Theme {
    bool isDark = false;

    // Backgrounds (macOS-aligned semantics)
    QColor bg0;          // window background
    QColor bg1;          // surface / control background
    QColor bg2;          // elevated surface / hover
    QColor bg3;          // header / separator
    QColor surface;      // card / input backgrounds (alias for bg1)
    QColor elevatedSurface;

    // Text
    QColor text;         // primary label
    QColor textMuted;    // secondary label
    QColor textDisabled; // tertiary / disabled label

    // Borders / Dividers
    QColor border;
    QColor borderSoft;

    // Accents
    QColor accent;
    QColor accentSoft;

    // Status colors (semantic)
    QColor success;
    QColor warning;
    QColor error;
    QColor info;

    // Legacy business semantics (kept for compatibility)
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

// Style helpers to keep button appearance consistent with the empty-state primary button.
QString primaryButtonStyleSheet();
void stylePrimaryButton(QPushButton* button);

} // namespace xlsone::ui
