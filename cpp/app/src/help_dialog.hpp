#pragma once

#include <QDialog>

class QTreeWidget;
class QTreeWidgetItem;
class QTextBrowser;

/// Industrial-grade Quick Reference Guide dialog.
/// Left sidebar (QTreeWidget) provides categorized topic navigation;
/// right pane (QTextBrowser) renders the HTML help content from resources.
class HelpDialog final : public QDialog {
    Q_OBJECT

public:
    explicit HelpDialog(QWidget* parent = nullptr);

private slots:
    void onItemClicked(QTreeWidgetItem* item, int column);

private:
    void buildUi();
    void addTopic(const QString& title, const QString& anchor, QTreeWidgetItem* parent = nullptr);

    QTreeWidget* tree_ = nullptr;
    QTextBrowser* browser_ = nullptr;
};
