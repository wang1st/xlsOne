#include "ui_theme.hpp"

#include <QApplication>

namespace xlsone::ui {

const Theme& theme()
{
    static const Theme instance;
    return instance;
}

void applyAppStyle(QWidget* root)
{
    const auto& t = theme();
    const QString style = QStringLiteral(R"(
        QMainWindow, QWidget {
            color: %1;
            font-size: 13px;
        }
        QMenuBar {
            background: %2;
            border-bottom: 1px solid %3;
        }
        QStatusBar {
            background: %2;
            border-top: 1px solid %3;
        }
        QTableView {
            background: white;
            alternate-background-color: %4;
            gridline-color: %3;
            border: none;
            selection-background-color: transparent;
            selection-color: %1;
        }
        QHeaderView::section {
            background: %4;
            color: %5;
            border: none;
            border-right: 1px solid %3;
            border-bottom: 1px solid %3;
            padding: 5px 7px;
            font-weight: 600;
        }
        QScrollBar:vertical, QScrollBar:horizontal {
            background: transparent;
            margin: 0;
        }
        QScrollBar::handle:vertical, QScrollBar::handle:horizontal {
            background: rgba(120, 128, 140, 90);
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
        }
        QPushButton:hover, QToolButton:hover {
            background: rgba(0, 0, 0, 7);
        }
        QPushButton:pressed, QToolButton:pressed {
            background: rgba(0, 0, 0, 14);
        }
        QComboBox {
            border: 1px solid %3;
            border-radius: 8px;
            padding: 5px 9px;
            background: white;
        }
    )")
        .arg(t.text.name(), t.windowTop.name(), t.border.name(), t.panelSoft.name(), t.textMuted.name());

    if (root != nullptr) {
        root->setStyleSheet(style);
    } else if (qApp != nullptr) {
        qApp->setStyleSheet(style);
    }
}

} // namespace xlsone::ui
