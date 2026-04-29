#pragma once

#include <QScrollArea>
#include <QWidget>

class QHBoxLayout;

struct SheetStripItem {
    QString sheetName;
    bool mergeable = true;
    int anomalyCount = 0;
    QString subtitle;
    QString tooltip;
};

class SheetStrip final : public QWidget {
    Q_OBJECT

public:
    explicit SheetStrip(QWidget* parent = nullptr);

    void setItems(const QList<SheetStripItem>& items);
    void setCurrentSheet(const QString& sheetName, bool mergeable);

signals:
    void sheetSelected(QString sheetName, bool mergeable);

private:
    void rebuild();

    QScrollArea* scrollArea_ = nullptr;
    QWidget* content_ = nullptr;
    QHBoxLayout* layout_ = nullptr;
    QList<SheetStripItem> items_;
    QString currentSheet_;
    bool currentMergeable_ = true;
};
