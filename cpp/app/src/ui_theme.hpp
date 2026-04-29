#pragma once

#include <QColor>
#include <QWidget>

namespace xlsone::ui {

struct Theme {
    QColor windowTop = QColor(246, 247, 250);
    QColor windowBottom = QColor(255, 255, 255);
    QColor panel = QColor(255, 255, 255);
    QColor panelSoft = QColor(248, 249, 251);
    QColor border = QColor(220, 224, 232);
    QColor borderSoft = QColor(232, 235, 240);
    QColor text = QColor(31, 35, 40);
    QColor textMuted = QColor(100, 109, 122);
    QColor accent = QColor(42, 117, 255);
    QColor accentSoft = QColor(232, 240, 255);
    QColor sumSoft = QColor(239, 246, 255);
    QColor warningSoft = QColor(255, 247, 229);
    QColor adjusted = QColor(38, 158, 143);
    QColor shadow = QColor(0, 0, 0, 20);
};

const Theme& theme();
void applyAppStyle(QWidget* root);

} // namespace xlsone::ui
