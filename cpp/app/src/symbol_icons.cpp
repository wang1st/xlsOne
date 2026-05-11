#include "symbol_icons.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace xlsone::ui {

namespace {

void drawPlus(QPainter& painter)
{
    painter.drawLine(QPointF(9, 4), QPointF(9, 14));
    painter.drawLine(QPointF(4, 9), QPointF(14, 9));
}

void drawRefresh(QPainter& painter)
{
    painter.drawArc(QRectF(3.8, 3.8, 10.8, 10.8), 35 * 16, 280 * 16);
    QPainterPath arrow;
    arrow.moveTo(14.8, 4.4);
    arrow.lineTo(14.6, 8.2);
    arrow.lineTo(11.2, 6.5);
    painter.drawPath(arrow);
}

void drawXmark(QPainter& painter)
{
    painter.drawLine(QPointF(5, 5), QPointF(13, 13));
    painter.drawLine(QPointF(13, 5), QPointF(5, 13));
}

void drawExport(QPainter& painter)
{
    QPainterPath box;
    box.moveTo(4, 9.5);
    box.lineTo(4, 15);
    box.lineTo(14, 15);
    box.lineTo(14, 9.5);
    painter.drawPath(box);

    painter.drawLine(QPointF(9, 3.5), QPointF(9, 11));
    QPainterPath arrow;
    arrow.moveTo(5.8, 6.7);
    arrow.lineTo(9, 3.5);
    arrow.lineTo(12.2, 6.7);
    painter.drawPath(arrow);
}

void drawFolderPlus(QPainter& painter)
{
    QPainterPath folder;
    folder.moveTo(2.8, 6);
    folder.lineTo(7.2, 6);
    folder.lineTo(8.6, 7.6);
    folder.lineTo(15.2, 7.6);
    folder.quadTo(16.2, 7.6, 16.2, 8.6);
    folder.lineTo(16.2, 14);
    folder.quadTo(16.2, 15.2, 15, 15.2);
    folder.lineTo(3, 15.2);
    folder.quadTo(1.8, 15.2, 1.8, 14);
    folder.lineTo(1.8, 7.2);
    folder.quadTo(1.8, 6, 2.8, 6);
    painter.drawPath(folder);

    painter.drawLine(QPointF(12.5, 3.2), QPointF(12.5, 7.2));
    painter.drawLine(QPointF(10.5, 5.2), QPointF(14.5, 5.2));
}

} // namespace

QIcon makeSymbolIcon(SymbolIcon symbol, const QColor& color, const QSize& size)
{
    constexpr int canvasSize = 18;
    QPixmap pixmap(size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(size.width() / static_cast<qreal>(canvasSize), size.height() / static_cast<qreal>(canvasSize));
    painter.setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);

    switch (symbol) {
    case SymbolIcon::Plus:
        drawPlus(painter);
        break;
    case SymbolIcon::Refresh:
        drawRefresh(painter);
        break;
    case SymbolIcon::Xmark:
        drawXmark(painter);
        break;
    case SymbolIcon::Export:
        drawExport(painter);
        break;
    case SymbolIcon::FolderPlus:
        drawFolderPlus(painter);
        break;
    }

    return QIcon(pixmap);
}

} // namespace xlsone::ui
