#pragma once

#include <QPushButton>
#include <QWidget>

class QPainter;

class EmptyWorkspaceView final : public QWidget {
    Q_OBJECT

public:
    explicit EmptyWorkspaceView(QWidget* parent = nullptr);

    void setDropTargeted(bool targeted);

signals:
    void openRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    QRect cardRect() const;
    QRect artworkRect(const QRect& card) const;
    void layoutButton();
    void drawArtwork(QPainter& painter, const QRect& area);

    QPushButton* openButton_ = nullptr;
    bool dropTargeted_ = false;
};
