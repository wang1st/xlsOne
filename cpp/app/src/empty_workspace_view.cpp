#include "empty_workspace_view.hpp"

#include "ui_theme.hpp"

#include <QLinearGradient>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QStyle>
#include <algorithm>

EmptyWorkspaceView::EmptyWorkspaceView(QWidget* parent) : QWidget(parent)
{
    setAutoFillBackground(false);
    openButton_ = new QPushButton(tr("选择文件"), this);
    openButton_->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
    openButton_->setCursor(Qt::PointingHandCursor);
    openButton_->setObjectName(QStringLiteral("primaryImportButton"));
    const auto& t = xlsone::ui::theme();
    openButton_->setStyleSheet(QStringLiteral(
        "QPushButton#primaryImportButton {"
        " background: %1; color: white; border: 1px solid %2;"
        " border-radius: 10px; padding: 9px 16px; font-weight: 600;"
        "}"
        "QPushButton#primaryImportButton:hover { background: %3; }"
        "QPushButton#primaryImportButton:pressed { background: %4; }"
    )
        .arg(t.accent.name(),
             t.isDark ? QStringLiteral("rgba(82,148,255,0.30)") : QStringLiteral("rgba(42,117,255,0.30)"),
             t.isDark ? QStringLiteral("#1e5fd6") : QStringLiteral("#2368e8"),
             t.isDark ? QStringLiteral("#1647a8") : QStringLiteral("#1c59cc")));
    connect(openButton_, &QPushButton::clicked, this, &EmptyWorkspaceView::openRequested);
}

void EmptyWorkspaceView::setDropTargeted(bool targeted)
{
    if (dropTargeted_ == targeted) {
        return;
    }
    dropTargeted_ = targeted;
    update();
}

void EmptyWorkspaceView::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    const auto& t = xlsone::ui::theme();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QLinearGradient background(rect().topLeft(), rect().bottomLeft());
    background.setColorAt(0.0, t.bg0);
    background.setColorAt(1.0, t.bg1);
    painter.fillRect(rect(), background);

    const QRect card = cardRect();
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(dropTargeted_
        ? (t.isDark ? QColor(255, 255, 255, 20) : QColor(0, 0, 0, 20))
        : (t.isDark ? QColor(255, 255, 255, 10) : QColor(0, 0, 0, 10)));
    painter.drawRoundedRect(card.translated(0, 12), 28, 28);
    painter.restore();

    QPainterPath cardPath;
    cardPath.addRoundedRect(card, 28, 28);
    painter.fillPath(cardPath, QColor(255, 255, 255, dropTargeted_ ? 244 : 184));
    painter.save();
    painter.setClipPath(cardPath);
    drawBackdrop(painter, card);
    painter.restore();

    painter.setPen(QPen(dropTargeted_ ? QColor(42, 117, 255, 120) : QColor(120, 130, 150, 38), dropTargeted_ ? 2 : 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(card.adjusted(1, 1, -1, -1), 28, 28);

    drawArtwork(painter, artworkRect(card));

    const QString title = dropTargeted_ ? tr("松手即可导入") : tr("拖入 Excel 文件");
    const QString subtitle = dropTargeted_
        ? tr("支持多个 .xlsx / .xls")
        : tr("支持多个 .xlsx / .xls，导入结构一致的文件后即可开始汇总");

    QFont titleFont = font();
    titleFont.setPointSize(28);
    titleFont.setWeight(QFont::DemiBold);
    painter.setFont(titleFont);
    painter.setPen(t.isDark ? QColor(31, 35, 40) : t.text);
    const QRect titleRect(card.left() + 40, card.top() + 186, card.width() - 80, 42);
    painter.drawText(titleRect, Qt::AlignHCenter | Qt::AlignVCenter, title);

    QFont subtitleFont = font();
    subtitleFont.setPointSize(13);
    painter.setFont(subtitleFont);
    painter.setPen(t.isDark ? QColor(100, 109, 122) : t.textMuted);
    const QRect subtitleRect(card.left() + 52, titleRect.bottom() + 4, card.width() - 104, 42);
    painter.drawText(subtitleRect, Qt::AlignHCenter | Qt::TextWordWrap, subtitle);
}

void EmptyWorkspaceView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    layoutButton();
}

QRect EmptyWorkspaceView::cardRect() const
{
    const int width = std::min(560, std::max(320, this->width() - 80));
    const int height = std::min(360, std::max(300, this->height() - 80));
    return QRect((this->width() - width) / 2, (this->height() - height) / 2, width, height);
}

QRect EmptyWorkspaceView::artworkRect(const QRect& card) const
{
    return QRect(card.center().x() - 95, card.top() + 42, 190, 124);
}

void EmptyWorkspaceView::layoutButton()
{
    if (openButton_ == nullptr) {
        return;
    }
    const QRect card = cardRect();
    openButton_->adjustSize();
    const QSize size(std::max(openButton_->width(), 126), 38);
    openButton_->setGeometry(card.center().x() - size.width() / 2, card.bottom() - 74, size.width(), size.height());
}

void EmptyWorkspaceView::drawBackdrop(QPainter& painter, const QRect& card)
{
    QLinearGradient cardGradient(card.topLeft(), card.bottomRight());
    cardGradient.setColorAt(0.0, QColor(255, 255, 255, 184));
    cardGradient.setColorAt(1.0, QColor(241, 244, 249, 224));
    painter.fillRect(card, cardGradient);

    painter.setPen(QPen(QColor(55, 65, 81, 14), 1));
    for (int row = card.top() + 44; row < card.bottom() - 30; row += 36) {
        painter.drawLine(card.left() + 32, row, card.right() - 32, row);
    }
    for (int column = card.left() + 80; column < card.right() - 60; column += 56) {
        painter.drawLine(column, card.top() + 26, column, card.bottom() - 26);
    }
}

void EmptyWorkspaceView::drawArtwork(QPainter& painter, const QRect& area)
{
    auto drawSheet = [&](QRect sheet, int alpha, bool front) {
        painter.save();
        painter.setPen(QPen(QColor(30, 40, 55, front ? 36 : 24), front ? 1.2 : 1));
        painter.setBrush(QColor(255, 255, 255, alpha));
        painter.drawRoundedRect(sheet, 14, 14);
        painter.setPen(Qt::NoPen);
        painter.setBrush(dropTargeted_ ? QColor(42, 117, 255, front ? 56 : 34) : QColor(30, 40, 55, front ? 22 : 14));
        painter.drawRoundedRect(QRect(sheet.left() + 14, sheet.top() + 14, sheet.width() - 28, 10), 5, 5);
        painter.setBrush(QColor(30, 40, 55, front ? 18 : 12));
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                painter.drawRoundedRect(QRect(sheet.left() + 14 + col * 38, sheet.top() + 38 + row * 22, 32, 14), 4, 4);
            }
        }
        painter.restore();
    };

    drawSheet(QRect(area.left() + 4, area.top() + 12, 118, 92), 120, false);
    drawSheet(QRect(area.left() + 64, area.top() + 18, 126, 98), 138, false);
    drawSheet(QRect(area.left() + 26, area.top() + 8, 138, 106), 242, true);
}
