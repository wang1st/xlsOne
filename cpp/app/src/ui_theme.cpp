#include "ui_theme.hpp"

#include <QApplication>
#include <QPalette>

namespace xlsone::ui {

Theme Theme::light()
{
    Theme t;
    t.isDark = false;
    t.bg0      = QColor(246, 247, 250);
    t.bg1      = QColor(255, 255, 255);
    t.bg2      = QColor(248, 249, 251);
    t.bg3      = QColor(232, 235, 240);
    t.border   = QColor(220, 224, 232);
    t.borderSoft = QColor(232, 235, 240);
    t.text     = QColor(31, 35, 40);
    t.textMuted = QColor(100, 109, 122);
    t.accent   = QColor(42, 117, 255);
    t.accentSoft = QColor(232, 240, 255);
    t.labelBg  = QColor(236, 253, 245);
    t.labelFg  = QColor(22, 116, 79);
    t.labelBorder = QColor(189, 235, 212);
    t.sumBg    = QColor(239, 246, 255);
    t.sumFg    = QColor(29, 95, 191);
    t.sumBorder = QColor(189, 215, 255);
    t.warningSoft = QColor(255, 247, 229);
    t.adjusted  = QColor(38, 158, 143);
    return t;
}

Theme Theme::dark()
{
    Theme t;
    t.isDark = true;
    t.bg0      = QColor(28, 30, 34);
    t.bg1      = QColor(36, 38, 44);
    t.bg2      = QColor(42, 44, 50);
    t.bg3      = QColor(50, 52, 58);
    t.border   = QColor(58, 60, 68);
    t.borderSoft = QColor(48, 50, 56);
    t.text     = QColor(228, 230, 235);
    t.textMuted = QColor(152, 158, 170);
    t.accent   = QColor(82, 148, 255);
    t.accentSoft = QColor(32, 38, 62);
    t.labelBg  = QColor(28, 54, 42);
    t.labelFg  = QColor(88, 196, 148);
    t.labelBorder = QColor(38, 74, 58);
    t.sumBg    = QColor(26, 42, 68);
    t.sumFg    = QColor(98, 168, 255);
    t.sumBorder = QColor(38, 62, 96);
    t.warningSoft = QColor(52, 44, 24);
    t.adjusted  = QColor(72, 198, 178);
    return t;
}

bool isSystemDark()
{
    if (qApp == nullptr) return false;
    const QPalette& p = qApp->palette();
    return p.color(QPalette::Window).lightness() < 128;
}

const Theme& theme()
{
    static Theme t = isSystemDark() ? Theme::dark() : Theme::light();
    return t;
}

void applyAppStyle(QWidget* root)
{
    const auto& t = theme();

    auto hex = [](const QColor& c) { return c.name(); };

    const QString style = QStringLiteral(R"(
        QMainWindow, QWidget {
            color: %1;
            font-size: 13px;
        }
        QMessageBox {
            background-color: %2;
            color: %1;
        }
        QMessageBox QLabel {
            color: %1;
        }
        QMenuBar {
            background: %3;
            border-bottom: 1px solid %4;
            color: %1;
        }
        QMenuBar::item:selected {
            background: %5;
        }
        QMenu {
            background: %2;
            border: 1px solid %4;
            color: %1;
        }
        QMenu::item:selected {
            background: %6;
        }
        QStatusBar {
            background: %3;
            border-top: 1px solid %4;
            color: %1;
        }
        QTableView {
            background: %2;
            alternate-background-color: %5;
            gridline-color: %4;
            border: none;
            selection-background-color: transparent;
            selection-color: %1;
        }
        QHeaderView::section {
            background: %5;
            color: %7;
            border: none;
            border-right: 1px solid %4;
            border-bottom: 1px solid %4;
            padding: 5px 7px;
            font-weight: 600;
        }
        QScrollBar:vertical, QScrollBar:horizontal {
            background: transparent;
            margin: 0;
        }
        QScrollBar::handle:vertical, QScrollBar::handle:horizontal {
            background: %8;
            border-radius: 5px;
            min-height: 24px;
            min-width: 24px;
        }
        QScrollBar::add-line, QScrollBar::sub-line {
            width: 0;
            height: 0;
        }
        QPushButton, QToolButton {
            border: 1px solid transparent;
            border-radius: 9px;
            padding: 7px 11px;
            background: transparent;
            color: %1;
        }
        QPushButton:hover, QToolButton:hover {
            background: %9;
        }
        QPushButton:pressed, QToolButton:pressed {
            background: %10;
        }
        QComboBox {
            border: 1px solid %4;
            border-radius: 8px;
            padding: 5px 9px;
            background: %2;
            color: %1;
        }
    )")
        .arg(hex(t.text))
        .arg(hex(t.bg1))
        .arg(hex(t.bg0))
        .arg(hex(t.border))
        .arg(hex(t.bg2))
        .arg(hex(t.accentSoft))
        .arg(hex(t.textMuted))
        .arg(t.isDark ? QStringLiteral("rgba(180,180,190,60)") : QStringLiteral("rgba(120,128,140,90)"))
        .arg(t.isDark ? QStringLiteral("rgba(255,255,255,10)") : QStringLiteral("rgba(0,0,0,7)"))
        .arg(t.isDark ? QStringLiteral("rgba(255,255,255,18)") : QStringLiteral("rgba(0,0,0,14)"));

    if (root != nullptr) {
        root->setStyleSheet(style);
    } else if (qApp != nullptr) {
        qApp->setStyleSheet(style);
    }
}

} // namespace xlsone::ui
