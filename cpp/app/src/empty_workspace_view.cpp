#include "empty_workspace_view.hpp"

#include "symbol_icons.hpp"
#include "ui_theme.hpp"

#include <QKeyEvent>
#include <QLinearGradient>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QStyleOption>
#include <algorithm>

EmptyWorkspaceView::EmptyWorkspaceView(QWidget* parent) : QWidget(parent)
{
    setAutoFillBackground(false);
    setFocusPolicy(Qt::StrongFocus);

    openButton_ = new QPushButton(tr("选择文件"), this);
    openButton_->setIcon(xlsone::ui::makeSymbolIcon(xlsone::ui::SymbolIcon::FolderPlus, Qt::white));
    openButton_->setIconSize(QSize(16, 16));
    openButton_->setCursor(Qt::PointingHandCursor);
    openButton_->setObjectName(QStringLiteral("primaryImportButton"));

    const auto& t = xlsone::ui::theme();
    openButton_->setStyleSheet(QStringLiteral(
        "QPushButton#primaryImportButton {"
        " background: %1; color: white; border: 1px solid %2;"
        " border-radius: 8px; padding: 9px 18px; font-weight: 600; font-size: 13px;"
        "}"
        "QPushButton#primaryImportButton:hover { background: %3; }"
        "QPushButton#primaryImportButton:pressed { background: %4; }"
        "QPushButton#primaryImportButton:disabled { background: %5; color: %6; }"
    )
        .arg(t.accent.name())
        .arg(t.isDark ? QStringLiteral("rgba(82,148,255,0.30)") : QStringLiteral("rgba(42,117,255,0.30)"))
        .arg(t.isDark ? QStringLiteral("#1e5fd6") : QStringLiteral("#2368e8"))
        .arg(t.isDark ? QStringLiteral("#1647a8") : QStringLiteral("#1c59cc"))
        .arg(t.isDark ? QStringLiteral("#2c2e36") : QStringLiteral("#e2e6ef"))
        .arg(t.textDisabled.name()));

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

void EmptyWorkspaceView::keyPressEvent(QKeyEvent* event)
{
    if (event->matches(QKeySequence::Open)) {
        emit openRequested();
        return;
    }
    QWidget::keyPressEvent(event);
}

void EmptyWorkspaceView::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    const auto& t = xlsone::ui::theme();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Background
    painter.fillRect(rect(), t.bg0);

    const QRect card = cardRect();

    // Card shadow
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(dropTargeted_
        ? (t.isDark ? QColor(0, 0, 0, 60) : QColor(0, 0, 0, 30))
        : (t.isDark ? QColor(0, 0, 0, 40) : QColor(0, 0, 0, 18)));
    painter.drawRoundedRect(card.translated(0, 10), 20, 20);
    painter.restore();

    // Card background
    QPainterPath cardPath;
    cardPath.addRoundedRect(card, 20, 20);
    painter.fillPath(cardPath, t.surface);

    // Card border
    painter.save();
    painter.setPen(QPen(dropTargeted_ ? t.accent : t.border, dropTargeted_ ? 2 : 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(card.adjusted(1, 1, -1, -1), 20, 20);
    painter.restore();

    // Brand header
    const int headerY = card.top() + 28;
    const int iconSize = 28;
    const QRect iconRect(card.center().x() - iconSize / 2, headerY, iconSize, iconSize);
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(t.accent);
    painter.drawRoundedRect(iconRect, 7, 7);
    painter.setPen(QPen(Qt::white, 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(iconRect.left() + 7, iconRect.center().y(), iconRect.right() - 7, iconRect.center().y());
    painter.drawLine(iconRect.center().x(), iconRect.top() + 7, iconRect.center().x(), iconRect.bottom() - 7);
    painter.restore();

    QFont brandFont = font();
    brandFont.setPointSize(13);
    brandFont.setWeight(QFont::DemiBold);
    painter.setFont(brandFont);
    painter.setPen(t.text);
    painter.drawText(QRect(card.left(), iconRect.bottom() + 6, card.width(), 22),
                     Qt::AlignHCenter | Qt::AlignVCenter,
                     tr("表表归一"));

    // Artwork
    const QRect artwork = artworkRect(card);
    drawArtwork(painter, artwork);

    // Title
    const QString title = dropTargeted_ ? tr("松手即可导入") : tr("拖入 Excel 文件");
    const QString subtitle = dropTargeted_
        ? tr("支持多个 .xlsx / .xls")
        : tr("支持多个 .xlsx / .xls，自动识别表头与可汇总列");

    QFont titleFont = font();
    titleFont.setPointSize(22);
    titleFont.setWeight(QFont::DemiBold);
    painter.setFont(titleFont);
    painter.setPen(t.text);
    const QRect titleRect(card.left() + 40, artwork.bottom() + 20, card.width() - 80, 36);
    painter.drawText(titleRect, Qt::AlignHCenter | Qt::AlignVCenter, title);

    QFont subtitleFont = font();
    subtitleFont.setPointSize(12);
    painter.setFont(subtitleFont);
    painter.setPen(t.textMuted);
    const QRect subtitleRect(card.left() + 48, titleRect.bottom() + 2, card.width() - 96, 40);
    painter.drawText(subtitleRect, Qt::AlignHCenter | Qt::TextWordWrap, subtitle);

    // Footer hint
    QFont hintFont = font();
    hintFont.setPointSize(10);
    painter.setFont(hintFont);
    painter.setPen(t.textDisabled);
    const QRect hintRect(card.left(), card.bottom() - 34, card.width(), 20);
    painter.drawText(hintRect, Qt::AlignHCenter | Qt::AlignVCenter,
                     tr("也可以按 Ctrl+O 选择文件"));
}

void EmptyWorkspaceView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    layoutButton();
}

QRect EmptyWorkspaceView::cardRect() const
{
    const int width = std::min(520, std::max(340, this->width() - 80));
    const int height = std::min(460, std::max(380, this->height() - 80));
    return QRect((this->width() - width) / 2, (this->height() - height) / 2, width, height);
}

QRect EmptyWorkspaceView::artworkRect(const QRect& card) const
{
    return QRect(card.center().x() - 90, card.top() + 88, 180, 130);
}

void EmptyWorkspaceView::layoutButton()
{
    if (openButton_ == nullptr) {
        return;
    }
    const QRect card = cardRect();
    openButton_->adjustSize();
    const QSize size(std::max(openButton_->width(), 132), 40);
    openButton_->setGeometry(card.center().x() - size.width() / 2,
                             card.bottom() - 92,
                             size.width(),
                             size.height());
}

void EmptyWorkspaceView::drawArtwork(QPainter& painter, const QRect& area)
{
    const auto& t = xlsone::ui::theme();

    auto drawSheet = [&](QRect sheet, int alpha, bool front) {
        painter.save();
        painter.setPen(QPen(t.border, front ? 1.2 : 1));
        painter.setBrush(t.surface);
        painter.drawRoundedRect(sheet, 12, 12);

        // Header bar
        painter.setPen(Qt::NoPen);
        painter.setBrush(front ? QColor(t.accent.red(), t.accent.green(), t.accent.blue(), 60)
                               : QColor(t.textDisabled.red(), t.textDisabled.green(), t.textDisabled.blue(), 40));
        painter.drawRoundedRect(QRect(sheet.left() + 10, sheet.top() + 12, sheet.width() - 20, 9), 4, 4);

        // Data rows
        painter.setBrush(QColor(t.textDisabled.red(), t.textDisabled.green(), t.textDisabled.blue(), front ? 45 : 30));
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                painter.drawRoundedRect(
                    QRect(sheet.left() + 10 + col * 36, sheet.top() + 34 + row * 18, 28, 11),
                    3, 3);
            }
        }
        painter.restore();
    };

    // Background source sheets
    drawSheet(QRect(area.left() + 4, area.top() + 14, 110, 86), 120, false);
    drawSheet(QRect(area.left() + 64, area.top() + 20, 118, 92), 140, false);

    // Foreground merged sheet
    drawSheet(QRect(area.left() + 26, area.top() + 6, 132, 100), 255, true);

    // Merge arrow
    const int arrowY = area.top() + 108;
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(t.accent);
    painter.drawEllipse(QPoint(area.center().x(), arrowY), 12, 12);
    painter.setPen(QPen(Qt::white, 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(area.center().x(), arrowY - 5, area.center().x(), arrowY + 4);
    painter.drawLine(area.center().x() - 4, arrowY + 1, area.center().x(), arrowY + 5);
    painter.drawLine(area.center().x() + 4, arrowY + 1, area.center().x(), arrowY + 5);
    painter.restore();
}
